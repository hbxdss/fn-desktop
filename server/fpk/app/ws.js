'use strict';

/**
 * WebSocket 服务：/ws/docker/logs?token=...&id=...&tail=200
 * 用途：把 Docker 容器的实时日志转发给桌面客户端。
 * 鉴权：Token 通过查询参数传递（浏览器 WebSocket 无法自定义请求头），服务端仍只存哈希。
 * 实现：通过 docker CLI 子进程（docker logs -f）转发日志，参数以数组传递，不拼接 shell 字符串。
 */

const cp = require('child_process');
const { WebSocketServer } = require('ws');

const auth = require('./auth');
const db = require('./db');
const { DOCKER_BIN, runDocker } = require('./docker');
const { PERMS, clientIp } = require('./util');

const HEARTBEAT_INTERVAL_MS = 30000;
const CONTAINER_ID_PATTERN = /^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,127}$/;

function sendJson(ws, payload) {
  if (ws.readyState === ws.OPEN) {
    ws.send(JSON.stringify(payload));
  }
}

function rejectUpgrade(socket, status, message) {
  const body = Buffer.from(JSON.stringify({ ok: false, error: message }), 'utf8');
  socket.write(
    `HTTP/1.1 ${status} ${status === 401 ? 'Unauthorized' : 'Bad Request'}\r\n` +
      'Content-Type: application/json; charset=utf-8\r\n' +
      `Content-Length: ${body.length}\r\n` +
      'Connection: close\r\n\r\n'
  );
  socket.write(body);
  socket.destroy();
}

function attach(server) {
  const wss = new WebSocketServer({ noServer: true });

  server.on('upgrade', (req, socket, head) => {
    let parsed;
    try {
      parsed = new URL(req.url, `https://${req.headers.host || 'localhost'}`);
    } catch (err) {
      rejectUpgrade(socket, 400, 'WebSocket 地址不合法');
      return;
    }
    if (parsed.pathname !== '/ws/docker/logs') {
      rejectUpgrade(socket, 400, '未知的 WebSocket 路径');
      return;
    }
    const token = parsed.searchParams.get('token') || '';
    if (!/^[A-Fa-f0-9]{64}$/.test(token)) {
      rejectUpgrade(socket, 401, '缺少或格式错误的访问令牌');
      return;
    }
    const fakeReq = { headers: { authorization: `Bearer ${token}` } };
    const authResult = auth.authenticate(fakeReq);
    if (!authResult.user) {
      rejectUpgrade(socket, 401, authResult.error || '登录状态无效');
      return;
    }
    if (!auth.hasPermission(authResult.user, PERMS.DOCKER_VIEW)) {
      rejectUpgrade(socket, 401, '没有查看容器日志的权限');
      return;
    }
    const containerId = parsed.searchParams.get('id') || '';
    if (!CONTAINER_ID_PATTERN.test(containerId)) {
      rejectUpgrade(socket, 400, '容器 ID 格式不合法');
      return;
    }
    const tailRaw = Number(parsed.searchParams.get('tail') || '200');
    const tail = Number.isFinite(tailRaw) ? Math.min(Math.max(Math.trunc(tailRaw), 1), 2000) : 200;

    wss.handleUpgrade(req, socket, head, (ws) => {
      wss.emit('connection', ws, req, {
        user: authResult.user,
        containerId,
        tail
      });
    });
  });

  wss.on('connection', async (ws, req, meta) => {
    const user = meta.user;
    const containerId = meta.containerId;
    const tail = meta.tail;
    let proc = null;
    let closed = false;
    let alive = true;

    db.addAudit({
      userId: user.id,
      username: user.username,
      action: 'DOCKER_LOG_STREAM',
      detail: `订阅容器日志 ${containerId.slice(0, 12)}`,
      ip: clientIp(req)
    });

    const heartbeat = setInterval(() => {
      if (!alive) {
        ws.terminate();
        return;
      }
      alive = false;
      try {
        ws.ping();
      } catch (pingErr) {
        console.error('[WebSocket] 心跳发送失败：', pingErr.message);
      }
    }, HEARTBEAT_INTERVAL_MS);

    ws.on('pong', () => {
      alive = true;
    });

    const cleanup = () => {
      if (closed) {
        return;
      }
      closed = true;
      clearInterval(heartbeat);
      if (proc) {
        try {
          proc.kill();
        } catch (destroyErr) {
          console.error('[WebSocket] 关闭日志子进程失败：', destroyErr.message);
        }
        proc = null;
      }
    };

    ws.on('close', cleanup);
    ws.on('error', (err) => {
      console.error('[WebSocket] 连接错误：', err.message);
      cleanup();
    });

    const fail = (message) => {
      sendJson(ws, { type: 'error', message });
      cleanup();
      if (ws.readyState === ws.OPEN) {
        ws.close(1011, '启动日志流失败');
      }
    };

    try {
      // 先确认容器存在并读取是否为 TTY 模式（同时完成参数校验）
      const { stdout: ttyOut } = await runDocker(
        ['inspect', '--type', 'container', '--format', '{{json .Config.Tty}}', containerId],
        { timeout: 15000 }
      );
      const isTty = /^true$/i.test(String(ttyOut || '').trim());
      if (closed || ws.readyState !== ws.OPEN) {
        return;
      }

      // 以数组传参启动 docker logs -f，绝不拼接 shell 字符串
      proc = cp.spawn(DOCKER_BIN, ['logs', '--follow', '--tail', String(tail), containerId], {
        windowsHide: true,
        stdio: ['ignore', 'pipe', 'pipe']
      });

      sendJson(ws, {
        type: 'start',
        containerId,
        tty: isTty,
        time: Date.now()
      });

      proc.stdout.on('data', (chunk) => {
        sendJson(ws, { type: 'data', stream: 'stdout', data: chunk.toString('utf8') });
      });

      proc.stderr.on('data', (chunk) => {
        const text = chunk.toString('utf8');
        // docker 守护进程不可用等错误信息会出现在 stderr，直接透传并标记 stream
        sendJson(ws, { type: 'data', stream: 'stderr', data: text });
      });

      proc.on('error', (err) => {
        console.error('[WebSocket] 启动日志子进程失败：', err.message);
        let message = `启动容器日志流失败：${err.message || '未知错误'}`;
        if (err && err.code === 'ENOENT') {
          message = '未检测到 Docker 服务，请确认已安装 Docker 并加入 PATH';
        }
        fail(message);
      });

      proc.on('exit', (code, signal) => {
        if (closed) {
          return;
        }
        if (code !== 0 && code !== null) {
          sendJson(ws, { type: 'error', message: `日志流已中断（退出码 ${code}${signal ? '，信号 ' + signal : ''}）` });
          cleanup();
          if (ws.readyState === ws.OPEN) {
            ws.close(1011, '日志流中断');
          }
          return;
        }
        sendJson(ws, { type: 'end', message: '容器日志流已结束' });
        cleanup();
        if (ws.readyState === ws.OPEN) {
          ws.close(1000, '日志流结束');
        }
      });
    } catch (err) {
      const status = err && err.status;
      let message = '启动容器日志流失败';
      if (status === 404) {
        message = '容器不存在或已被删除';
      } else if (status === 503) {
        message = '未检测到 Docker 服务，请确认 Docker 已启动';
      } else {
        message = `启动容器日志流失败：${err.message || '未知错误'}`;
      }
      fail(message);
    }
  });

  wss.on('error', (err) => {
    console.error('[WebSocket] 服务端错误：', err.message);
  });

  return wss;
}

module.exports = { attach };
