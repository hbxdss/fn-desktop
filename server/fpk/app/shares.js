'use strict';

/**
 * 分享链接路由
 * - 登录态：GET/POST /api/shares、DELETE /api/shares/:id
 * - 公开态：GET /api/share/:token、GET /s/:token、GET /s/:token/download、GET /s/:token/zip
 * 公开链接支持提取密码（复用 auth.js 的哈希/校验）、过期时间、下载次数限制，
 * 并对同一 IP + token 的密码爆破做惰性限流（10 分钟内 5 次错误即封禁）。
 */

const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

const auth = require('./auth');
const db = require('./db');
const filesRouter = require('./files');
const { createZip } = require('./zip');
const {
  FILES_ROOT,
  DATA_DIR,
  PUBLIC_DIR,
  PERMS,
  ensureDir,
  sendJson,
  sendError,
  readJson,
  mimeOf,
  safeResolve,
  clientIp
} = require('./util');

const { sendFileWithRange, pathSize, isAdminUser } = filesRouter;

/** 分享打包临时目录 */
const SHARE_TMP_DIR = path.join(DATA_DIR, 'share-tmp');
/** 密码爆破限流窗口与阈值 */
const PASSWORD_WINDOW_MS = 10 * 60 * 1000;
const PASSWORD_MAX_FAILS = 5;
/** 分享有效期上限（一年） */
const MAX_EXPIRE_HOURS = 24 * 365;
/** 分享页兜底模板文件 */
const SHARE_PAGE_FILE = path.join(PUBLIC_DIR, 'share.html');

/* ---------------------------- 密码爆破限流 ---------------------------- */

/** key: `${ip}|${token}` → { fails, firstAt, blockedUntil } */
const passwordFailures = new Map();

function passwordKey(req, token) {
  return `${clientIp(req)}|${token}`;
}

/** 惰性清理：无定时器，仅在访问时顺带清理过期记录 */
function sweepPasswordFailures(now) {
  for (const [key, entry] of passwordFailures) {
    if (entry.blockedUntil <= now && now - entry.firstAt > PASSWORD_WINDOW_MS) {
      passwordFailures.delete(key);
    }
  }
}

function checkPasswordLock(req, token) {
  const now = Date.now();
  if (passwordFailures.size > 500) {
    sweepPasswordFailures(now);
  }
  const entry = passwordFailures.get(passwordKey(req, token));
  if (!entry) {
    return 0;
  }
  if (entry.blockedUntil > now) {
    return Math.ceil((entry.blockedUntil - now) / 1000);
  }
  if (now - entry.firstAt > PASSWORD_WINDOW_MS) {
    passwordFailures.delete(passwordKey(req, token));
  }
  return 0;
}

function recordPasswordFailure(req, token) {
  const now = Date.now();
  const key = passwordKey(req, token);
  let entry = passwordFailures.get(key);
  if (!entry || now - entry.firstAt > PASSWORD_WINDOW_MS) {
    entry = { fails: 0, firstAt: now, blockedUntil: 0 };
  }
  entry.fails += 1;
  if (entry.fails >= PASSWORD_MAX_FAILS) {
    entry.fails = 0;
    entry.firstAt = now;
    entry.blockedUntil = now + PASSWORD_WINDOW_MS;
    console.warn('[分享] 同一来源密码错误次数过多，已临时封禁该分享链接');
  }
  passwordFailures.set(key, entry);
}

function clearPasswordFailures(req, token) {
  passwordFailures.delete(passwordKey(req, token));
}

/* ------------------------------ 通用辅助 ------------------------------ */

function audit(ctx, action, detail) {
  db.addAudit({
    userId: ctx.user.id,
    username: ctx.user.username,
    action,
    detail,
    ip: clientIp(ctx.req)
  });
}

/** 分享记录转接口视图（附公开访问地址，不含 password_hash） */
function shareView(row) {
  const view = db.serializeShare(row);
  if (view) {
    view.url = `/s/${row.token}`;
  }
  return view;
}

function normalizeToken(raw) {
  if (typeof raw !== 'string') {
    return null;
  }
  const token = raw.trim();
  if (!/^[A-Za-z0-9_-]{16,64}$/.test(token)) {
    return null;
  }
  return token;
}

