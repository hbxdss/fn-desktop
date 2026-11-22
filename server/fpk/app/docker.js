'use strict';

/**
 * Docker 管理路由：/api/docker
 *
 * 设计说明：
 * - 一律通过 docker CLI（child_process.execFile 数组参数，绝不拼接 shell 字符串）执行，避免命令注入；
 * - docker 不可用时统一返回 503 与中文提示「未检测到 Docker 服务」，不影响文件/用户功能；
 * - 所有 name / id 先做正则校验，不合法直接 400；
 * - 所有写操作写入审计（db.addAudit），动作名如 DOCKER_CONTAINER_START；
 * - docker 输出优先使用 --format '{{json .}}' 逐行解析，解析失败走中文兜底，不让整个请求 500。
 */

const cp = require('child_process');

const auth = require('./auth');
const db = require('./db');
const { PERMS, sendJson, sendError, readBody, clientIp } = require('./util');

/** docker 可执行文件名，可用环境变量覆盖（便于测试或自定义安装路径） */
const DOCKER_BIN = process.env.DOCKER_BIN || 'docker';

/** 普通命令超时（毫秒） */
const COMMAND_TIMEOUT_MS = 20000;
/** 拉取镜像超时（毫秒） */
const PULL_TIMEOUT_MS = 300000;
/** 单次命令输出上限（字节），防止 docker 输出过大撑爆内存 */
const MAX_BUFFER_BYTES = 32 * 1024 * 1024;

/** 容器/镜像/卷/网络名称通用校验：首字符字母数字，后续允许字母数字、下划线、点、短横线 */
const NAME_PATTERN = /^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,127}$/;
/** 容器 ID：12 位或 64 位十六进制 */
const HEX_ID_PATTERN = /^(?:[A-Fa-f0-9]{12}|[A-Fa-f0-9]{64})$/;
/** 镜像引用：允许 registry:port/namespace/name:tag@digest 形式 */
const IMAGE_REF_PATTERN = /^[a-zA-Z0-9][a-zA-Z0-9_.\-/:@]{0,254}$/;
/** 镜像 ID：允许可选 sha256: 前缀的 12/64 位十六进制 */
const IMAGE_ID_PATTERN = /^(?:sha256:)?[A-Fa-f0-9]{12}(?:[A-Fa-f0-9]{52})?$/;
/** 环境变量键名 */
const ENV_KEY_PATTERN = /^[A-Za-z_][A-Za-z0-9_]*$/;

const RESTART_POLICIES = new Set(['no', 'always', 'unless-stopped', 'on-failure']);
const PORT_PROTOCOLS = new Set(['tcp', 'udp', 'sctp']);
/** Docker 内置网络驱动白名单（第三方驱动插件不在本接口支持范围） */
const NETWORK_DRIVERS = new Set(['bridge', 'host', 'none', 'overlay', 'macvlan', 'ipvlan']);

/** 同一镜像的并发拉取去重：image -> Promise，防止重复拉取浪费带宽 */
const pullInFlight = new Map();

/* ------------------------------ 错误类型 ------------------------------ */

/** 带 HTTP 状态码的业务错误，便于统一转成中文响应 */
class DockerError extends Error {
  constructor(status, message, code) {
    super(message);
    this.name = 'DockerError';
    this.status = status;
    this.code = code || 'DOCKER_ERROR';
  }
}

function badRequest(message, code) {
  return new DockerError(400, message, code || 'INVALID_PARAM');
}

function notFound(message, code) {
  return new DockerError(404, message, code || 'DOCKER_NOT_FOUND');
}

function conflict(message, code) {
  return new DockerError(409, message, code || 'DOCKER_CONFLICT');
}

/* ------------------------------ CLI 执行 ------------------------------ */

/**
 * 执行 docker 命令。
 * @param {string[]} args 参数数组（不含 docker 本身）
 * @param {{timeout?:number}} options 可选项：超时毫秒数
 * @returns {Promise<{stdout:string, stderr:string}>}
 */
function runDocker(args, options) {
  const opts = options || {};
  const timeout = Number.isFinite(opts.timeout) && opts.timeout > 0 ? opts.timeout : COMMAND_TIMEOUT_MS;
  const printable = ['docker', ...args].join(' ');
  return new Promise((resolve, reject) => {
    cp.execFile(
      DOCKER_BIN,
      args,
      {
        timeout,
        maxBuffer: MAX_BUFFER_BYTES,
        windowsHide: true,
        encoding: 'utf8',
        env: process.env
      },
      (err, stdout, stderr) => {
        if (!err) {
          resolve({ stdout: stdout || '', stderr: stderr || '' });
          return;
        }
        const stderrText = typeof stderr === 'string' ? stderr : '';
        const stdoutText = typeof stdout === 'string' ? stdout : '';
        const combined = `${stderrText}\n${stdoutText}`.trim();
        console.error(`[Docker] 命令失败：${printable}（${err.code || err.message}）${combined ? '：' + combined.slice(0, 500) : ''}`);

        if (err.killed || err.signal === 'SIGTERM' || /ETIMEDOUT/.test(String(err.code))) {
          reject(new DockerError(504, `Docker 操作超时（${Math.round(timeout / 1000)} 秒），请稍后重试`, 'DOCKER_TIMEOUT'));
          return;
        }
        if (err.code === 'ENOENT') {
          reject(new DockerError(503, '未检测到 Docker 服务，请确认已安装 Docker 并加入 PATH', 'DOCKER_UNAVAILABLE'));
          return;
        }
        if (/ECONNREFUSED|EPIPE|socket hang up/i.test(combined) || /Cannot connect to the Docker daemon|Is the docker daemon running/i.test(combined)) {
          reject(new DockerError(503, '未检测到 Docker 服务，Docker 守护进程可能未启动', 'DOCKER_UNAVAILABLE'));
          return;
        }
        if (err.code === 'ERR_CHILD_PROCESS_STDIO_MAXBUFFER') {
          reject(new DockerError(500, 'Docker 输出内容过大，无法完整读取', 'DOCKER_OUTPUT_TOO_LARGE'));
          return;
        }
        if (/No such container/i.test(combined)) {
          reject(notFound('指定的容器不存在或已被删除', 'CONTAINER_NOT_FOUND'));
          return;
        }
        if (/No such image/i.test(combined)) {
          reject(notFound('指定的镜像不存在或已被删除', 'IMAGE_NOT_FOUND'));
          return;
        }
        if (/No such volume/i.test(combined)) {
          reject(notFound('指定的数据卷不存在', 'VOLUME_NOT_FOUND'));
          return;
        }
        if (/No such network/i.test(combined)) {
          reject(notFound('指定的网络不存在', 'NETWORK_NOT_FOUND'));
          return;
        }
        if (/already in use|is in use|being used by/i.test(combined)) {
          reject(conflict('资源正在被使用，请先解除占用后重试', 'DOCKER_IN_USE'));
          return;
        }
        if (/already exists/i.test(combined)) {
          reject(conflict('同名资源已存在，请更换名称后重试', 'DOCKER_ALREADY_EXISTS'));
          return;
        }
        // 删除仍被使用的镜像/容器/网络时 Docker 返回 conflict，归入 409
        if (/conflict:|must be forced|cannot be forced|is being used by/i.test(combined)) {
          reject(conflict('资源仍在使用中，请先解除占用（或改用强制删除）后重试', 'DOCKER_CONFLICT'));
          return;
        }
        if (/is already running/i.test(combined)) {
          reject(conflict('容器已处于运行状态，无需重复启动', 'CONTAINER_ALREADY_RUNNING'));
          return;
        }
        const detail = combined.split('\n').filter((line) => line.trim().length > 0).pop() || err.message || '未知错误';
        reject(new DockerError(500, `Docker 操作失败：${detail.slice(0, 300)}`, 'DOCKER_ERROR'));
      }
    );
  });
}

/** 统一异常响应：业务错误按状态码返回，其他错误统一 500 中文兜底 */
function failUnexpected(res, err, label) {
  if (err instanceof DockerError) {
    sendError(res, err.status, err.message, err.code);
    return;
  }
  const statusCode = err && err.statusCode;
  if (statusCode === 400) {
    sendError(res, 400, err.message || '请求参数不合法', 'INVALID_BODY');
    return;
  }
  if (statusCode === 413) {
    sendError(res, 413, err.message || '请求体超过大小限制', 'BODY_TOO_LARGE');
    return;
  }
  console.error(`[Docker] ${label}失败：`, err && err.message ? err.message : err);
  sendError(res, 500, `Docker 操作失败：${(err && err.message) || '未知错误'}`, 'DOCKER_ERROR');
}

/* ------------------------------ 校验工具 ------------------------------ */

