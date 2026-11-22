'use strict';

/**
 * 纯 Node.js 压缩/解压模块（仅依赖内置 zlib，不引入任何第三方库）。
 * - createZip：流式生成 zip（deflateRaw level 6，data descriptor 记录 CRC/大小），不做 zip64
 * - extractZip：解析中央目录，支持 stored(0) / deflate(8)，逐条目校验路径防目录穿越
 * 说明：单文件 >= 4GB 或条目数 >= 65535 时直接抛出中文错误（未实现 zip64）。
 */

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const { Transform, Writable } = require('stream');
const { pipeline } = require('stream/promises');

const { safeResolve } = require('./util');

/** 单个文件大小上限（4GB） */
const MAX_ENTRY_SIZE = 0xffffffff;
/** 条目数量上限（65535 为 zip 非 zip64 的上限） */
const MAX_ENTRY_COUNT = 0xffff;
/** 压缩级别 */
const DEFLATE_LEVEL = 6;

/* -------------------------------- CRC32 -------------------------------- */

/** CRC32 查表（IEEE 802.3 多项式 0xEDB88320） */
const CRC_TABLE = (() => {
  const table = new Int32Array(256);
  for (let i = 0; i < 256; i += 1) {
    let value = i;
    for (let bit = 0; bit < 8; bit += 1) {
      value = value & 1 ? (value >>> 1) ^ 0xedb88320 : value >>> 1;
    }
    table[i] = value;
  }
  return table;
})();

/** 增量更新 CRC32（传入 0 开始，返回无符号整数） */
function crc32Update(crc, buffer) {
  let value = crc | 0;
  for (let i = 0; i < buffer.length; i += 1) {
    value = CRC_TABLE[(value ^ buffer[i]) & 0xff] ^ (value >>> 8);
  }
  return value | 0;
}

function crc32Finish(crc) {
  return (crc ^ -1) >>> 0;
}

/* ------------------------------ 工具函数 ------------------------------ */

/** 判断文件名是否为 zip */
function isZipPath(name) {
  return typeof name === 'string' && /\.zip$/i.test(name.trim());
}

/** 把日期转换为 DOS 时间/日期（使用本地时间，zip 规范要求） */
function dosDateTime(input) {
  const date = input instanceof Date ? input : new Date(input);
  const year = Math.max(date.getFullYear(), 1980);
  const dosTime =
    ((date.getHours() & 0x1f) << 11) | ((date.getMinutes() & 0x3f) << 5) | ((Math.floor(date.getSeconds() / 2)) & 0x1f);
  const dosDate = (((year - 1980) & 0x7f) << 9) | (((date.getMonth() + 1) & 0x0f) << 5) | (date.getDate() & 0x1f);
  return { dosTime, dosDate };
}

/** DOS 时间还原为本地时间 Date */
function fromDosDateTime(dosTime, dosDate) {
  const year = 1980 + ((dosDate >> 9) & 0x7f);
  const month = ((dosDate >> 5) & 0x0f) - 1;
  const day = dosDate & 0x1f;
  const hour = (dosTime >> 11) & 0x1f;
  const minute = (dosTime >> 5) & 0x3f;
  const second = (dosTime & 0x1f) * 2;
  const result = new Date(year, Math.max(month, 0), Math.max(day, 1), hour, minute, second);
  if (Number.isNaN(result.getTime())) {
    return new Date();
  }
  return result;
}

/** 带背压的写块（await drain） */
async function writeChunk(out, chunk) {
  if (out.destroyed) {
    throw new Error('压缩输出流已关闭，写入失败');
  }
  const ready = out.write(chunk);
  if (ready) {
    return;
  }
  await new Promise((resolve, reject) => {
    const onDrain = () => {
      cleanup();
      resolve();
    };
    const onError = (err) => {
      cleanup();
      reject(err);
    };
    function cleanup() {
      out.removeListener('drain', onDrain);
      out.removeListener('error', onError);
    }
    out.once('drain', onDrain);
    out.once('error', onError);
  });
}

/** 归一化 zip 内部路径（使用 / 分隔，不允许绝对路径与 ..） */
function normalizeEntryName(name) {
  if (typeof name !== 'string' || name.trim().length === 0) {
    throw new Error('压缩条目的名称不能为空');
  }
  const unified = name.replace(/\\/g, '/').replace(/^\/+/, '');
  const segments = unified.split('/').filter((segment) => segment.length > 0 && segment !== '.');
  if (segments.length === 0) {
    throw new Error(`压缩条目的名称不合法：${name}`);
  }
  if (segments.some((segment) => segment === '..')) {
    throw new Error(`压缩条目的名称不能包含 ..：${name}`);
  }
  return segments.join('/');
}