/** 校验分享是否可用（存在/未过期/次数未用完），失败时已写出响应 */
function loadShare(res, token) {
  const row = db.getShareByToken(token);
  if (!row) {
    sendError(res, 404, '分享链接不存在或已被取消', 'SHARE_NOT_FOUND');
    return null;
  }
  if (row.expires_at && row.expires_at <= Date.now()) {
    sendError(res, 410, '分享链接已过期', 'SHARE_EXPIRED');
    return null;
  }
  if (row.max_downloads > 0 && row.downloads >= row.max_downloads) {
    sendError(res, 410, '分享链接的下载次数已用完', 'SHARE_LIMIT_REACHED');
    return null;
  }
  return row;
}

/** 解析分享目标文件的绝对路径与 stat，失败时已写出响应 */
async function statShareTarget(row, res) {
  const resolved = safeResolve(FILES_ROOT, row.path);
  if (!resolved) {
    sendError(res, 404, '分享的内容已不可用', 'SHARE_UNAVAILABLE');
    return null;
  }
  const stat = await fs.promises.stat(resolved.abs).catch(() => null);
  if (!stat) {
    sendError(res, 410, '分享的内容已被删除', 'SHARE_UNAVAILABLE');
    return null;
  }
  return { resolved, stat };
}

/** 提取密码校验，通过返回 true；失败时已写出响应 */
async function passPasswordGate(ctx, row) {
  const lockedSeconds = checkPasswordLock(ctx.req, row.token);
  if (lockedSeconds > 0) {
    sendError(
      ctx.res,
      429,
      `提取密码错误次数过多，请 ${lockedSeconds} 秒后再试`,
      'SHARE_PASSWORD_LOCKED'
    );
    return false;
  }
  if (!row.password_hash) {
    return true;
  }
  const pwd = ctx.url.searchParams.get('pwd') || '';
  if (pwd.length === 0) {
    sendError(ctx.res, 401, '该分享需要提取密码', 'SHARE_PASSWORD_REQUIRED');
    return false;
  }
  const ok = await auth.verifyPassword(row.password_hash, pwd);
  if (!ok) {
    recordPasswordFailure(ctx.req, row.token);
    sendError(ctx.res, 401, '提取密码不正确', 'SHARE_PASSWORD_REQUIRED');
    return false;
  }
  clearPasswordFailures(ctx.req, row.token);
  return true;
}

/** 下载完成后计数（同一响应只计一次） */
function countDownload(ctx, row) {
  let counted = false;
  return function bump() {
    if (counted) {
      return;
    }
    counted = true;
    db.incShareDownloads(row.id);
    db.addAudit({
      userId: row.owner_id,
      username: row.owner_name,
      action: 'SHARE_DOWNLOAD',
      detail: `通过分享链接下载 ${row.name}（${row.token}）`,
      ip: clientIp(ctx.req)
    });
  };
}

/* ---------------------------- 登录态分享管理 ---------------------------- */

function handleShareList(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_SHARE, '查看分享链接')) {
    return;
  }
  const wantAll = ctx.url.searchParams.get('all') === '1' && isAdminUser(ctx.user);
  const rows = db.listShares(wantAll ? null : ctx.user.id);
  sendJson(ctx.res, 200, {
    ok: true,
    items: rows.map((row) => shareView(row)),
    total: rows.length
  });
}