function isValidName(value) {
  return typeof value === 'string' && NAME_PATTERN.test(value);
}

/** 容器标识：名称或 12/64 位十六进制 ID */
function isValidContainerId(value) {
  return typeof value === 'string' && (NAME_PATTERN.test(value) || HEX_ID_PATTERN.test(value));
}

function isValidImageId(value) {
  return typeof value === 'string' && IMAGE_ID_PATTERN.test(value);
}

function isValidImageRef(value) {
  if (typeof value !== 'string' || !IMAGE_REF_PATTERN.test(value)) {
    return false;
  }
  // 禁止 .. 与连续斜杠，避免出现异常的镜像引用
  if (value.includes('..') || value.includes('//')) {
    return false;
  }
  return true;
}

/** 容器内绝对路径校验：必须以 / 开头，且不含 .. 片段 */
function isValidContainerPath(value) {
  if (typeof value !== 'string' || !value.startsWith('/') || value.length > 4096) {
    return false;
  }
  // eslint-disable-next-line no-control-regex
  if (/[\u0000-\u001f]/.test(value)) {
    return false;
  }
  return !value.split('/').some((seg) => seg === '..');
}

/** 宿主绝对路径校验：必须以 / 开头（Linux 宿主），且不含 .. 片段 */
function isValidHostPath(value) {
  if (typeof value !== 'string' || !value.startsWith('/') || value.length > 4096) {
    return false;
  }
  // eslint-disable-next-line no-control-regex
  if (/[\u0000-\u001f]/.test(value)) {
    return false;
  }
  return !value.split('/').some((seg) => seg === '..');
}

function toPositiveInt(value, name, min, max) {
  const num = Number(value);
  if (!Number.isInteger(num) || num < min || num > max) {
    throw badRequest(`${name}必须是 ${min}~${max} 之间的整数`, 'INVALID_PARAM');
  }
  return num;
}

/** 读取可选 JSON 请求体：空体返回 {}，非法 JSON 返回 400 */
async function readOptionalJson(req, limit) {
  const raw = await readBody(req, limit || 256 * 1024);
  if (raw.length === 0) {
    return {};
  }
  let data;
  try {
    data = JSON.parse(raw.toString('utf8'));
  } catch (parseErr) {
    throw badRequest('请求体不是合法的 JSON', 'INVALID_BODY');
  }
  if (data === null || typeof data !== 'object' || Array.isArray(data)) {
    throw badRequest('请求体必须是 JSON 对象', 'INVALID_BODY');
  }
  return data;
}

/* ------------------------------ 输出解析 ------------------------------ */

/** 把 --format '{{json .}}' 的多行输出解析成对象数组，非法行跳过并计数 */
function parseJsonLines(text) {
  const items = [];
  let skipped = 0;
  for (const line of String(text || '').split(/\r?\n/)) {
    const trimmed = line.trim();
    if (!trimmed) {
      continue;
    }
    try {
      const obj = JSON.parse(trimmed);
      if (obj && typeof obj === 'object') {
        items.push(obj);
      } else {
        skipped += 1;
      }
    } catch (parseErr) {
      skipped += 1;
    }
  }
  if (skipped > 0) {
    console.warn(`[Docker] 有 ${skipped} 行输出无法解析为 JSON，已跳过`);
  }
  return items;
}

/** 解析人类可读容量（如 142MB / 1.5GiB），返回字节数；无法解析返回 0 */
function parseHumanSize(text) {
  if (typeof text === 'number') {
    return Number.isFinite(text) ? Math.max(0, Math.round(text)) : 0;
  }
  // 去掉 "2.3GB (25%)" 这类附带百分比的后缀
  const cleaned = String(text || '')
    .replace(/\([^)]*\)/g, '')
    .trim();
  const match = cleaned.match(/^([0-9]+(?:\.[0-9]+)?)\s*([kKmMgGtTpP]?i?[bB]?)$/);
  if (!match) {
    return 0;
  }
  const value = Number(match[1]);
  const unit = match[2].toLowerCase();
  const factors = {
    '': 1,
    b: 1,
    k: 1000,
    kb: 1000,
    kib: 1024,
    m: 1000 * 1000,
    mb: 1000 * 1000,
    mib: 1024 * 1024,
    g: 1000 * 1000 * 1000,
    gb: 1000 * 1000 * 1000,
    gib: 1024 * 1024 * 1024,
    t: 1000 * 1000 * 1000 * 1000,
    tb: 1000 * 1000 * 1000 * 1000,
    tib: 1024 * 1024 * 1024 * 1024,
    p: Math.pow(1000, 5),
    pb: Math.pow(1000, 5),
    pib: Math.pow(1024, 5)
  };
  const factor = factors[unit];
  if (!factor) {
    return 0;
  }
  return Math.round(value * factor);
}

/** 解析 "1.5MiB / 2GiB" 形式的双值输出 */
function parseSizePair(text) {
  const parts = String(text || '').split('/');
  return {
    used: parseHumanSize(parts[0]),
    total: parts.length > 1 ? parseHumanSize(parts[1]) : 0
  };
}

/** 解析百分比字符串（如 "12.34%"） */
function parsePercent(text) {
  const match = String(text || '').match(/(-?[0-9]+(?:\.[0-9]+)?)\s*%/);
  if (!match) {
    return 0;
  }
  const value = Number(match[1]);
  return Number.isFinite(value) ? Math.round(value * 100) / 100 : 0;
}

/* ------------------------------ 审计 ------------------------------ */

function audit(ctx, action, detail) {
  try {
    db.addAudit({
      userId: ctx.user.id,
      username: ctx.user.username,
      action,
      detail,
      ip: clientIp(ctx.req)
    });
  } catch (auditErr) {
    // 审计失败不影响主流程，但必须留下日志
    console.error('[Docker] 写入审计日志失败：', auditErr.message);
  }
}

function shortId(id) {
  return String(id || '').replace(/^sha256:/, '').slice(0, 12);
}

/* ------------------------------ 容器详情 ------------------------------ */

/** 读取容器的完整 inspect 数据 */
async function inspectContainer(containerId) {
  const { stdout } = await runDocker(['inspect', '--type', 'container', containerId], { timeout: COMMAND_TIMEOUT_MS });
  let list;
  try {
    list = JSON.parse(stdout);
  } catch (parseErr) {
    throw new DockerError(500, 'Docker 返回的容器信息无法解析', 'DOCKER_PARSE_ERROR');
  }
  if (!Array.isArray(list) || list.length === 0 || !list[0] || typeof list[0] !== 'object') {
    throw notFound('指定的容器不存在或已被删除', 'CONTAINER_NOT_FOUND');
  }
  return list[0];
}

