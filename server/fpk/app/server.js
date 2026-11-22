'use strict';

/**
 * 飞牛终端服务端入口
 * - HTTPS（仅 TLS 1.3，自签证书）
 * - REST API + WebSocket（Docker 日志）
 * - 托管内置于 public/ 的 Web 管理后台
 */

const fs = require('fs');
const https = require('https');
const os = require('os');
const path = require('path');

const auth = require('./auth');
const db = require('./db');
const discover = require('./discover');
const dockerRouter = require('./docker');
const filesRouter = require('./files');
const fnosOfficial = require('./fnos_official');
const mounts = require('./mounts');
const sharesRouter = require('./shares');
const usersRouter = require('./users');
const wsServer = require('./ws');
const { generateCertificate } = require('./gen_cert');
const {
  PUBLIC_DIR,
  DATA_DIR,
  FILES_ROOT,
  PORT,
  HOST,
  SERVER_NAME,
  SERVER_VERSION,
  PERMS,
  ROLES,
  ROLE_LABELS,
  ALL_PERMISSIONS,
  ensureBaseDirs,
  sendJson,
  sendError,
  readJson,
  mimeOf,
  safeResolve,
  isValidUsername,
  passwordPolicyMessage,
  clientIp
} = require('./util');

ensureBaseDirs();
mounts.ensureMountAbsDirs();

/* ------------------------------ CPU 采样 ------------------------------ */

let lastCpuSample = null;

function readCpuTimes() {
  return os.cpus().map((cpu) => {
    const times = cpu.times;
    return {
      idle: times.idle,
      total: times.user + times.nice + times.sys + times.idle + times.irq
    };
  });
}

function cpuUsagePercent() {
  const current = readCpuTimes();
  if (current.length === 0) {
    return 0;
  }
  if (!lastCpuSample || lastCpuSample.length !== current.length) {
    lastCpuSample = current;
    let idle = 0;
    let total = 0;
    for (const sample of current) {
      idle += sample.idle;
      total += sample.total;
    }
    return total > 0 ? Math.round((1 - idle / total) * 1000) / 10 : 0;
  }
  let idleDelta = 0;
  let totalDelta = 0;
  for (let i = 0; i < current.length; i += 1) {
    idleDelta += current[i].idle - lastCpuSample[i].idle;
    totalDelta += current[i].total - lastCpuSample[i].total;
  }
  lastCpuSample = current;
  if (totalDelta <= 0) {
    return 0;
  }
  const percent = (1 - idleDelta / totalDelta) * 100;
  return Math.round(Math.max(0, Math.min(100, percent)) * 10) / 10;
}

function systemInfo() {
  const cpus = os.cpus();
  const totalMem = os.totalmem();
  const freeMem = os.freemem();
  const usedMem = totalMem - freeMem;
  return {
    hostname: os.hostname(),
    platform: os.platform(),
    arch: os.arch(),
    release: os.release(),
    cpuModel: cpus.length > 0 ? cpus[0].model.trim() : '未知处理器',
    cpuCores: cpus.length,
    cpuUsage: cpuUsagePercent(),
    memTotal: totalMem,
    memUsed: usedMem,
    memFree: freeMem,
    memUsage: totalMem > 0 ? Math.round((usedMem / totalMem) * 1000) / 10 : 0,
    uptime: os.uptime(),
    processUptime: process.uptime(),
    loadavg: os.loadavg(),
    nodeVersion: process.version,
    serverVersion: SERVER_VERSION,
    dataDir: DATA_DIR,
    filesRoot: FILES_ROOT
  };
}

/* ------------------------------ 证书与配置 ------------------------------ */

let certInfo;
try {
  certInfo = generateCertificate(false);
} catch (err) {
  console.error('[证书] 初始化失败：', err.message);
  process.exit(1);
}

const serverStartedAt = Date.now();

function publicPing() {
  return {
    ok: true,
    name: SERVER_NAME,
    version: SERVER_VERSION,
    initialized: db.countUsers() > 0,
    time: Date.now(),
    startedAt: serverStartedAt,
    tls: 'TLSv1.3',
    fingerprint: certInfo.fingerprint,
    fingerprintPretty: certInfo.fingerprintPretty,
    hostname: os.hostname()
  };
}

/* ------------------------------ 认证路由 ------------------------------ */

