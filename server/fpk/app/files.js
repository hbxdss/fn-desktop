'use strict';

/**
 * 文件管理路由：/api/files
 * 支持目录浏览、分块断点续传上传、带 Range 的下载、重命名、删除（默认进回收站）、
 * 新建文件夹、递归搜索、在线预览、压缩/解压、批量操作、收藏夹与最近访问。
 * 所有路径都限制在 FILES_ROOT（应用私有目录）与已挂载的 NAS 目录之内，拒绝目录穿越。
 * 挂载点统一以 /m/<name> 形式暴露，解析逻辑见 mounts.js。
 */

const crypto = require('crypto');
const fs = require('fs');
const path = require('path');
const { pipeline } = require('stream/promises');

const auth = require('./auth');
const db = require('./db');
const mounts = require('./mounts');
const { createZip, extractZip, isZipPath } = require('./zip');
const {
  FILES_ROOT,
  DATA_DIR,
  PERMS,
  ROLES,
  ensureDir,
  sendJson,
  sendError,
  readJson,
  mimeOf,
  safeName,
  clientIp,
  joinVirtual
} = require('./util');

/** 回收站文件实体目录：DATA_DIR/trash/<uuid><原扩展名> */
const TRASH_DIR = path.join(DATA_DIR, 'trash');
/** 搜索遍历上限 */
const SEARCH_MAX_ENTRIES = 20000;
const SEARCH_MAX_DEPTH = 12;
const SEARCH_DEFAULT_LIMIT = 200;
/** 在线预览文本类文件大小上限（15MB） */
const PREVIEW_MAX_TEXT_BYTES = 15 * 1024 * 1024;

const PREVIEW_IMAGE_EXTS = new Set(['png', 'jpg', 'jpeg', 'gif', 'webp', 'bmp', 'ico', 'svg']);
const PREVIEW_AUDIO_EXTS = new Set(['mp3', 'wav', 'flac', 'aac', 'ogg', 'oga', 'm4a', 'opus', 'wma']);
const PREVIEW_VIDEO_EXTS = new Set(['mp4', 'webm', 'ogv', 'mov', 'mkv', 'avi', 'm4v']);
const PREVIEW_PDF_EXTS = new Set(['pdf']);
const PREVIEW_TEXT_EXTS = new Set([
  'txt', 'log', 'md', 'markdown', 'json', 'xml', 'csv', 'tsv', 'ini', 'conf', 'cfg', 'yml', 'yaml',
  'html', 'htm', 'css', 'js', 'mjs', 'cjs', 'ts', 'jsx', 'tsx', 'vue', 'sh', 'bash', 'bat', 'cmd',
  'ps1', 'py', 'java', 'c', 'h', 'cpp', 'hpp', 'go', 'rs', 'sql', 'toml', 'env', 'properties',
  'srt', 'lrc'
]);

/** 预览使用的安全 MIME 白名单（文本类统一按 text/plain 返回，避免内联脚本执行） */
const PREVIEW_MIME = Object.freeze({
  png: 'image/png',
  jpg: 'image/jpeg',
  jpeg: 'image/jpeg',
  gif: 'image/gif',
  webp: 'image/webp',
  bmp: 'image/bmp',
  ico: 'image/x-icon',
  svg: 'image/svg+xml',
  mp3: 'audio/mpeg',
  wav: 'audio/wav',
  flac: 'audio/flac',
  aac: 'audio/aac',
  ogg: 'audio/ogg',
  oga: 'audio/ogg',
  m4a: 'audio/mp4',
  opus: 'audio/opus',
  wma: 'audio/x-ms-wma',
  mp4: 'video/mp4',
  webm: 'video/webm',
  ogv: 'video/ogg',
  mov: 'video/quicktime',
  mkv: 'video/x-matroska',
  avi: 'video/x-msvideo',
  m4v: 'video/x-m4v',
  pdf: 'application/pdf'
});

function audit(ctx, action, detail) {
  db.addAudit({
    userId: ctx.user.id,
    username: ctx.user.username,
    action,
    detail,
    ip: clientIp(ctx.req)
  });
}

function fsErrorMessage(err) {
  switch (err && err.code) {
    case 'ENOENT':
      return '文件或目录不存在';
    case 'EACCES':
    case 'EPERM':
      return '没有访问该文件或目录的操作系统权限';
    case 'EEXIST':
      return '同名文件或目录已存在';
    case 'ENOTDIR':
      return '路径中包含的不是目录';
    case 'EISDIR':
      return '目标是目录，不能按文件处理';
    case 'ENOSPC':
      return '磁盘空间不足';
    case 'EBUSY':
      return '文件正被其他程序占用';
    case 'ENAMETOOLONG':
      return '文件或目录名过长';
    default:
      return `文件操作失败：${err && err.message ? err.message : '未知错误'}`;
  }
}

function statusForFsError(err) {
  switch (err && err.code) {
    case 'ENOENT':
      return 404;
    case 'EACCES':
    case 'EPERM':
      return 403;
    case 'EEXIST':
      return 409;
    case 'ENOSPC':
      return 507;
    default:
      return 500;
  }
}

function failFs(res, err) {
  sendError(res, statusForFsError(err), fsErrorMessage(err), (err && err.code) || 'FS_ERROR');
}

/** 解析虚拟路径（支持 /m/<name> 挂载点），失败时写出 400 响应 */
function resolveOrFail(res, virtualPath) {
  const resolved = mounts.resolveVirtual(virtualPath || '/');
  if (!resolved) {
    sendError(res, 400, '非法的路径，禁止访问数据目录以外的位置', 'INVALID_PATH');
    return null;
  }
  return resolved;
}

/** 只读挂载点禁止写操作；命中时写出 403 响应并返回 true */
function rejectReadonly(res, resolved) {
  if (resolved && resolved.readonly) {
    sendError(res, 403, '该挂载点是只读的，无法修改', 'READONLY_MOUNT');
    return true;
  }
  return false;
}

/** 是否管理员（超级管理员或管理员） */
function isAdminUser(user) {
  return Boolean(user) && (user.role === ROLES.ADMIN || user.role === ROLES.SUPER);
}

/** 解析布尔型查询/请求体参数 */
function isTruthy(value) {
  return value === true || value === 1 || value === '1' || value === 'true';
}

function ensureTrashDir() {
  ensureDir(DATA_DIR);
  ensureDir(TRASH_DIR);
  return TRASH_DIR;
}

/** 判断路径是否存在（lstat，不跟随符号链接） */
async function pathExists(abs) {
  try {
    await fs.promises.lstat(abs);
    return true;
  } catch (err) {
    if (!err || err.code !== 'ENOENT') {
      console.warn('[文件] 检查路径是否存在失败：', abs, err && err.message);
    }
    return false;
  }
}

/** 递归统计目录大小（跳过符号链接，条目数上限防止死循环） */
async function pathSize(abs, state) {
  const counter = state || { count: 0 };
  const stat = await fs.promises.lstat(abs).catch((err) => {
    if (err && err.code !== 'ENOENT') {
      console.warn('[文件] 统计大小时读取失败：', abs, err.message);
    }
    return null;
  });
  if (!stat) {
    return 0;
  }
  if (!stat.isDirectory() || stat.isSymbolicLink()) {
    return stat.size;
  }
  let total = 0;
  const entries = await fs.promises.readdir(abs, { withFileTypes: true }).catch((err) => {
    console.warn('[文件] 统计大小时枚举目录失败：', abs, err.message);
    return [];
  });
  for (const entry of entries) {
    counter.count += 1;
    if (counter.count > 200000) {
      break;
    }
    if (entry.isSymbolicLink()) {
      continue;
    }
    total += await pathSize(path.join(abs, entry.name), counter);
  }
  return total;
}

/** 在目标目录下生成不冲突的虚拟路径，重名时自动追加 (1)、(2)… */
async function uniqueTarget(dirVirtual, name) {
  const first = mounts.resolveVirtual(joinVirtual(dirVirtual, name));
  if (!first) {
    return null;
  }
  if (!(await pathExists(first.abs))) {
    return first;
  }
  const ext = path.extname(name);
  const base = ext.length > 0 && ext !== name ? name.slice(0, name.length - ext.length) : name;
  for (let index = 1; index <= 999; index += 1) {
    const candidate = mounts.resolveVirtual(joinVirtual(dirVirtual, `${base} (${index})${ext}`));
    if (!candidate) {
      continue;
    }
    if (!(await pathExists(candidate.abs))) {
      return candidate;
    }
  }
  return null;
}