/** 把 inspect 结果裁剪成前端需要的精简结构 */
function normalizeInspect(info) {
  const config = info.Config || {};
  const state = info.State || {};
  const netInfo = info.NetworkSettings || {};
  const hostConfig = info.HostConfig || {};

  const envList = Array.isArray(config.Env) ? config.Env.map((item) => String(item)) : [];
  const cmd = Array.isArray(config.Cmd) ? config.Cmd.map((item) => String(item)) : [];
  const entrypoint = Array.isArray(config.Entrypoint) ? config.Entrypoint.map((item) => String(item)) : [];

  const ports = [];
  const portBindings = hostConfig.PortBindings || {};
  for (const key of Object.keys(portBindings)) {
    const slash = key.lastIndexOf('/');
    const containerPort = Number(key.slice(0, slash)) || null;
    const protocol = slash >= 0 ? key.slice(slash + 1) : 'tcp';
    const bindings = Array.isArray(portBindings[key]) ? portBindings[key] : [];
    if (bindings.length === 0) {
      ports.push({ host: null, container: containerPort, protocol, hostIp: '' });
      continue;
    }
    for (const binding of bindings) {
      ports.push({
        host: binding && binding.HostPort ? Number(binding.HostPort) || null : null,
        container: containerPort,
        protocol,
        hostIp: (binding && binding.HostIp) || ''
      });
    }
  }
  // 只有 EXPOSE 没有映射时再补充一遍
  const exposed = (config.ExposedPorts && typeof config.ExposedPorts === 'object') ? Object.keys(config.ExposedPorts) : [];
  for (const key of exposed) {
    const slash = key.lastIndexOf('/');
    const containerPort = Number(key.slice(0, slash)) || null;
    const protocol = slash >= 0 ? key.slice(slash + 1) : 'tcp';
    if (!ports.some((port) => port.container === containerPort && port.protocol === protocol)) {
      ports.push({ host: null, container: containerPort, protocol, hostIp: '' });
    }
  }

  const mounts = Array.isArray(info.Mounts)
    ? info.Mounts.map((mount) => ({
        type: String(mount.Type || ''),
        source: String(mount.Source || mount.Name || ''),
        destination: String(mount.Destination || ''),
        ro: Boolean(mount.RW === false)
      }))
    : [];

  const networks = Object.keys(netInfo.Networks || {});
  const restartPolicy = hostConfig.RestartPolicy && hostConfig.RestartPolicy.Name ? hostConfig.RestartPolicy.Name : 'no';

  return {
    id: String(info.Id || ''),
    shortId: shortId(info.Id),
    name: String(info.Name || '').replace(/^\//, ''),
    image: String(config.Image || ''),
    imageId: String(info.Image || ''),
    state: String(state.Status || ''),
    status: String(state.Status || ''),
    running: Boolean(state.Running),
    paused: Boolean(state.Paused),
    exitCode: Number.isInteger(state.ExitCode) ? state.ExitCode : 0,
    startedAt: String(state.StartedAt || ''),
    finishedAt: String(state.FinishedAt || ''),
    created: String(info.Created || ''),
    createdAt: String(info.Created || ''),
    cmd,
    entrypoint,
    env: envList,
    ports,
    mounts,
    networks,
    restartPolicy,
    labels: config.Labels || {},
    hostConfig: {
      memory: Number(hostConfig.Memory) || 0,
      cpuShares: Number(hostConfig.CpuShares) || 0,
      nanoCpus: Number(hostConfig.NanoCpus) || 0,
      autoRemove: Boolean(hostConfig.AutoRemove),
      networkMode: String(hostConfig.NetworkMode || '')
    },
    ipAddress: String((netInfo.Networks && Object.values(netInfo.Networks)[0] && Object.values(netInfo.Networks)[0].IPAddress) || '')
  };
}

/* ------------------------------ 数据获取 ------------------------------ */

/**
 * 获取 docker info（JSON 格式）。
 * 返回 { version, containers, containersRunning, containersPaused, containersStopped, images, driver, memTotal, os, arch, ncpu }
 */
async function fetchInfo() {
  const { stdout } = await runDocker(['info', '--format', '{{json .}}'], { timeout: COMMAND_TIMEOUT_MS });
  let data = {};
  try {
    data = JSON.parse(stdout.trim().split(/\r?\n/)[0] || '{}');
  } catch (parseErr) {
    console.warn('[Docker] docker info 输出解析失败，返回空信息：', parseErr.message);
    data = {};
  }
  const pick = (camel, upper, fallback) => {
    if (data[camel] !== undefined && data[camel] !== null) {
      return data[camel];
    }
    if (data[upper] !== undefined && data[upper] !== null) {
      return data[upper];
    }
    return fallback;
  };
  return {
    version: String(pick('ServerVersion', 'ServerVersion', '未知版本') || '未知版本'),
    containers: Number(pick('Containers', 'Containers', 0)) || 0,
    containersRunning: Number(pick('ContainersRunning', 'ContainersRunning', 0)) || 0,
    containersPaused: Number(pick('ContainersPaused', 'ContainersPaused', 0)) || 0,
    containersStopped: Number(pick('ContainersStopped', 'ContainersStopped', 0)) || 0,
    images: Number(pick('Images', 'Images', 0)) || 0,
    driver: String(pick('Driver', 'Driver', '未知驱动') || '未知驱动'),
    memTotal: Number(pick('MemTotal', 'MemTotal', 0)) || 0,
    os: String(pick('OperatingSystem', 'OperatingSystem', '未知系统') || '未知系统'),
    arch: String(pick('Architecture', 'Architecture', '未知架构') || '未知架构'),
    ncpu: Number(pick('NCPU', 'NCPU', 0)) || 0
  };
}

/** 从 docker ps 的 Status 文本推断容器状态 */
function deriveState(rawState, status) {
  if (typeof rawState === 'string' && rawState.trim()) {
    return rawState.trim().toLowerCase();
  }
  const text = String(status || '').trim();
  if (!text) {
    return 'unknown';
  }
  if (/^Up\b/i.test(text)) {
    return /\(Paused\)/i.test(text) ? 'paused' : 'running';
  }
  if (/^Restarting\b/i.test(text)) {
    return 'restarting';
  }
  if (/^Exited\b/i.test(text)) {
    return 'exited';
  }
  if (/^Created\b/i.test(text)) {
    return 'created';
  }
  if (/^Dead\b/i.test(text)) {
    return 'dead';
  }
  if (/^Removal In Progress\b/i.test(text)) {
    return 'removing';
  }
  return text.toLowerCase();
}

/**
 * 解析 docker ps 输出的 Ports 文本。
 * 兼容 "0.0.0.0:8080->80/tcp"、"[::]:8080->80/tcp"、"80/tcp"、"8080-8081->80-81/tcp"。
 */
function parsePortsText(text) {
  const raw = String(text || '').trim();
  if (!raw) {
    return [];
  }
  const ports = [];
  for (const segment of raw.split(',')) {
    const part = segment.trim();
    if (!part) {
      continue;
    }
    const mapping = part.match(/^(?:\[?([^\]]*)\]?:)?([0-9]+(?:-[0-9]+)?)->([0-9]+(?:-[0-9]+)?)\/([a-z]+)$/i);
    if (mapping) {
      ports.push({
        host: Number(mapping[2].split('-')[0]) || null,
        container: Number(mapping[3].split('-')[0]) || null,
        protocol: String(mapping[4] || 'tcp').toLowerCase(),
        hostIp: mapping[1] || '',
        hostRange: mapping[2],
        containerRange: mapping[3]
      });
      continue;
    }
    const exposed = part.match(/^([0-9]+(?:-[0-9]+)?)\/([a-z]+)$/i);
    if (exposed) {
      ports.push({
        host: null,
        container: Number(exposed[1].split('-')[0]) || null,
        protocol: String(exposed[2] || 'tcp').toLowerCase(),
        hostIp: '',
        hostRange: '',
        containerRange: exposed[1]
      });
    }
  }
  return ports;
}

/** 解析 ps 输出的 Labels 文本（"a=b,c=d"） */
function parseLabelsText(text) {
  const labels = {};
  const raw = String(text || '').trim();
  if (!raw) {
    return labels;
  }
  for (const pair of raw.split(',')) {
    const index = pair.indexOf('=');
    if (index <= 0) {
      continue;
    }
    labels[pair.slice(0, index).trim()] = pair.slice(index + 1).trim();
  }
  return labels;
}