/* ------------------------------ 生成 zip ------------------------------ */

/** 递归收集待压缩条目（{absPath, name} → 展开为文件/目录清单） */
async function collectEntries(entries) {
  const result = [];
  const queue = entries.map((entry) => ({ absPath: entry.absPath, name: entry.name }));
  while (queue.length > 0) {
    const current = queue.shift();
    const abs = path.resolve(current.absPath);
    const entryName = normalizeEntryName(current.name);
    const stat = await fs.promises.stat(abs);
    if (stat.isDirectory()) {
      result.push({ abs, name: `${entryName}/`, stat, dir: true });
      const children = await fs.promises.readdir(abs, { withFileTypes: true });
      children.sort((a, b) => a.name.localeCompare(b.name, 'zh-CN'));
      for (const child of children) {
        queue.push({ absPath: path.join(abs, child.name), name: `${entryName}/${child.name}` });
      }
    } else if (stat.isFile()) {
      result.push({ abs, name: entryName, stat, dir: false });
    } else {
      throw new Error(`不支持的压缩源类型（仅支持普通文件与目录）：${entryName}`);
    }
    if (result.length > MAX_ENTRY_COUNT) {
      throw new Error(`压缩条目超过 ${MAX_ENTRY_COUNT} 个，暂不支持 zip64 格式`);
    }
  }
  return result;
}

function buildLocalHeader({ nameBuf, dosTime, dosDate, flags, method, crc, compSize, rawSize }) {
  const header = Buffer.alloc(30);
  header.writeUInt32LE(0x04034b50, 0);
  header.writeUInt16LE(20, 4);
  header.writeUInt16LE(flags, 6);
  header.writeUInt16LE(method, 8);
  header.writeUInt16LE(dosTime, 10);
  header.writeUInt16LE(dosDate, 12);
  header.writeUInt32LE(crc >>> 0, 14);
  header.writeUInt32LE(compSize >>> 0, 18);
  header.writeUInt32LE(rawSize >>> 0, 22);
  header.writeUInt16LE(nameBuf.length, 26);
  header.writeUInt16LE(0, 28);
  return Buffer.concat([header, nameBuf]);
}

function buildCentralHeader({
  nameBuf,
  dosTime,
  dosDate,
  flags,
  method,
  crc,
  compSize,
  rawSize,
  localOffset,
  externalAttr
}) {
  const header = Buffer.alloc(46);
  header.writeUInt32LE(0x02014b50, 0);
  header.writeUInt16LE(20, 4);
  header.writeUInt16LE(20, 6);
  header.writeUInt16LE(flags, 8);
  header.writeUInt16LE(method, 10);
  header.writeUInt16LE(dosTime, 12);
  header.writeUInt16LE(dosDate, 14);
  header.writeUInt32LE(crc >>> 0, 16);
  header.writeUInt32LE(compSize >>> 0, 20);
  header.writeUInt32LE(rawSize >>> 0, 24);
  header.writeUInt16LE(nameBuf.length, 28);
  header.writeUInt16LE(0, 30);
  header.writeUInt16LE(0, 32);
  header.writeUInt16LE(0, 34);
  header.writeUInt16LE(0, 36);
  header.writeUInt32LE(externalAttr >>> 0, 38);
  header.writeUInt32LE(localOffset >>> 0, 42);
  return Buffer.concat([header, nameBuf]);
}

/**
 * 生成 zip 文件。
 * entries: [{ absPath, name }]，name 为 zip 内相对路径（用 / 分隔）
 * 返回写入的字节数。
 */