/** 复制文件或目录 */
async function copyPath(srcAbs, destAbs, isDir) {
  if (isDir) {
    await fs.promises.cp(srcAbs, destAbs, {
      recursive: true,
      errorOnExist: true,
      force: false,
      dereference: false
    });
    return;
  }
  await fs.promises.copyFile(srcAbs, destAbs);
}

/** 移动文件或目录，跨设备（EXDEV）时回退为「复制后删除」 */
async function movePath(srcAbs, destAbs, isDir) {
  try {
    await fs.promises.rename(srcAbs, destAbs);
    return;
  } catch (err) {
    if (!err || err.code !== 'EXDEV') {
      throw err;
    }
    console.warn('[文件] 跨设备移动，回退为复制后删除：', srcAbs, '->', destAbs);
  }
  await copyPath(srcAbs, destAbs, isDir);
  if (isDir) {
    await fs.promises.rm(srcAbs, { recursive: true, force: true });
  } else {
    await fs.promises.unlink(srcAbs);
  }
}

/** 回收站中文件实体的绝对路径（由 uuid + 原扩展名决定） */
function trashStoredPath(row) {
  const ext = row.is_dir ? '' : path.extname(row.name);
  return path.join(TRASH_DIR, `${row.uuid}${ext}`);
}

/** 把文件/目录移入回收站并写库，返回 uuid 与统计大小 */
async function moveToTrash(user, resolved, stat) {
  ensureTrashDir();
  const uuid = crypto.randomUUID();
  const isDir = stat.isDirectory() && !stat.isSymbolicLink();
  const name = path.basename(resolved.abs);
  const storedAbs = path.join(TRASH_DIR, `${uuid}${isDir ? '' : path.extname(name)}`);
  const size = await pathSize(resolved.abs);
  db.addTrashRecord({
    uuid,
    originalPath: resolved.virtual,
    name,
    isDir,
    size,
    ownerId: user.id,
    ownerName: user.username
  });
  try {
    await movePath(resolved.abs, storedAbs, isDir);
  } catch (err) {
    db.deleteTrashRecord(uuid);
    throw err;
  }
  return { uuid, size, name, isDir };
}

/** 预览类型判定与 MIME 换算 */
function previewContentType(ext) {
  if (PREVIEW_IMAGE_EXTS.has(ext)) {
    return PREVIEW_MIME[ext] || 'application/octet-stream';
  }
  if (PREVIEW_PDF_EXTS.has(ext)) {
    return 'application/pdf';
  }
  if (PREVIEW_AUDIO_EXTS.has(ext)) {
    return PREVIEW_MIME[ext] || 'audio/mpeg';
  }
  if (PREVIEW_VIDEO_EXTS.has(ext)) {
    return PREVIEW_MIME[ext] || 'video/mp4';
  }
  if (PREVIEW_TEXT_EXTS.has(ext)) {
    return 'text/plain; charset=utf-8';
  }
  return null;
}

/** 带 Range 的响应输出（200/206/416），供预览与分享下载复用 */
function sendFileWithRange(ctx, absPath, stat, baseHeaders) {
  const headers = Object.assign({}, baseHeaders, { 'Accept-Ranges': 'bytes' });
  const rangeHeader = ctx.req.headers['range'];
  let start = 0;
  let end = stat.size - 1;
  let status = 200;

  if (typeof rangeHeader === 'string' && rangeHeader.length > 0) {
    const match = rangeHeader.match(/^bytes=(\d*)-(\d*)$/);
    if (!match || (match[1] === '' && match[2] === '')) {
      ctx.res.writeHead(416, { 'Content-Range': `bytes */${stat.size}` });
      ctx.res.end();
      return;
    }
    if (match[1] !== '') {
      start = Number(match[1]);
      end = match[2] !== '' ? Number(match[2]) : stat.size - 1;
    } else {
      const suffixLength = Number(match[2]);
      start = Math.max(stat.size - suffixLength, 0);
      end = stat.size - 1;
    }
    if (!Number.isFinite(start) || !Number.isFinite(end) || start > end || start >= stat.size) {
      ctx.res.writeHead(416, { 'Content-Range': `bytes */${stat.size}` });
      ctx.res.end();
      return;
    }
    end = Math.min(end, stat.size - 1);
    status = 206;
    headers['Content-Range'] = `bytes ${start}-${end}/${stat.size}`;
  }

  headers['Content-Length'] = Math.max(end - start + 1, 0);
  ctx.res.writeHead(status, headers);
  if (stat.size === 0) {
    ctx.res.end();
    return;
  }
  const stream = fs.createReadStream(absPath, { start, end });
  stream.on('error', (err) => {
    console.error('[文件] 读取流出错：', err.message);
    ctx.res.destroy();
  });
  stream.pipe(ctx.res);
}

async function handleList(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_READ, '浏览文件')) {
    return;
  }
  const resolved = resolveOrFail(ctx.res, ctx.url.searchParams.get('path'));
  if (!resolved) {
    return;
  }
  const stat = await fs.promises.stat(resolved.abs).catch(() => null);
  if (!stat) {
    sendError(ctx.res, 404, '目录不存在', 'NOT_FOUND');
    return;
  }
  if (!stat.isDirectory()) {
    sendError(ctx.res, 400, '目标不是目录', 'NOT_A_DIRECTORY');
    return;
  }
  let entries;
  try {
    entries = await fs.promises.readdir(resolved.abs, { withFileTypes: true });
  } catch (err) {
    if (err && (err.code === 'EACCES' || err.code === 'EPERM')) {
      sendError(ctx.res, 403, `没有读取权限。${mounts.PERMISSION_HINT}：${resolved.virtual}`, 'NO_READ_PERMISSION');
      return;
    }
    failFs(ctx.res, err);
    return;
  }
  const items = [];
  for (const entry of entries) {
    const abs = path.join(resolved.abs, entry.name);
    const info = await fs.promises.lstat(abs).catch(() => null);
    if (!info) {
      continue;
    }
    items.push({
      name: entry.name,
      path: joinVirtual(resolved.virtual, entry.name),
      isDir: entry.isDirectory(),
      isLink: entry.isSymbolicLink(),
      size: entry.isDirectory() ? 0 : info.size,
      mtime: info.mtimeMs,
      ext: entry.isDirectory() ? '' : path.extname(entry.name).replace('.', '').toLowerCase()
    });
  }
  // 根目录额外展示已挂载的 NAS 目录（虚拟目录，实体仍在原位置）
  if (resolved.virtual === '/' && !resolved.mount) {
    const mountItems = await mounts.listMountsEnriched();
    for (const mount of mountItems) {
      items.push({
        name: mount.name,
        path: mount.path,
        isDir: true,
        isLink: false,
        isMount: true,
        readonly: mount.readonly,
        readable: mount.readable,
        writable: mount.writable,
        size: 0,
        mtime: null,
        ext: '',
        mountAbsPath: mount.absPath,
        error: mount.error
      });
    }
  }
  items.sort((a, b) => {
    if (a.isDir !== b.isDir) {
      return a.isDir ? -1 : 1;
    }
    return a.name.localeCompare(b.name, 'zh-CN');
  });
  sendJson(ctx.res, 200, {
    ok: true,
    path: resolved.virtual,
    parent: resolved.virtual === '/' ? null : path.posix.dirname(resolved.virtual),
    items
  });
}

