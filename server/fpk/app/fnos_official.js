'use strict';
/**
 * 飞牛官方 Web API 桥接（WebSocket 协议）。
 *
 * 协议参考（社区整理的 fnOS Web API 文档）：https://fnosp.github.io/fnnas-api/
 *
 * 通信要点：
 *  - WebSocket 地址：ws://{host}:{port}/websocket?type=main（默认 127.0.0.1:5666）
 *  - 登录：先调 util.crypto.getRSAPub 拿 RSA 公钥；本地生成 AES key/iv，
 *    用 RSA 公钥加密 AES key，AES 加密 user.login 请求体，以 encrypted 帧发送；
 *  - 登录返回的 secret 需要用会话 AES key/iv 解密，得到 HMAC-SHA256 签名密钥；
 *  - 后续请求：JSON 字符串前拼接 Base64(HMAC-SHA256(json, key)) 签名后发送；
 *  - 心跳：60s 发 {"req":"ping"}，服务端回 {"res":"pong"}；
 *  - reqid 用于匹配请求与响应。
 *
 * 部分公开命令（getHostName / getHardwareInfo）无需登录即可调用，
 * 因此未配置 NAS 凭据时也能获取基础系统信息。
 *
 * 凭据来源（可选）：环境变量 FNOS_OFFICIAL_USER / FNOS_OFFICIAL_PASSWORD。
 */

const crypto = require('crypto');

const HOST = process.env.FNOS_OFFICIAL_HOST || '127.0.0.1';
const PORT = Number(process.env.FNOS_OFFICIAL_PORT || '5666');
const WS_URL = `ws://${HOST}:${PORT}/websocket?type=main`;
const USER = process.env.FNOS_OFFICIAL_USER || '';
const PASSWORD = process.env.FNOS_OFFICIAL_PASSWORD || '';

const REQUEST_TIMEOUT_MS = 8000;
const STATUS_CACHE_MS = 60000;

let reqidCounter = 0;
function nextReqid() {
  reqidCounter = (reqidCounter + 1) % 0xffff;
  return `${Date.now().toString(16)}${process.pid.toString(16).padStart(8, '0')}${reqidCounter
    .toString(16)
    .padStart(8, '0')}`;
}

/** 官方 API 会话：一条 WebSocket 连接 + 可选的登录态 */
class OfficialSession {
  constructor() {
    this.ws = null;
    this.pending = new Map(); // reqid -> {resolve, reject, timer}
    this.secretKey = null; // HMAC 签名密钥（base64）
    this.loggedIn = false;
    this.loginUser = null;
    this.lastError = null;
    this.pingTimer = null;
    this.connecting = null;
  }