/** 统一容器条目结构（同时保留旧字段，兼容老前端） */
function normalizeContainerRow(raw) {
  const id = String(raw.ID || raw.Id || '');
  const name = String(raw.Names || '').split(',')[0].replace(/^\//, '') || shortId(id);
  const state = deriveState(raw.State, raw.Status);
  const ports = parsePortsText(raw.Ports);
  return {
    id,
    shortId: shortId(id),
    name,
    names: String(raw.Names || '')
      .split(',')
      .map((item) => item.trim().replace(/^\//, ''))
      .filter(Boolean),
    image: String(raw.Image || ''),
    imageId: String(raw.ImageID || raw.ImageDigest || ''),
    command: String(raw.Command || ''),
    state,
    status: String(raw.Status || ''),
    created: String(raw.CreatedAt || ''),
    createdAt: String(raw.CreatedAt || ''),
    runningFor: String(raw.RunningFor || ''),
    ports,
    portText: ports
      .map((port) => (port.host ? `${port.host}->${port.container}/${port.protocol}` : `${port.container}/${port.protocol}`))
      .join(', '),
    labels: parseLabelsText(raw.Labels),
    networks: String(raw.Networks || '')
      .split(',')
      .map((item) => item.trim())
      .filter(Boolean)
  };
}

/** 列出容器，all=false 时仅运行中 */
async function fetchContainers(all) {
  const args = ['ps', '--no-trunc', '--format', '{{json .}}'];
  if (all) {
    args.splice(1, 0, '-a');
  }
  const { stdout } = await runDocker(args, { timeout: COMMAND_TIMEOUT_MS });
  const rows = parseJsonLines(stdout).map(normalizeContainerRow);
  rows.sort((a, b) => {
    const aRunning = a.state === 'running' ? 0 : 1;
    const bRunning = b.state === 'running' ? 0 : 1;
    if (aRunning !== bRunning) {
      return aRunning - bRunning;
    }
    return a.name.localeCompare(b.name, 'zh-CN');
  });
  return rows;
}

/** 列出镜像 */
async function fetchImages() {
  const { stdout } = await runDocker(['images', '--no-trunc', '--format', '{{json .}}'], { timeout: COMMAND_TIMEOUT_MS });
  return parseJsonLines(stdout).map((raw) => {
    const repository = String(raw.Repository || '');
    const tag = String(raw.Tag || '');
    const repoTag = repository && repository !== '<none>' ? `${repository}${tag && tag !== '<none>' ? ':' + tag : ''}` : '<none>';
    const sizeBytes = parseHumanSize(raw.Size);
    return {
      id: String(raw.ID || ''),
      shortId: shortId(raw.ID),
      repoTag,
      repository,
      tag,
      digest: String(raw.Digest || ''),
      size: String(raw.Size || '0B'),
      sizeBytes,
      created: String(raw.CreatedAt || raw.CreatedSince || ''),
      createdSince: String(raw.CreatedSince || '')
    };
  });
}

/** 列出数据卷 */
async function fetchVolumes() {
  const { stdout } = await runDocker(['volume', 'ls', '--format', '{{json .}}'], { timeout: COMMAND_TIMEOUT_MS });
  return parseJsonLines(stdout).map((raw) => ({
    name: String(raw.Name || ''),
    driver: String(raw.Driver || ''),
    scope: String(raw.Scope || ''),
    mountpoint: String(raw.Mountpoint || ''),
    labels: parseLabelsText(raw.Labels)
  }));
}

/** 列出网络 */
async function fetchNetworks() {
  const { stdout } = await runDocker(['network', 'ls', '--format', '{{json .}}'], { timeout: COMMAND_TIMEOUT_MS });
  return parseJsonLines(stdout).map((raw) => ({
    id: String(raw.ID || ''),
    shortId: shortId(raw.ID),
    name: String(raw.Name || ''),
    driver: String(raw.Driver || ''),
    scope: String(raw.Scope || ''),
    ipv6: String(raw.IPv6 || 'false') === 'true',
    internal: String(raw.Internal || 'false') === 'true',
    labels: parseLabelsText(raw.Labels)
  }));
}

/** 解析 system df */
async function fetchDiskUsage() {
  const { stdout } = await runDocker(['system', 'df', '--format', '{{json .}}'], { timeout: COMMAND_TIMEOUT_MS });
  const rows = parseJsonLines(stdout);
  const mapping = [
    { key: 'images', label: 'Images' },
    { key: 'containers', label: 'Containers' },
    { key: 'volumes', label: 'Local Volumes' },
    { key: 'buildCache', label: 'Build Cache' }
  ];
  const result = {};
  for (const item of mapping) {
    const row = rows.find((entry) => String(entry.Type || '').toLowerCase() === item.label.toLowerCase());
    const legacy = rows.find((entry) => String(entry.Type || '').toLowerCase().includes(item.label.toLowerCase().split(' ')[0]));
    const hit = row || legacy;
    result[item.key] = hit
      ? {
          used: String(hit.Size || hit.Used || '0B'),
          usedBytes: parseHumanSize(hit.Size || hit.Used),
          reclaimable: String(hit.Reclaimable || '0B'),
          reclaimableBytes: parseHumanSize(hit.Reclaimable),
          total: Number(hit.TotalCount) || 0,
          active: Number(hit.Active) || 0
        }
      : { used: '0B', usedBytes: 0, reclaimable: '0B', reclaimableBytes: 0, total: 0, active: 0 };
  }
  return result;
}

/* ------------------------------ 概述 ------------------------------ */

async function handleOverview(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_VIEW, '查看 Docker 概览')) {
    return;
  }
  try {
    // 并行获取，任一失败都按 Docker 不可用处理
    const [info, containers, images, volumes, networks] = await Promise.all([
      fetchInfo(),
      fetchContainers(true),
      fetchImages(),
      fetchVolumes(),
      fetchNetworks()
    ]);
    const imagesSize = images.reduce((sum, item) => sum + (item.sizeBytes || 0), 0);
    sendJson(ctx.res, 200, {
      ok: true,
      available: true,
      version: info.version,
      info: {
        containers: info.containers,
        containersRunning: info.containersRunning,
        containersPaused: info.containersPaused,
        containersStopped: info.containersStopped,
        images: info.images,
        driver: info.driver,
        memTotal: info.memTotal,
        os: info.os,
        arch: info.arch
      },
      stats: {
        containers: containers.length,
        running: containers.filter((item) => item.state === 'running').length,
        images: images.length,
        imagesSize,
        volumes: volumes.length,
        networks: networks.length
      },
      cpus: info.ncpu
    });
  } catch (err) {
    console.error('[Docker] 获取 Docker 概览失败：', err.message);
    failUnexpected(ctx.res, err, '获取 Docker 概览');
  }
}

/* ------------------------------ 容器列表 ------------------------------ */

async function handleContainerList(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_VIEW, '查看容器列表')) {
    return;
  }
  try {
    const allParam = ctx.url.searchParams.get('all');
    const all = allParam === null ? true : !/^(0|false|no)$/i.test(allParam);
    const items = await fetchContainers(all);
    sendJson(ctx.res, 200, {
      ok: true,
      available: true,
      items,
      total: items.length,
      running: items.filter((item) => item.state === 'running').length
    });
  } catch (err) {
    console.error('[Docker] 获取容器列表失败：', err.message);
    failUnexpected(ctx.res, err, '获取容器列表');
  }
}

/* ------------------------------ 旧版信息接口（保留兼容） ------------------------------ */

async function handleLegacyInfo(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_VIEW, '查看 Docker 信息')) {
    return;
  }
  try {
    const info = await fetchInfo();
    sendJson(ctx.res, 200, {
      ok: true,
      version: info.version,
      containers: info.containers,
      containersRunning: info.containersRunning,
      containersStopped: info.containersStopped,
      containersPaused: info.containersPaused,
      images: info.images,
      driver: info.driver,
      operatingSystem: info.os,
      architecture: info.arch,
      memoryTotal: info.memTotal,
      cpus: info.ncpu
    });
  } catch (err) {
    console.error('[Docker] 获取 Docker 信息失败：', err.message);
    failUnexpected(ctx.res, err, '获取 Docker 信息');
  }
}

/* ------------------------------ 生命周期操作 ------------------------------ */

const ACTION_CONFIG = Object.freeze({
  start: { perm: PERMS.DOCKER_START, label: '启动', audit: 'DOCKER_CONTAINER_START', needPermission: '启动容器' },
  stop: { perm: PERMS.DOCKER_STOP, label: '停止', audit: 'DOCKER_CONTAINER_STOP', needPermission: '停止容器' },
  restart: { perm: PERMS.DOCKER_START, label: '重启', audit: 'DOCKER_CONTAINER_RESTART', needPermission: '重启容器' },
  pause: { perm: PERMS.DOCKER_STOP, label: '暂停', audit: 'DOCKER_CONTAINER_PAUSE', needPermission: '暂停容器' },
  unpause: { perm: PERMS.DOCKER_START, label: '恢复', audit: 'DOCKER_CONTAINER_UNPAUSE', needPermission: '恢复容器' },
  kill: { perm: PERMS.DOCKER_STOP, label: '强制结束', audit: 'DOCKER_CONTAINER_KILL', needPermission: '强制结束容器' }
});

async function handleAction(ctx, containerId, action) {
  const config = ACTION_CONFIG[action];
  if (!config) {
    sendError(ctx.res, 404, '不支持的操作', 'UNKNOWN_ACTION');
    return;
  }
  if (!auth.requirePermission(ctx.res, ctx.user, config.perm, config.needPermission)) {
    return;
  }
  if (!isValidContainerId(containerId)) {
    sendError(ctx.res, 400, '容器 ID 或名称格式不合法', 'INVALID_CONTAINER_ID');
    return;
  }
  try {
    const body = await readOptionalJson(ctx.req, 16 * 1024);
    const args = [action];
    if ((action === 'stop' || action === 'restart') && body.timeout !== undefined) {
      const timeout = toPositiveInt(body.timeout, '超时时间', 0, 600);
      args.push('-t', String(timeout));
    }
    // kill 的 timeout 参数被 Docker CLI 忽略：仅做校验，防止调用方误传非法值
    if (action === 'kill' && body.timeout !== undefined) {
      toPositiveInt(body.timeout, '超时时间', 0, 600);
    }
    args.push(containerId);
    await runDocker(args, { timeout: COMMAND_TIMEOUT_MS });
    audit(ctx, config.audit, `${config.label}容器 ${shortId(containerId)}`);
    sendJson(ctx.res, 200, { ok: true, action, id: containerId });
  } catch (err) {
    console.error(`[Docker] ${config.label}容器失败：`, err.message);
    failUnexpected(ctx.res, err, `${config.label}容器`);
  }
}

/* ------------------------------ 删除容器 ------------------------------ */

