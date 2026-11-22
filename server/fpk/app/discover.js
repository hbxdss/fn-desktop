'use strict';

/**
 * 应用发现与存储卷信息。
 *
 * - GET /api/apps            发现本机可用的应用：
 *     · 飞牛官方应用（影视 / 相册 / 音乐）：通过反向代理探测是否已安装
 *     · Docker 容器：docker ps 里有发布端口的容器
 * - GET /api/system/storage  飞牛存储卷（/vol1、/vol2 …）容量信息
 */

const fs = require('fs');
const http = require('http');
const https = require('https');

const auth = require('./auth');
const docker = require('./docker');
const { sendJson } = require('./util');

/** 飞牛官方应用市场里与「应用」页相关的三个应用（网页端口 + 路径前缀）。 */
const OFFICIAL_APPS = [
  {
    id: 'media',
    name: '飞牛影视',
    path: '/v',
    description: '影视库刮削与播放',
    downloadHint: '在飞牛「应用中心」搜索「飞牛影视」安装后再回到这里'
  },
  {
    id: 'photos',
    name: '飞牛相册',
    path: '/p',
    description: '照片备份、浏览与共享',
    downloadHint: '在飞牛「应用中心」搜索「飞牛相册」安装后再回到这里'
  },
  {
    id: 'music',
    name: '飞牛音乐',
    path: '/music',
    description: '音乐库与在线播放',
    downloadHint: '在飞牛「应用中心」搜索「飞牛音乐」安装后再回到这里'
  }
];

const PROBE_TIMEOUT_MS = 2500;

function probeStatus(url) {
  return new Promise((resolve) => {
    const lib = url.startsWith('https') ? https : http;
    let settled = false;
    const done = (value) => {
      if (settled) return;
      settled = true;
      resolve(value);
    };
    try {
      const req = lib.get(
        url,
        {
          rejectUnauthorized: false,
          timeout: PROBE_TIMEOUT_MS,
          headers: { 'User-Agent': 'fn-desktop-plus', Accept: 'text/html' }
        },
        (res) => {
          res.resume();
          done(Number(res.statusCode) || 0);
        }
      );
      req.on('timeout', () => {
        req.destroy();
        done(0);
      });
      req.on('error', () => done(0));
    } catch (err) {
      done(0);
    }
  });
}

/**
 * 判断某个应用是否已安装。
 * 飞牛网页端对未安装的应用统一返回 502（反代在后面没有目标），
 * 因此「有有效响应且不是 502」即视为已安装。
 */
async function isInstalled(appPath) {
  const codes = [
    await probeStatus(`https://127.0.0.1:5667${appPath}`),
    await probeStatus(`http://127.0.0.1:5666${appPath}`)
  ];
  return codes.some((code) => code >= 200 && code < 500 && code !== 502);
}

async function discoverOfficial() {
  const items = await Promise.all(
    OFFICIAL_APPS.map(async (app) => {
      const installed = await isInstalled(app.path);
      return {
        id: app.id,
        name: app.name,
        path: app.path,
        description: app.description,
        downloadHint: app.downloadHint,
        installed
      };
    })
  );
  return items;
}

/** 解析 docker ps 的 Ports 文本，取出宿主机发布的 TCP 端口。 */
function parsePublishedPorts(text) {
  const ports = new Set();
  String(text || '')
    .split(',')
    .forEach((chunk) => {
      const match = chunk.trim().match(/^(?:[\d.]+|\[[0-9a-fA-F:]+\]):(\d+)->/);
      if (match) {
        const port = Number(match[1]);
        if (Number.isInteger(port) && port > 0) {
          ports.add(port);
        }
      }
    });
  return Array.from(ports).sort((a, b) => a - b);
}

async function listDockerApps() {
  let stdout = '';
  try {
    const result = await docker.runDocker(['ps', '--format', '{{json .}}'], { timeout: 8000 });
    stdout = String(result.stdout || '');
  } catch (err) {
    // Docker 未安装 / 未运行时不打扰用户，应用页只显示官方应用
    return [];
  }

  const items = [];
  stdout
    .split('\n')
    .map((line) => line.trim())
    .filter(Boolean)
    .forEach((line) => {
      let row;
      try {
        row = JSON.parse(line);
      } catch (err) {
        return;
      }
      const ports = parsePublishedPorts(row.Ports);
      if (ports.length === 0) {
        return;
      }
      const rawName = String(row.Names || row.ID || '').trim();
      const name = rawName.replace(/^\//, '').split(',')[0];
      items.push({
        container: name,
        name,
        image: String(row.Image || ''),
        status: String(row.Status || ''),
        ports,
        portText: String(row.Ports || '')
      });
    });
  return items;
}

async function handleApps(req, res) {
  const user = auth.requireAuth(req, res);
  if (!user) {
    return;
  }
  const [official, containers] = await Promise.all([discoverOfficial(), listDockerApps()]);
  sendJson(res, 200, { ok: true, official, docker: containers });
}

/* ------------------------------ 存储卷 ------------------------------ */

/** 从 /proc/mounts 里挑出飞牛存储卷（/vol1、/vol2 …，跳过 /vol00 系统盘）。 */
async function listVolumeMounts() {
  let text = '';
  try {
    text = await fs.promises.readFile('/proc/mounts', 'utf8');
  } catch (err) {
    return [];
  }
  const seen = new Set();
  const mounts = [];
  text.split('\n').forEach((line) => {
    const parts = line.trim().split(/\s+/);
    if (parts.length < 3) {
      return;
    }
    const mountPoint = parts[1];
    const match = mountPoint.match(/^\/vol(\d+)$/);
    if (!match) {
      return;
    }
    if (match[1] === '00' || seen.has(mountPoint)) {
      return;
    }
    seen.add(mountPoint);
    mounts.push({ mount: mountPoint, device: parts[0], fsType: parts[2], index: Number(match[1]) });
  });
  mounts.sort((a, b) => a.index - b.index);
  return mounts;
}

async function readVolume(mountPoint) {
  try {
    const stats = await fs.promises.statfs(mountPoint);
    const blockSize = Number(stats.bsize);
    const total = Number(stats.blocks) * blockSize;
    const free = Number(stats.bfree) * blockSize;
    const available = Number(stats.bavail) * blockSize;
    const used = total - free;
    return {
      total,
      used,
      free: available,
      usagePercent: total > 0 ? Math.round((used / total) * 1000) / 10 : 0
    };
  } catch (err) {
    return { total: 0, used: 0, free: 0, usagePercent: 0, error: err.message };
  }
}

async function handleStorage(req, res) {
  const user = auth.requireAuth(req, res);
  if (!user) {
    return;
  }
  const mounts = await listVolumeMounts();
  const items = await Promise.all(
    mounts.map(async (mount) => {
      const usage = await readVolume(mount.mount);
      return {
        mount: mount.mount,
        device: mount.device,
        fsType: mount.fsType,
        name: `存储空间${mount.index}`,
        ...usage
      };
    })
  );
  const usable = items.filter((item) => item.total > 0);
  const total = usable.reduce((sum, item) => sum + item.total, 0);
  const used = usable.reduce((sum, item) => sum + item.used, 0);
  sendJson(res, 200, {
    ok: true,
    source: 'fnos-volumes',
    items: usable,
    total,
    used,
    usagePercent: total > 0 ? Math.round((used / total) * 1000) / 10 : 0
  });
}

module.exports = { handleApps, handleStorage, OFFICIAL_APPS };