async function handleDownload(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_DOWNLOAD, '下载文件')) {
    return;
  }
  const resolved = resolveOrFail(ctx.res, ctx.url.searchParams.get('path'));
  if (!resolved) {
    return;
  }
  const stat = await fs.promises.stat(resolved.abs).catch(() => null);
  if (!stat) {
    sendError(ctx.res, 404, '文件不存在', 'NOT_FOUND');
    return;
  }
  if (stat.isDirectory()) {
    sendError(ctx.res, 400, '不能直接下载文件夹，请先选择具体文件', 'IS_DIRECTORY');
    return;
  }

  const fileName = path.basename(resolved.abs);
  const asciiFallback = fileName.replace(/[^\x20-\x7e]/g, '_').replace(/"/g, "'");
  const headers = {
    'Content-Type': mimeOf(resolved.abs),
    'Content-Disposition': `attachment; filename="${asciiFallback}"; filename*=UTF-8''${encodeURIComponent(fileName)}`,
    'Accept-Ranges': 'bytes',
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff'
  };

  const rangeHeader = ctx.req.headers['range'];
  let start = 0;
  let end = stat.size - 1;
  let status = 200;

  if (typeof rangeHeader === 'string' && rangeHeader.length > 0) {
    const match = rangeHeader.match(/^bytes=(\d*)-(\d*)$/);
    if (!match) {
      ctx.res.writeHead(416, { 'Content-Range': `bytes */${stat.size}` });
      ctx.res.end();
      return;
    }
    const hasStart = match[1] !== '';
    const hasEnd = match[2] !== '';
    if (!hasStart && !hasEnd) {
      ctx.res.writeHead(416, { 'Content-Range': `bytes */${stat.size}` });
      ctx.res.end();
      return;
    }
    if (hasStart) {
      start = Number(match[1]);
      end = hasEnd ? Number(match[2]) : stat.size - 1;
    } else {
      const suffixLength = Number(match[2]);
      start = Math.max(stat.size - suffixLength, 0);
      end = stat.size - 1;
    }
    if (!Number.isFinite(start) || !Number.isFinite(end) || start > end || start >= stat.size) {
      ctx.res.writeHead(416, { 'Content-Range': `bytes */${stat.size}` });
      ctx.res.end();
      return;
    }
    end = Math.min(end, stat.size - 1);
    status = 206;
    headers['Content-Range'] = `bytes ${start}-${end}/${stat.size}`;
  }

  headers['Content-Length'] = Math.max(end - start + 1, 0);
  ctx.res.writeHead(status, headers);
  if (stat.size === 0) {
    ctx.res.end();
    return;
  }

  const stream = fs.createReadStream(resolved.abs, { start, end });
  stream.on('error', (err) => {
    console.error('[文件] 下载流出错：', err.message);
    ctx.res.destroy();
  });
  stream.pipe(ctx.res);
  audit(ctx, 'FILE_DOWNLOAD', `下载文件 ${resolved.virtual}（${start}-${end}）`);
}

/**
 * 文件夹打包下载：GET /api/files/download-folder?path=/xxx
 * 服务端把整个目录压缩为临时 zip 后流式返回，下载结束后自动删除临时文件。
 */
async function handleDownloadFolder(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_DOWNLOAD, '下载文件夹')) {
    return;
  }
  const resolved = resolveOrFail(ctx.res, ctx.url.searchParams.get('path'));
  if (!resolved) {
    return;
  }
  const stat = await fs.promises.stat(resolved.abs).catch(() => null);
  if (!stat) {
    sendError(ctx.res, 404, '文件夹不存在', 'NOT_FOUND');
    return;
  }
  if (!stat.isDirectory()) {
    sendError(ctx.res, 400, '目标不是文件夹，请直接下载文件', 'NOT_A_DIRECTORY');
    return;
  }

  const baseName = path.basename(resolved.abs) || '下载';
  const zipName = `${safeName(baseName.replace(/[\u0000-\u001f<>:"|?*]/g, '_')) || 'download'}.zip`;
  const tmpDir = ensureDir(path.join(DATA_DIR, 'tmp'));
  const tmpZip = path.join(
    tmpDir,
    `dl-${Date.now()}-${crypto.randomBytes(6).toString('hex')}.zip`
  );

  try {
    await createZip([{ absPath: resolved.abs, name: baseName }], tmpZip);
  } catch (err) {
    fs.promises.unlink(tmpZip).catch(() => {});
    console.error('[文件] 打包文件夹失败：', err && err.message ? err.message : err);
    sendError(ctx.res, 500, `打包文件夹失败：${err && err.message ? err.message : '未知错误'}`, 'ZIP_ERROR');
    return;
  }

  const zipStat = await fs.promises.stat(tmpZip).catch(() => null);
  if (!zipStat) {
    sendError(ctx.res, 500, '打包文件生成失败', 'ZIP_ERROR');
    return;
  }

  // 无论成功与否，响应结束后清理临时压缩包
  const cleanup = () => {
    fs.promises.unlink(tmpZip).catch(() => {});
  };
  ctx.res.on('close', cleanup);
  ctx.res.on('finish', cleanup);

  const asciiFallback = zipName.replace(/[^\x20-\x7e]/g, '_').replace(/"/g, "'");
  sendFileWithRange(ctx, tmpZip, zipStat, {
    'Content-Type': 'application/zip',
    'Content-Disposition': `attachment; filename="${asciiFallback}"; filename*=UTF-8''${encodeURIComponent(zipName)}`,
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff'
  });
  audit(ctx, 'FILE_DOWNLOAD', `打包下载文件夹 ${resolved.virtual}`);
}

async function handleUploadStatus(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_UPLOAD, '上传文件')) {
    return;
  }
  const dirPath = ctx.url.searchParams.get('path') || '/';
  const rawName = ctx.url.searchParams.get('name');
  const name = safeName(rawName);
  if (!name) {
    sendError(ctx.res, 400, '文件名不合法（不能包含路径分隔符或特殊字符）', 'INVALID_NAME');
    return;
  }
  const dir = resolveOrFail(ctx.res, dirPath);
  if (!dir) {
    return;
  }
  const dirStat = await fs.promises.stat(dir.abs).catch(() => null);
  if (!dirStat || !dirStat.isDirectory()) {
    sendError(ctx.res, 404, '目标目录不存在', 'NOT_FOUND');
    return;
  }
  const resolved = mounts.resolveVirtual(joinVirtual(dir.virtual, name));
  if (!resolved) {
    sendError(ctx.res, 400, '非法路径', 'INVALID_PATH');
    return;
  }
  const stat = await fs.promises.stat(resolved.abs).catch(() => null);
  sendJson(ctx.res, 200, {
    ok: true,
    exists: Boolean(stat && stat.isFile()),
    size: stat && stat.isFile() ? stat.size : 0,
    mtime: stat ? stat.mtimeMs : null,
    path: resolved.virtual
  });
}

async function handleUpload(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_UPLOAD, '上传文件')) {
    ctx.req.resume();
    return;
  }
  const dirPath = ctx.url.searchParams.get('path') || '/';
  const name = safeName(ctx.url.searchParams.get('name'));
  const offset = Number(ctx.url.searchParams.get('offset') || '0');
  if (!name) {
    sendError(ctx.res, 400, '文件名不合法（不能包含路径分隔符或特殊字符）', 'INVALID_NAME');
    ctx.req.resume();
    return;
  }
  if (!Number.isInteger(offset) || offset < 0) {
    sendError(ctx.res, 400, '分块偏移量必须是非负整数', 'INVALID_OFFSET');
    ctx.req.resume();
    return;
  }
  const dir = resolveOrFail(ctx.res, dirPath);
  if (!dir) {
    ctx.req.resume();
    return;
  }
  if (rejectReadonly(ctx.res, dir)) {
    ctx.req.resume();
    return;
  }
  const resolved = mounts.resolveVirtual(joinVirtual(dir.virtual, name));
  if (!resolved) {
    sendError(ctx.res, 400, '非法路径', 'INVALID_PATH');
    ctx.req.resume();
    return;
  }

  try {
    const dirStat = await fs.promises.stat(dir.abs);
    if (!dirStat.isDirectory()) {
      sendError(ctx.res, 400, '目标不是目录', 'NOT_A_DIRECTORY');
      ctx.req.resume();
      return;
    }
    const existing = await fs.promises.stat(resolved.abs).catch(() => null);
    if (existing && existing.isDirectory()) {
      sendError(ctx.res, 409, '同名目录已存在，无法上传同名文件', 'IS_DIRECTORY');
      ctx.req.resume();
      return;
    }
    if (!existing && offset > 0) {
      sendError(ctx.res, 409, '断点数据已失效，请从头开始上传', 'OFFSET_MISMATCH');
      ctx.req.resume();
      return;
    }
    if (existing && offset > existing.size) {
      sendError(
        ctx.res,
        409,
        `断点偏移超出文件当前大小（${existing.size} 字节），请重新开始上传`,
        'OFFSET_MISMATCH'
      );
      ctx.req.resume();
      return;
    }

    const writeOptions = existing ? { flags: 'r+', start: offset } : { flags: 'w' };
    const stream = fs.createWriteStream(resolved.abs, writeOptions);
    let received = 0;
    ctx.req.on('data', (chunk) => {
      received += chunk.length;
    });

    await pipeline(ctx.req, stream);

    sendJson(ctx.res, 200, {
      ok: true,
      path: resolved.virtual,
      received,
      size: offset + received
    });
    audit(ctx, 'FILE_UPLOAD', `上传文件 ${resolved.virtual}（偏移 ${offset}，本次 ${received} 字节）`);
  } catch (err) {
    if (err && (err.code === 'ERR_STREAM_PREMATURE_CLOSE' || err.code === 'ECONNRESET')) {
      console.warn('[文件] 上传被客户端中断：', resolved.virtual);
      return;
    }
    console.error('[文件] 上传失败：', err && err.message ? err.message : err);
    if (!ctx.res.headersSent) {
      failFs(ctx.res, err);
    } else {
      ctx.res.destroy();
    }
  }
}