async function handleRemove(ctx, containerId) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_DELETE, '删除容器')) {
    return;
  }
  if (!isValidContainerId(containerId)) {
    sendError(ctx.res, 400, '容器 ID 或名称格式不合法', 'INVALID_CONTAINER_ID');
    return;
  }
  try {
    const force = /^(1|true|yes)$/i.test(ctx.url.searchParams.get('force') || '');
    const volumes = /^(1|true|yes)$/i.test(ctx.url.searchParams.get('volumes') || '');
    const args = ['rm'];
    if (force) {
      args.push('-f');
    }
    if (volumes) {
      args.push('-v');
    }
    args.push(containerId);
    const { stdout } = await runDocker(args, { timeout: COMMAND_TIMEOUT_MS });
    audit(ctx, 'DOCKER_CONTAINER_DELETE', `删除容器 ${shortId(containerId)}${force ? '（强制）' : ''}`);
    sendJson(ctx.res, 200, { ok: true, id: containerId, output: String(stdout || '').trim() });
  } catch (err) {
    console.error('[Docker] 删除容器失败：', err.message);
    failUnexpected(ctx.res, err, '删除容器');
  }
}

/* ------------------------------ 重命名容器 ------------------------------ */

async function handleRename(ctx, containerId) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_CREATE, '重命名容器')) {
    return;
  }
  if (!isValidContainerId(containerId)) {
    sendError(ctx.res, 400, '容器 ID 或名称格式不合法', 'INVALID_CONTAINER_ID');
    return;
  }
  try {
    const body = await readOptionalJson(ctx.req, 16 * 1024);
    if (!isValidName(body.name)) {
      sendError(ctx.res, 400, '容器名称不合法：需以字母或数字开头，仅允许字母、数字、下划线、点和短横线，最长 128 位', 'INVALID_NAME');
      return;
    }
    await runDocker(['rename', containerId, body.name], { timeout: COMMAND_TIMEOUT_MS });
    audit(ctx, 'DOCKER_CONTAINER_RENAME', `重命名容器 ${shortId(containerId)} → ${body.name}`);
    sendJson(ctx.res, 200, { ok: true, id: containerId, name: body.name });
  } catch (err) {
    console.error('[Docker] 重命名容器失败：', err.message);
    failUnexpected(ctx.res, err, '重命名容器');
  }
}

/* ------------------------------ 更新容器资源限制 ------------------------------ */

async function handleUpdate(ctx, containerId) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_CREATE, '调整容器配置')) {
    return;
  }
  if (!isValidContainerId(containerId)) {
    sendError(ctx.res, 400, '容器 ID 或名称格式不合法', 'INVALID_CONTAINER_ID');
    return;
  }
  try {
    const body = await readOptionalJson(ctx.req, 64 * 1024);
    const args = ['update'];
    const restartPolicy = body.restartPolicy === undefined ? null : String(body.restartPolicy);
    if (restartPolicy !== null) {
      if (!RESTART_POLICIES.has(restartPolicy)) {
        sendError(ctx.res, 400, '重启策略仅支持 no、always、unless-stopped、on-failure', 'INVALID_RESTART_POLICY');
        return;
      }
      args.push('--restart', restartPolicy);
    }
    let cpuShares = null;
    if (body.cpuShares !== undefined && body.cpuShares !== null) {
      cpuShares = toPositiveInt(body.cpuShares, 'CPU 权重', 0, 262144);
      args.push('--cpu-shares', String(cpuShares));
    }
    let memory = null;
    if (body.memory !== undefined && body.memory !== null) {
      // 前端传 MB，转成字节交给 docker
      const memoryMb = Number(body.memory);
      if (!Number.isFinite(memoryMb) || memoryMb <= 0 || memoryMb > 1024 * 1024) {
        sendError(ctx.res, 400, '内存限制需为 1~1048576 之间的 MB 数值', 'INVALID_MEMORY');
        return;
      }
      memory = Math.round(memoryMb * 1024 * 1024);
      args.push('--memory', String(memory));
      args.push('--memory-swap', String(memory));
    }
    if (args.length === 1) {
      sendError(ctx.res, 400, '请至少提供 restartPolicy、cpuShares 或 memory 中的一项', 'NO_UPDATE_FIELD');
      return;
    }
    args.push(containerId);
    const { stdout } = await runDocker(args, { timeout: COMMAND_TIMEOUT_MS });
    const detail = [
      restartPolicy !== null ? `重启策略=${restartPolicy}` : '',
      cpuShares !== null ? `CPU权重=${cpuShares}` : '',
      memory !== null ? `内存=${Math.round(memory / 1024 / 1024)}MB` : ''
    ]
      .filter(Boolean)
      .join('，');
    audit(ctx, 'DOCKER_CONTAINER_UPDATE', `调整容器 ${shortId(containerId)}：${detail}`);
    sendJson(ctx.res, 200, {
      ok: true,
      id: containerId,
      restartPolicy,
      cpuShares,
      memory,
      output: String(stdout || '').trim()
    });
  } catch (err) {
    console.error('[Docker] 调整容器配置失败：', err.message);
    failUnexpected(ctx.res, err, '调整容器配置');
  }
}

/* ------------------------------ 容器详情 ------------------------------ */

async function handleInspect(ctx, containerId) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_VIEW, '查看容器详情')) {
    return;
  }
  if (!isValidContainerId(containerId)) {
    sendError(ctx.res, 400, '容器 ID 或名称格式不合法', 'INVALID_CONTAINER_ID');
    return;
  }
  try {
    const info = await inspectContainer(containerId);
    sendJson(ctx.res, 200, Object.assign({ ok: true }, normalizeInspect(info)));
  } catch (err) {
    console.error('[Docker] 读取容器详情失败：', err.message);
    failUnexpected(ctx.res, err, '读取容器详情');
  }
}

/* ------------------------------ 容器日志（非流式） ------------------------------ */

/** 解析带时间戳的日志行：RFC3339 时间戳后跟正文 */
function splitTimestamp(line) {
  const match = line.match(/^(\d{4}-\d{2}-\d{2}T[0-9:.]+Z)\s?([\s\S]*)$/);
  if (!match) {
    return { line };
  }
  // 同时提供简写 t 与语义化的 timestamp，便于不同前端读取
  return { t: match[1], timestamp: match[1], line: match[2] };
}

/**
 * 容器日志（非流式）。
 * legacy 为 true 时（兼容旧路径 /api/docker/:id/logs）额外把 logs 字段输出为纯文本，
 * 新路径 /api/docker/containers/:id/logs 的 logs 为 [{t?, line}] 数组。
 */
async function handleLogs(ctx, containerId, legacy) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_VIEW, '查看容器日志')) {
    return;
  }
  if (!isValidContainerId(containerId)) {
    sendError(ctx.res, 400, '容器 ID 或名称格式不合法', 'INVALID_CONTAINER_ID');
    return;
  }
  try {
    const tailRaw = Number(ctx.url.searchParams.get('tail') || '200');
    const tail = Number.isFinite(tailRaw) ? Math.min(Math.max(Math.trunc(tailRaw), 1), 5000) : 200;
    const withTimestamps = /^(1|true|yes)$/i.test(ctx.url.searchParams.get('timestamps') || '');
    const args = ['logs', '--tail', String(tail)];
    if (withTimestamps) {
      args.push('--timestamps');
    }
    args.push(containerId);
    const { stdout, stderr } = await runDocker(args, { timeout: COMMAND_TIMEOUT_MS });
    // stdout 与 stderr 合并后按行切分（docker CLI 已将容器 stderr 归入 stderr）
    const merged = `${stdout || ''}${stderr || ''}`;
    const lines = merged.split(/\r?\n/);
    // 末尾空行丢弃
    while (lines.length > 0 && lines[lines.length - 1] === '') {
      lines.pop();
    }
    const logs = lines.map((line) => (withTimestamps ? splitTimestamp(line) : { line }));
    const text = lines.join('\n');
    const payload = {
      ok: true,
      id: containerId,
      tail,
      timestamps: withTimestamps,
      count: logs.length,
      logs,
      lines: logs,
      // 兼容老客户端：部分调用方仍读取字符串形式的日志
      text
    };
    if (legacy) {
      payload.logs = text;
    }
    sendJson(ctx.res, 200, payload);
  } catch (err) {
    console.error('[Docker] 读取容器日志失败：', err.message);
    failUnexpected(ctx.res, err, '读取容器日志');
  }
}

/* ------------------------------ 容器资源采样 ------------------------------ */

