'use strict';

/**
 * 认证与加密模块
 * - 密码：Argon2id（N=2^15, r=8, p=4, salt=32B, 输出 64B），实现见 password.js
 * - Token：randomBytes(32).hex，数据库只落 SHA-256 哈希
 * - 敏感字段：AES-256-GCM（key 文件权限 0600）
 */

const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

const { PARAMS: ARGON_OPTIONS, hashPassword, verifyPassword } = require('./password');
const db = require('./db');
const {
  AES_KEY_PATH,
  TOKEN_TTL_MS,
  PERMS,
  ALL_PERMISSIONS,
  ROLES,
  ensureDir,
  clientIp,
  sendError
} = require('./util');

const LOGIN_MAX_FAILS = 8;
const LOGIN_LOCK_MS = 60 * 1000;
const loginAttempts = new Map();

function sha256(value) {
  return crypto.createHash('sha256').update(value).digest('hex');
}

function issueToken(user, ip) {
  const token = crypto.randomBytes(32).toString('hex');
  const expiresAt = Date.now() + TOKEN_TTL_MS;
  db.createTokenRecord({
    userId: user.id,
    tokenHash: sha256(token),
    expiresAt,
    ip: ip || ''
  });
  return { token, expiresAt };
}

function readBearerToken(req) {
  const header = req.headers['authorization'];
  if (typeof header !== 'string') {
    return null;
  }
  const match = header.match(/^Bearer\s+([A-Fa-f0-9]{64})$/);
  return match ? match[1] : null;
}

/**
 * 校验 Bearer Token。
 * 返回 { user, tokenHash } 或 { error } 说明失败原因。
 */
function authenticate(req) {
  const token = readBearerToken(req);
  if (!token) {
    return { error: '缺少或格式错误的 Bearer Token' };
  }
  const tokenHash = sha256(token);
  const row = db.getTokenByHash(tokenHash);
  if (!row) {
    return { error: '登录状态无效，请重新登录' };
  }
  if (row.expires_at <= Date.now()) {
    db.deleteTokenByHash(tokenHash);
    return { error: '登录已过期，请重新登录' };
  }
  const userRow = db.getUserById(row.user_id);
  if (!userRow) {
    db.deleteTokenByHash(tokenHash);
    return { error: '账号不存在，请重新登录' };
  }
  if (userRow.disabled) {
    return { error: '账号已被禁用，请联系管理员' };
  }
  db.touchToken(row.id);
  return { user: db.serializeUser(userRow), tokenHash };
}

/**
 * 强制要求登录；失败时已写出 401 响应，返回 null。
 */
function requireAuth(req, res) {
  const auth = authenticate(req);
  if (!auth.user) {
    sendError(res, 401, auth.error || '未登录', 'UNAUTHORIZED');
    return null;
  }
  return auth.user;
}

function hasPermission(user, bit) {
  if (!user) {
    return false;
  }
  if (user.role === ROLES.SUPER) {
    return true;
  }
  return (user.perms & bit) === bit;
}

/**
 * 权限校验；失败时写出 403 响应并返回 false。
 */
function requirePermission(res, user, bit, actionLabel) {
  if (hasPermission(user, bit)) {
    return true;
  }
  sendError(res, 403, `没有权限执行该操作：${actionLabel}`, 'FORBIDDEN');
  return false;
}

function sanitizePermissions(value, role) {
  const numeric = Number(value);
  if (!Number.isInteger(numeric) || numeric < 0) {
    return null;
  }
  const bounded = numeric & ALL_PERMISSIONS;
  if (role === ROLES.SUPER) {
    return ALL_PERMISSIONS;
  }
  return bounded;
}

function userPermissionList(user) {
  const list = [];
  for (const [key, bit] of Object.entries(PERMS)) {
    if (hasPermission(user, bit)) {
      list.push(key);
    }
  }
  return list;
}

/* ---------------------------- 登录失败限流 ---------------------------- */