async function handleUploadFinish(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_UPLOAD, '上传文件')) {
    return;
  }
  const body = await readJson(ctx.req, 64 * 1024);
  const name = safeName(body.name);
  if (!name) {
    sendError(ctx.res, 400, '文件名不合法', 'INVALID_NAME');
    return;
  }
  const dir = resolveOrFail(ctx.res, body.path || '/');
  if (!dir) {
    return;
  }
  if (rejectReadonly(ctx.res, dir)) {
    return;
  }
  const resolved = mounts.resolveVirtual(joinVirtual(dir.virtual, name));
  if (!resolved) {
    sendError(ctx.res, 400, '非法路径', 'INVALID_PATH');
    return;
  }
  const stat = await fs.promises.stat(resolved.abs).catch(() => null);
  if (!stat || !stat.isFile()) {
    sendError(ctx.res, 404, '上传后的文件不存在', 'NOT_FOUND');
    return;
  }
  const expected = Number(body.size);
  if (Number.isFinite(expected) && expected >= 0 && stat.size !== expected) {
    sendError(
      ctx.res,
      409,
      `文件大小校验失败：期望 ${expected} 字节，实际 ${stat.size} 字节`,
      'SIZE_MISMATCH'
    );
    return;
  }
  sendJson(ctx.res, 200, {
    ok: true,
    name,
    path: resolved.virtual,
    size: stat.size,
    mtime: stat.mtimeMs
  });
}

async function handleMkdir(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_WRITE, '新建文件夹')) {
    return;
  }
  const body = await readJson(ctx.req, 64 * 1024);
  const name = safeName(body.name);
  if (!name) {
    sendError(ctx.res, 400, '文件夹名不合法', 'INVALID_NAME');
    return;
  }
  const dir = resolveOrFail(ctx.res, body.path || '/');
  if (!dir) {
    return;
  }
  if (rejectReadonly(ctx.res, dir)) {
    return;
  }
  const resolved = mounts.resolveVirtual(joinVirtual(dir.virtual, name));
  if (!resolved) {
    sendError(ctx.res, 400, '非法路径', 'INVALID_PATH');
    return;
  }
  const existing = await fs.promises.stat(resolved.abs).catch(() => null);
  if (existing) {
    sendError(ctx.res, 409, '同名文件或文件夹已存在', 'EXISTS');
    return;
  }
  await fs.promises.mkdir(resolved.abs, { recursive: false });
  audit(ctx, 'FILE_MKDIR', `新建文件夹 ${resolved.virtual}`);
  sendJson(ctx.res, 201, { ok: true, path: resolved.virtual, name });
}

async function handleRename(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_WRITE, '重命名文件')) {
    return;
  }
  const body = await readJson(ctx.req, 64 * 1024);
  const name = safeName(body.name);
  if (!name) {
    sendError(ctx.res, 400, '新名称不合法', 'INVALID_NAME');
    return;
  }
  const source = resolveOrFail(ctx.res, body.path);
  if (!source) {
    return;
  }
  if (rejectReadonly(ctx.res, source)) {
    return;
  }
  if (source.virtual === '/') {
    sendError(ctx.res, 400, '不能重命名根目录', 'INVALID_PATH');
    return;
  }
  const stat = await fs.promises.stat(source.abs).catch(() => null);
  if (!stat) {
    sendError(ctx.res, 404, '目标不存在', 'NOT_FOUND');
    return;
  }
  const target = mounts.resolveVirtual(joinVirtual(path.posix.dirname(source.virtual), name));
  if (!target) {
    sendError(ctx.res, 400, '非法路径', 'INVALID_PATH');
    return;
  }
  if (rejectReadonly(ctx.res, target)) {
    return;
  }
  if (target.abs === source.abs) {
    sendJson(ctx.res, 200, { ok: true, path: target.virtual, name });
    return;
  }
  const existing = await fs.promises.stat(target.abs).catch(() => null);
  if (existing) {
    sendError(ctx.res, 409, '同名文件或文件夹已存在', 'EXISTS');
    return;
  }
  await fs.promises.rename(source.abs, target.abs);
  audit(ctx, 'FILE_RENAME', `重命名 ${source.virtual} -> ${target.virtual}`);
  sendJson(ctx.res, 200, { ok: true, path: target.virtual, name });
}

async function handleDelete(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_DELETE, '删除文件')) {
    return;
  }
  const body = await readJson(ctx.req, 64 * 1024);
  const target = resolveOrFail(ctx.res, body.path);
  if (!target) {
    return;
  }
  if (rejectReadonly(ctx.res, target)) {
    return;
  }
  if (target.virtual === '/') {
    sendError(ctx.res, 400, '禁止删除根目录', 'INVALID_PATH');
    return;
  }
  const permanent = isTruthy(body.permanent) || isTruthy(ctx.url.searchParams.get('permanent'));
  const stat = await fs.promises.lstat(target.abs).catch(() => null);
  if (!stat) {
    sendError(ctx.res, 404, '目标不存在', 'NOT_FOUND');
    return;
  }
  try {
    if (permanent) {
      if (stat.isDirectory() && !stat.isSymbolicLink()) {
        await fs.promises.rm(target.abs, { recursive: true, force: false });
      } else {
        await fs.promises.unlink(target.abs);
      }
      audit(ctx, 'FILE_DELETE', `永久删除 ${target.virtual}`);
      sendJson(ctx.res, 200, { ok: true, permanent: true });
      return;
    }
    const record = await moveToTrash(ctx.user, target, stat);
    audit(ctx, 'FILE_TRASH', `移入回收站 ${target.virtual}（${record.uuid}）`);
    sendJson(ctx.res, 200, {
      ok: true,
      permanent: false,
      uuid: record.uuid,
      size: record.size,
      path: target.virtual
    });
  } catch (err) {
    failFs(ctx.res, err);
  }
}

/** 回收站列表：管理员可查看全部，普通用户只能看自己的 */
function handleTrashList(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_READ, '查看回收站')) {
    return;
  }
  const rows = db.listTrash(isAdminUser(ctx.user) ? null : ctx.user.id);
  sendJson(ctx.res, 200, {
    ok: true,
    items: rows.map((row) => db.serializeTrash(row)),
    total: rows.length
  });
}

/** 从回收站恢复到原目录（重名自动追加 (n)，原目录不存在时自动创建） */
async function handleTrashRestore(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_DELETE, '恢复回收站文件')) {
    return;
  }
  const body = await readJson(ctx.req, 64 * 1024);
  const ids = Array.isArray(body.ids) ? body.ids : [];
  if (ids.length === 0) {
    sendError(ctx.res, 400, '请选择要恢复的条目', 'EMPTY_IDS');
    return;
  }
  if (ids.length > 500) {
    sendError(ctx.res, 400, '单次最多恢复 500 个条目', 'TOO_MANY_IDS');
    return;
  }
  const admin = isAdminUser(ctx.user);
  const restored = [];
  const failed = [];

  for (const rawId of ids) {
    try {
      const id = Number(rawId);
      if (!Number.isInteger(id) || id <= 0) {
        throw new Error('条目编号不合法');
      }
      const row = db.getTrashById(id);
      if (!row) {
        throw new Error('回收站条目不存在或已被清理');
      }
      if (!admin && row.owner_id !== ctx.user.id) {
        throw new Error('没有权限恢复他人删除的文件');
      }
      const storedAbs = trashStoredPath(row);
      if (!(await pathExists(storedAbs))) {
        throw new Error('回收站中的文件实体已丢失，无法恢复');
      }
      const originalDir = path.posix.dirname(row.original_path);
      const dirResolved = mounts.resolveVirtual(originalDir);
      if (!dirResolved) {
        throw new Error('原始目录路径非法，无法恢复');
      }
      if (dirResolved.readonly) {
        throw new Error('该挂载点是只读的，无法恢复');
      }
      await fs.promises.mkdir(dirResolved.abs, { recursive: true });
      const target = await uniqueTarget(dirResolved.virtual, row.name);
      if (!target) {
        throw new Error('原目录下无法生成可用的文件名');
      }
      await movePath(storedAbs, target.abs, Boolean(row.is_dir));
      db.deleteTrashRecord(row.uuid);
      restored.push({ id: row.id, path: target.virtual, name: row.name });
    } catch (err) {
      failed.push({
        id: Number(rawId),
        error: err && err.message ? err.message : '恢复失败'
      });
    }
  }

  audit(
    ctx,
    'TRASH_RESTORE',
    `从回收站恢复 ${restored.length} 个条目（失败 ${failed.length} 个）`
  );
  sendJson(ctx.res, 200, { ok: true, restored, failed });
}