async function handleSetupStatus(req, res) {
  const total = db.countUsers();
  sendJson(res, 200, {
    ok: true,
    initialized: total > 0,
    userCount: total
  });
}

async function handleSetupInit(req, res) {
  if (db.countUsers() > 0) {
    sendError(res, 409, '系统已经初始化，不能重复创建超级管理员', 'ALREADY_INITIALIZED');
    return;
  }
  const body = await readJson(req, 64 * 1024);
  const username = typeof body.username === 'string' ? body.username.trim() : '';
  if (!isValidUsername(username)) {
    sendError(res, 400, '用户名需为 3~32 位字母、数字、下划线、点或短横线', 'INVALID_USERNAME');
    return;
  }
  const passwordError = passwordPolicyMessage(body.password);
  if (passwordError) {
    sendError(res, 400, passwordError, 'WEAK_PASSWORD');
    return;
  }
  const passwordHash = await auth.hashPassword(body.password);
  const created = db.createUser({
    username,
    passwordHash,
    role: ROLES.SUPER,
    perms: ALL_PERMISSIONS,
    disabled: false
  });
  db.addAudit({
    userId: created.id,
    username,
    action: 'SYSTEM_INIT',
    detail: '系统初始化，创建超级管理员',
    ip: clientIp(req)
  });
  const issued = auth.issueToken(created, clientIp(req));
  sendJson(res, 201, {
    ok: true,
    user: db.serializeUser(created),
    token: issued.token,
    expiresAt: issued.expiresAt,
    permissions: auth.userPermissionList(db.serializeUser(created))
  });
}

async function handleLogin(req, res) {
  const body = await readJson(req, 64 * 1024);
  const username = typeof body.username === 'string' ? body.username.trim() : '';
  const password = typeof body.password === 'string' ? body.password : '';
  if (!username || !password) {
    sendError(res, 400, '请输入用户名和密码', 'MISSING_CREDENTIALS');
    return;
  }
  const lockedSeconds = auth.checkLoginLock(req, username);
  if (lockedSeconds > 0) {
    sendError(res, 429, `登录失败次数过多，请 ${lockedSeconds} 秒后再试`, 'LOGIN_LOCKED');
    return;
  }
  const row = db.getUserByUsername(username);
  const passwordOk = row ? await auth.verifyPassword(row.password_hash, password) : false;
  if (!row || !passwordOk) {
    auth.recordLoginFailure(req, username);
    db.addAudit({
      userId: row ? row.id : null,
      username,
      action: 'LOGIN_FAILED',
      detail: '用户名或密码错误',
      ip: clientIp(req)
    });
    sendError(res, 401, '用户名或密码错误', 'INVALID_CREDENTIALS');
    return;
  }
  if (row.disabled) {
    sendError(res, 403, '账号已被禁用，请联系管理员', 'ACCOUNT_DISABLED');
    return;
  }
  auth.clearLoginFailures(req, username);
  const user = db.serializeUser(row);
  const issued = auth.issueToken(user, clientIp(req));
  db.addAudit({
    userId: user.id,
    username: user.username,
    action: 'LOGIN',
    detail: '登录成功',
    ip: clientIp(req)
  });
  sendJson(res, 200, {
    ok: true,
    token: issued.token,
    expiresAt: issued.expiresAt,
    user,
    roleLabel: ROLE_LABELS[user.role] || user.role,
    permissions: auth.userPermissionList(user)
  });
}

function handleLogout(req, res) {
  const result = auth.authenticate(req);
  if (!result.user) {
    sendError(res, 401, result.error || '未登录', 'UNAUTHORIZED');
    return;
  }
  db.deleteTokenByHash(result.tokenHash);
  db.addAudit({
    userId: result.user.id,
    username: result.user.username,
    action: 'LOGOUT',
    detail: '退出登录',
    ip: clientIp(req)
  });
  sendJson(res, 200, { ok: true });
}

function handleMe(req, res) {
  const user = auth.requireAuth(req, res);
  if (!user) {
    return;
  }
  sendJson(res, 200, {
    ok: true,
    user,
    roleLabel: ROLE_LABELS[user.role] || user.role,
    permissions: auth.userPermissionList(user)
  });
}

