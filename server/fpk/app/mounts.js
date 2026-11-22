'use strict';

/**
 * 网盘挂载点模块
 * - 把 NAS 上的任意绝对目录「挂载」到文件管理的虚拟路径 /m/<name> 下
 * - 挂载信息保存在数据库 mounts 表，文件实体本身不做任何移动
 * - 虚拟路径解析、权限探测统一在这里实现，files.js 只负责路由与响应
 */

const fs = require('fs');
const path = require('path');

const db = require('./db');
const { FILES_ROOT, safeResolve, ensureDir } = require('./util');

/** 挂载点在虚拟路径中的前缀 */
const MOUNT_PREFIX = '/m';

/** 统一的权限指引文案（飞牛应用文件夹权限） */
const PERMISSION_HINT =
  '请在飞牛「设置 → 应用 → 飞牛终端 → 文件夹权限」中授予该目录的访问权限后重试';

/**
 * 探测绝对路径的可用性。
 * 返回 { exists, isDir, readable, writable, error }，error 为简洁中文描述。
 */
async function probePath(absPath) {
  const result = { exists: false, isDir: false, readable: false, writable: false, error: null };
  const target = typeof absPath === 'string' ? absPath.trim() : '';
  if (!target || !path.isAbsolute(target)) {
    result.error = '路径必须是以 / 开头的绝对路径';
    return result;
  }
  let stat;
  try {
    stat = await fs.promises.stat(target);
  } catch (err) {
    if (err && err.code === 'ENOENT') {
      result.error = '目录不存在';
    } else if (err && (err.code === 'EACCES' || err.code === 'EPERM')) {
      result.error = '没有读取权限（EACCES）';
    } else if (err && err.code === 'ENOTDIR') {
      result.error = '路径中包含的不是目录';
    } else {
      result.error = `无法访问该路径：${err && err.message ? err.message : '未知错误'}`;
    }
    return result;
  }
  result.exists = true;
  result.isDir = stat.isDirectory();
  if (!result.isDir) {
    result.error = '目标不是目录';
    return result;
  }
  try {
    await fs.promises.access(target, fs.constants.R_OK);
    result.readable = true;
  } catch (err) {
    result.error = err && (err.code === 'EACCES' || err.code === 'EPERM')
      ? '没有读取权限（EACCES）'
      : `没有读取权限（${err && err.code ? err.code : '未知错误'}）`;
    return result;
  }
  try {
    await fs.promises.access(target, fs.constants.R_OK | fs.constants.W_OK);
    result.writable = true;
  } catch (err) {
    result.error = err && (err.code === 'EACCES' || err.code === 'EPERM')
      ? '没有写入权限'
      : `没有写入权限（${err && err.code ? err.code : '未知错误'}）`;
  }
  return result;
}

/** 挂载点数据库行转前端对象（含挂载点本身，不含实时探测结果） */
function serializeMount(row) {
  if (!row) {
    return null;
  }
  return {
    id: row.id,
    name: row.name,
    path: `${MOUNT_PREFIX}/${row.name}`,
    absPath: path.resolve(row.abs_path),
    readonly: Boolean(row.readonly),
    createdAt: row.created_at
  };
}

/** 挂载点数据库行转前端对象（附带 exists/readable/writable 实时探测结果） */
async function enrichMount(row) {
  const item = serializeMount(row);
  const probe = await probePath(item.absPath);
  return Object.assign(item, {
    exists: probe.exists,
    readable: probe.readable,
    writable: probe.writable,
    error: probe.error
  });
}

/** 列出全部挂载点并附带实时可用性 */
async function listMountsEnriched() {
  const rows = db.listMounts();
  const items = [];
  for (const row of rows) {
    items.push(await enrichMount(row));
  }
  return items;
}

/**
 * 把虚拟路径解析为真实绝对路径。
 * - '/' → FILES_ROOT
 * - '/m/<name>/sub/...' → 对应挂载点目录下的子路径（禁止越出挂载根）
 * - 其他 → 回退到 FILES_ROOT 语义
 * 非法路径返回 null。
 */
function resolveVirtual(virtualPath) {
  const raw = typeof virtualPath === 'string' ? virtualPath : '/';
  const unified = '/' + raw.replace(/\\/g, '/');
  const rawSegments = unified.split('/').filter((seg) => seg.length > 0);
  if (rawSegments.some((seg) => seg === '..')) {
    return null;
  }
  const segments = rawSegments.filter((seg) => seg !== '.');

  if (segments.length === 0) {
    return { abs: path.resolve(FILES_ROOT), virtual: '/', readonly: false, mount: null };
  }

  if (segments[0] === MOUNT_PREFIX.slice(1) && segments.length >= 2) {
    const mountRow = db.getMountByName(segments[1]);
    if (mountRow) {
      const rootAbs = path.resolve(mountRow.abs_path);
      const abs = path.resolve(rootAbs, ...segments.slice(2));
      if (abs !== rootAbs && !abs.startsWith(rootAbs + path.sep)) {
        return null;
      }
      return {
        abs,
        virtual: '/' + segments.join('/'),
        readonly: Boolean(mountRow.readonly),
        mount: serializeMount(mountRow)
      };
    }
  }

  const fallback = safeResolve(FILES_ROOT, virtualPath);
  if (!fallback) {
    return null;
  }
  return { abs: fallback.abs, virtual: fallback.virtual, readonly: false, mount: null };
}

/** 启动时确保应用私有目录存在（挂载目录本身不创建，避免误建空目录） */
function ensureMountAbsDirs() {
  ensureDir(FILES_ROOT);
  return FILES_ROOT;
}

/** 校验挂载目标是否可用：不存在 / 不是目录 / 不可读时返回中文错误字符串 */
async function verifyMountAccess(absPath) {
  const probe = await probePath(absPath);
  if (!probe.exists) {
    return '目录不存在';
  }
  if (!probe.isDir) {
    return '目标不是目录';
  }
  if (!probe.readable) {
    return probe.error || '没有读取权限';
  }
  return null;
}

module.exports = {
  MOUNT_PREFIX,
  PERMISSION_HINT,
  probePath,
  serializeMount,
  enrichMount,
  listMountsEnriched,
  resolveVirtual,
  ensureMountAbsDirs,
  verifyMountAccess
};