/** 彻底删除回收站条目（单个/多个，或管理员清空全部） */
async function handleTrashPurge(ctx) {
  const admin = isAdminUser(ctx.user);
  if (!admin && !auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_DELETE, '清空回收站')) {
    return;
  }
  const body = await readJson(ctx.req, 64 * 1024);
  const all = isTruthy(body.all);
  const ids = Array.isArray(body.ids) ? body.ids : [];
  if (!all && ids.length === 0) {
    sendError(ctx.res, 400, '请选择要彻底删除的条目，或指定 all: true', 'EMPTY_IDS');
    return;
  }
  if (all && !admin) {
    sendError(ctx.res, 403, '只有管理员可以清空整个回收站', 'FORBIDDEN');
    return;
  }
  if (!all && ids.length > 500) {
    sendError(ctx.res, 400, '单次最多彻底删除 500 个条目', 'TOO_MANY_IDS');
    return;
  }

  let rows;
  if (all) {
    rows = db.listTrash(null);
  } else {
    rows = [];
    for (const rawId of ids) {
      const id = Number(rawId);
      if (!Number.isInteger(id) || id <= 0) {
        continue;
      }
      const row = db.getTrashById(id);
      if (row && (admin || row.owner_id === ctx.user.id)) {
        rows.push(row);
      }
    }
  }

  const purged = [];
  const failed = [];
  for (const row of rows) {
    try {
      const storedAbs = trashStoredPath(row);
      await fs.promises.rm(storedAbs, { recursive: true, force: true });
      db.deleteTrashRecord(row.uuid);
      purged.push(row.id);
    } catch (err) {
      failed.push({
        id: row.id,
        error: err && err.message ? err.message : '彻底删除失败'
      });
    }
  }

  audit(ctx, 'TRASH_PURGE', `彻底删除回收站条目 ${purged.length} 个（失败 ${failed.length} 个）`);
  sendJson(ctx.res, 200, { ok: true, purged, failed });
}

/** 递归搜索：在指定目录下查找名称包含关键字的条目 */
async function handleSearch(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_READ, '搜索文件')) {
    return;
  }
  const keyword = (ctx.url.searchParams.get('q') || '').trim();
  if (keyword.length === 0) {
    sendError(ctx.res, 400, '搜索关键字不能为空', 'EMPTY_KEYWORD');
    return;
  }
  const base = resolveOrFail(ctx.res, ctx.url.searchParams.get('path'));
  if (!base) {
    return;
  }
  const baseStat = await fs.promises.stat(base.abs).catch(() => null);
  if (!baseStat || !baseStat.isDirectory()) {
    sendError(ctx.res, 404, '搜索目录不存在', 'NOT_FOUND');
    return;
  }
  const rawLimit = Number(ctx.url.searchParams.get('limit') || String(SEARCH_DEFAULT_LIMIT));
  const limit = Math.min(Math.max(Number.isFinite(rawLimit) ? Math.floor(rawLimit) : SEARCH_DEFAULT_LIMIT, 1), 1000);
  const needle = keyword.toLowerCase();

  const items = [];
  let visited = 0;
  let truncated = false;
  const queue = [{ abs: base.abs, virtual: base.virtual, depth: 0 }];

  while (queue.length > 0) {
    const current = queue.shift();
    if (current.depth > SEARCH_MAX_DEPTH) {
      truncated = true;
      continue;
    }
    const entries = await fs.promises.readdir(current.abs, { withFileTypes: true }).catch((err) => {
      console.warn('[文件] 搜索时读取目录失败：', current.virtual, err.message);
      return [];
    });
    for (const entry of entries) {
      visited += 1;
      if (visited > SEARCH_MAX_ENTRIES) {
        truncated = true;
        break;
      }
      const isLink = entry.isSymbolicLink();
      const isDir = entry.isDirectory() && !isLink;
      const abs = path.join(current.abs, entry.name);
      const virtual = joinVirtual(current.virtual, entry.name);
      const match = entry.name.toLowerCase().includes(needle);
      if (match) {
        if (items.length >= limit) {
          truncated = true;
          break;
        }
        const info = isLink ? null : await fs.promises.lstat(abs).catch(() => null);
        items.push({
          name: entry.name,
          path: virtual,
          isDir,
          isLink,
          size: isDir ? 0 : info ? info.size : 0,
          mtime: info ? info.mtimeMs : null,
          ext: isDir ? '' : path.extname(entry.name).replace('.', '').toLowerCase()
        });
      }
      if (isDir) {
        queue.push({ abs, virtual, depth: current.depth + 1 });
      }
    }
    if (visited > SEARCH_MAX_ENTRIES || items.length >= limit) {
      truncated = true;
      break;
    }
  }

  sendJson(ctx.res, 200, {
    ok: true,
    path: base.virtual,
    q: keyword,
    items,
    truncated
  });
}

/** 在线预览：inline 输出，支持 Range，文本类超过 15MB 拒绝 */
async function handlePreview(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_READ, '预览文件')) {
    return;
  }
  const resolved = resolveOrFail(ctx.res, ctx.url.searchParams.get('path'));
  if (!resolved) {
    return;
  }
  const stat = await fs.promises.stat(resolved.abs).catch(() => null);
  if (!stat) {
    sendError(ctx.res, 404, '文件不存在', 'NOT_FOUND');
    return;
  }
  if (stat.isDirectory()) {
    sendError(ctx.res, 400, '目录不支持在线预览', 'IS_DIRECTORY');
    return;
  }
  const ext = path.extname(resolved.abs).replace('.', '').toLowerCase();
  const contentType = previewContentType(ext);
  if (!contentType) {
    sendError(ctx.res, 415, `不支持预览该类型的文件：${ext ? `.${ext}` : '无扩展名'}`, 'UNSUPPORTED_TYPE');
    return;
  }
  if (PREVIEW_TEXT_EXTS.has(ext) && stat.size > PREVIEW_MAX_TEXT_BYTES) {
    sendError(ctx.res, 413, '文本文件超过 15MB，无法在线预览，请下载后查看', 'PREVIEW_TOO_LARGE');
    return;
  }

  const fileName = path.basename(resolved.abs);
  const asciiFallback = fileName.replace(/[^\x20-\x7e]/g, '_').replace(/"/g, "'");
  sendFileWithRange(ctx, resolved.abs, stat, {
    'Content-Type': contentType,
    'Content-Disposition': `inline; filename="${asciiFallback}"; filename*=UTF-8''${encodeURIComponent(fileName)}`,
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff'
  });
}