async function handleChangePassword(req, res) {
  const user = auth.requireAuth(req, res);
  if (!user) {
    return;
  }
  const body = await readJson(req, 64 * 1024);
  const row = db.getUserById(user.id);
  if (!row) {
    sendError(res, 404, '账号不存在', 'NOT_FOUND');
    return;
  }
  const oldOk = await auth.verifyPassword(row.password_hash, typeof body.oldPassword === 'string' ? body.oldPassword : '');
  if (!oldOk) {
    sendError(res, 400, '当前密码不正确', 'INVALID_OLD_PASSWORD');
    return;
  }
  const passwordError = passwordPolicyMessage(body.newPassword);
  if (passwordError) {
    sendError(res, 400, passwordError, 'WEAK_PASSWORD');
    return;
  }
  const newHash = await auth.hashPassword(body.newPassword);
  db.updateUserPassword(user.id, newHash);
  db.deleteTokensByUser(user.id);
  const fresh = db.getUserById(user.id);
  const issued = auth.issueToken(fresh, clientIp(req));
  db.addAudit({
    userId: user.id,
    username: user.username,
    action: 'PASSWORD_CHANGE',
    detail: '修改自己的登录密码',
    ip: clientIp(req)
  });
  sendJson(res, 200, {
    ok: true,
    token: issued.token,
    expiresAt: issued.expiresAt,
    message: '密码已更新，其他设备的登录状态已失效'
  });
}

function handleAuditList(req, res, url) {
  const user = auth.requireAuth(req, res);
  if (!user) {
    return;
  }
  if (!auth.requirePermission(res, user, PERMS.SYSTEM_AUDIT_VIEW, '查看审计日志')) {
    return;
  }
  const limit = Number(url.searchParams.get('limit') || '50');
  const offset = Number(url.searchParams.get('offset') || '0');
  const result = db.listAudit(limit, offset);
  sendJson(res, 200, {
    ok: true,
    total: result.total,
    items: result.items.map((row) => ({
      id: row.id,
      userId: row.user_id,
      username: row.username,
      action: row.action,
      detail: row.detail,
      ip: row.ip,
      createdAt: row.created_at
    }))
  });
}

function handleSystemInfo(req, res) {
  const user = auth.requireAuth(req, res);
  if (!user) {
    return;
  }
  sendJson(res, 200, Object.assign({ ok: true }, systemInfo()));
}

/* ------------------------------ 静态资源 ------------------------------ */

const SECURITY_HEADERS = {
  'X-Content-Type-Options': 'nosniff',
  'X-Frame-Options': 'DENY',
  'Referrer-Policy': 'no-referrer',
  'Content-Security-Policy':
    "default-src 'self'; style-src 'self' 'unsafe-inline'; script-src 'self'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'self'; form-action 'self'"
};

/**
 * 公开分享页专用 CSP：分享页是内联单页（无外部 CDN、无外链资源），
 * 因此放开内联脚本与内联样式，其余仍只允许同源资源。
 */
const SHARE_PAGE_CSP =
  "default-src 'self'; style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'; img-src 'self' data:; media-src 'self'";

/** 判断是否为公开分享页路径（/s/<token>） */
function isSharePagePath(pathname) {
  return pathname === '/s' || pathname.startsWith('/s/');
}

function serveStatic(req, res, pathname) {
  if (req.method !== 'GET' && req.method !== 'HEAD') {
    sendError(res, 405, '静态资源仅支持 GET 请求', 'METHOD_NOT_ALLOWED');
    return;
  }
  let decoded;
  try {
    decoded = decodeURIComponent(pathname);
  } catch (decodeErr) {
    sendError(res, 400, '请求路径编码不正确', 'INVALID_PATH');
    return;
  }
  const virtualPath = decoded === '/' ? '/index.html' : decoded;
  const resolved = safeResolve(PUBLIC_DIR, virtualPath);
  if (!resolved) {
    sendError(res, 400, '非法的静态资源路径', 'INVALID_PATH');
    return;
  }

  fs.promises.stat(resolved.abs)
    .then((stat) => {
      if (!stat.isFile()) {
        sendError(res, 404, '资源不存在', 'NOT_FOUND');
        return null;
      }
      const ext = path.extname(resolved.abs).toLowerCase();
      const headers = Object.assign(
        {
          'Content-Type': mimeOf(resolved.abs),
          'Content-Length': stat.size,
          'Cache-Control': 'no-cache, must-revalidate'
        },
        SECURITY_HEADERS
      );
      res.writeHead(200, headers);
      if (req.method === 'HEAD') {
        res.end();
        return null;
      }
      const stream = fs.createReadStream(resolved.abs);
      stream.on('error', (err) => {
        console.error('[静态资源] 读取失败：', err.message);
        res.destroy();
      });
      stream.pipe(res);
      return null;
    })
    .catch((err) => {
      if (err && err.code === 'ENOENT') {
        sendError(res, 404, '资源不存在', 'NOT_FOUND');
        return;
      }
      console.error('[静态资源] 处理失败：', err.message);
      sendError(res, 500, `读取静态资源失败：${err.message}`, 'STATIC_ERROR');
    });
}