  connect() {
    if (this.ws && this.ws.readyState === 1) {
      return Promise.resolve();
    }
    if (this.connecting) {
      return this.connecting;
    }
    this.lastError = null;
    this.connecting = new Promise((resolve, reject) => {
      let settled = false;
      let ws;
      try {
        ws = new WebSocket(WS_URL); // Node 22 内置 WebSocket
      } catch (err) {
        this.connecting = null;
        reject(new Error(`无法创建 WebSocket：${err.message}`));
        return;
      }
      const timer = setTimeout(() => {
        if (!settled) {
          settled = true;
          this.connecting = null;
          try { ws.close(); } catch (_) { /* ignore */ }
          reject(new Error('连接官方 API 超时（ws://…/websocket）'));
        }
      }, REQUEST_TIMEOUT_MS);
      ws.addEventListener('open', () => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        this.ws = ws;
        this.attach(ws);
        this.startPing();
        resolve();
      });
      ws.addEventListener('error', () => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        this.connecting = null;
        reject(new Error(`无法连接官方 API（${WS_URL}），请确认飞牛 Web 服务已开启`));
      });
    });
    this.connecting.catch(() => { /* 由调用方处理 */ });
    return this.connecting;
  }

  attach(ws) {
    ws.addEventListener('message', (event) => {
      const raw = typeof event.data === 'string' ? event.data : '';
      if (!raw) return;
      let msg;
      try {
        msg = JSON.parse(raw);
      } catch (_) {
        return;
      }
      if (msg.res === 'pong') return;
      const reqid = msg.reqid;
      if (!reqid || !this.pending.has(reqid)) return;
      const entry = this.pending.get(reqid);
      this.pending.delete(reqid);
      clearTimeout(entry.timer);
      if (msg.result === 'succ' || msg.result === undefined) {
        entry.resolve(msg);
      } else {
        const err = new Error(trimErrorMessage(msg.result) || `官方 API 错误 ${msg.errno ?? ''}`);
        err.errno = msg.errno;
        entry.reject(err);
      }
    });
    ws.addEventListener('close', () => {
      this.ws = null;
      this.loggedIn = false;
      this.secretKey = null;
      this.stopPing();
      for (const [, entry] of this.pending) {
        clearTimeout(entry.timer);
        entry.reject(new Error('官方 API 连接已断开'));
      }
      this.pending.clear();
    });
  }

  startPing() {
    this.stopPing();
    this.pingTimer = setInterval(() => {
      if (this.ws && this.ws.readyState === 1) {
        try { this.ws.send(JSON.stringify({ req: 'ping' })); } catch (_) { /* ignore */ }
      }
    }, 60000);
  }

  stopPing() {
    if (this.pingTimer) {
      clearInterval(this.pingTimer);
      this.pingTimer = null;
    }
  }

  /** 发送一帧并等待同 reqid 的响应 */
  sendFrame(payload) {
    return new Promise((resolve, reject) => {
      if (!this.ws || this.ws.readyState !== 1) {
        reject(new Error('官方 API 未连接'));
        return;
      }
      const reqid = payload.reqid;
      const timer = setTimeout(() => {
        this.pending.delete(reqid);
        reject(new Error('官方 API 响应超时'));
      }, REQUEST_TIMEOUT_MS);
      this.pending.set(reqid, { resolve, reject, timer });
      try {
        this.ws.send(JSON.stringify(payload));
      } catch (err) {
        clearTimeout(timer);
        this.pending.delete(reqid);
        reject(new Error(`官方 API 发送失败：${err.message}`));
      }
    });
  }

  /** 公开命令（无需登录） */
  async publicRequest(req, data) {
    await this.connect();
    return this.sendFrame(Object.assign({ reqid: nextReqid(), req }, data || {}));
  }

  /** 登录（RSA + AES 加密），成功后保存签名密钥 */
  async login(user, password) {
    await this.connect();
    const pubRes = await this.publicRequest('util.crypto.getRSAPub');
    const pem = pubRes.pub;
    const si = pubRes.si;
    if (!pem) {
      throw new Error('官方 API 未返回 RSA 公钥');
    }

    // 依次尝试 AES-128 / AES-256（文档未标明密钥长度，按两种常见规格兜底）
    let lastErr = null;
    for (const keyLen of [16, 32]) {
      try {
        const result = await this.loginAttempt(pem, si, user, password, keyLen);
        return result;
      } catch (err) {
        lastErr = err;
        if (err.errno === 131072 || err.errno === 131073) {
          throw err; // 用户名或密码错误 —— 与密钥长度无关，直接抛出
        }
      }
    }
    throw lastErr || new Error('官方 API 登录失败');
  }

  async loginAttempt(pem, si, user, password, aesKeyLen) {
    const aesKey = crypto.randomBytes(aesKeyLen);
    const aesIv = crypto.randomBytes(16);

    const payload = {
      req: 'user.login',
      reqid: nextReqid(),
      user,
      password,
      deviceType: 'Browser',
      deviceName: 'FnDesktopPlus',
      stay: false,
      si: si || ''
    };
    const cipher = crypto.createCipheriv(
      aesKeyLen === 16 ? 'aes-128-cbc' : 'aes-256-cbc', aesKey, aesIv
    );
    const encrypted = Buffer.concat([
      cipher.update(Buffer.from(JSON.stringify(payload), 'utf8')),
      cipher.final()
    ]);
    const rsaEnc = crypto.publicEncrypt(
      { key: pem, padding: crypto.constants.RSA_PKCS1_PADDING },
      aesKey
    );

    const frame = {
      req: 'encrypted',
      iv: aesIv.toString('base64'),
      rsa: rsaEnc.toString('base64'),
      aes: encrypted.toString('base64')
    };
    // encrypted 帧本身不走 sendFrame 的 reqid 匹配（响应里 reqid 与 payload 一致）
    const res = await this.sendFrame(Object.assign({ reqid: payload.reqid }, frame));
    if (res.result !== 'succ') {
      const err = new Error(trimErrorMessage(res.result) || '官方 API 登录失败');
      err.errno = res.errno;
      throw err;
    }

    // secret 用会话 AES key/iv 解密 → HMAC 签名密钥（base64）
    let hmacKey = null;
    if (res.secret) {
      try {
        const decipher = crypto.createDecipheriv(
          aesKeyLen === 16 ? 'aes-128-cbc' : 'aes-256-cbc', aesKey, aesIv
        );
        hmacKey = Buffer.concat([
          decipher.update(Buffer.from(res.secret, 'base64')),
          decipher.final()
        ]).toString('utf8');
      } catch (_) {
        hmacKey = res.secret; // 未加密时直接使用
      }
    }
    this.secretKey = hmacKey;
    this.loggedIn = true;
    this.loginUser = user;
    return res;
  }

  /** 已登录的签名请求 */
  async signedRequest(req, data) {
    if (!this.loggedIn || !this.secretKey) {
      throw new Error('官方 API 未登录');
    }
    const body = Object.assign({ reqid: nextReqid(), req }, data || {});
    const json = JSON.stringify(body);
    const sign = crypto
      .createHmac('sha256', Buffer.from(this.secretKey, 'base64'))
      .update(json, 'utf8')
      .digest('base64');
    const raw = sign + json;
    return new Promise((resolve, reject) => {
      if (!this.ws || this.ws.readyState !== 1) {
        reject(new Error('官方 API 未连接'));
        return;
      }
      const timer = setTimeout(() => {
        reject(new Error('官方 API 响应超时'));
      }, REQUEST_TIMEOUT_MS);
      const onMessage = (event) => {
        const rawText = typeof event.data === 'string' ? event.data : '';
        if (!rawText) return;
        let msg;
        try { msg = JSON.parse(rawText); } catch (_) { return; }
        if (msg.reqid !== body.reqid) return;
        clearTimeout(timer);
        this.ws.removeEventListener('message', onMessage);
        if (msg.result === 'succ' || msg.result === undefined) {
          resolve(msg);
        } else {
          const err = new Error(trimErrorMessage(msg.result) || `官方 API 错误 ${msg.errno ?? ''}`);
          err.errno = msg.errno;
          reject(err);
        }
      };
      this.ws.addEventListener('message', onMessage);
      try {
        this.ws.send(raw);
      } catch (err) {
        clearTimeout(timer);
        this.ws.removeEventListener('message', onMessage);
        reject(new Error(`官方 API 发送失败：${err.message}`));
      }
    });
  }
}