/** 压缩：把一个或多个文件/目录打包为 zip */
async function handleCompress(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_READ, '压缩文件')) {
    return;
  }
  const body = await readJson(ctx.req, 256 * 1024);
  const rawPaths = Array.isArray(body.paths) ? body.paths : [];
  if (rawPaths.length === 0) {
    sendError(ctx.res, 400, '请选择要压缩的文件或目录', 'EMPTY_PATHS');
    return;
  }
  if (rawPaths.length > 200) {
    sendError(ctx.res, 400, '单次最多压缩 200 个来源', 'TOO_MANY_PATHS');
    return;
  }

  const sources = [];
  const usedNames = new Set();
  for (const rawPath of rawPaths) {
    const source = mounts.resolveVirtual(rawPath);
    if (!source) {
      sendError(ctx.res, 400, `非法路径：${rawPath}`, 'INVALID_PATH');
      return;
    }
    if (source.virtual === '/') {
      sendError(ctx.res, 400, '不能压缩根目录', 'INVALID_PATH');
      return;
    }
    const stat = await fs.promises.lstat(source.abs).catch(() => null);
    if (!stat) {
      sendError(ctx.res, 404, `源路径不存在：${source.virtual}`, 'NOT_FOUND');
      return;
    }
    const baseName = path.basename(source.abs);
    if (usedNames.has(baseName)) {
      sendError(ctx.res, 409, `压缩包内存在同名条目：${baseName}`, 'DUPLICATE_NAME');
      return;
    }
    usedNames.add(baseName);
    sources.push({
      absPath: source.abs,
      name: baseName,
      virtual: source.virtual,
      isDir: stat.isDirectory() && !stat.isSymbolicLink()
    });
  }

  const targetRaw = typeof body.target === 'string' ? body.target.trim() : '';
  if (!targetRaw) {
    sendError(ctx.res, 400, '请指定压缩包保存路径', 'INVALID_TARGET');
    return;
  }
  if (!isZipPath(targetRaw)) {
    sendError(ctx.res, 400, '压缩包文件名必须以 .zip 结尾', 'INVALID_TARGET');
    return;
  }
  const target = mounts.resolveVirtual(targetRaw);
  if (!target || target.virtual === '/') {
    sendError(ctx.res, 400, '非法的压缩包保存路径', 'INVALID_PATH');
    return;
  }
  if (rejectReadonly(ctx.res, target)) {
    return;
  }
  if (await pathExists(target.abs)) {
    sendError(ctx.res, 409, '压缩包保存路径已存在，请换一个文件名', 'EXISTS');
    return;
  }
  const targetDirStat = await fs.promises.stat(path.dirname(target.abs)).catch(() => null);
  if (!targetDirStat || !targetDirStat.isDirectory()) {
    sendError(ctx.res, 404, '压缩包所在的目录不存在', 'NOT_FOUND');
    return;
  }
  for (const source of sources) {
    if (source.isDir && (target.abs === source.abs || target.abs.startsWith(source.abs + path.sep))) {
      sendError(ctx.res, 400, '压缩包不能保存在待压缩目录内部', 'TARGET_IN_SOURCE');
      return;
    }
  }

  try {
    const bytes = await createZip(
      sources.map((source) => ({ absPath: source.absPath, name: source.name })),
      target.abs
    );
    audit(ctx, 'FILE_COMPRESS', `压缩 ${sources.map((s) => s.virtual).join('、')} -> ${target.virtual}`);
    sendJson(ctx.res, 200, { ok: true, path: target.virtual, bytes });
  } catch (err) {
    console.error('[文件] 压缩失败：', err && err.message ? err.message : err);
    sendError(ctx.res, 500, `压缩失败：${err && err.message ? err.message : '未知错误'}`, 'COMPRESS_ERROR');
  }
}

/** 解压：仅支持 .zip，目标目录不存在时自动创建 */
async function handleExtract(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_READ, '解压文件')) {
    return;
  }
  const body = await readJson(ctx.req, 64 * 1024);
  const archive = resolveOrFail(ctx.res, body.path);
  if (!archive) {
    return;
  }
  if (!isZipPath(archive.abs)) {
    sendError(ctx.res, 400, '只支持解压 .zip 格式的压缩包', 'UNSUPPORTED_ARCHIVE');
    return;
  }
  const archiveStat = await fs.promises.stat(archive.abs).catch(() => null);
  if (!archiveStat || !archiveStat.isFile()) {
    sendError(ctx.res, 404, '压缩包不存在', 'NOT_FOUND');
    return;
  }
  // 默认解压到压缩包所在目录（而不是数据根目录），避免文件散落到根上
  const parentVirtual = path.posix.dirname(archive.virtual);
  const dest = resolveOrFail(ctx.res, body.dest || (parentVirtual === '.' ? '/' : parentVirtual));
  if (!dest) {
    return;
  }
  if (rejectReadonly(ctx.res, dest)) {
    return;
  }
  const destStat = await fs.promises.stat(dest.abs).catch(() => null);
  if (destStat && !destStat.isDirectory()) {
    sendError(ctx.res, 409, '解压目标已存在且不是目录', 'NOT_A_DIRECTORY');
    return;
  }
  try {
    if (!destStat) {
      await fs.promises.mkdir(dest.abs, { recursive: true });
    }
    const count = await extractZip(archive.abs, dest.abs);
    audit(ctx, 'FILE_EXTRACT', `解压 ${archive.virtual} -> ${dest.virtual}（${count} 个条目）`);
    sendJson(ctx.res, 200, { ok: true, count, path: dest.virtual });
  } catch (err) {
    console.error('[文件] 解压失败：', err && err.message ? err.message : err);
    sendError(ctx.res, 400, `解压失败：${err && err.message ? err.message : '未知错误'}`, 'EXTRACT_ERROR');
  }
}

/** 批量删除/复制/移动 */
async function handleBatch(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_DELETE, '批量操作文件')) {
    return;
  }
  const body = await readJson(ctx.req, 256 * 1024);
  const action = body.action;
  if (action !== 'delete' && action !== 'copy' && action !== 'move') {
    sendError(ctx.res, 400, '批量操作只支持 delete、copy、move', 'INVALID_ACTION');
    return;
  }
  const rawPaths = Array.isArray(body.paths) ? body.paths : [];
  if (rawPaths.length === 0) {
    sendError(ctx.res, 400, '请选择要操作的文件或目录', 'EMPTY_PATHS');
    return;
  }
  if (rawPaths.length > 500) {
    sendError(ctx.res, 400, '单次最多操作 500 个条目', 'TOO_MANY_PATHS');
    return;
  }

  let targetDir = null;
  if (action !== 'delete') {
    targetDir = mounts.resolveVirtual(body.target || '');
    if (!targetDir) {
      sendError(ctx.res, 400, '非法的目标目录', 'INVALID_PATH');
      return;
    }
    if (rejectReadonly(ctx.res, targetDir)) {
      return;
    }
    const targetStat = await fs.promises.stat(targetDir.abs).catch(() => null);
    if (!targetStat || !targetStat.isDirectory()) {
      sendError(ctx.res, 404, '目标目录不存在', 'NOT_FOUND');
      return;
    }
  }

  const done = [];
  const failed = [];
  for (const rawPath of rawPaths) {
    try {
      const source = mounts.resolveVirtual(rawPath);
      if (!source) {
        throw new Error('非法的路径');
      }
      if (source.readonly) {
        throw new Error('该挂载点是只读的，无法修改');
      }
      if (source.virtual === '/') {
        throw new Error('禁止操作根目录');
      }
      const stat = await fs.promises.lstat(source.abs).catch(() => null);
      if (!stat) {
        throw new Error('源路径不存在');
      }
      const isDir = stat.isDirectory() && !stat.isSymbolicLink();

      if (action === 'delete') {
        const record = await moveToTrash(ctx.user, source, stat);
        done.push({ path: source.virtual, uuid: record.uuid });
        continue;
      }

      if (targetDir.abs === source.abs || targetDir.abs.startsWith(source.abs + path.sep)) {
        throw new Error('不能把目录复制或移动到自身或其子目录中');
      }
      const target = await uniqueTarget(targetDir.virtual, path.basename(source.abs));
      if (!target) {
        throw new Error('目标目录下无法生成可用的文件名');
      }
      if (action === 'copy') {
        await copyPath(source.abs, target.abs, isDir);
      } else {
        await movePath(source.abs, target.abs, isDir);
      }
      done.push({ path: source.virtual, target: target.virtual });
    } catch (err) {
      failed.push({
        path: typeof rawPath === 'string' ? rawPath : String(rawPath),
        error: err && err.message ? err.message : '操作失败'
      });
    }
  }

  audit(
    ctx,
    action === 'delete' ? 'FILE_BATCH_DELETE' : action === 'copy' ? 'FILE_BATCH_COPY' : 'FILE_BATCH_MOVE',
    `批量${action === 'delete' ? '删除' : action === 'copy' ? '复制' : '移动'} ${done.length} 个条目（失败 ${failed.length} 个）`
  );
  sendJson(ctx.res, 200, { ok: true, action, done, failed });
}

/** 收藏列表 */
function handleFavoritesList(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_READ, '查看收藏夹')) {
    return;
  }
  const rows = db.listFavorites(ctx.user.id);
  sendJson(ctx.res, 200, {
    ok: true,
    items: rows.map((row) => db.serializeFavorite(row)),
    total: rows.length
  });
}