async function createZip(entries, outPath) {
  if (!Array.isArray(entries) || entries.length === 0) {
    throw new Error('没有可压缩的文件或目录');
  }
  if (typeof outPath !== 'string' || outPath.trim().length === 0) {
    throw new Error('压缩包输出路径不能为空');
  }
  const list = await collectEntries(entries);
  if (list.length === 0) {
    throw new Error('没有可压缩的文件或目录');
  }
  if (list.length > MAX_ENTRY_COUNT) {
    throw new Error(`压缩条目超过 ${MAX_ENTRY_COUNT} 个，暂不支持 zip64 格式`);
  }
  for (const item of list) {
    if (!item.dir && item.stat.size >= MAX_ENTRY_SIZE) {
      throw new Error(`文件过大（≥4GB），暂不支持 zip64 格式：${item.name}`);
    }
  }

  const absOut = path.resolve(outPath);
  await fs.promises.mkdir(path.dirname(absOut), { recursive: true });

  const out = fs.createWriteStream(absOut, { flags: 'w' });
  let written = 0;
  const centralEntries = [];

  const writer = {
    async write(chunk) {
      await writeChunk(out, chunk);
      written += chunk.length;
    }
  };

  try {
    for (const item of list) {
      const nameBuf = Buffer.from(item.name, 'utf8');
      const { dosTime, dosDate } = dosDateTime(item.stat.mtime);
      const localOffset = written;

      if (item.dir) {
        await writer.write(
          buildLocalHeader({
            nameBuf,
            dosTime,
            dosDate,
            flags: 0x0800,
            method: 0,
            crc: 0,
            compSize: 0,
            rawSize: 0
          })
        );
        centralEntries.push({
          nameBuf,
          dosTime,
          dosDate,
          flags: 0x0800,
          method: 0,
          crc: 0,
          compSize: 0,
          rawSize: 0,
          localOffset,
          externalAttr: (0o40755 << 16) | 0x10
        });
        continue;
      }

      // 文件：先写占位本地头（通用标志位 bit3 表示 CRC/大小在数据描述符中）
      const flags = 0x0808;
      await writer.write(
        buildLocalHeader({
          nameBuf,
          dosTime,
          dosDate,
          flags,
          method: 8,
          crc: 0,
          compSize: 0,
          rawSize: 0
        })
      );

      let crc = 0;
      let rawSize = 0;
      let compSize = 0;

      const crcCounter = new Transform({
        transform(chunk, encoding, callback) {
          crc = crc32Update(crc, chunk);
          rawSize += chunk.length;
          callback(null, chunk);
        }
      });

      const sink = new Writable({
        highWaterMark: 64 * 1024,
        write(chunk, encoding, callback) {
          compSize += chunk.length;
          writer.write(chunk).then(() => callback(), callback);
        }
      });

      await pipeline(fs.createReadStream(item.abs), crcCounter, zlib.createDeflateRaw({ level: DEFLATE_LEVEL }), sink);

      const finalCrc = crc32Finish(crc);
      const descriptor = Buffer.alloc(16);
      descriptor.writeUInt32LE(0x08074b50, 0);
      descriptor.writeUInt32LE(finalCrc >>> 0, 4);
      descriptor.writeUInt32LE(compSize >>> 0, 8);
      descriptor.writeUInt32LE(rawSize >>> 0, 12);
      await writer.write(descriptor);

      centralEntries.push({
        nameBuf,
        dosTime,
        dosDate,
        flags,
        method: 8,
        crc: finalCrc,
        compSize,
        rawSize,
        localOffset,
        externalAttr: (0o100644 << 16) >>> 0
      });
    }

    const centralOffset = written;
    let centralSize = 0;
    for (const item of centralEntries) {
      const record = buildCentralHeader(item);
      await writer.write(record);
      centralSize += record.length;
    }

    const eocd = Buffer.alloc(22);
    eocd.writeUInt32LE(0x06054b50, 0);
    eocd.writeUInt16LE(0, 4);
    eocd.writeUInt16LE(0, 6);
    eocd.writeUInt16LE(centralEntries.length, 8);
    eocd.writeUInt16LE(centralEntries.length, 10);
    eocd.writeUInt32LE(centralSize >>> 0, 12);
    eocd.writeUInt32LE(centralOffset >>> 0, 16);
    eocd.writeUInt16LE(0, 20);
    await writer.write(eocd);

    await new Promise((resolve, reject) => {
      out.end(() => resolve());
      out.once('error', reject);
    });
    return written;
  } catch (err) {
    out.destroy();
    await fs.promises.rm(absOut, { force: true }).catch((removeErr) => {
      console.warn('[压缩] 清理未完成的压缩包失败：', removeErr.message);
    });
    throw err;
  }
}

/* ------------------------------ 解压 zip ------------------------------ */