/* ------------------------------ 主请求处理 ------------------------------ */

async function route(req, res) {
  const url = new URL(req.url, `https://${req.headers.host || 'localhost'}`);
  const pathname = url.pathname;
  const method = req.method;

  if (pathname.startsWith('/api/')) {
    res.setHeader('Cache-Control', 'no-store');
  }
  const sharePageRequest = isSharePagePath(pathname);
  for (const [key, value] of Object.entries(SECURITY_HEADERS)) {
    if (key === 'Content-Security-Policy' && sharePageRequest) {
      res.setHeader(key, SHARE_PAGE_CSP);
      continue;
    }
    res.setHeader(key, value);
  }

  if (pathname === '/api/ping' && method === 'GET') {
    sendJson(res, 200, publicPing());
    return;
  }

  if (pathname === '/api/setup/status' && method === 'GET') {
    await handleSetupStatus(req, res);
    return;
  }

  if (pathname === '/api/setup/init' && method === 'POST') {
    await handleSetupInit(req, res);
    return;
  }

  if (pathname === '/api/auth/login' && method === 'POST') {
    await handleLogin(req, res);
    return;
  }

  if (pathname === '/api/auth/logout' && method === 'POST') {
    handleLogout(req, res);
    return;
  }

  if (pathname === '/api/auth/me' && method === 'GET') {
    handleMe(req, res);
    return;
  }

  if (pathname === '/api/auth/password' && method === 'POST') {
    await handleChangePassword(req, res);
    return;
  }

  if (pathname === '/api/system/info' && method === 'GET') {
    handleSystemInfo(req, res);
    return;
  }

  if (pathname === '/api/system/storage' && method === 'GET') {
    await discover.handleStorage(req, res);
    return;
  }

  if (pathname === '/api/apps' && method === 'GET') {
    await discover.handleApps(req, res);
    return;
  }

  if (pathname === '/api/audit' && method === 'GET') {
    handleAuditList(req, res, url);
    return;
  }

  /* ---------------------- 飞牛官方 API 桥接 ---------------------- */

  if (pathname === '/api/fnos/status' && method === 'GET') {
    const user = auth.requireAuth(req, res);
    if (!user) {
      return;
    }
    await fnosOfficial.handleStatus(req, res, sendJson);
    return;
  }

  /* ---------------------- 公开分享路由（无需登录） ---------------------- */

  if (pathname.startsWith('/api/share/')) {
    const handled = await sharesRouter.handlePublicApi({ req, res, url, pathname, method });
    if (!handled) {
      sendError(res, 404, '接口不存在', 'NOT_FOUND');
    }
    return;
  }

  if (isSharePagePath(pathname)) {
    const handled = await sharesRouter.handleSharePage({ req, res, url, pathname, method });
    if (!handled) {
      sendError(res, 404, '分享不存在', 'SHARE_NOT_FOUND');
    }
    return;
  }

  if (pathname === '/api/users' || pathname.startsWith('/api/users/')) {
    const user = auth.requireAuth(req, res);
    if (!user) {
      return;
    }
    const handled = await usersRouter.handle({ req, res, url, pathname, method, user });
    if (!handled) {
      sendError(res, 404, '接口不存在', 'NOT_FOUND');
    }
    return;
  }

  if (pathname === '/api/files' || pathname.startsWith('/api/files/')) {
    const user = auth.requireAuth(req, res);
    if (!user) {
      return;
    }
    await filesRouter.handle({ req, res, url, pathname, method, user });
    return;
  }

  /* ---------------------- 存储 / 网盘挂载 ---------------------- */

  if (pathname === '/api/storage' || pathname.startsWith('/api/storage/')) {
    const user = auth.requireAuth(req, res);
    if (!user) {
      return;
    }
    await filesRouter.handleStorageApi({ req, res, url, pathname, method, user });
    return;
  }

  if (pathname === '/api/shares' || pathname.startsWith('/api/shares/')) {
    const user = auth.requireAuth(req, res);
    if (!user) {
      return;
    }
    const handled = await sharesRouter.handle({ req, res, url, pathname, method, user });
    if (!handled) {
      sendError(res, 404, '接口不存在', 'NOT_FOUND');
    }
    return;
  }

  if (pathname === '/api/docker' || pathname.startsWith('/api/docker/')) {
    const user = auth.requireAuth(req, res);
    if (!user) {
      return;
    }
    await dockerRouter.handle({ req, res, url, pathname, method, user });
    return;
  }

  if (pathname.startsWith('/api/')) {
    sendError(res, 404, '接口不存在', 'NOT_FOUND');
    return;
  }

  serveStatic(req, res, pathname);
}