/** 添加收藏 */
async function handleFavoriteAdd(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_READ, '收藏文件')) {
    return;
  }
  const body = await readJson(ctx.req, 64 * 1024);
  const target = resolveOrFail(ctx.res, body.path);
  if (!target) {
    return;
  }
  const stat = await fs.promises.lstat(target.abs).catch(() => null);
  if (!stat) {
    sendError(ctx.res, 404, '收藏的路径不存在', 'NOT_FOUND');
    return;
  }
  const row = db.addFavorite({
    userId: ctx.user.id,
    path: target.virtual,
    name: path.basename(target.abs) || target.virtual,
    isDir: stat.isDirectory() && !stat.isSymbolicLink()
  });
  sendJson(ctx.res, 200, { ok: true, item: db.serializeFavorite(row) });
}

/** 取消收藏 */
function handleFavoriteRemove(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_READ, '取消收藏')) {
    return;
  }
  const target = resolveOrFail(ctx.res, ctx.url.searchParams.get('path'));
  if (!target) {
    return;
  }
  const changes = db.removeFavorite(ctx.user.id, target.virtual);
  sendJson(ctx.res, 200, { ok: true, removed: changes > 0 });
}

/** 最近访问列表（最多 50 条） */
function handleRecentList(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_READ, '查看最近访问')) {
    return;
  }
  const rows = db.listRecent(ctx.user.id, 50);
  sendJson(ctx.res, 200, {
    ok: true,
    items: rows.map((row) => db.serializeRecent(row)),
    total: rows.length
  });
}

/** 记录一次打开（最近访问） */
async function handleRecentAdd(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_READ, '记录最近访问')) {
    return;
  }
  const body = await readJson(ctx.req, 64 * 1024);
  const target = resolveOrFail(ctx.res, body.path);
  if (!target) {
    return;
  }
  const stat = await fs.promises.lstat(target.abs).catch(() => null);
  if (!stat) {
    sendError(ctx.res, 404, '访问的路径不存在', 'NOT_FOUND');
    return;
  }
  db.upsertRecent({
    userId: ctx.user.id,
    path: target.virtual,
    name: path.basename(target.abs) || target.virtual,
    isDir: stat.isDirectory() && !stat.isSymbolicLink()
  });
  sendJson(ctx.res, 200, { ok: true });
}

async function handleStorage(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.STORAGE_VIEW, '查看存储情况')) {
    return;
  }
  try {
    const stats = await fs.promises.statfs(FILES_ROOT);
    const blockSize = Number(stats.bsize);
    const total = Number(stats.blocks) * blockSize;
    const free = Number(stats.bfree) * blockSize;
    const available = Number(stats.bavail) * blockSize;
    const used = total - free;
    sendJson(ctx.res, 200, {
      ok: true,
      root: FILES_ROOT,
      total,
      used,
      free: available,
      usagePercent: total > 0 ? Math.round((used / total) * 1000) / 10 : 0
    });
  } catch (err) {
    sendError(ctx.res, 500, `读取存储信息失败：${err.message}`, 'STORAGE_ERROR');
  }
}

/* ------------------------------ 存储 / 挂载 ------------------------------ */

/** 存储扫描的默认根目录 */
const STORAGE_SCAN_ROOT = '/vol1';

/** 管理员角色校验；失败时写出 403 响应并返回 false */
function requireAdminRole(ctx, actionLabel) {
  if (isAdminUser(ctx.user)) {
    return true;
  }
  sendError(ctx.res, 403, `没有权限执行该操作：${actionLabel}`, 'FORBIDDEN');
  return false;
}

/** 已挂载的绝对路径集合与名称集合，用于判断候选目录是否已挂载 */
function mountKeySets() {
  const rows = db.listMounts();
  return {
    names: new Set(rows.map((row) => row.name)),
    paths: new Set(rows.map((row) => path.resolve(row.abs_path)))
  };
}

/** 挂载点列表：GET /api/storage/mounts */
async function handleStorageMountList(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.STORAGE_VIEW, '查看挂载点')) {
    return;
  }
  const items = await mounts.listMountsEnriched();
  // 没有任何挂载点，或所有挂载点都不可读（多半是还没给应用授权文件夹权限）时，
  // 前端需要引导用户先去飞牛「设置 → 应用 → 飞牛终端 → 文件夹权限」授权。
  const usable = items.filter((item) => item && item.readable);
  sendJson(ctx.res, 200, { ok: true, items, needsSetup: usable.length === 0 });
}

/** 新增挂载点：POST /api/storage/mounts */
async function handleStorageMountCreate(ctx) {
  if (!requireAdminRole(ctx, '管理挂载点')) {
    return;
  }
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.STORAGE_MANAGE, '管理挂载点')) {
    return;
  }
  const body = await readJson(ctx.req, 64 * 1024);
  const name = safeName(body.name);
  if (!name) {
    sendError(ctx.res, 400, '挂载名不合法（不能包含路径分隔符或特殊字符）', 'INVALID_NAME');
    return;
  }
  if (name === mounts.MOUNT_PREFIX.slice(1)) {
    sendError(ctx.res, 400, '挂载名不能使用保留字 m', 'RESERVED_NAME');
    return;
  }
  const rawAbsPath = typeof body.absPath === 'string' ? body.absPath.trim() : '';
  if (!rawAbsPath || !path.isAbsolute(rawAbsPath)) {
    sendError(ctx.res, 400, '挂载路径必须是以 / 开头的绝对路径', 'INVALID_PATH');
    return;
  }
  const absPath = path.resolve(rawAbsPath);
  const probe = await mounts.probePath(absPath);
  if (!probe.exists) {
    sendError(ctx.res, 400, `目录不存在：${absPath}`, 'NOT_FOUND');
    return;
  }
  if (!probe.isDir) {
    sendError(ctx.res, 400, `目标不是目录：${absPath}`, 'NOT_A_DIRECTORY');
    return;
  }
  if (!probe.readable) {
    sendError(ctx.res, 400, `无读取权限，${mounts.PERMISSION_HINT}：${absPath}`, 'NO_READ_PERMISSION');
    return;
  }
  const readonly = isTruthy(body.readonly);
  if (!readonly && !probe.writable) {
    sendError(
      ctx.res,
      400,
      `无写入权限，${mounts.PERMISSION_HINT}，或勾选「只读挂载」后重试：${absPath}`,
      'NO_WRITE_PERMISSION'
    );
    return;
  }
  if (db.getMountByName(name)) {
    sendError(ctx.res, 409, `同名挂载点已存在：${name}`, 'MOUNT_EXISTS');
    return;
  }
  const row = db.insertMount({ name, abs_path: absPath, readonly, created_at: Date.now() });
  audit(ctx, 'STORAGE_MOUNT', `挂载 ${absPath} -> /m/${name}（${readonly ? '只读' : '读写'}）`);
  sendJson(ctx.res, 201, { ok: true, item: await mounts.enrichMount(row) });
}

/** 卸载挂载点：DELETE /api/storage/mounts?id=<id> */
function handleStorageMountDelete(ctx) {
  if (!requireAdminRole(ctx, '管理挂载点')) {
    return;
  }
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.STORAGE_MANAGE, '管理挂载点')) {
    return;
  }
  const id = Number(ctx.url.searchParams.get('id') || '0');
  if (!Number.isInteger(id) || id <= 0) {
    sendError(ctx.res, 400, '挂载点编号不合法', 'INVALID_ID');
    return;
  }
  const row = db.getMountById(id);
  if (!row) {
    sendError(ctx.res, 404, '挂载点不存在', 'NOT_FOUND');
    return;
  }
  db.deleteMount(row.id);
  audit(ctx, 'STORAGE_UNMOUNT', `卸载 ${row.abs_path}（/m/${row.name}）`);
  sendJson(ctx.res, 200, { ok: true });
}