function attemptKey(req, username) {
  return `${clientIp(req)}|${String(username || '').toLowerCase()}`;
}

function checkLoginLock(req, username) {
  const key = attemptKey(req, username);
  const entry = loginAttempts.get(key);
  if (!entry) {
    return 0;
  }
  if (entry.blockedUntil > Date.now()) {
    return Math.ceil((entry.blockedUntil - Date.now()) / 1000);
  }
  if (entry.blockedUntil > 0) {
    loginAttempts.delete(key);
  }
  return 0;
}

function recordLoginFailure(req, username) {
  const key = attemptKey(req, username);
  const entry = loginAttempts.get(key) || { fails: 0, blockedUntil: 0 };
  entry.fails += 1;
  if (entry.fails >= LOGIN_MAX_FAILS) {
    entry.fails = 0;
    entry.blockedUntil = Date.now() + LOGIN_LOCK_MS;
  }
  loginAttempts.set(key, entry);
}

function clearLoginFailures(req, username) {
  loginAttempts.delete(attemptKey(req, username));
}

/* ---------------------------- AES-256-GCM ---------------------------- */

let cachedKey = null;

function getAesKey() {
  if (cachedKey) {
    return cachedKey;
  }
  ensureDir(path.dirname(AES_KEY_PATH));
  if (fs.existsSync(AES_KEY_PATH)) {
    const hex = fs.readFileSync(AES_KEY_PATH, 'utf8').trim();
    if (!/^[A-Fa-f0-9]{64}$/.test(hex)) {
      throw new Error('AES 密钥文件已损坏，请删除后重新启动服务以重新生成');
    }
    cachedKey = Buffer.from(hex, 'hex');
  } else {
    cachedKey = crypto.randomBytes(32);
    fs.writeFileSync(AES_KEY_PATH, cachedKey.toString('hex'), { mode: 0o600 });
  }
  try {
    fs.chmodSync(AES_KEY_PATH, 0o600);
  } catch (chmodErr) {
    console.warn('[加密] 无法设置密钥文件权限：', chmodErr.message);
  }
  return cachedKey;
}

/** 加密敏感字段，输出 v1:iv:tag:cipher（base64） */
function encryptSecret(plainText) {
  if (typeof plainText !== 'string') {
    throw new Error('待加密内容必须是字符串');
  }
  const iv = crypto.randomBytes(12);
  const cipher = crypto.createCipheriv('aes-256-gcm', getAesKey(), iv);
  const encrypted = Buffer.concat([cipher.update(plainText, 'utf8'), cipher.final()]);
  const tag = cipher.getAuthTag();
  return ['v1', iv.toString('base64'), tag.toString('base64'), encrypted.toString('base64')].join(':');
}

/** 解密敏感字段；格式错误或密钥不匹配时抛出中文错误 */
function decryptSecret(payload) {
  if (typeof payload !== 'string') {
    throw new Error('待解密内容必须是字符串');
  }
  const parts = payload.split(':');
  if (parts.length !== 4 || parts[0] !== 'v1') {
    throw new Error('密文格式不正确，无法解密');
  }
  const iv = Buffer.from(parts[1], 'base64');
  const tag = Buffer.from(parts[2], 'base64');
  const data = Buffer.from(parts[3], 'base64');
  const decipher = crypto.createDecipheriv('aes-256-gcm', getAesKey(), iv);
  decipher.setAuthTag(tag);
  try {
    return Buffer.concat([decipher.update(data), decipher.final()]).toString('utf8');
  } catch (err) {
    throw new Error('解密失败，密钥不匹配或数据已损坏');
  }
}

module.exports = {
  ARGON_OPTIONS,
  hashPassword,
  verifyPassword,
  sha256,
  issueToken,
  readBearerToken,
  authenticate,
  requireAuth,
  hasPermission,
  requirePermission,
  sanitizePermissions,
  userPermissionList,
  checkLoginLock,
  recordLoginFailure,
  clearLoginFailures,
  encryptSecret,
  decryptSecret
};