async function handleShareCreate(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_SHARE, '创建分享链接')) {
    return;
  }
  const body = await readJson(ctx.req, 64 * 1024);
  const resolved = safeResolve(FILES_ROOT, body.path);
  if (!resolved) {
    sendError(ctx.res, 400, '非法的路径，禁止分享数据目录以外的位置', 'INVALID_PATH');
    return;
  }
  if (resolved.virtual === '/') {
    sendError(ctx.res, 400, '不能分享根目录', 'INVALID_PATH');
    return;
  }
  const stat = await fs.promises.stat(resolved.abs).catch(() => null);
  if (!stat) {
    sendError(ctx.res, 404, '要分享的文件或目录不存在', 'NOT_FOUND');
    return;
  }

  const password = typeof body.password === 'string' ? body.password.trim() : '';
  if (password.length > 128) {
    sendError(ctx.res, 400, '提取密码不能超过 128 位', 'INVALID_PASSWORD');
    return;
  }
  const passwordHash = password.length > 0 ? await auth.hashPassword(password) : null;

  let expiresAt = null;
  const rawHours = body.expiresInHours;
  if (rawHours !== undefined && rawHours !== null && rawHours !== '') {
    const hours = Number(rawHours);
    if (!Number.isFinite(hours) || hours <= 0) {
      sendError(ctx.res, 400, '有效期必须是大于 0 的小时数', 'INVALID_EXPIRES');
      return;
    }
    expiresAt = Date.now() + Math.min(hours, MAX_EXPIRE_HOURS) * 3600 * 1000;
  }

  let maxDownloads = 0;
  const rawMax = body.maxDownloads;
  if (rawMax !== undefined && rawMax !== null && rawMax !== '') {
    maxDownloads = Number(rawMax);
    if (!Number.isInteger(maxDownloads) || maxDownloads < 0) {
      sendError(ctx.res, 400, '最大下载次数必须是非负整数（0 表示不限制）', 'INVALID_MAX_DOWNLOADS');
      return;
    }
  }

  const token = crypto.randomBytes(16).toString('base64url');
  const row = db.createShare({
    token,
    path: resolved.virtual,
    name: path.basename(resolved.abs) || resolved.virtual,
    isDir: stat.isDirectory(),
    ownerId: ctx.user.id,
    ownerName: ctx.user.username,
    passwordHash,
    expiresAt,
    maxDownloads
  });
  audit(
    ctx,
    'SHARE_CREATE',
    `创建分享 ${resolved.virtual}（${token}，密码 ${passwordHash ? '有' : '无'}，有效期 ${
      expiresAt ? new Date(expiresAt).toLocaleString('zh-CN') : '永久'
    }，次数上限 ${maxDownloads > 0 ? maxDownloads : '不限'}）`
  );
  sendJson(ctx.res, 201, { ok: true, share: shareView(row) });
}

function handleShareDelete(ctx, id) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.FILE_SHARE, '删除分享链接')) {
    return;
  }
  const row = db.getShareById(id);
  if (!row) {
    sendError(ctx.res, 404, '分享链接不存在', 'SHARE_NOT_FOUND');
    return;
  }
  if (!isAdminUser(ctx.user) && row.owner_id !== ctx.user.id) {
    sendError(ctx.res, 403, '只能删除自己创建的分享链接', 'FORBIDDEN');
    return;
  }
  db.deleteShare(row.id);
  audit(ctx, 'SHARE_DELETE', `删除分享 ${row.path}（${row.token}）`);
  sendJson(ctx.res, 200, { ok: true });
}

/* ------------------------------ 公开接口 ------------------------------ */

/** GET /api/share/:token?pwd= 分享元信息 */
async function handleShareMeta(ctx, token) {
  const row = loadShare(ctx.res, token);
  if (!row) {
    return;
  }
  if (!(await passPasswordGate(ctx, row))) {
    return;
  }
  const target = await statShareTarget(row, ctx.res);
  if (!target) {
    return;
  }
  const isDir = target.stat.isDirectory();
  const size = isDir ? await pathSize(target.resolved.abs) : target.stat.size;
  sendJson(ctx.res, 200, {
    ok: true,
    name: row.name,
    isDir,
    size,
    needPassword: Boolean(row.password_hash),
    expired: false,
    remaining: row.max_downloads > 0 ? Math.max(row.max_downloads - row.downloads, 0) : null,
    expiresAt: row.expires_at,
    downloads: row.downloads,
    maxDownloads: row.max_downloads
  });
}

