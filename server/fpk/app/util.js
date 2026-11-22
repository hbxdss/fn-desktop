'use strict';

/**
 * 公共工具模块：路径常量、权限位定义、HTTP 响应与路径安全校验。
 * 所有对外文案统一使用中文，便于客户端与 Web 后台直接展示。
 */

const fs = require('fs');
const path = require('path');

const APP_DIR = __dirname;
const PUBLIC_DIR = path.join(APP_DIR, 'public');
const DATA_DIR = process.env.FNOS_DATA_DIR
  ? path.resolve(process.env.FNOS_DATA_DIR)
  : path.join(APP_DIR, 'data');
const FILES_ROOT = process.env.FNOS_FILES_ROOT
  ? path.resolve(process.env.FNOS_FILES_ROOT)
  : path.join(DATA_DIR, 'files');
const CERT_DIR = path.join(DATA_DIR, 'certs');
const DB_PATH = path.join(DATA_DIR, 'fnos.db');
const AES_KEY_PATH = path.join(DATA_DIR, 'aes.key');

const PORT = Number.isFinite(Number(process.env.FNOS_PORT)) ? Number(process.env.FNOS_PORT) : 59120;
const HOST = process.env.FNOS_HOST || '0.0.0.0';
const SERVER_NAME = 'fnos-terminal';
const SERVER_VERSION = '1.0.0';
const TOKEN_TTL_MS = 7 * 24 * 60 * 60 * 1000;

/** 24 位权限位表（当前定义 21 位，21~23 位保留给后续扩展） */
const PERMS = Object.freeze({
  USER_CREATE: 1 << 0,
  USER_DELETE: 1 << 1,
  USER_EDIT: 1 << 2,
  USER_RESET_PWD: 1 << 3,
  SYSTEM_SETTINGS: 1 << 4,
  SYSTEM_AUDIT_VIEW: 1 << 5,
  SYSTEM_SHUTDOWN: 1 << 6,
  FILE_READ: 1 << 7,
  FILE_WRITE: 1 << 8,
  FILE_DELETE: 1 << 9,
  FILE_UPLOAD: 1 << 10,
  FILE_DOWNLOAD: 1 << 11,
  FILE_SHARE: 1 << 12,
  DOCKER_VIEW: 1 << 13,
  DOCKER_START: 1 << 14,
  DOCKER_STOP: 1 << 15,
  DOCKER_DELETE: 1 << 16,
  DOCKER_CREATE: 1 << 17,
  DOCKER_EXEC: 1 << 18,
  STORAGE_VIEW: 1 << 19,
  STORAGE_MANAGE: 1 << 20
});

const PERM_BITS = 24;
const ALL_PERMISSIONS = (1 << PERM_BITS) - 1;

const ROLES = Object.freeze({
  SUPER: 'super',
  ADMIN: 'admin',
  USER: 'user'
});

const ROLE_LABELS = Object.freeze({
  super: '超级管理员',
  admin: '管理员',
  user: '普通用户'
});

/** 普通用户默认权限：只读浏览 + 上传下载 + 查看 Docker/存储 */
const DEFAULT_USER_PERMS =
  PERMS.FILE_READ |
  PERMS.FILE_WRITE |
  PERMS.FILE_UPLOAD |
  PERMS.FILE_DOWNLOAD |
  PERMS.DOCKER_VIEW |
  PERMS.STORAGE_VIEW;

/** 管理员默认权限：普通用户权限 + 用户管理（删除类与系统级权限默认关闭） */
const DEFAULT_ADMIN_PERMS =
  DEFAULT_USER_PERMS |
  PERMS.USER_CREATE |
  PERMS.USER_EDIT |
  PERMS.USER_RESET_PWD |
  PERMS.SYSTEM_AUDIT_VIEW |
  PERMS.DOCKER_START |
  PERMS.DOCKER_STOP |
  PERMS.DOCKER_EXEC |
  PERMS.FILE_DELETE;

const MIME_TYPES = Object.freeze({
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.gif': 'image/gif',
  '.ico': 'image/x-icon',
  '.woff2': 'font/woff2',
  '.txt': 'text/plain; charset=utf-8',
  '.map': 'application/json; charset=utf-8'
});

function ensureDir(dir) {
  if (!fs.existsSync(dir)) {
    fs.mkdirSync(dir, { recursive: true });
  }
  return dir;
}

function ensureBaseDirs() {
  ensureDir(DATA_DIR);
  ensureDir(FILES_ROOT);
  ensureDir(CERT_DIR);
}

function sendJson(res, status, payload, extraHeaders) {
  const body = Buffer.from(JSON.stringify(payload), 'utf8');
  const headers = Object.assign(
    {
      'Content-Type': 'application/json; charset=utf-8',
      'Content-Length': body.length,
      'Cache-Control': 'no-store',
      'X-Content-Type-Options': 'nosniff'
    },
    extraHeaders || {}
  );
  res.writeHead(status, headers);
  res.end(body);
}