async function handleStats(ctx, containerId) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_VIEW, '查看容器资源占用')) {
    return;
  }
  if (!isValidContainerId(containerId)) {
    sendError(ctx.res, 400, '容器 ID 或名称格式不合法', 'INVALID_CONTAINER_ID');
    return;
  }
  try {
    const { stdout } = await runDocker(['stats', '--no-stream', '--format', '{{json .}}', containerId], {
      timeout: COMMAND_TIMEOUT_MS
    });
    const rows = parseJsonLines(stdout);
    const row = rows[0];
    if (!row) {
      sendJson(ctx.res, 200, {
        ok: true,
        available: false,
        id: containerId,
        message: '未获取到容器资源数据（容器可能未运行）'
      });
      return;
    }
    const mem = parseSizePair(row.MemUsage);
    const net = parseSizePair(row.NetIO);
    const block = parseSizePair(row.BlockIO);
    sendJson(ctx.res, 200, {
      ok: true,
      available: true,
      id: containerId,
      name: String(row.Name || ''),
      cpuPercent: parsePercent(row.CPUPerc),
      memUsage: mem.used,
      memLimit: mem.total,
      memPercent: parsePercent(row.MemPerc),
      netIn: net.used,
      netOut: net.total,
      blockRead: block.used,
      blockWrite: block.total,
      pids: Number(row.PIDs) || 0,
      raw: {
        cpu: String(row.CPUPerc || ''),
        mem: String(row.MemUsage || ''),
        net: String(row.NetIO || ''),
        block: String(row.BlockIO || '')
      }
    });
  } catch (err) {
    console.error('[Docker] 获取容器资源占用失败：', err.message);
    if (err instanceof DockerError && (err.status === 404 || err.status === 500)) {
      // 容器未运行或统计不可用时不算错误，返回 available:false
      sendJson(ctx.res, 200, {
        ok: true,
        available: false,
        id: containerId,
        message: err.message
      });
      return;
    }
    failUnexpected(ctx.res, err, '获取容器资源占用');
  }
}

/* ------------------------------ 创建容器 ------------------------------ */

/** 把命令字符串拆成参数数组，支持单双引号包裹 */
function splitCommandText(text) {
  const tokens = [];
  let current = '';
  let quote = '';
  for (const char of String(text)) {
    if (quote) {
      if (char === quote) {
        quote = '';
      } else {
        current += char;
      }
      continue;
    }
    if (char === '"' || char === "'") {
      quote = char;
      continue;
    }
    if (/\s/.test(char)) {
      if (current) {
        tokens.push(current);
        current = '';
      }
      continue;
    }
    current += char;
  }
  if (current) {
    tokens.push(current);
  }
  return tokens;
}

/** 校验并整理创建容器的命令参数 */
function resolveCommand(cmd) {
  if (cmd === undefined || cmd === null || cmd === '') {
    return [];
  }
  if (typeof cmd === 'string') {
    if (/[\u0000-\u001f]/.test(cmd)) {
      throw badRequest('启动命令包含非法控制字符', 'INVALID_CMD');
    }
    return splitCommandText(cmd);
  }
  if (Array.isArray(cmd)) {
    if (cmd.length > 64) {
      throw badRequest('启动命令参数过多（最多 64 个）', 'INVALID_CMD');
    }
    return cmd.map((item) => {
      if (typeof item !== 'string' || item.length === 0 || item.length > 4096) {
        throw badRequest('启动命令参数必须是长度 1~4096 的字符串', 'INVALID_CMD');
      }
      // eslint-disable-next-line no-control-regex
      if (/[\u0000-\u001f]/.test(item)) {
        throw badRequest('启动命令参数包含非法控制字符', 'INVALID_CMD');
      }
      return item;
    });
  }
  throw badRequest('启动命令必须是字符串或字符串数组', 'INVALID_CMD');
}

async function handleCreate(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_CREATE, '创建容器')) {
    return;
  }
  try {
    const body = await readOptionalJson(ctx.req, 256 * 1024);
    if (!isValidImageRef(body.image)) {
      sendError(ctx.res, 400, '镜像名称不合法，请填写如 nginx:latest 的镜像引用', 'INVALID_IMAGE');
      return;
    }
    const image = String(body.image);
    const args = ['create'];

    if (body.name !== undefined && body.name !== null && body.name !== '') {
      if (!isValidName(body.name)) {
        sendError(ctx.res, 400, '容器名称不合法：需以字母或数字开头，仅允许字母、数字、下划线、点和短横线，最长 128 位', 'INVALID_NAME');
        return;
      }
      args.push('--name', String(body.name));
    }

    const restartPolicy = body.restartPolicy === undefined || body.restartPolicy === null ? 'no' : String(body.restartPolicy);
    if (!RESTART_POLICIES.has(restartPolicy)) {
      sendError(ctx.res, 400, '重启策略仅支持 no、always、unless-stopped、on-failure', 'INVALID_RESTART_POLICY');
      return;
    }
    const autoRemove = body.autoRemove === true || body.autoRemove === '1' || body.autoRemove === 'true';
    if (autoRemove && restartPolicy !== 'no') {
      sendError(ctx.res, 400, '自动删除（--rm）不能与重启策略同时使用，请将 restartPolicy 设为 no', 'INVALID_RESTART_POLICY');
      return;
    }
    if (restartPolicy !== 'no') {
      args.push('--restart', restartPolicy);
    }
    if (autoRemove) {
      args.push('--rm');
    }

    if (body.network !== undefined && body.network !== null && body.network !== '') {
      if (!isValidName(body.network)) {
        sendError(ctx.res, 400, '网络名称不合法', 'INVALID_NETWORK');
        return;
      }
      args.push('--network', String(body.network));
    }

    if (body.env !== undefined && body.env !== null) {
      if (!Array.isArray(body.env)) {
        sendError(ctx.res, 400, '环境变量必须是字符串数组，格式为 KEY=VALUE', 'INVALID_ENV');
        return;
      }
      for (const item of body.env) {
        if (typeof item !== 'string' || item.length === 0 || item.length > 4096) {
          sendError(ctx.res, 400, '环境变量必须是长度 1~4096 的字符串', 'INVALID_ENV');
          return;
        }
        const index = item.indexOf('=');
        const key = index > 0 ? item.slice(0, index) : '';
        if (!ENV_KEY_PATTERN.test(key)) {
          sendError(ctx.res, 400, `环境变量格式不正确：${item}，应为 KEY=VALUE`, 'INVALID_ENV');
          return;
        }
        // eslint-disable-next-line no-control-regex
        if (/[\u0000-\u001f]/.test(item)) {
          sendError(ctx.res, 400, '环境变量包含非法控制字符', 'INVALID_ENV');
          return;
        }
        args.push('-e', item);
      }
    }

    if (body.ports !== undefined && body.ports !== null) {
      if (!Array.isArray(body.ports) || body.ports.length > 128) {
        sendError(ctx.res, 400, '端口映射必须是数组，且最多 128 项', 'INVALID_PORTS');
        return;
      }
      for (const item of body.ports) {
        if (!item || typeof item !== 'object' || Array.isArray(item)) {
          sendError(ctx.res, 400, '端口映射项必须是对象 {host, container, protocol}', 'INVALID_PORTS');
          return;
        }
        const containerPort = Number(item.container);
        if (!Number.isInteger(containerPort) || containerPort < 1 || containerPort > 65535) {
          sendError(ctx.res, 400, '容器端口必须是 1~65535 的整数', 'INVALID_PORTS');
          return;
        }
        const protocol = item.protocol === undefined || item.protocol === null || item.protocol === '' ? 'tcp' : String(item.protocol).toLowerCase();
        if (!PORT_PROTOCOLS.has(protocol)) {
          sendError(ctx.res, 400, '端口协议仅支持 tcp、udp、sctp', 'INVALID_PORTS');
          return;
        }
        // host 省略时由 Docker 随机分配宿主端口
        if (item.host === undefined || item.host === null || item.host === '') {
          args.push('-p', `${containerPort}/${protocol}`);
          continue;
        }
        const hostPort = Number(item.host);
        if (!Number.isInteger(hostPort) || hostPort < 1 || hostPort > 65535) {
          sendError(ctx.res, 400, '宿主端口必须是 1~65535 的整数', 'INVALID_PORTS');
          return;
        }
        args.push('-p', `${hostPort}:${containerPort}/${protocol}`);
      }
    }

    if (body.volumes !== undefined && body.volumes !== null) {
      if (!Array.isArray(body.volumes) || body.volumes.length > 128) {
        sendError(ctx.res, 400, '卷挂载必须是数组，且最多 128 项', 'INVALID_VOLUMES');
        return;
      }
      for (const item of body.volumes) {
        if (!item || typeof item !== 'object' || Array.isArray(item)) {
          sendError(ctx.res, 400, '卷挂载项必须是对象 {host, container, ro?}', 'INVALID_VOLUMES');
          return;
        }
        if (!isValidHostPath(item.host)) {
          sendError(ctx.res, 400, '宿主路径必须是 / 开头的绝对路径，且不能包含 ..', 'INVALID_VOLUMES');
          return;
        }
        if (!isValidContainerPath(item.container)) {
          sendError(ctx.res, 400, '容器路径必须是 / 开头的绝对路径，且不能包含 ..', 'INVALID_VOLUMES');
          return;
        }
        const ro = item.ro === true || item.ro === '1' || item.ro === 'true';
        args.push('-v', `${item.host}:${item.container}${ro ? ':ro' : ''}`);
      }
    }

    const command = resolveCommand(body.cmd);
    args.push(image);
    for (const token of command) {
      args.push(token);
    }

    const { stdout } = await runDocker(args, { timeout: COMMAND_TIMEOUT_MS });
    const createdId = String(stdout || '').trim().split(/\r?\n/).pop() || '';
    let createdName = body.name ? String(body.name) : '';
    if (createdId) {
      try {
        const info = await inspectContainer(createdId);
        createdName = String(info.Name || createdName).replace(/^\//, '');
      } catch (inspectErr) {
        // 容器已创建成功，读取名称失败不影响返回结果
        console.warn('[Docker] 读取新建容器信息失败：', inspectErr.message);
      }
    }
    audit(ctx, 'DOCKER_CONTAINER_CREATE', `创建容器 ${createdName || shortId(createdId)}（镜像 ${image}）`);
    sendJson(ctx.res, 201, { ok: true, id: createdId, name: createdName, image });
  } catch (err) {
    console.error('[Docker] 创建容器失败：', err.message);
    failUnexpected(ctx.res, err, '创建容器');
  }
}