/** 扫描绝对目录下的候选文件夹：GET /api/storage/folders?path=/vol1 */
async function handleStorageFolders(ctx) {
  if (!requireAdminRole(ctx, '浏览存储目录')) {
    return;
  }
  const rawBase = (ctx.url.searchParams.get('path') || STORAGE_SCAN_ROOT).trim() || STORAGE_SCAN_ROOT;
  if (!path.isAbsolute(rawBase)) {
    sendError(ctx.res, 400, '目录路径必须是以 / 开头的绝对路径', 'INVALID_PATH');
    return;
  }
  const base = path.resolve(rawBase);
  const baseProbe = await mounts.probePath(base);
  if (!baseProbe.exists) {
    sendError(ctx.res, 400, `目录不存在：${base}`, 'NOT_FOUND');
    return;
  }
  if (!baseProbe.isDir) {
    sendError(ctx.res, 400, `目标不是目录：${base}`, 'NOT_A_DIRECTORY');
    return;
  }
  if (!baseProbe.readable) {
    sendError(ctx.res, 403, `没有读取权限，${mounts.PERMISSION_HINT}：${base}`, 'NO_READ_PERMISSION');
    return;
  }
  let entries;
  try {
    entries = await fs.promises.readdir(base, { withFileTypes: true });
  } catch (err) {
    sendError(ctx.res, 403, `读取目录失败：${fsErrorMessage(err)}`, (err && err.code) || 'FS_ERROR');
    return;
  }
  const mounted = mountKeySets();
  const items = [];
  for (const entry of entries) {
    if (entry.name.startsWith('.')) {
      continue;
    }
    if (!entry.isDirectory() || entry.isSymbolicLink()) {
      continue;
    }
    const absPath = path.join(base, entry.name);
    const probe = await mounts.probePath(absPath);
    items.push({
      name: entry.name,
      path: absPath,
      readable: probe.readable,
      writable: probe.writable,
      error: probe.error,
      mounted: mounted.names.has(entry.name) || mounted.paths.has(absPath)
    });
  }
  items.sort((a, b) => {
    const aSystem = a.name.startsWith('@');
    const bSystem = b.name.startsWith('@');
    if (aSystem !== bSystem) {
      return aSystem ? 1 : -1;
    }
    return a.name.localeCompare(b.name, 'zh-CN');
  });
  sendJson(ctx.res, 200, { ok: true, base, items });
}

/** 探测单个绝对路径：GET /api/storage/probe?path=/绝对路径 */
async function handleStorageProbe(ctx) {
  if (!requireAdminRole(ctx, '探测存储目录')) {
    return;
  }
  const rawPath = (ctx.url.searchParams.get('path') || '').trim();
  if (!rawPath || !path.isAbsolute(rawPath)) {
    sendError(ctx.res, 400, '目录路径必须是以 / 开头的绝对路径', 'INVALID_PATH');
    return;
  }
  const absPath = path.resolve(rawPath);
  const probe = await mounts.probePath(absPath);
  const mounted = mountKeySets();
  sendJson(ctx.res, 200, {
    ok: true,
    path: absPath,
    exists: probe.exists,
    isDir: probe.isDir,
    readable: probe.readable,
    writable: probe.writable,
    error: probe.error,
    mounted: mounted.paths.has(absPath)
  });
}

/** 自动挂载：POST /api/storage/mounts/auto（扫描 /vol1 下可读的非 @ 目录） */
async function handleStorageMountAuto(ctx) {
  if (!requireAdminRole(ctx, '自动挂载目录')) {
    return;
  }
  const base = STORAGE_SCAN_ROOT;
  const baseProbe = await mounts.probePath(base);
  if (!baseProbe.exists || !baseProbe.isDir) {
    sendError(ctx.res, 400, `目录不存在或不是目录：${base}`, 'NOT_FOUND');
    return;
  }
  if (!baseProbe.readable) {
    sendError(ctx.res, 403, `没有读取权限，${mounts.PERMISSION_HINT}：${base}`, 'NO_READ_PERMISSION');
    return;
  }
  let entries;
  try {
    entries = await fs.promises.readdir(base, { withFileTypes: true });
  } catch (err) {
    sendError(ctx.res, 403, `读取目录失败：${fsErrorMessage(err)}`, (err && err.code) || 'FS_ERROR');
    return;
  }
  const added = [];
  const skipped = [];
  for (const entry of entries) {
    if (entry.name.startsWith('.')) {
      continue;
    }
    if (!entry.isDirectory() || entry.isSymbolicLink()) {
      continue;
    }
    if (entry.name.startsWith('@')) {
      skipped.push({ name: entry.name, reason: '以 @ 开头的系统目录，已跳过' });
      continue;
    }
    const absPath = path.join(base, entry.name);
    if (db.getMountByName(entry.name)) {
      skipped.push({ name: entry.name, reason: '已存在同名挂载点' });
      continue;
    }
    const probe = await mounts.probePath(absPath);
    if (!probe.readable) {
      skipped.push({ name: entry.name, reason: probe.error || '没有读取权限' });
      continue;
    }
    const readonly = !probe.writable;
    const row = db.insertMount({ name: entry.name, abs_path: absPath, readonly, created_at: Date.now() });
    audit(ctx, 'STORAGE_MOUNT', `自动挂载 ${absPath} -> /m/${entry.name}（${readonly ? '只读' : '读写'}）`);
    added.push(await mounts.enrichMount(row));
  }
  sendJson(ctx.res, 200, { ok: true, added, skipped });
}

/** /api/storage/* 统一入口 */
async function handleStorageApi(ctx) {
  const { pathname, method } = ctx;

  if (pathname === '/api/storage/mounts' && method === 'GET') {
    await handleStorageMountList(ctx);
    return true;
  }
  if (pathname === '/api/storage/mounts' && method === 'POST') {
    await handleStorageMountCreate(ctx);
    return true;
  }
  if (pathname === '/api/storage/mounts' && method === 'DELETE') {
    handleStorageMountDelete(ctx);
    return true;
  }
  if (pathname === '/api/storage/mounts/auto' && method === 'POST') {
    await handleStorageMountAuto(ctx);
    return true;
  }
  if (pathname === '/api/storage/folders' && method === 'GET') {
    await handleStorageFolders(ctx);
    return true;
  }
  if (pathname === '/api/storage/probe' && method === 'GET') {
    await handleStorageProbe(ctx);
    return true;
  }

  sendError(ctx.res, 404, '接口不存在', 'NOT_FOUND');
  return true;
}

async function handle(ctx) {
  const { pathname, method } = ctx;

  if (pathname === '/api/files' && method === 'GET') {
    await handleList(ctx);
    return true;
  }
  if (pathname === '/api/files/download' && method === 'GET') {
    await handleDownload(ctx);
    return true;
  }
  if (pathname === '/api/files/download-folder' && method === 'GET') {
    await handleDownloadFolder(ctx);
    return true;
  }
  if (pathname === '/api/files/upload/status' && method === 'GET') {
    await handleUploadStatus(ctx);
    return true;
  }
  if (pathname === '/api/files/upload' && method === 'POST') {
    await handleUpload(ctx);
    return true;
  }
  if (pathname === '/api/files/upload/finish' && method === 'POST') {
    await handleUploadFinish(ctx);
    return true;
  }
  if (pathname === '/api/files/mkdir' && method === 'POST') {
    await handleMkdir(ctx);
    return true;
  }
  if (pathname === '/api/files/rename' && method === 'POST') {
    await handleRename(ctx);
    return true;
  }
  if (pathname === '/api/files/delete' && method === 'POST') {
    await handleDelete(ctx);
    return true;
  }
  if (pathname === '/api/files/storage' && method === 'GET') {
    await handleStorage(ctx);
    return true;
  }
  if (pathname === '/api/files/trash' && method === 'GET') {
    handleTrashList(ctx);
    return true;
  }
  if (pathname === '/api/files/trash/restore' && method === 'POST') {
    await handleTrashRestore(ctx);
    return true;
  }
  if (pathname === '/api/files/trash/purge' && method === 'POST') {
    await handleTrashPurge(ctx);
    return true;
  }
  if (pathname === '/api/files/search' && method === 'GET') {
    await handleSearch(ctx);
    return true;
  }
  if (pathname === '/api/files/preview' && method === 'GET') {
    await handlePreview(ctx);
    return true;
  }
  if (pathname === '/api/files/compress' && method === 'POST') {
    await handleCompress(ctx);
    return true;
  }
  if (pathname === '/api/files/extract' && method === 'POST') {
    await handleExtract(ctx);
    return true;
  }
  if (pathname === '/api/files/batch' && method === 'POST') {
    await handleBatch(ctx);
    return true;
  }
  if (pathname === '/api/files/favorites' && method === 'GET') {
    handleFavoritesList(ctx);
    return true;
  }
  if (pathname === '/api/files/favorites' && method === 'POST') {
    await handleFavoriteAdd(ctx);
    return true;
  }
  if (pathname === '/api/files/favorites' && method === 'DELETE') {
    handleFavoriteRemove(ctx);
    return true;
  }
  if (pathname === '/api/files/recent' && method === 'GET') {
    handleRecentList(ctx);
    return true;
  }
  if (pathname === '/api/files/recent' && method === 'POST') {
    await handleRecentAdd(ctx);
    return true;
  }

  sendError(ctx.res, 404, '接口不存在', 'NOT_FOUND');
  return true;
}

module.exports = { handle, handleStorageApi, sendFileWithRange, pathSize, isAdminUser };