function trimErrorMessage(text) {
  if (!text) return '';
  return String(text).slice(0, 200);
}

const session = new OfficialSession();
let statusCache = null;
let statusCacheAt = 0;

/** 拉取官方系统信息（公开命令 + 可选登录命令），带 60s 缓存 */
async function collectStatus() {
  const now = Date.now();
  if (statusCache && now - statusCacheAt < STATUS_CACHE_MS) {
    return statusCache;
  }

  const status = {
    host: HOST,
    port: PORT,
    wsUrl: WS_URL,
    credentialsConfigured: Boolean(USER && PASSWORD),
    loggedIn: false,
    connected: false
  };

  try {
    const hostRes = await session.publicRequest('appcgi.sysinfo.getHostName');
    status.connected = true;
    status.hostName = hostRes.data ? hostRes.data.hostName : undefined;
    status.hasUsers = hostRes.data ? hostRes.data.hasUsers : undefined;
    status.trimVersion = hostRes.data ? hostRes.data.trimVersion : undefined;
  } catch (err) {
    status.lastError = err.message;
    statusCache = status;
    statusCacheAt = now;
    return status;
  }

  try {
    const hwRes = await session.publicRequest('appcgi.sysinfo.getHardwareInfo');
    if (hwRes.data) {
      status.cpu = hwRes.data.cpu;
      status.memory = hwRes.data.mem;
      status.system = hwRes.data.bios ? hwRes.data.bios.system : undefined;
    }
  } catch (_) { /* 可选 */ }

  if (USER && PASSWORD) {
    try {
      if (!session.loggedIn || session.loginUser !== USER) {
        await session.login(USER, PASSWORD);
      }
      status.loggedIn = true;
      status.loginUser = USER;
      try {
        const verRes = await session.signedRequest('appcgi.sysinfo.getTrimVersion');
        if (verRes.data && verRes.data.trimVersion) {
          status.trimVersion = verRes.data.trimVersion;
        }
      } catch (_) { /* 可选 */ }
    } catch (err) {
      status.loginError = err.message;
      if (err.errno) {
        status.loginErrno = err.errno;
      }
    }
  }

  statusCache = status;
  statusCacheAt = now;
  return status;
}

/** HTTP 处理：GET /api/fnos/status */
async function handleStatus(req, res, sendJson) {
  try {
    const status = await collectStatus();
    sendJson(res, 200, Object.assign({ ok: true }, status));
  } catch (err) {
    sendJson(res, 200, {
      ok: false,
      connected: false,
      wsUrl: WS_URL,
      credentialsConfigured: Boolean(USER && PASSWORD),
      lastError: err.message
    });
  }
}

module.exports = { handleStatus };