/** GET /s/:token/download 流式下载（支持 Range），目录分享返回 409 */
async function handleShareDownload(ctx, token) {
  const row = loadShare(ctx.res, token);
  if (!row) {
    return;
  }
  if (!(await passPasswordGate(ctx, row))) {
    return;
  }
  const target = await statShareTarget(row, ctx.res);
  if (!target) {
    return;
  }
  if (target.stat.isDirectory()) {
    sendError(ctx.res, 409, '这是目录分享，请使用打包下载（/s/<token>/zip）', 'SHARE_IS_DIRECTORY');
    return;
  }

  const fileName = path.basename(target.resolved.abs);
  const asciiFallback = fileName.replace(/[^\x20-\x7e]/g, '_').replace(/"/g, "'");
  ctx.res.on('finish', countDownload(ctx, row));
  sendFileWithRange(ctx, target.resolved.abs, target.stat, {
    'Content-Type': mimeOf(target.resolved.abs),
    'Content-Disposition': `attachment; filename="${asciiFallback}"; filename*=UTF-8''${encodeURIComponent(fileName)}`,
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff'
  });
}

/** GET /s/:token/zip 目录分享现场打包后流式返回 */
async function handleShareZip(ctx, token) {
  const row = loadShare(ctx.res, token);
  if (!row) {
    return;
  }
  if (!(await passPasswordGate(ctx, row))) {
    return;
  }
  const target = await statShareTarget(row, ctx.res);
  if (!target) {
    return;
  }
  if (!target.stat.isDirectory()) {
    sendError(ctx.res, 400, '该分享不是目录，请直接下载文件', 'SHARE_NOT_DIRECTORY');
    return;
  }

  ensureDir(DATA_DIR);
  ensureDir(SHARE_TMP_DIR);
  const tmpPath = path.join(SHARE_TMP_DIR, `${crypto.randomUUID()}.zip`);
  try {
    await createZip([{ absPath: target.resolved.abs, name: row.name }], tmpPath);
    const stat = await fs.promises.stat(tmpPath);
    const fileName = `${row.name}.zip`;
    const asciiFallback = fileName.replace(/[^\x20-\x7e]/g, '_').replace(/"/g, "'");
    const cleanup = () => {
      fs.promises.rm(tmpPath, { force: true }).catch((err) => {
        console.warn('[分享] 清理临时压缩包失败：', err.message);
      });
    };
    ctx.res.on('finish', countDownload(ctx, row));
    ctx.res.on('close', cleanup);
    sendFileWithRange(ctx, tmpPath, stat, {
      'Content-Type': 'application/zip',
      'Content-Disposition': `attachment; filename="${asciiFallback}"; filename*=UTF-8''${encodeURIComponent(fileName)}`,
      'Cache-Control': 'no-store',
      'X-Content-Type-Options': 'nosniff'
    });
  } catch (err) {
    console.error('[分享] 打包目录失败：', err && err.message ? err.message : err);
    await fs.promises.rm(tmpPath, { force: true }).catch((removeErr) => {
      console.warn('[分享] 清理失败的临时压缩包失败：', removeErr.message);
    });
    if (!ctx.res.headersSent) {
      sendError(ctx.res, 500, `打包目录失败：${err && err.message ? err.message : '未知错误'}`, 'SHARE_ZIP_ERROR');
    } else {
      ctx.res.destroy();
    }
  }
}

/* ------------------------------ 分享页面 ------------------------------ */

/** 分享页兜底 HTML（不依赖任何外部 CDN 与静态资源） */
function inlineSharePage(token) {
  return `<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>文件分享</title>
<style>
  * { box-sizing: border-box; }
  body {
    margin: 0;
    min-height: 100vh;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 24px;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", "PingFang SC", "Microsoft YaHei", sans-serif;
    background: radial-gradient(circle at top, #1f2937, #0b1120 70%);
    color: #e5e7eb;
  }
  .card {
    width: 100%;
    max-width: 420px;
    padding: 28px 24px;
    border-radius: 16px;
    background: rgba(17, 24, 39, 0.92);
    border: 1px solid rgba(148, 163, 184, 0.25);
    box-shadow: 0 18px 40px rgba(0, 0, 0, 0.45);
  }
  h1 { font-size: 20px; margin: 0 0 8px; word-break: break-all; }
  .meta { margin: 0 0 18px; font-size: 13px; color: #94a3b8; min-height: 18px; word-break: break-all; }
  .hidden { display: none; }
  label { display: block; font-size: 13px; color: #cbd5e1; margin-bottom: 6px; }
  input[type="password"] {
    width: 100%;
    padding: 10px 12px;
    border-radius: 10px;
    border: 1px solid rgba(148, 163, 184, 0.4);
    background: rgba(15, 23, 42, 0.9);
    color: #e5e7eb;
    font-size: 14px;
    outline: none;
  }
  input[type="password"]:focus { border-color: #38bdf8; }
  button, .btn {
    display: block;
    width: 100%;
    margin-top: 14px;
    padding: 11px 16px;
    border: none;
    border-radius: 10px;
    background: linear-gradient(135deg, #0ea5e9, #2563eb);
    color: #ffffff;
    font-size: 15px;
    font-weight: 600;
    text-align: center;
    text-decoration: none;
    cursor: pointer;
  }
  button:active, .btn:active { opacity: 0.85; }
  .error { margin: 12px 0 0; font-size: 13px; color: #f87171; min-height: 18px; white-space: pre-wrap; }
  .tips { margin: 18px 0 0; font-size: 12px; color: #64748b; line-height: 1.6; }
</style>
</head>
<body>
  <div class="card">
    <h1 id="title">文件分享</h1>
    <p class="meta" id="meta">正在加载分享信息…</p>
    <form id="pwdForm" class="hidden">
      <label for="pwd">提取密码</label>
      <input id="pwd" type="password" autocomplete="off" placeholder="请输入提取密码" />
      <button type="submit">确定</button>
    </form>
    <div id="actions" class="hidden">
      <a class="btn" id="downloadLink" href="#">下载</a>
    </div>
    <p class="error" id="error"></p>
    <p class="tips">本页面由飞牛终端文件服务生成，链接可能设有有效期与下载次数限制。</p>
  </div>
<script>
(function () {
  var token = ${JSON.stringify(token)};
  var pwd = '';
  var titleEl = document.getElementById('title');
  var metaEl = document.getElementById('meta');
  var pwdForm = document.getElementById('pwdForm');
  var pwdInput = document.getElementById('pwd');
  var actions = document.getElementById('actions');
  var link = document.getElementById('downloadLink');
  var errorEl = document.getElementById('error');

  function humanSize(value) {
    if (typeof value !== 'number' || !isFinite(value) || value < 0) { return ''; }
    var units = ['B', 'KB', 'MB', 'GB', 'TB'];
    var index = 0;
    var size = value;
    while (size >= 1024 && index < units.length - 1) { size = size / 1024; index += 1; }
    return (index === 0 ? String(size) : size.toFixed(1)) + ' ' + units[index];
  }

  function showError(text) { errorEl.textContent = text || ''; }

  function render(data) {
    titleEl.textContent = data.name || '文件分享';
    var parts = [data.isDir ? '文件夹' : '文件'];
    if (typeof data.size === 'number' && data.size > 0) { parts.push(humanSize(data.size)); }
    if (data.remaining !== null && data.remaining !== undefined) { parts.push('剩余下载次数：' + data.remaining); }
    if (data.expiresAt) { parts.push('过期时间：' + new Date(data.expiresAt).toLocaleString()); }
    metaEl.textContent = parts.join(' · ');
    pwdForm.classList.add('hidden');
    actions.classList.remove('hidden');
    var url = '/s/' + encodeURIComponent(token) + (data.isDir ? '/zip' : '/download');
    if (pwd) { url += '?pwd=' + encodeURIComponent(pwd); }
    link.href = url;
    link.textContent = data.isDir ? '打包下载（ZIP）' : '下载文件';
    showError('');
  }

  function load() {
    var url = '/api/share/' + encodeURIComponent(token);
    if (pwd) { url += '?pwd=' + encodeURIComponent(pwd); }
    fetch(url, { credentials: 'omit', cache: 'no-store' })
      .then(function (res) {
        return res.json().then(function (body) { return { status: res.status, body: body }; });
      })
      .then(function (result) {
        var body = result.body || {};
        if (result.status === 200 && body.ok) { render(body); return; }
        if (result.status === 401) {
          actions.classList.add('hidden');
          pwdForm.classList.remove('hidden');
          metaEl.textContent = '该分享已设置提取密码';
          showError(pwd ? (body.error || '提取密码不正确') : '请输入提取密码');
          return;
        }
        pwdForm.classList.add('hidden');
        actions.classList.add('hidden');
        metaEl.textContent = '';
        showError(body.error || '分享不可用');
      })
      .catch(function () { showError('网络请求失败，请稍后重试'); });
  }

  pwdForm.addEventListener('submit', function (event) {
    event.preventDefault();
    pwd = (pwdInput.value || '').trim();
    if (!pwd) { showError('请输入提取密码'); return; }
    load();
  });

  load();
})();
</script>
</body>
</html>`;
}

/** 输出分享页（优先使用 public/share.html，不存在时使用内联模板） */
async function serveSharePage(ctx, token) {
  const row = db.getShareByToken(token);
  if (!row) {
    sendError(ctx.res, 404, '分享链接不存在或已被取消', 'SHARE_NOT_FOUND');
    return;
  }
  if (row.expires_at && row.expires_at <= Date.now()) {
    sendError(ctx.res, 410, '分享链接已过期', 'SHARE_EXPIRED');
    return;
  }
  if (row.max_downloads > 0 && row.downloads >= row.max_downloads) {
    sendError(ctx.res, 410, '分享链接的下载次数已用完', 'SHARE_LIMIT_REACHED');
    return;
  }

  let html = null;
  try {
    const exists = await fs.promises
      .stat(SHARE_PAGE_FILE)
      .then((stat) => stat.isFile())
      .catch(() => false);
    if (exists) {
      html = await fs.promises.readFile(SHARE_PAGE_FILE, 'utf8');
    }
  } catch (err) {
    console.warn('[分享] 读取分享页模板失败，改用内联页面：', err.message);
  }
  if (!html) {
    html = inlineSharePage(token);
  }

  const body = Buffer.from(html, 'utf8');
  ctx.res.writeHead(200, {
    'Content-Type': 'text/html; charset=utf-8',
    'Content-Length': body.length,
    'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff'
  });
  ctx.res.end(body);
}

/* ------------------------------ 路由分发 ------------------------------ */

/** 登录态接口：/api/shares */
async function handle(ctx) {
  const { pathname, method } = ctx;

  if (pathname === '/api/shares' && method === 'GET') {
    handleShareList(ctx);
    return true;
  }
  if (pathname === '/api/shares' && method === 'POST') {
    await handleShareCreate(ctx);
    return true;
  }
  const match = pathname.match(/^\/api\/shares\/(\d+)$/);
  if (match && method === 'DELETE') {
    handleShareDelete(ctx, Number(match[1]));
    return true;
  }
  if (pathname === '/api/shares' || pathname.startsWith('/api/shares/')) {
    sendError(ctx.res, 404, '接口不存在', 'NOT_FOUND');
    return true;
  }
  return false;
}

/** 公开接口：/api/share/:token（无需登录） */
async function handlePublicApi(ctx) {
  const match = ctx.pathname.match(/^\/api\/share\/([A-Za-z0-9_-]{16,64})$/);
  if (!match) {
    sendError(ctx.res, 404, '接口不存在', 'NOT_FOUND');
    return true;
  }
  if (ctx.method !== 'GET') {
    sendError(ctx.res, 405, '该接口只支持 GET 请求', 'METHOD_NOT_ALLOWED');
    return true;
  }
  const token = normalizeToken(match[1]);
  if (!token) {
    sendError(ctx.res, 404, '分享链接不存在或已被取消', 'SHARE_NOT_FOUND');
    return true;
  }
  await handleShareMeta(ctx, token);
  return true;
}

/** 公开页面与下载：/s/:token、/s/:token/download、/s/:token/zip（无需登录） */
async function handleSharePage(ctx) {
  const match = ctx.pathname.match(/^\/s\/([A-Za-z0-9_-]{16,64})(\/download|\/zip)?\/?$/);
  if (!match) {
    sendError(ctx.res, 404, '页面不存在', 'NOT_FOUND');
    return true;
  }
  const token = normalizeToken(match[1]);
  if (!token) {
    sendError(ctx.res, 404, '分享链接不存在或已被取消', 'SHARE_NOT_FOUND');
    return true;
  }
  const suffix = match[2] || '';

  if (suffix === '/download') {
    if (ctx.method !== 'GET') {
      sendError(ctx.res, 405, '下载接口只支持 GET 请求', 'METHOD_NOT_ALLOWED');
      return true;
    }
    await handleShareDownload(ctx, token);
    return true;
  }
  if (suffix === '/zip') {
    if (ctx.method !== 'GET') {
      sendError(ctx.res, 405, '打包下载接口只支持 GET 请求', 'METHOD_NOT_ALLOWED');
      return true;
    }
    await handleShareZip(ctx, token);
    return true;
  }
  if (ctx.method !== 'GET' && ctx.method !== 'HEAD') {
    sendError(ctx.res, 405, '分享页面只支持 GET 请求', 'METHOD_NOT_ALLOWED');
    return true;
  }
  await serveSharePage(ctx, token);
  return true;
}

module.exports = { handle, handlePublicApi, handleSharePage };