/* ------------------------------ 镜像管理 ------------------------------ */

async function handleImageList(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_VIEW, '查看镜像列表')) {
    return;
  }
  try {
    const items = await fetchImages();
    const totalSize = items.reduce((sum, item) => sum + (item.sizeBytes || 0), 0);
    sendJson(ctx.res, 200, { ok: true, available: true, items, total: items.length, totalSize });
  } catch (err) {
    console.error('[Docker] 获取镜像列表失败：', err.message);
    failUnexpected(ctx.res, err, '获取镜像列表');
  }
}

/** 截断过长命令输出，避免响应体过大 */
function trimOutput(text, maxLines, maxChars) {
  const raw = String(text || '').trim();
  if (!raw) {
    return '';
  }
  let lines = raw.split(/\r?\n/);
  if (lines.length > maxLines) {
    lines = lines.slice(lines.length - maxLines);
  }
  let out = lines.join('\n');
  if (out.length > maxChars) {
    out = out.slice(out.length - maxChars);
  }
  return out;
}

async function handleImagePull(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_CREATE, '拉取镜像')) {
    return;
  }
  let image = '';
  try {
    const body = await readOptionalJson(ctx.req, 16 * 1024);
    if (!isValidImageRef(body.image)) {
      sendError(ctx.res, 400, '镜像名称不合法，请填写如 nginx:latest 的镜像引用', 'INVALID_IMAGE');
      return;
    }
    image = String(body.image);
    if (pullInFlight.has(image)) {
      sendError(ctx.res, 409, `镜像 ${image} 正在拉取中，请稍后重试`, 'IMAGE_PULLING');
      return;
    }
    const task = runDocker(['pull', image], { timeout: PULL_TIMEOUT_MS }).then((result) => `${result.stdout || ''}${result.stderr || ''}`);
    pullInFlight.set(image, task);
    try {
      const output = await task;
      audit(ctx, 'DOCKER_IMAGE_PULL', `拉取镜像 ${image}`);
      sendJson(ctx.res, 200, { ok: true, image, output: trimOutput(output, 500, 64 * 1024) });
    } finally {
      pullInFlight.delete(image);
    }
  } catch (err) {
    console.error(`[Docker] 拉取镜像失败：${err.message}`);
    failUnexpected(ctx.res, err, `拉取镜像${image ? ' ' + image : ''}`);
  }
}

async function handleImageDelete(ctx, imageId) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_DELETE, '删除镜像')) {
    return;
  }
  if (!isValidImageId(imageId)) {
    sendError(ctx.res, 400, '镜像 ID 格式不合法', 'INVALID_IMAGE_ID');
    return;
  }
  try {
    const force = /^(1|true|yes)$/i.test(ctx.url.searchParams.get('force') || '');
    const args = ['rmi'];
    if (force) {
      args.push('-f');
    }
    args.push(imageId);
    const { stdout } = await runDocker(args, { timeout: COMMAND_TIMEOUT_MS });
    audit(ctx, 'DOCKER_IMAGE_DELETE', `删除镜像 ${shortId(imageId)}${force ? '（强制）' : ''}`);
    sendJson(ctx.res, 200, {
      ok: true,
      id: imageId,
      output: trimOutput(stdout, 100, 8192)
    });
  } catch (err) {
    console.error('[Docker] 删除镜像失败：', err.message);
    failUnexpected(ctx.res, err, '删除镜像');
  }
}

async function handleImagePrune(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_DELETE, '清理镜像')) {
    return;
  }
  try {
    const body = await readOptionalJson(ctx.req, 16 * 1024);
    const all = body.all === true || body.all === '1' || body.all === 'true';
    const args = ['image', 'prune', '-f'];
    if (all) {
      args.push('-a');
    }
    const { stdout } = await runDocker(args, { timeout: 120000 });
    const reclaimedMatch = String(stdout || '').match(/Total reclaimed space:\s*(.+)/i);
    audit(ctx, 'DOCKER_IMAGE_PRUNE', `清理未使用镜像${all ? '（包含无标签镜像）' : ''}`);
    sendJson(ctx.res, 200, {
      ok: true,
      all,
      reclaimed: reclaimedMatch ? reclaimedMatch[1].trim() : '0B',
      reclaimedBytes: reclaimedMatch ? parseHumanSize(reclaimedMatch[1]) : 0,
      output: trimOutput(stdout, 200, 16 * 1024)
    });
  } catch (err) {
    console.error('[Docker] 清理镜像失败：', err.message);
    failUnexpected(ctx.res, err, '清理镜像');
  }
}

/* ------------------------------ 数据卷管理 ------------------------------ */

async function handleVolumeList(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_VIEW, '查看数据卷列表')) {
    return;
  }
  try {
    const items = await fetchVolumes();
    sendJson(ctx.res, 200, { ok: true, available: true, items, total: items.length });
  } catch (err) {
    console.error('[Docker] 获取数据卷列表失败：', err.message);
    failUnexpected(ctx.res, err, '获取数据卷列表');
  }
}

async function handleVolumeCreate(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_CREATE, '创建数据卷')) {
    return;
  }
  try {
    const body = await readOptionalJson(ctx.req, 16 * 1024);
    if (!isValidName(body.name)) {
      sendError(ctx.res, 400, '数据卷名称不合法：需以字母或数字开头，仅允许字母、数字、下划线、点和短横线', 'INVALID_NAME');
      return;
    }
    const name = String(body.name);
    const driver = body.driver === undefined || body.driver === null || body.driver === '' ? '' : String(body.driver);
    if (driver && !isValidName(driver)) {
      sendError(ctx.res, 400, '数据卷驱动名称不合法', 'INVALID_DRIVER');
      return;
    }
    const args = ['volume', 'create'];
    if (driver) {
      args.push('--driver', driver);
    }
    args.push(name);
    await runDocker(args, { timeout: COMMAND_TIMEOUT_MS });
    audit(ctx, 'DOCKER_VOLUME_CREATE', `创建数据卷 ${name}${driver ? `（驱动 ${driver}）` : ''}`);
    sendJson(ctx.res, 201, { ok: true, name, driver: driver || 'local' });
  } catch (err) {
    console.error('[Docker] 创建数据卷失败：', err.message);
    failUnexpected(ctx.res, err, '创建数据卷');
  }
}

async function handleVolumeDelete(ctx, volumeName) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_DELETE, '删除数据卷')) {
    return;
  }
  if (!isValidName(volumeName)) {
    sendError(ctx.res, 400, '数据卷名称不合法', 'INVALID_NAME');
    return;
  }
  try {
    const force = /^(1|true|yes)$/i.test(ctx.url.searchParams.get('force') || '');
    const args = ['volume', 'rm'];
    if (force) {
      args.push('-f');
    }
    args.push(volumeName);
    await runDocker(args, { timeout: COMMAND_TIMEOUT_MS });
    audit(ctx, 'DOCKER_VOLUME_DELETE', `删除数据卷 ${volumeName}${force ? '（强制）' : ''}`);
    sendJson(ctx.res, 200, { ok: true, name: volumeName });
  } catch (err) {
    console.error('[Docker] 删除数据卷失败：', err.message);
    failUnexpected(ctx.res, err, '删除数据卷');
  }
}

/* ------------------------------ 网络管理 ------------------------------ */

