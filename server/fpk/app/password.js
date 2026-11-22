'use strict';

/**
 * Argon2id 后端选择（独立模块，避免 auth.js 静态 require 原生模块）：
 * 1) 优先用 Node.js 22+ 内置的 crypto.argon2Sync / crypto.argon2（OpenSSL 3.2+），无需编译、无 ABI 风险；
 * 2) 否则退回 npm 的 argon2 原生模块。
 *
 * npm 版 argon2 使用 N-API 异步 worker 线程，在飞牛应用中心自带的沙箱化 Node（v20.18.1）里
 * 一加载/调用就段错误，因此这里：
 *   - 只有在没有内置实现时才 require 原生模块；
 *   - 并且先用子进程做一次真实哈希自检，子进程崩溃不会带走主服务。
 */

const crypto = require('crypto');
const path = require('path');
const { promisify } = require('util');
const { spawnSync } = require('child_process');

const argon2Async = typeof crypto.argon2 === 'function' ? promisify(crypto.argon2) : null;

const PARAMS = Object.freeze({
  memory: 1 << 15,
  passes: 3,
  parallelism: 4,
  tagLength: 64,
  saltLength: 32
});

const BASE64 = 'base64';

let backend = null;

function probeNodeCrypto() {
  if (typeof crypto.argon2Sync !== 'function' || typeof crypto.argon2 !== 'function') {
    return '当前 Node.js 没有内置 Argon2';
  }
  try {
    const tag = crypto.argon2Sync('argon2id', {
      message: Buffer.from('probe', 'utf8'),
      nonce: Buffer.alloc(16, 7),
      parallelism: 1,
      tagLength: 16,
      memory: 256,
      passes: 1
    });
    if (!Buffer.isBuffer(tag) || tag.length !== 16) {
      return '内置 Argon2 自检返回了异常结果';
    }
    return null;
  } catch (err) {
    return `内置 Argon2 自检失败：${err.message}`;
  }
}

function probeNativeModule() {
  const script = [
    'const m = require(process.argv[1]);',
    'm.hash("probe", { memoryCost: 256, timeCost: 1, parallelism: 1, hashLength: 16, salt: Buffer.alloc(16, 7) })',
    '  .then((h) => { if (typeof h !== "string" || h.indexOf("$argon2id$") !== 0) { process.exit(3); } process.exit(0); })',
    '  .catch((e) => { console.error(e && e.message); process.exit(4); });'
  ].join('\n');
  const result = spawnSync(process.execPath, ['-e', script, path.join(__dirname, 'node_modules', 'argon2')], {
    timeout: 20000,
    encoding: 'utf8'
  });
  if (result.error) {
    return `argon2 原生模块自检无法执行：${result.error.message}`;
  }
  if (result.signal) {
    return `argon2 原生模块自检进程被信号 ${result.signal} 终止（该运行时与原生模块不兼容）`;
  }
  if (result.status !== 0) {
    const detail = (result.stderr || '').trim() || `退出码 ${result.status}`;
    return `argon2 原生模块自检失败：${detail}`;
  }
  return null;
}

/**
 * 返回 { kind: 'node-crypto' | 'native', impl? }；两种后端都不可用时抛出中文错误。
 */
function getBackend() {
  if (backend) {
    return backend;
  }
  const builtinError = probeNodeCrypto();
  if (!builtinError) {
    backend = { kind: 'node-crypto' };
    return backend;
  }
  console.warn(`[认证] ${builtinError}`);
  const nativeError = probeNativeModule();
  if (!nativeError) {
    backend = { kind: 'native', impl: require('argon2') };
    return backend;
  }
  console.error(`[认证] ${nativeError}`);
  throw new Error(`${builtinError}；${nativeError}。请使用 Node.js 22.7 及以上版本运行服务端`);
}

/** 去掉 Base64 尾部填充（PHC 规范要求无填充） */
function b64(buf) {
  return buf.toString(BASE64).replace(/=+$/, '');
}

/** 生成 PHC 字符串：$argon2id$v=19$m=32768,t=3,p=4$salt$hash（salt/hash 为无填充 Base64） */
function formatPhc(memory, passes, parallelism, salt, hash) {
  return `$argon2id$v=19$m=${memory},t=${passes},p=${parallelism}$${b64(salt)}$${b64(hash)}`;
}

function parsePhc(digest) {
  const parts = String(digest).split('$');
  if (parts.length !== 6 || parts[1] !== 'argon2id' || parts[2] !== 'v=19') {
    return null;
  }
  const params = {};
  for (const piece of parts[3].split(',')) {
    const pair = piece.split('=');
    if (pair.length === 2) {
      params[pair[0]] = Number(pair[1]);
    }
  }
  const memory = params.m;
  const passes = params.t;
  const parallelism = params.p;
  if (![memory, passes, parallelism].every((value) => Number.isInteger(value) && value > 0)) {
    return null;
  }
  const salt = Buffer.from(parts[4], 'base64');
  const hash = Buffer.from(parts[5], 'base64');
  if (salt.length === 0 || hash.length === 0) {
    return null;
  }
  return { memory, passes, parallelism, salt, hash };
}

/** 计算 Argon2id 原始摘要（Buffer） */
async function deriveRaw(password, salt, memory, passes, parallelism, tagLength) {
  const impl = getBackend();
  if (impl.kind === 'node-crypto') {
    const tag = await argon2Async('argon2id', {
      message: Buffer.from(password, 'utf8'),
      nonce: salt,
      parallelism,
      tagLength,
      memory,
      passes
    });
    return Buffer.isBuffer(tag) ? tag : Buffer.from(tag);
  }
  const encoded = await impl.impl.hash(password, {
    type: impl.impl.argon2id,
    memoryCost: memory,
    timeCost: passes,
    parallelism,
    hashLength: tagLength,
    salt,
    raw: true
  });
  if (!Buffer.isBuffer(encoded)) {
    throw new Error('argon2 原生模块返回了非预期的摘要类型');
  }
  return encoded;
}

/** 生成密码哈希（默认参数 N=2^15, t=3, p=4, salt 32B, 输出 64B） */
async function hashPassword(password) {
  if (typeof password !== 'string' || password.length === 0) {
    throw new Error('密码不能为空');
  }
  const salt = crypto.randomBytes(PARAMS.saltLength);
  const raw = await deriveRaw(password, salt, PARAMS.memory, PARAMS.passes, PARAMS.parallelism, PARAMS.tagLength);
  return formatPhc(PARAMS.memory, PARAMS.passes, PARAMS.parallelism, salt, raw);
}

/** 校验密码；参数从哈希串自身解析（支持后续升级参数后的旧哈希） */
async function verifyPassword(digest, password) {
  if (typeof digest !== 'string' || typeof password !== 'string' || password.length === 0) {
    return false;
  }
  const parsed = parsePhc(digest);
  if (!parsed) {
    console.error('[认证] 密码哈希格式不正确，无法校验');
    return false;
  }
  try {
    const raw = await deriveRaw(
      password,
      parsed.salt,
      parsed.memory,
      parsed.passes,
      parsed.parallelism,
      parsed.hash.length
    );
    if (raw.length !== parsed.hash.length) {
      return false;
    }
    return crypto.timingSafeEqual(raw, parsed.hash);
  } catch (err) {
    console.error('[认证] 密码校验失败：', err.message);
    return false;
  }
}

module.exports = {
  PARAMS,
  getBackend,
  hashPassword,
  verifyPassword
};