/** 校验 zip 条目名，返回 { abs, isDir }；非法时抛出中文错误 */
function resolveEntryTarget(destDir, entryName) {
  if (typeof entryName !== 'string' || entryName.length === 0) {
    throw new Error('压缩包中存在空的条目名，已拒绝解压');
  }
  if (entryName.indexOf('\u0000') >= 0) {
    throw new Error('压缩包条目名包含非法字符，已拒绝解压');
  }
  if (/^[A-Za-z]:/.test(entryName)) {
    throw new Error(`压缩包条目包含盘符路径，已拒绝解压：${entryName}`);
  }
  if (entryName.startsWith('/') || entryName.startsWith('\\')) {
    throw new Error(`压缩包条目包含绝对路径，已拒绝解压：${entryName}`);
  }
  const isDir = /[\/\\]$/.test(entryName);
  const unified = entryName.replace(/\\/g, '/');
  const segments = unified.split('/').filter((segment) => segment.length > 0 && segment !== '.');
  if (segments.some((segment) => segment === '..')) {
    throw new Error(`压缩包条目包含路径穿越（..），已拒绝解压：${entryName}`);
  }
  if (segments.length === 0) {
    return null;
  }
  const resolved = safeResolve(destDir, `/${segments.join('/')}`);
  if (!resolved) {
    throw new Error(`压缩包条目越出解压目标目录，已拒绝解压：${entryName}`);
  }
  return { abs: resolved.abs, isDir };
}

/** 读取指定偏移的字节数，读不满时抛出中文错误 */
async function readExact(handle, length, position) {
  const buffer = Buffer.alloc(length);
  let offset = 0;
  while (offset < length) {
    const { bytesRead } = await handle.read(buffer, offset, length - offset, position + offset);
    if (bytesRead <= 0) {
      throw new Error('压缩包文件已损坏（数据不完整）');
    }
    offset += bytesRead;
  }
  return buffer;
}

/** 定位并解析中央目录 */
async function readCentralDirectory(handle, fileSize) {
  if (fileSize < 22) {
    throw new Error('不是有效的 zip 文件（文件过小）');
  }
  const tailLength = Math.min(fileSize, 65557);
  const tail = await readExact(handle, tailLength, fileSize - tailLength);
  let eocdIndex = -1;
  for (let i = tail.length - 22; i >= 0; i -= 1) {
    if (tail.readUInt32LE(i) === 0x06054b50) {
      eocdIndex = i;
      break;
    }
  }
  if (eocdIndex < 0) {
    throw new Error('不是有效的 zip 文件（未找到中央目录结束记录）');
  }
  const entryCount = tail.readUInt16LE(eocdIndex + 10);
  const cdSize = tail.readUInt32LE(eocdIndex + 12);
  const cdOffset = tail.readUInt32LE(eocdIndex + 16);
  if (entryCount === 0xffff || cdSize === 0xffffffff || cdOffset === 0xffffffff) {
    throw new Error('暂不支持 zip64 格式的压缩包');
  }
  if (cdOffset + cdSize > fileSize) {
    throw new Error('压缩包文件已损坏（中央目录超出文件范围）');
  }
  const cd = await readExact(handle, cdSize, cdOffset);
  const entries = [];
  let pointer = 0;
  for (let i = 0; i < entryCount; i += 1) {
    if (pointer + 46 > cd.length) {
      throw new Error('压缩包文件已损坏（中央目录条目不完整）');
    }
    if (cd.readUInt32LE(pointer) !== 0x02014b50) {
      throw new Error('压缩包文件已损坏（中央目录签名不正确）');
    }
    const versionNeeded = cd.readUInt16LE(pointer + 6);
    const flags = cd.readUInt16LE(pointer + 8);
    const method = cd.readUInt16LE(pointer + 10);
    const dosTime = cd.readUInt16LE(pointer + 12);
    const dosDate = cd.readUInt16LE(pointer + 14);
    const crc = cd.readUInt32LE(pointer + 16);
    const compSize = cd.readUInt32LE(pointer + 20);
    const rawSize = cd.readUInt32LE(pointer + 24);
    const nameLength = cd.readUInt16LE(pointer + 28);
    const extraLength = cd.readUInt16LE(pointer + 30);
    const commentLength = cd.readUInt16LE(pointer + 32);
    const externalAttr = cd.readUInt32LE(pointer + 38);
    const localOffset = cd.readUInt32LE(pointer + 42);
    if (pointer + 46 + nameLength > cd.length) {
      throw new Error('压缩包文件已损坏（条目名超出中央目录范围）');
    }
    const name = cd.toString('utf8', pointer + 46, pointer + 46 + nameLength);
    entries.push({
      name,
      versionNeeded,
      flags,
      method,
      dosTime,
      dosDate,
      crc,
      compSize,
      rawSize,
      externalAttr,
      localOffset
    });
    pointer += 46 + nameLength + extraLength + commentLength;
  }
  return entries;
}

/**
 * 解压 zip 到 destDir（目录不存在会自动创建）。
 * 返回解压条目数（含目录条目）。
 */