async function handleNetworkList(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_VIEW, '查看网络列表')) {
    return;
  }
  try {
    const items = await fetchNetworks();
    sendJson(ctx.res, 200, { ok: true, available: true, items, total: items.length });
  } catch (err) {
    console.error('[Docker] 获取网络列表失败：', err.message);
    failUnexpected(ctx.res, err, '获取网络列表');
  }
}

async function handleNetworkCreate(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_CREATE, '创建网络')) {
    return;
  }
  try {
    const body = await readOptionalJson(ctx.req, 16 * 1024);
    if (!isValidName(body.name)) {
      sendError(ctx.res, 400, '网络名称不合法：需以字母或数字开头，仅允许字母、数字、下划线、点和短横线', 'INVALID_NAME');
      return;
    }
    const name = String(body.name);
    const driver = body.driver === undefined || body.driver === null || body.driver === '' ? 'bridge' : String(body.driver);
    if (!isValidName(driver)) {
      sendError(ctx.res, 400, '网络驱动名称不合法', 'INVALID_DRIVER');
      return;
    }
    if (!NETWORK_DRIVERS.has(driver)) {
      sendError(ctx.res, 400, '网络驱动仅支持 bridge、host、none、overlay、macvlan、ipvlan', 'INVALID_DRIVER');
      return;
    }
    const { stdout } = await runDocker(['network', 'create', '--driver', driver, name], { timeout: COMMAND_TIMEOUT_MS });
    audit(ctx, 'DOCKER_NETWORK_CREATE', `创建网络 ${name}（驱动 ${driver}）`);
    sendJson(ctx.res, 201, { ok: true, name, driver, id: String(stdout || '').trim() });
  } catch (err) {
    console.error('[Docker] 创建网络失败：', err.message);
    failUnexpected(ctx.res, err, '创建网络');
  }
}

async function handleNetworkDelete(ctx, networkName) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_DELETE, '删除网络')) {
    return;
  }
  if (!isValidName(networkName)) {
    sendError(ctx.res, 400, '网络名称不合法', 'INVALID_NAME');
    return;
  }
  try {
    await runDocker(['network', 'rm', networkName], { timeout: COMMAND_TIMEOUT_MS });
    audit(ctx, 'DOCKER_NETWORK_DELETE', `删除网络 ${networkName}`);
    sendJson(ctx.res, 200, { ok: true, name: networkName });
  } catch (err) {
    console.error('[Docker] 删除网络失败：', err.message);
    failUnexpected(ctx.res, err, '删除网络');
  }
}

/* ------------------------------ 磁盘占用 ------------------------------ */

async function handleDiskUsage(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.DOCKER_VIEW, '查看磁盘占用')) {
    return;
  }
  try {
    const usage = await fetchDiskUsage();
    sendJson(ctx.res, 200, {
      ok: true,
      available: true,
      images: usage.images,
      containers: usage.containers,
      volumes: usage.volumes,
      buildCache: usage.buildCache
    });
  } catch (err) {
    console.error('[Docker] 获取磁盘占用失败：', err.message);
    failUnexpected(ctx.res, err, '获取磁盘占用');
  }
}

/* ------------------------------ 路由表 ------------------------------ */

/** 解码路径参数，失败返回 null */
function safeDecode(value) {
  try {
    return decodeURIComponent(value);
  } catch (decodeErr) {
    return null;
  }
}

/** 需要 :id / :name 参数的路由包装：解码失败直接 400 */
function withParam(run) {
  return async (ctx, match) => {
    const param = safeDecode(match[1]);
    if (param === null) {
      sendError(ctx.res, 400, '路径参数编码不合法', 'INVALID_PATH');
      return;
    }
    await run(ctx, param);
  };
}

const ROUTE_TABLE = [
  // 概述
  { method: 'GET', re: /^\/api\/docker$/, run: (ctx) => handleOverview(ctx) },
  { method: 'GET', re: /^\/api\/docker\/overview$/, run: (ctx) => handleOverview(ctx) },
  // 旧版信息接口（保留兼容）
  { method: 'GET', re: /^\/api\/docker\/info$/, run: (ctx) => handleLegacyInfo(ctx) },
  // 容器
  { method: 'GET', re: /^\/api\/docker\/containers$/, run: (ctx) => handleContainerList(ctx) },
  { method: 'POST', re: /^\/api\/docker\/containers$/, run: (ctx) => handleCreate(ctx) },
  {
    method: 'GET',
    re: /^\/api\/docker\/containers\/([^/]+)\/logs$/,
    run: withParam((ctx, id) => handleLogs(ctx, id, false))
  },
  {
    method: 'GET',
    re: /^\/api\/docker\/containers\/([^/]+)\/stats$/,
    run: withParam((ctx, id) => handleStats(ctx, id))
  },
  {
    method: 'POST',
    re: /^\/api\/docker\/containers\/([^/]+)\/rename$/,
    run: withParam((ctx, id) => handleRename(ctx, id))
  },
  {
    method: 'POST',
    re: /^\/api\/docker\/containers\/([^/]+)\/update$/,
    run: withParam((ctx, id) => handleUpdate(ctx, id))
  },
  {
    method: 'POST',
    re: /^\/api\/docker\/containers\/([^/]+)\/(start|stop|restart|pause|unpause|kill)$/,
    run: (ctx, match) => {
      const id = safeDecode(match[1]);
      if (id === null) {
        sendError(ctx.res, 400, '路径参数编码不合法', 'INVALID_PATH');
        return Promise.resolve();
      }
      return handleAction(ctx, id, match[2]);
    }
  },
  {
    method: 'DELETE',
    re: /^\/api\/docker\/containers\/([^/]+)$/,
    run: withParam((ctx, id) => handleRemove(ctx, id))
  },
  {
    method: 'GET',
    re: /^\/api\/docker\/containers\/([^/]+)$/,
    run: withParam((ctx, id) => handleInspect(ctx, id))
  },
  // 镜像
  { method: 'GET', re: /^\/api\/docker\/images$/, run: (ctx) => handleImageList(ctx) },
  { method: 'POST', re: /^\/api\/docker\/images\/pull$/, run: (ctx) => handleImagePull(ctx) },
  { method: 'POST', re: /^\/api\/docker\/images\/prune$/, run: (ctx) => handleImagePrune(ctx) },
  {
    method: 'DELETE',
    re: /^\/api\/docker\/images\/([^/]+)$/,
    run: withParam((ctx, id) => handleImageDelete(ctx, id))
  },
  // 数据卷
  { method: 'GET', re: /^\/api\/docker\/volumes$/, run: (ctx) => handleVolumeList(ctx) },
  { method: 'POST', re: /^\/api\/docker\/volumes$/, run: (ctx) => handleVolumeCreate(ctx) },
  {
    method: 'DELETE',
    re: /^\/api\/docker\/volumes\/([^/]+)$/,
    run: withParam((ctx, name) => handleVolumeDelete(ctx, name))
  },
  // 网络
  { method: 'GET', re: /^\/api\/docker\/networks$/, run: (ctx) => handleNetworkList(ctx) },
  { method: 'POST', re: /^\/api\/docker\/networks$/, run: (ctx) => handleNetworkCreate(ctx) },
  {
    method: 'DELETE',
    re: /^\/api\/docker\/networks\/([^/]+)$/,
    run: withParam((ctx, name) => handleNetworkDelete(ctx, name))
  },
  // 磁盘占用
  { method: 'GET', re: /^\/api\/docker\/df$/, run: (ctx) => handleDiskUsage(ctx) },
  // 旧版容器路径（保留兼容）：/api/docker/:id/{start|stop|restart}
  {
    method: 'POST',
    re: /^\/api\/docker\/([^/]+)\/(start|stop|restart)$/,
    run: (ctx, match) => {
      const id = safeDecode(match[1]);
      if (id === null) {
        sendError(ctx.res, 400, '路径参数编码不合法', 'INVALID_PATH');
        return Promise.resolve();
      }
      return handleAction(ctx, id, match[2]);
    }
  },
  // 旧版日志路径（保留兼容）：/api/docker/:id/logs，logs 字段保持字符串
  {
    method: 'GET',
    re: /^\/api\/docker\/([^/]+)\/logs$/,
    run: withParam((ctx, id) => handleLogs(ctx, id, true))
  }
];

/** 处理 /api/docker 下的全部请求，返回 true 表示已处理 */
async function handle(ctx) {
  const { pathname, method } = ctx;
  for (const route of ROUTE_TABLE) {
    if (route.method !== method) {
      continue;
    }
    const match = pathname.match(route.re);
    if (!match) {
      continue;
    }
    await route.run(ctx, match);
    return true;
  }
  sendError(ctx.res, 404, '接口不存在', 'NOT_FOUND');
  return true;
}

module.exports = { handle, runDocker, DockerError, DOCKER_BIN };