const server = https.createServer(
  {
    key: certInfo.key,
    cert: certInfo.cert,
    minVersion: 'TLSv1.3',
    maxVersion: 'TLSv1.3'
  },
  (req, res) => {
    route(req, res).catch((err) => {
      const status = err && err.statusCode ? err.statusCode : 500;
      const message = err && err.message ? err.message : '服务器内部错误';
      console.error(`[HTTP] ${req.method} ${req.url} 处理失败：`, err);
      if (!res.headersSent) {
        sendError(res, status, message, status === 500 ? 'INTERNAL_ERROR' : 'BAD_REQUEST');
      } else {
        res.destroy();
      }
    });
  }
);

wsServer.attach(server);

server.on('tlsClientError', (err, socket) => {
  if (err && err.code !== 'ECONNRESET') {
    console.warn('[TLS] 客户端握手失败：', err.message);
  }
  if (socket && !socket.destroyed) {
    socket.destroy();
  }
});

server.on('error', (err) => {
  if (err && err.code === 'EADDRINUSE') {
    console.error(`[服务] 端口 ${PORT} 已被占用，请先停止占用该端口的程序，或设置 FNOS_PORT 更换端口。`);
  } else {
    console.error('[服务] 启动失败：', err.message);
  }
  process.exit(1);
});

server.listen(PORT, HOST, () => {
  const interfaces = os.networkInterfaces();
  const addresses = [];
  for (const name of Object.keys(interfaces)) {
    for (const item of interfaces[name] || []) {
      if (item && item.family === 'IPv4' && !item.internal) {
        addresses.push(item.address);
      }
    }
  }
  console.log('=========================================================');
  console.log(` 飞牛终端服务端 v${SERVER_VERSION} 已启动`);
  console.log(` 监听地址：https://${HOST}:${PORT}`);
  for (const address of addresses) {
    console.log(` 局域网访问：https://${address}:${PORT}`);
  }
  console.log(` 本机访问：https://127.0.0.1:${PORT}`);
  console.log(` 证书指纹：${certInfo.fingerprintPretty}`);
  console.log(` 数据目录：${DATA_DIR}`);
  console.log(` 文件目录：${FILES_ROOT}`);
  console.log(` 数据库：${path.join(DATA_DIR, 'fnos.db')}`);
  console.log(` Node 版本：${process.version}（TLS 1.3 已强制启用）`);
  console.log('=========================================================');
  if (db.countUsers() === 0) {
    console.log(' 当前系统尚未初始化，请打开 Web 后台或客户端创建超级管理员。');
  }
});

/* ------------------------------ 进程守护 ------------------------------ */

process.on('unhandledRejection', (reason) => {
  console.error('[进程] 未处理的 Promise 拒绝：', reason instanceof Error ? reason.message : reason);
});

process.on('uncaughtException', (err) => {
  console.error('[进程] 未捕获异常：', err && err.stack ? err.stack : err);
});

function shutdown(signal) {
  console.log(`\n[服务] 收到 ${signal}，正在关闭...`);
  server.close(() => {
    try {
      db.db.close();
    } catch (closeErr) {
      console.error('[服务] 关闭数据库失败：', closeErr.message);
    }
    console.log('[服务] 已安全退出');
    process.exit(0);
  });
  setTimeout(() => {
    console.warn('[服务] 关闭超时，强制退出');
    process.exit(1);
  }, 5000).unref();
}

process.on('SIGINT', () => shutdown('SIGINT'));
process.on('SIGTERM', () => shutdown('SIGTERM'));