async function extractZip(zipPath, destDir) {
  if (typeof zipPath !== 'string' || zipPath.trim().length === 0) {
    throw new Error('待解压的压缩包路径不能为空');
  }
  if (typeof destDir !== 'string' || destDir.trim().length === 0) {
    throw new Error('解压目标目录不能为空');
  }
  const absZip = path.resolve(zipPath);
  const absDest = path.resolve(destDir);
  const stat = await fs.promises.stat(absZip);
  if (!stat.isFile()) {
    throw new Error('待解压的不是普通文件');
  }
  await fs.promises.mkdir(absDest, { recursive: true });

  const handle = await fs.promises.open(absZip, 'r');
  let count = 0;
  try {
    const entries = await readCentralDirectory(handle, stat.size);

    // 先整体校验路径，避免解压到一半才发现非法条目
    const plan = [];
    for (const entry of entries) {
      if ((entry.flags & 0x0001) !== 0) {
        throw new Error(`暂不支持加密的 zip 条目：${entry.name}`);
      }
      if (entry.method !== 0 && entry.method !== 8) {
        throw new Error(`不支持的压缩算法（仅支持 stored/deflate）：${entry.name}`);
      }
      if (entry.compSize === 0xffffffff || entry.rawSize === 0xffffffff) {
        throw new Error(`暂不支持 zip64 格式的压缩包：${entry.name}`);
      }
      const target = resolveEntryTarget(absDest, entry.name);
      if (target) {
        plan.push({ entry, target });
      }
    }

    for (const item of plan) {
      const { entry, target } = item;
      const mtime = fromDosDateTime(entry.dosTime, entry.dosDate);

      if (target.isDir) {
        await fs.promises.mkdir(target.abs, { recursive: true });
        await fs.promises.utimes(target.abs, mtime, mtime).catch((timeErr) => {
          console.warn('[解压] 设置目录时间失败：', target.abs, timeErr.message);
        });
        count += 1;
        continue;
      }

      const localHeader = await readExact(handle, 30, entry.localOffset);
      if (localHeader.readUInt32LE(0) !== 0x04034b50) {
        throw new Error(`压缩包本地文件头损坏：${entry.name}`);
      }
      const localNameLength = localHeader.readUInt16LE(26);
      const localExtraLength = localHeader.readUInt16LE(28);
      const dataOffset = entry.localOffset + 30 + localNameLength + localExtraLength;
      if (dataOffset + entry.compSize > stat.size) {
        throw new Error(`压缩包数据超出文件范围：${entry.name}`);
      }

      await fs.promises.mkdir(path.dirname(target.abs), { recursive: true });

      let crc = 0;
      let rawSize = 0;
      const crcCounter = new Transform({
        transform(chunk, encoding, callback) {
          crc = crc32Update(crc, chunk);
          rawSize += chunk.length;
          callback(null, chunk);
        }
      });

      const outStream = fs.createWriteStream(target.abs, { flags: 'w' });
      try {
        if (entry.compSize === 0) {
          outStream.end();
          await new Promise((resolve, reject) => {
            outStream.once('finish', resolve);
            outStream.once('error', reject);
          });
        } else {
          const source =
            entry.compSize > 0
              ? fs.createReadStream(absZip, { start: dataOffset, end: dataOffset + entry.compSize - 1 })
              : null;
          if (entry.method === 8) {
            await pipeline(source, zlib.createInflateRaw(), crcCounter, outStream);
          } else {
            await pipeline(source, crcCounter, outStream);
          }
        }
      } catch (err) {
        outStream.destroy();
        await fs.promises.rm(target.abs, { force: true }).catch((removeErr) => {
          console.warn('[解压] 清理未完成文件失败：', removeErr.message);
        });
        throw err;
      }

      if (entry.compSize > 0) {
        const finalCrc = crc32Finish(crc);
        if (finalCrc !== (entry.crc >>> 0)) {
          await fs.promises.rm(target.abs, { force: true }).catch((removeErr) => {
            console.warn('[解压] 删除 CRC 校验失败的文件失败：', removeErr.message);
          });
          throw new Error(`压缩包条目 CRC 校验失败，文件可能已损坏：${entry.name}`);
        }
      }

      await fs.promises.utimes(target.abs, mtime, mtime).catch((timeErr) => {
        console.warn('[解压] 设置文件时间失败：', target.abs, timeErr.message);
      });
      count += 1;
    }

    return count;
  } finally {
    await handle.close().catch((closeErr) => {
      console.warn('[解压] 关闭压缩包失败：', closeErr.message);
    });
  }
}

module.exports = {
  createZip,
  extractZip,
  isZipPath,
  crc32Update,
  crc32Finish
};