function sendError(res, status, message, code) {
  sendJson(res, status, { ok: false, error: message, code: code || 'ERROR' });
}

function readBody(req, limit) {
  const maxBytes = limit || 1024 * 1024;
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    let finished = false;
    req.on('data', (chunk) => {
      if (finished) {
        return;
      }
      size += chunk.length;
      if (size > maxBytes) {
        finished = true;
        const err = new Error('请求体超过大小限制');
        err.statusCode = 413;
        reject(err);
        req.destroy();
        return;
      }
      chunks.push(chunk);
    });
    req.on('end', () => {
      if (!finished) {
        finished = true;
        resolve(Buffer.concat(chunks));
      }
    });
    req.on('error', (err) => {
      if (!finished) {
        finished = true;
        reject(err);
      }
    });
  });
}

async function readJson(req, limit) {
  const raw = await readBody(req, limit || 1024 * 1024);
  if (raw.length === 0) {
    const err = new Error('请求体不能为空');
    err.statusCode = 400;
    throw err;
  }
  let data;
  try {
    data = JSON.parse(raw.toString('utf8'));
  } catch (parseErr) {
    const err = new Error('请求体不是合法的 JSON');
    err.statusCode = 400;
    throw err;
  }
  if (data === null || typeof data !== 'object' || Array.isArray(data)) {
    const err = new Error('请求体必须是 JSON 对象');
    err.statusCode = 400;
    throw err;
  }
  return data;
}

function mimeOf(filePath) {
  return MIME_TYPES[path.extname(filePath).toLowerCase()] || 'application/octet-stream';
}

/**
 * 将虚拟路径（如 /docs/a.txt）安全地解析到 root 目录下。
 * 任何试图越出 root 的路径都会返回 null。
 */
function safeResolve(root, virtualPath) {
  const raw = typeof virtualPath === 'string' ? virtualPath : '/';
  const unified = '/' + raw.replace(/\\/g, '/');
  const rawSegments = unified.split('/').filter((seg) => seg.length > 0);
  if (rawSegments.some((seg) => seg === '..')) {
    return null;
  }
  const segments = rawSegments.filter((seg) => seg !== '.');
  const rootAbs = path.resolve(root);
  const abs = path.resolve(rootAbs, ...segments);
  if (abs !== rootAbs && !abs.startsWith(rootAbs + path.sep)) {
    return null;
  }
  return { abs, virtual: '/' + segments.join('/') };
}

/** 校验单个文件/目录名是否合法（拒绝路径分隔符、控制字符与 Windows 保留名） */
function safeName(name) {
  if (typeof name !== 'string') {
    return null;
  }
  const trimmed = name.trim();
  if (!trimmed || trimmed === '.' || trimmed === '..') {
    return null;
  }
  if (trimmed.length > 255) {
    return null;
  }
  if (/[\/\\]/.test(trimmed)) {
    return null;
  }
  // eslint-disable-next-line no-control-regex
  if (/[\u0000-\u001f<>:"|?*]/.test(trimmed)) {
    return null;
  }
  if (/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(\.|$)/i.test(trimmed)) {
    return null;
  }
  return trimmed;
}

function isValidUsername(username) {
  return typeof username === 'string' && /^[A-Za-z0-9_.-]{3,32}$/.test(username);
}

function passwordPolicyMessage(password) {
  if (typeof password !== 'string' || password.length < 8) {
    return '密码长度至少 8 位';
  }
  if (password.length > 128) {
    return '密码长度不能超过 128 位';
  }
  if (!/[A-Za-z]/.test(password) || !/[0-9]/.test(password)) {
    return '密码必须同时包含字母和数字';
  }
  return null;
}

function clientIp(req) {
  const forwarded = req.headers['x-forwarded-for'];
  if (typeof forwarded === 'string' && forwarded.length > 0) {
    return forwarded.split(',')[0].trim();
  }
  return (req.socket && req.socket.remoteAddress) || 'unknown';
}

function joinVirtual(dirVirtual, name) {
  const base = dirVirtual === '/' ? '' : dirVirtual.replace(/\/+$/, '');
  return `${base}/${name}`;
}

module.exports = {
  APP_DIR,
  PUBLIC_DIR,
  DATA_DIR,
  FILES_ROOT,
  CERT_DIR,
  DB_PATH,
  AES_KEY_PATH,
  PORT,
  HOST,
  SERVER_NAME,
  SERVER_VERSION,
  TOKEN_TTL_MS,
  PERMS,
  PERM_BITS,
  ALL_PERMISSIONS,
  ROLES,
  ROLE_LABELS,
  DEFAULT_USER_PERMS,
  DEFAULT_ADMIN_PERMS,
  ensureDir,
  ensureBaseDirs,
  sendJson,
  sendError,
  readBody,
  readJson,
  mimeOf,
  safeResolve,
  safeName,
  isValidUsername,
  passwordPolicyMessage,
  clientIp,
  joinVirtual
};
