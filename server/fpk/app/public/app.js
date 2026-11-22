'use strict';

/**
 * 飞牛终端 · Web 管理后台脚本
 * 纯原生 JavaScript，无任何外部依赖。
 * 登录态保存在 sessionStorage，所有请求通过 Bearer Token 鉴权。
 */

(function () {
  const TOKEN_KEY = 'fnos_admin_token';
  const VIEW_META = {
    dashboard: { title: '仪表盘', subtitle: '系统运行概览' },
    files: { title: '文件管理', subtitle: '浏览、上传、预览、压缩与分享' },
    shares: { title: '分享管理', subtitle: '分享链接、提取密码与有效期' },
    docker: { title: '容器管理', subtitle: 'Docker 容器状态与启停控制' },
    users: { title: '用户管理', subtitle: '账号、角色与 24 位权限位' },
    audit: { title: '审计日志', subtitle: '登录、文件与容器操作的完整记录' },
    settings: { title: '系统设置', subtitle: '外观偏好、账号安全与服务器信息' }
  };

  const state = {
    token: '',
    me: null,
    roleLabel: '',
    catalog: null,
    view: 'dashboard',
    pollTimer: null,
    dockerItems: [],
    userItems: [],
    userPage: 1,
    authMode: 'login'
  };

  /**
   * 视图钩子注册表：files.js / shares.js 等独立脚本通过 registerView 注入
   * mount（首次进入视图时加载）与 refresh（顶部刷新按钮）
   */
  const viewHooks = Object.create(null);

  function registerView(name, hooks) {
    if (!name) {
      return;
    }
    viewHooks[name] = hooks && typeof hooks === 'object' ? hooks : {};
  }

  try {
    state.token = sessionStorage.getItem(TOKEN_KEY) || '';
  } catch (storageErr) {
    state.token = '';
    console.warn('浏览器禁用了 sessionStorage，登录状态将无法保持：', storageErr);
  }

  /* ------------------------------ 主题切换 ------------------------------ */

  const THEME_KEY = 'fnos_theme';
  const MODE_KEY = 'fnos_mode';
  const ACCENT_KEY = 'fnos_accent';
  let currentAccent = '';
  const THEME_IDS = ['win11', 'win10', 'win7', 'glass'];
  const THEME_COLOR = {
    win11: { dark: '#1b1b1b', light: '#f3f3f3' },
    win10: { dark: '#1f1f1f', light: '#f2f2f2' },
    win7: { dark: '#0f2236', light: '#cfe1f2' },
    glass: { dark: '#07101d', light: '#eef4ff' }
  };

  function readPreference(key) {
    try {
      return window.localStorage.getItem(key) || '';
    } catch (storageErr) {
      console.warn('浏览器禁用了 localStorage，界面偏好将无法保存：', storageErr);
      return '';
    }
  }

  function savePreference(key, value) {
    try {
      window.localStorage.setItem(key, value);
    } catch (storageErr) {
      console.warn('无法保存界面偏好：', storageErr);
    }
  }

  function syncThemeControls(theme, mode) {
    const pickers = document.querySelectorAll('[data-theme-picker]');
    for (const picker of pickers) {
      if (picker.value !== theme) {
        picker.value = theme;
      }
    }
    const toggles = document.querySelectorAll('[data-mode-toggle]');
    for (const toggle of toggles) {
      const use = toggle.querySelector('use');
      if (use) {
        use.setAttribute('href', mode === 'dark' ? '#i-sun' : '#i-moon');
      }
      const label = mode === 'dark' ? '切换到浅色模式' : '切换到深色模式';
      toggle.setAttribute('aria-label', label);
      toggle.setAttribute('title', label);
    }
  }

  function applyTheme(theme, mode) {
    const nextTheme = THEME_IDS.indexOf(theme) >= 0 ? theme : 'win11';
    const nextMode = mode === 'light' ? 'light' : 'dark';
    const root = document.documentElement;
    root.dataset.theme = nextTheme;
    root.dataset.mode = nextMode;
    savePreference(THEME_KEY, nextTheme);
    savePreference(MODE_KEY, nextMode);

    const schemeMeta = document.querySelector('meta[name="color-scheme"]');
    if (schemeMeta) {
      schemeMeta.setAttribute('content', nextMode === 'dark' ? 'dark light' : 'light dark');
    }
    const colorMeta = document.querySelector('meta[name="theme-color"]');
    if (colorMeta) {
      colorMeta.setAttribute('content', THEME_COLOR[nextTheme][nextMode]);
    }
    syncThemeControls(nextTheme, nextMode);
    // 主题/模式切换后重新推导强调色的派生变量
    applyAccent(currentAccent, false);
  }

  function loadTheme() {
    const theme = readPreference(THEME_KEY);
    const mode = readPreference(MODE_KEY);
    applyTheme(
      THEME_IDS.indexOf(theme) >= 0 ? theme : 'win11',
      mode === 'light' ? 'light' : 'dark'
    );
  }

  /* ------------------------------ 强调色 ------------------------------ */

  const ACCENT_VARS = ['--accent', '--accent-strong', '--accent-soft', '--accent-text'];

  function normalizeHex(value) {
    const text = String(value || '').trim();
    return /^#[0-9a-fA-F]{6}$/.test(text) ? text.toLowerCase() : '';
  }

  function hexToRgb(hex) {
    const value = normalizeHex(hex);
    if (!value) {
      return null;
    }
    return {
      r: parseInt(value.slice(1, 3), 16),
      g: parseInt(value.slice(3, 5), 16),
      b: parseInt(value.slice(5, 7), 16)
    };
  }

  function rgbToHex(r, g, b) {
    const clamp = (n) => Math.max(0, Math.min(255, Math.round(n)));
    return `#${[r, g, b].map((n) => clamp(n).toString(16).padStart(2, '0')).join('')}`;
  }

  /** 把 hex 朝 target（#ffffff / #000000）混合，ratio 取值 0~1 */
  function mixHex(hex, target, ratio) {
    const from = hexToRgb(hex);
    const to = hexToRgb(target);
    if (!from || !to) {
      return normalizeHex(hex);
    }
    const t = Math.max(0, Math.min(1, Number(ratio) || 0));
    return rgbToHex(
      from.r + (to.r - from.r) * t,
      from.g + (to.g - from.g) * t,
      from.b + (to.b - from.b) * t
    );
  }

  function rgbaOf(hex, alpha) {
    const rgb = hexToRgb(hex);
    if (!rgb) {
      return '';
    }
    return `rgba(${rgb.r}, ${rgb.g}, ${rgb.b}, ${Number(alpha) || 0})`;
  }

  /** 根据强调色亮度决定其上的文字颜色 */
  function accentTextColor(hex) {
    const rgb = hexToRgb(hex);
    if (!rgb) {
      return '#ffffff';
    }
    const toLinear = (c) => {
      const v = c / 255;
      return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
    };
    const lum = 0.2126 * toLinear(rgb.r) + 0.7152 * toLinear(rgb.g) + 0.0722 * toLinear(rgb.b);
    return lum > 0.45 ? '#08131f' : '#ffffff';
  }

  function syncAccentControls(accent) {
    const dots = document.querySelectorAll('[data-accent]');
    for (const dot of dots) {
      const active = normalizeHex(dot.dataset.accent) === accent;
      dot.classList.toggle('is-active', active);
      dot.setAttribute('aria-pressed', active ? 'true' : 'false');
    }
    const colorInput = $('accentColor');
    if (colorInput) {
      // 未自定义时回落到当前主题的强调色
      const fallback = normalizeHex(
        getComputedStyle(document.documentElement).getPropertyValue('--accent')
      );
      const next = accent || fallback || '#2f81f7';
      if (colorInput.value !== next) {
        colorInput.value = next;
      }
    }
  }

  /**
   * 应用强调色：accent 为空字符串表示恢复主题默认。
   * persist 为 true 时写入 localStorage。
   */
  function applyAccent(accent, persist) {
    const next = normalizeHex(accent);
    currentAccent = next;
    const root = document.documentElement;
    const mode = root.dataset.mode === 'light' ? 'light' : 'dark';

    if (next) {
      const strong = mode === 'dark' ? mixHex(next, '#ffffff', 0.24) : mixHex(next, '#000000', 0.16);
      root.style.setProperty('--accent', next);
      root.style.setProperty('--accent-strong', strong);
      root.style.setProperty('--accent-soft', rgbaOf(next, mode === 'dark' ? 0.18 : 0.12));
      root.style.setProperty('--accent-text', accentTextColor(next));
    } else {
      for (const name of ACCENT_VARS) {
        root.style.removeProperty(name);
      }
    }

    if (persist) {
      if (next) {
        savePreference(ACCENT_KEY, next);
      } else {
        try {
          window.localStorage.removeItem(ACCENT_KEY);
        } catch (storageErr) {
          console.warn('无法清除强调色偏好：', storageErr);
        }
      }
    }
    syncAccentControls(next);
  }

  function loadAccent() {
    applyAccent(readPreference(ACCENT_KEY), false);
  }

  /* ------------------------------ 移动端抽屉 ------------------------------ */

  function toggleSidebar(open) {
    const app = $('appView');
    if (!app) {
      return;
    }
    const next = typeof open === 'boolean' ? open : !app.classList.contains('sidebar-open');
    app.classList.toggle('sidebar-open', next);
    const button = $('menuBtn');
    if (button) {
      button.setAttribute('aria-expanded', next ? 'true' : 'false');
    }
  }

  /* ------------------------------ 侧栏折叠 ------------------------------ */

  const SIDEBAR_KEY = 'fnos_sidebar_collapsed';

  /** 应用侧栏折叠状态；persist 为 true 时写入 localStorage */
  function applyCollapse(collapsed, persist) {
    const app = $('appView');
    if (!app) {
      return;
    }
    const next = Boolean(collapsed);
    app.classList.toggle('sidebar-collapsed', next);
    const button = $('sidebarCollapse');
    if (button) {
      const label = next ? '展开侧栏' : '收起侧栏';
      button.setAttribute('aria-expanded', next ? 'false' : 'true');
      button.setAttribute('aria-label', label);
      button.dataset.tip = label;
      const text = button.querySelector('span');
      if (text) {
        text.textContent = label;
      }
    }
    if (persist) {
      savePreference(SIDEBAR_KEY, next ? '1' : '0');
    }
  }

  function toggleCollapse() {
    const app = $('appView');
    if (!app) {
      return;
    }
    applyCollapse(!app.classList.contains('sidebar-collapsed'), true);
  }

  /* ------------------------------ 顶栏用户菜单 ------------------------------ */

  function setUserMenu(open) {
    const menu = $('userMenu');
    const button = $('userMenuBtn');
    if (!menu || !button) {
      return;
    }
    menu.hidden = !open;
    button.setAttribute('aria-expanded', open ? 'true' : 'false');
  }

  /* ------------------------------ 面包屑 ------------------------------ */

  /** 生成内联 SVG 图标（复用页面内的 symbol 精灵） */
  function svgIcon(name, className) {
    const ns = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(ns, 'svg');
    svg.setAttribute('class', className || 'icon');
    svg.setAttribute('aria-hidden', 'true');
    const use = document.createElementNS(ns, 'use');
    use.setAttribute('href', `#${name}`);
    svg.appendChild(use);
    return svg;
  }

  function renderCrumbs(view) {
    const nav = $('crumbs');
    if (!nav) {
      return;
    }
    clear(nav);
    const meta = VIEW_META[view] || VIEW_META.dashboard;

    const home = create('a', 'crumbs-item');
    home.href = '#/dashboard';
    home.appendChild(svgIcon('i-dashboard'));
    home.appendChild(create('span', null, '首页'));
    nav.appendChild(home);

    nav.appendChild(svgIcon('i-arrow', 'icon crumbs-sep'));
    nav.appendChild(create('span', 'crumbs-item current', meta.title));
  }

  /* ------------------------------ 基础工具 ------------------------------ */

  function $(id) {
    return document.getElementById(id);
  }

  function create(tag, className, text) {
    const node = document.createElement(tag);
    if (className) {
      node.className = className;
    }
    if (text !== undefined) {
      node.textContent = text;
    }
    return node;
  }

  function clear(node) {
    while (node.firstChild) {
      node.removeChild(node.firstChild);
    }
  }

  function formatBytes(bytes) {
    const value = Number(bytes);
    if (!Number.isFinite(value) || value < 0) {
      return '-';
    }
    const units = ['B', 'KB', 'MB', 'GB', 'TB', 'PB'];
    let size = value;
    let index = 0;
    while (size >= 1024 && index < units.length - 1) {
      size /= 1024;
      index += 1;
    }
    const text = index === 0 ? String(Math.round(size)) : size.toFixed(size >= 100 ? 0 : 1);
    return `${text} ${units[index]}`;
  }

  function formatDuration(totalSeconds) {
    const seconds = Math.max(0, Math.floor(Number(totalSeconds) || 0));
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    if (days > 0) {
      return `${days} 天 ${hours} 小时`;
    }
    if (hours > 0) {
      return `${hours} 小时 ${minutes} 分`;
    }
    if (minutes > 0) {
      return `${minutes} 分 ${seconds % 60} 秒`;
    }
    return `${seconds} 秒`;
  }

  function pad2(value) {
    return String(value).padStart(2, '0');
  }

  function formatDateTime(epochMs) {
    const value = Number(epochMs);
    if (!Number.isFinite(value) || value <= 0) {
      return '-';
    }
    const date = new Date(value);
    return `${date.getFullYear()}-${pad2(date.getMonth() + 1)}-${pad2(date.getDate())} ${pad2(
      date.getHours()
    )}:${pad2(date.getMinutes())}`;
  }

  /* ------------------------------ Toast ------------------------------ */

  function toast(message, type) {
    const root = $('toastRoot');
    if (!root) {
      return;
    }
    const box = create('div', `toast toast-${type || 'info'}`);
    const bar = create('span', 'toast-bar');
    const text = create('div', 'toast-text', String(message));
    box.append(bar, text);
    root.appendChild(box);
    window.setTimeout(() => {
      box.classList.add('leaving');
      window.setTimeout(() => {
        if (box.parentNode) {
          box.parentNode.removeChild(box);
        }
      }, 220);
    }, 3400);
  }

  /** 退出登录：侧栏与顶栏菜单共用 */
  async function doLogout() {
    try {
      await api('/api/auth/logout', { method: 'POST' });
    } catch (err) {
      if (err.status !== 401) {
        toast(`退出登录时出现异常：${err.message}`, 'warn');
      }
    }
    state.token = '';
    state.me = null;
    try {
      sessionStorage.removeItem(TOKEN_KEY);
    } catch (storageErr) {
      console.warn('无法清理登录状态：', storageErr);
    }
    stopPolling();
    clearMediaToken();
    showAuth('login');
    setAuthStatus('online', '已退出登录');
    toast('已退出登录', 'success');
  }

  /** 复制文本：优先 Clipboard API，不可用时回退到隐藏 textarea */
  async function copyText(text) {
    if (navigator.clipboard && window.isSecureContext) {
      await navigator.clipboard.writeText(text);
      return;
    }
    const area = document.createElement('textarea');
    area.value = text;
    area.setAttribute('readonly', 'readonly');
    area.style.position = 'fixed';
    area.style.opacity = '0';
    document.body.appendChild(area);
    area.select();
    const ok = document.execCommand('copy');
    document.body.removeChild(area);
    if (!ok) {
      throw new Error('复制命令被浏览器拒绝');
    }
  }

  /* ------------------------------ 弹窗 ------------------------------ */

  let modalCloseHandler = null;
  let modalLastFocus = null;

  const FOCUSABLE = 'button:not([disabled]), [href], input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])';

  /** 弹窗内键盘：ESC 关闭、Tab 循环（焦点陷阱） */
  function onModalKeydown(event) {
    if (event.key === 'Escape') {
      event.preventDefault();
      closeModal();
      return;
    }
    if (event.key !== 'Tab') {
      return;
    }
    const modal = $('modalRoot').querySelector('.modal');
    if (!modal) {
      return;
    }
    const nodes = Array.prototype.filter.call(
      modal.querySelectorAll(FOCUSABLE),
      (node) => node.offsetParent !== null || node === document.activeElement
    );
    if (nodes.length === 0) {
      event.preventDefault();
      return;
    }
    const first = nodes[0];
    const last = nodes[nodes.length - 1];
    const active = document.activeElement;
    if (event.shiftKey && (active === first || !modal.contains(active))) {
      event.preventDefault();
      last.focus();
    } else if (!event.shiftKey && (active === last || !modal.contains(active))) {
      event.preventDefault();
      first.focus();
    }
  }

  function closeModal() {
    const root = $('modalRoot');
    root.hidden = true;
    clear($('modalBody'));
    clear($('modalFoot'));
    document.removeEventListener('keydown', onModalKeydown);
    // 关闭后把焦点还给触发弹窗的元素，保持键盘操作连续
    const back = modalLastFocus;
    modalLastFocus = null;
    if (back && document.contains(back) && typeof back.focus === 'function') {
      back.focus();
    }
    const handler = modalCloseHandler;
    modalCloseHandler = null;
    if (typeof handler === 'function') {
      handler();
    }
  }

  function openModal(options) {
    const config = options || {};
    const root = $('modalRoot');
    const modal = root.querySelector('.modal');
    $('modalTitle').textContent = config.title || '提示';

    const body = $('modalBody');
    clear(body);
    if (typeof config.body === 'string') {
      body.appendChild(create('p', 'modal-desc', config.body));
    } else if (config.body) {
      body.appendChild(config.body);
    }

    const foot = $('modalFoot');
    clear(foot);
    const buttons = Array.isArray(config.buttons) ? config.buttons : [];
    for (const def of buttons) {
      const btn = create('button', `btn ${def.kind || 'btn-ghost'}`, def.label);
      btn.type = 'button';
      btn.addEventListener('click', () => {
        if (!def.onClick) {
          closeModal();
          return;
        }
        const setBusy = (busy) => {
          btn.disabled = Boolean(busy);
          btn.setAttribute('aria-busy', busy ? 'true' : 'false');
        };
        try {
          const result = def.onClick({ close: closeModal, setBusy });
          if (result && typeof result.catch === 'function') {
            result.catch((err) => {
              setBusy(false);
              toast(err && err.message ? err.message : '操作失败', 'error');
            });
          }
        } catch (err) {
          setBusy(false);
          toast(err && err.message ? err.message : '操作失败', 'error');
        }
      });
      foot.appendChild(btn);
    }

    modal.classList.toggle('modal-wide', Boolean(config.wide));
    modalCloseHandler = typeof config.onClose === 'function' ? config.onClose : null;
    modalLastFocus = document.activeElement instanceof HTMLElement ? document.activeElement : null;
    root.hidden = false;
    document.addEventListener('keydown', onModalKeydown);

    const focusTarget =
      body.querySelector('input:not([disabled]), select:not([disabled]), textarea:not([disabled])') ||
      modal.querySelector('.modal-foot .btn:not([disabled])') ||
      $('modalClose');
    if (focusTarget) {
      window.setTimeout(() => focusTarget.focus(), 40);
    }
  }

  /* ------------------------------ API ------------------------------ */

  class ApiError extends Error {
    constructor(message, status, code) {
      super(message);
      this.name = 'ApiError';
      this.status = status;
      this.code = code;
    }
  }

  async function api(path, options) {
    const config = options || {};
    const headers = {};
    if (config.body !== undefined) {
      headers['Content-Type'] = 'application/json; charset=utf-8';
    }
    if (config.auth !== false && state.token) {
      headers.Authorization = `Bearer ${state.token}`;
    }

    let response;
    try {
      response = await fetch(path, {
        method: config.method || 'GET',
        headers,
        body: config.body !== undefined ? JSON.stringify(config.body) : undefined,
        cache: 'no-store',
        credentials: 'same-origin'
      });
    } catch (networkErr) {
      throw new ApiError('无法连接服务器，请检查网络连接或服务端是否正在运行', 0, 'NETWORK_ERROR');
    }

    const raw = await response.text();
    let payload = null;
    if (raw) {
      try {
        payload = JSON.parse(raw);
      } catch (parseErr) {
        payload = null;
      }
    }

    const failed = !response.ok || (payload && payload.ok === false);
    if (failed) {
      const message =
        payload && payload.error
          ? payload.error
          : `请求失败（HTTP ${response.status}）`;
      const error = new ApiError(message, response.status, payload ? payload.code : undefined);
      if (response.status === 401 && config.auth !== false) {
        handleUnauthorized();
      }
      throw error;
    }
    return payload || { ok: true };
  }

  /* ------------------------------ 登录态 / 路由 ------------------------------ */

  function hasPerm(key) {
    if (!state.me) {
      return false;
    }
    if (state.me.role === 'super') {
      return true;
    }
    if (!state.catalog) {
      return false;
    }
    for (const group of state.catalog.groups) {
      for (const item of group.items) {
        if (item.key === key) {
          return (state.me.perms & item.bit) === item.bit;
        }
      }
    }
    return false;
  }

  function canManageUsers() {
    return (
      hasPerm('USER_CREATE') ||
      hasPerm('USER_EDIT') ||
      hasPerm('USER_DELETE') ||
      hasPerm('USER_RESET_PWD')
    );
  }

  function setAuthStatus(mode, message) {
    const dot = $('authConn');
    dot.className = 'status-dot' + (mode === 'online' ? ' online' : mode === 'offline' ? ' offline' : '');
    $('authConnText').textContent = message;
  }

  function handleUnauthorized() {
    state.token = '';
    state.me = null;
    try {
      sessionStorage.removeItem(TOKEN_KEY);
    } catch (storageErr) {
      console.warn('无法清理本地登录状态：', storageErr);
    }
    stopPolling();
    clearMediaToken();
    showAuth('login');
    setAuthStatus('offline', '登录状态已失效，请重新登录');
  }

  function showAuth(mode) {
    state.authMode = mode;
    toggleSidebar(false);
    $('appView').hidden = true;
    $('authView').hidden = false;
    $('loginPane').hidden = mode !== 'login';
    $('setupPane').hidden = mode !== 'setup';
    $('loginError').hidden = true;
    $('setupError').hidden = true;
    const first = mode === 'setup' ? $('setupUser') : $('loginUser');
    window.setTimeout(() => first.focus(), 60);
  }

  function showFormError(id, message) {
    const node = $(id);
    if (!message) {
      node.hidden = true;
      node.textContent = '';
      return;
    }
    node.textContent = message;
    node.hidden = false;
  }

  function setConnState(online) {
    const node = $('connState');
    clear(node);
    node.append(create('span', 'status-dot ' + (online ? 'online' : 'offline')), document.createTextNode(online ? '已连接' : '连接中断'));
  }

  function stopPolling() {
    if (state.pollTimer) {
      window.clearInterval(state.pollTimer);
      state.pollTimer = null;
    }
  }

  function startPolling(task, intervalMs) {
    stopPolling();
    state.pollTimer = window.setInterval(() => {
      if (!$('modalRoot').hidden) {
        return;
      }
      task().catch((err) => {
        if (err && err.status === 401) {
          return;
        }
        console.warn('轮询刷新失败：', err && err.message);
        setConnState(false);
      });
    }, intervalMs);
  }

  function navigate() {
    const raw = (window.location.hash || '#/dashboard').replace(/^#\/?/, '').split('?')[0];
    let target = Object.prototype.hasOwnProperty.call(VIEW_META, raw) ? raw : 'dashboard';
    // 未知路由统一回落到仪表盘，并把地址栏同步为规范形式，避免 URL 与内容不一致
    if (raw && target === 'dashboard' && raw !== 'dashboard') {
      window.location.replace('#/dashboard');
      return;
    }
    if (target === 'docker' && !hasPerm('DOCKER_VIEW')) {
      toast('没有权限查看容器列表', 'warn');
      target = 'dashboard';
    }
    if (target === 'users' && !canManageUsers()) {
      toast('没有权限管理用户', 'warn');
      target = 'dashboard';
    }
    if (target === 'files' && !hasPerm('FILE_READ')) {
      toast('没有权限浏览文件', 'warn');
      target = 'dashboard';
    }
    if (target === 'shares' && !hasPerm('FILE_SHARE')) {
      toast('没有权限管理分享链接', 'warn');
      target = 'dashboard';
    }
    if (target === 'audit' && !hasPerm('SYSTEM_AUDIT_VIEW')) {
      toast('没有权限查看审计日志', 'warn');
      target = 'dashboard';
    }
    setView(target);
  }

  function setView(view) {
    state.view = view;
    // 离开仪表盘时自动退出全屏，避免其它页面没有侧栏 / 顶栏无法导航
    if (view !== 'dashboard' && isDashboardFullscreen()) {
      toggleDashboardFullscreen();
    }
    toggleSidebar(false);
    setUserMenu(false);
    const items = document.querySelectorAll('.nav-item');
    for (const item of items) {
      item.classList.toggle('active', item.dataset.view === view);
    }
    const sections = document.querySelectorAll('.view');
    for (const section of sections) {
      section.hidden = section.id !== `view-${view}`;
    }
    const meta = VIEW_META[view];
    $('pageTitle').textContent = meta.title;
    $('pageSubtitle').textContent = meta.subtitle;
    renderCrumbs(view);

    stopPolling();
    if (view === 'dashboard') {
      refreshDashboard().catch(handleViewError);
      startPolling(refreshDashboard, 5000);
    } else if (view === 'users') {
      loadUsers().catch(handleViewError);
    }
    const hook = viewHooks[view];
    if (hook && typeof hook.mount === 'function') {
      try {
        const result = hook.mount();
        if (result && typeof result.catch === 'function') {
          result.catch(handleViewError);
        }
      } catch (err) {
        handleViewError(err);
      }
    }
  }

  function handleViewError(err) {
    if (!err || err.status === 401) {
      return;
    }
    setConnState(false);
    toast(err.message || '数据加载失败', 'error');
  }

  function renderIdentity() {
    const username = state.me ? state.me.username : '-';
    const roleText = state.roleLabel || (state.me ? state.me.role : '-');
    const initial = username.slice(0, 1).toUpperCase();
    const setText = (id, value) => {
      const node = $(id);
      if (node) {
        node.textContent = value;
      }
    };
    setText('userAvatar', initial);
    setText('topbarAvatar', initial);
    setText('userMenuAvatar', initial);
    setText('sideUsername', username);
    setText('userMenuName', username);
    setText('userMenuFullName', username);
    setText('sideRole', roleText);
    setText('userMenuRole', roleText);
  }

  function applyNavPermissions() {
    $('navFiles').classList.toggle('disabled', !hasPerm('FILE_READ'));
    $('navShares').classList.toggle('disabled', !hasPerm('FILE_SHARE'));
    $('navDocker').classList.toggle('disabled', !hasPerm('DOCKER_VIEW'));
    $('navUsers').classList.toggle('disabled', !canManageUsers());
    $('navAudit').classList.toggle('disabled', !hasPerm('SYSTEM_AUDIT_VIEW'));
    $('navSettings').classList.remove('disabled');
    $('refreshBtn').hidden = false;

    // 快捷入口按权限禁用（仅视觉与可达性，不改动链接地址）
    const quick = {
      quickFiles: hasPerm('FILE_READ'),
      quickShares: hasPerm('FILE_SHARE'),
      quickDocker: hasPerm('DOCKER_VIEW'),
      quickUsers: canManageUsers()
    };
    for (const [id, allowed] of Object.entries(quick)) {
      const node = $(id);
      if (!node) {
        continue;
      }
      node.classList.toggle('is-disabled', !allowed);
      node.setAttribute('aria-disabled', allowed ? 'false' : 'true');
      if (allowed) {
        node.removeAttribute('tabindex');
      } else {
        node.setAttribute('tabindex', '-1');
      }
    }
  }

  /** 把登录令牌同步给 Service Worker，供预览/下载请求注入 Authorization 头 */
  function syncMediaToken() {
    if (window.FnosMedia && typeof window.FnosMedia.setToken === 'function') {
      try {
        window.FnosMedia.setToken(state.token);
      } catch (err) {
        console.warn('同步媒体预览令牌失败：', err && err.message);
      }
    }
  }

  /** 退出登录或令牌失效时清空 Service Worker 中缓存的令牌 */
  function clearMediaToken() {
    if (window.FnosMedia && typeof window.FnosMedia.clear === 'function') {
      try {
        window.FnosMedia.clear();
      } catch (err) {
        console.warn('清理媒体预览令牌失败：', err && err.message);
      }
    }
  }

  async function enterApp(payload) {
    state.me = payload.user || payload;
    state.roleLabel = payload.roleLabel || (state.me ? state.me.role : '');
    $('authView').hidden = true;
    $('appView').hidden = false;
    renderIdentity();

    try {
      state.catalog = await api('/api/users/permissions');
    } catch (err) {
      state.catalog = null;
      if (err.status !== 401) {
        toast(`权限目录加载失败：${err.message}`, 'warn');
      }
    }

    applyNavPermissions();
    syncMediaToken();
    setConnState(true);
    if (!window.location.hash) {
      window.location.hash = '#/dashboard';
    }
    navigate();
    checkFirstRunStorage();
  }

  /**
   * 首次使用引导：没有任何挂载点时，提醒用户先给应用授权文件夹权限，
   * 再到「系统设置 → 存储挂载」把 NAS 目录挂载进来（每个会话只提示一次）。
   */
  const STORAGE_TIP_KEY = 'fnos_storage_tip_shown';

  async function checkFirstRunStorage() {
    let data = null;
    try {
      data = await api('/api/storage/mounts');
    } catch (err) {
      return;
    }
    if (!data || !data.needsSetup) {
      return;
    }
    try {
      if (window.sessionStorage.getItem(STORAGE_TIP_KEY) === '1') {
        return;
      }
      window.sessionStorage.setItem(STORAGE_TIP_KEY, '1');
    } catch (storageErr) {
      // 隐私模式下无法写入也照常提示
    }

    const body = create('div', 'setup-guide');
    body.appendChild(
      create(
        'p',
        'modal-desc',
        '文件管理目前没有可访问的目录。飞牛终端运行在独立的应用账号下，需要先授权文件夹权限，再把 NAS 目录挂载进来。'
      )
    );
    const steps = create('ol', 'setup-steps');
    const lines = [
      '在飞牛 NAS 桌面打开「设置 → 应用 → 飞牛终端 → 文件夹权限」；',
      '把要使用的共享文件夹（例如 /vol1/1000）加入允许列表并授予「读写」权限；',
      '回到本页面点「去挂载目录」，用「自动挂载 /vol1」或手动填写路径添加挂载点。'
    ];
    for (const line of lines) {
      steps.appendChild(create('li', null, line));
    }
    body.appendChild(steps);
    body.appendChild(
      create(
        'p',
        'modal-desc',
        '授权完成后可在「系统设置 → 存储挂载」随时增删挂载点；只读目录可以勾选「只读挂载」。'
      )
    );

    openModal({
      title: '首次使用：请先授权文件夹权限',
      body,
      buttons: [
        {
          label: '去挂载目录',
          kind: 'btn-primary',
          onClick: ({ close }) => {
            close();
            window.location.hash = '#/settings';
            window.setTimeout(() => {
              const panel = $('storageMountPanel');
              if (panel && panel.scrollIntoView) {
                panel.scrollIntoView({ behavior: 'smooth', block: 'start' });
              }
            }, 260);
          }
        },
        { label: '稍后再说' }
      ]
    });
  }

  /* ------------------------------ 仪表盘 ------------------------------ */

  /* 仪表盘全屏显示：整页进入全屏并隐藏侧栏 / 顶栏，按 Esc 或再次点击按钮退出。 */

  function isDashboardFullscreen() {
    return document.body.classList.contains('dash-fullscreen');
  }

  function syncDashboardFullscreen(on) {
    document.body.classList.toggle('dash-fullscreen', !!on);
    const text = $('dashFullscreenText');
    const button = $('dashFullscreenBtn');
    if (text) {
      text.textContent = on ? '退出全屏' : '全屏显示';
    }
    if (button) {
      button.setAttribute('title', on ? '退出全屏（Esc）' : '全屏显示仪表盘');
      button.setAttribute('aria-pressed', on ? 'true' : 'false');
      const use = button.querySelector('use');
      if (use) {
        use.setAttribute('href', on ? '#i-collapse' : '#i-expand');
      }
    }
  }

  /** 真实全屏是否成功进入（浏览器拒绝时退化为纯样式大屏，不需要回收） */
  let dashFullscreenEngaged = false;
  let dashFullscreenWatcher = 0;

  /**
   * 兜底监听：真实全屏被 Esc / 系统手势 / 其他页面退出后，个别环境不会派发
   * fullscreenchange，样式会残留导致侧栏、顶栏一直隐藏。这里每 400ms 核对一次。
   */
  function watchRealFullscreenExit() {
    if (dashFullscreenWatcher) {
      window.clearInterval(dashFullscreenWatcher);
    }
    dashFullscreenWatcher = window.setInterval(() => {
      if (!dashFullscreenEngaged || !isDashboardFullscreen()) {
        window.clearInterval(dashFullscreenWatcher);
        dashFullscreenWatcher = 0;
        return;
      }
      if (!document.fullscreenElement && !document.webkitFullscreenElement) {
        dashFullscreenEngaged = false;
        window.clearInterval(dashFullscreenWatcher);
        dashFullscreenWatcher = 0;
        syncDashboardFullscreen(false);
      }
    }, 400);
  }

  function toggleDashboardFullscreen() {
    if (isDashboardFullscreen()) {
      dashFullscreenEngaged = false;
      if (dashFullscreenWatcher) {
        window.clearInterval(dashFullscreenWatcher);
        dashFullscreenWatcher = 0;
      }
      if (document.fullscreenElement && typeof document.exitFullscreen === 'function') {
        document.exitFullscreen().catch(() => {});
      } else if (document.webkitFullscreenElement && typeof document.webkitExitFullscreen === 'function') {
        document.webkitExitFullscreen();
      }
      syncDashboardFullscreen(false);
      return;
    }
    const root = document.documentElement;
    if (root.requestFullscreen) {
      root.requestFullscreen({ navigationUI: 'hide' }).then(() => {
        dashFullscreenEngaged = true;
        watchRealFullscreenExit();
      }).catch(() => {
        // 浏览器拒绝进入全屏时仍然使用纯样式全屏，功能不受影响。
        dashFullscreenEngaged = false;
      });
    } else if (root.webkitRequestFullscreen) {
      try {
        root.webkitRequestFullscreen();
        dashFullscreenEngaged = true;
        watchRealFullscreenExit();
      } catch (err) {
        dashFullscreenEngaged = false;
      }
    } else {
      toast('当前浏览器不支持全屏显示，已切换为大屏视图', 'warn');
    }
    syncDashboardFullscreen(true);
    refreshDashboard().catch(() => {});
  }

  let lastCpuUsage = 0;
  let lastMemUsage = 0;

  function setRing(node, value) {
    node.style.setProperty('--p', String(Math.max(0, Math.min(100, value))));
  }

  function renderSystem(info) {
    lastCpuUsage = info.cpuUsage;
    lastMemUsage = info.memUsage;

    setRing($('cpuRing'), info.cpuUsage);
    $('cpuValue').textContent = info.cpuUsage.toFixed(1).replace(/\.0$/, '');
    $('cpuModel').textContent = info.cpuModel;
    $('cpuCores').textContent = `${info.cpuCores} 个逻辑核心`;
    const cpuBar = $('cpuBar');
    if (cpuBar) {
      cpuBar.style.width = `${Math.max(0, Math.min(100, info.cpuUsage))}%`;
    }

    setRing($('memRing'), info.memUsage);
    $('memValue').textContent = info.memUsage.toFixed(1).replace(/\.0$/, '');
    $('memTag').textContent = `${formatBytes(info.memTotal)}`;
    $('memUsed').textContent = `已用 ${formatBytes(info.memUsed)}`;
    $('memTotal').textContent = `空闲 ${formatBytes(info.memFree)}`;
    const memBar = $('memBar');
    if (memBar) {
      memBar.style.width = `${Math.max(0, Math.min(100, info.memUsage))}%`;
      memBar.classList.toggle('warn', info.memUsage >= 90);
    }

    $('uptimeText').textContent = formatDuration(info.uptime);
    $('bootTime').textContent = `服务已运行 ${formatDuration(info.processUptime)}`;
    $('hostnameText').textContent = info.hostname;
    $('archText').textContent = `${info.arch} · ${info.release}`;
    $('platformTag').textContent = info.platform;

    const list = $('sysInfoList');
    clear(list);
    const rows = [
      ['运行平台', `${info.platform} ${info.release}`],
      ['系统架构', info.arch],
      ['处理器', info.cpuModel],
      ['逻辑核心', `${info.cpuCores} 核`],
      ['平均负载', (info.loadavg || []).map((v) => Number(v).toFixed(2)).join(' / ')],
      ['Node 版本', info.nodeVersion],
      ['服务版本', `v${info.serverVersion}`],
      ['服务端口', window.location.port || '443'],
      ['数据目录', info.dataDir],
      ['文件目录', info.filesRoot]
    ];
    for (const [label, value] of rows) {
      const row = document.createElement('div');
      row.appendChild(create('dt', null, label));
      row.appendChild(create('dd', null, String(value)));
      list.appendChild(row);
    }
  }

  function renderStorage(info) {
    const usage = Number(info.usagePercent) || 0;
    const percent = Math.max(0, Math.min(100, usage));
    // 磁盘 KPI：环形、数值与进度条共用同一百分比
    setRing($('diskRing'), percent);
    const diskValue = $('diskValue');
    if (diskValue) {
      diskValue.textContent = usage.toFixed(1).replace(/\.0$/, '');
    }
    const diskRing = $('diskRing');
    if (diskRing) {
      diskRing.classList.toggle('accent-danger', usage >= 90);
      diskRing.classList.toggle('accent-warn', usage < 90);
    }
    $('storageTag').textContent = `${usage.toFixed(1)}% 已用`;
    const fill = $('storageFill');
    fill.style.width = `${percent}%`;
    fill.classList.toggle('warn', usage >= 85);
    $('storageUsed').textContent = formatBytes(info.used);
    $('storageFree').textContent = formatBytes(info.free);
    const pathNode = $('storagePath');
    pathNode.textContent = `容量 ${formatBytes(info.total)} · 根目录 ${info.root}`;
    pathNode.title = `${info.root}（总容量 ${formatBytes(info.total)}）`;
  }

  async function refreshDashboard() {
    const results = await Promise.allSettled([
      api('/api/system/info'),
      api('/api/files/storage'),
      api('/api/docker/containers'),
      api('/api/ping', { auth: false })
    ]);

    const [sys, storage, docker, ping] = results;
    if (sys.status === 'fulfilled') {
      renderSystem(sys.value);
    }
    if (storage.status === 'fulfilled') {
      renderStorage(storage.value);
    } else if (storage.reason && storage.reason.status === 403) {
      $('storageTag').textContent = '无权限';
      $('storageUsed').textContent = '-';
      $('storageFree').textContent = '-';
      $('storagePath').textContent = '当前账号没有查看存储的权限';
      $('storagePath').title = '';
      setRing($('diskRing'), 0);
    }
    if (docker.status === 'fulfilled') {
      $('dockerCount').textContent = String(docker.value.total);
      $('dockerRunning').textContent = String(docker.value.running);
      setConnState(true);
    } else {
      $('dockerCount').textContent = '-';
      $('dockerRunning').textContent = '-';
      if (docker.reason && docker.reason.status !== 401) {
        setConnState(false);
      }
    }
    if (ping.status === 'fulfilled') {
      const data = ping.value || {};
      const code = $('certFingerprint');
      const meta = $('certMeta');
      if (code) {
        code.textContent = data.fingerprintPretty || data.fingerprint || '未启用 TLS';
      }
      if (meta) {
        meta.textContent = `${data.tls || 'TLS'} · 端口 ${window.location.port || '443'}`;
      }
    }
  }

  /* ------------------------------ Docker ------------------------------ */

  const STATE_LABELS = {
    running: '运行中',
    exited: '已停止',
    created: '已创建',
    paused: '已暂停',
    restarting: '重启中',
    dead: '异常',
    removing: '删除中'
  };

  function stateBadge(stateName) {
    const label = STATE_LABELS[stateName] || stateName || '未知';
    let cls = 'badge badge-off';
    if (stateName === 'running') {
      cls = 'badge badge-on';
    } else if (stateName === 'paused' || stateName === 'restarting') {
      cls = 'badge badge-warn';
    } else if (stateName === 'dead') {
      cls = 'badge badge-danger';
    }
    return create('span', cls, label);
  }

  async function dockerAction(id, action, button, label) {
    button.disabled = true;
    button.setAttribute('aria-busy', 'true');
    try {
      await api(`/api/docker/${encodeURIComponent(id)}/${action}`, { method: 'POST' });
      toast(`${label}成功`, 'success');
      await loadDocker();
    } catch (err) {
      toast(err.message, 'error');
    } finally {
      button.disabled = false;
      button.removeAttribute('aria-busy');
    }
  }

  function renderDocker(data) {
    state.dockerItems = data.items || [];
    $('dockerSummary').textContent = `${data.total} 个容器 · ${data.running} 个运行中`;
    const body = $('dockerBody');
    clear(body);

    if (state.dockerItems.length === 0) {
      const row = document.createElement('tr');
      const cell = create('td', 'empty', '当前没有容器');
      cell.colSpan = 5;
      row.appendChild(cell);
      body.appendChild(row);
      return;
    }

    for (const item of state.dockerItems) {
      const row = document.createElement('tr');
      row.dataset.id = item.id;

      const nameCell = document.createElement('td');
      nameCell.dataset.label = '容器';
      nameCell.appendChild(create('div', 'cell-main', item.name || item.shortId));
      nameCell.appendChild(create('div', 'cell-sub', item.shortId));
      row.appendChild(nameCell);

      const imageCell = create('td', 'cell-mono', item.image || '-');
      imageCell.dataset.label = '镜像';
      row.appendChild(imageCell);

      const stateCell = document.createElement('td');
      stateCell.dataset.label = '状态';
      stateCell.appendChild(stateBadge(item.state));
      if (item.status) {
        stateCell.appendChild(create('div', 'cell-sub', item.status));
      }
      row.appendChild(stateCell);

      const portCell = create('td', 'cell-mono', item.portText || '无端口映射');
      portCell.dataset.label = '端口映射';
      row.appendChild(portCell);

      const actionCell = document.createElement('td');
      actionCell.className = 'col-actions';
      actionCell.dataset.label = '操作';
      const group = create('div', 'action-group');

      const running = item.state === 'running';
      if (running && hasPerm('DOCKER_STOP')) {
        const stopBtn = create('button', 'btn btn-warn btn-sm', '停止');
        stopBtn.type = 'button';
        stopBtn.addEventListener('click', () => dockerAction(item.id, 'stop', stopBtn, '停止容器'));
        group.appendChild(stopBtn);
      }
      if (!running && hasPerm('DOCKER_START')) {
        const startBtn = create('button', 'btn btn-primary btn-sm', '启动');
        startBtn.type = 'button';
        startBtn.addEventListener('click', () => dockerAction(item.id, 'start', startBtn, '启动容器'));
        group.appendChild(startBtn);
      }
      if (running && hasPerm('DOCKER_START')) {
        const restartBtn = create('button', 'btn btn-ghost btn-sm', '重启');
        restartBtn.type = 'button';
        restartBtn.addEventListener('click', () => dockerAction(item.id, 'restart', restartBtn, '重启容器'));
        group.appendChild(restartBtn);
      }
      if (!group.firstChild) {
        group.appendChild(create('span', 'muted', '无可用操作'));
      }
      actionCell.appendChild(group);
      row.appendChild(actionCell);
      body.appendChild(row);
    }
  }

  async function loadDocker() {
    try {
      const data = await api('/api/docker/containers');
      $('dockerError').hidden = true;
      renderDocker(data);
      setConnState(true);
    } catch (err) {
      if (err.status === 401) {
        throw err;
      }
      $('dockerError').hidden = false;
      $('dockerErrorText').textContent = err.message;
      $('dockerSummary').textContent = '不可用';
      const body = $('dockerBody');
      clear(body);
      const row = document.createElement('tr');
      const cell = create('td', 'empty', '无法获取容器列表');
      cell.colSpan = 5;
      row.appendChild(cell);
      body.appendChild(row);
      setConnState(false);
    }
  }

  /* ------------------------------ 权限勾选网格 ------------------------------ */

  function countBits(value) {
    let count = 0;
    let bits = Number(value) || 0;
    while (bits > 0) {
      if (bits & 1) {
        count += 1;
      }
      bits >>>= 1;
    }
    return count;
  }

  function createPermGrid(initialPerms, options) {
    const config = options || {};
    const locked = Boolean(config.locked);
    const root = document.createElement('div');

    const toolbar = document.createElement('div');
    toolbar.className = 'perm-toolbar';
    const heading = create('h4', null, '权限位（24 位掩码，当前定义 21 位）');
    const tools = create('div', 'action-group');
    const countLabel = create('span', 'perm-count', '');
    const allBtn = create('button', 'btn btn-ghost btn-sm', '全选');
    const noneBtn = create('button', 'btn btn-ghost btn-sm', '清空');
    allBtn.type = 'button';
    noneBtn.type = 'button';
    allBtn.disabled = locked;
    noneBtn.disabled = locked;
    tools.append(countLabel, allBtn, noneBtn);
    toolbar.append(heading, tools);
    root.appendChild(toolbar);

    const scroll = create('div', 'perm-scroll');
    const inputs = [];
    const groups = [];

    for (const group of state.catalog.groups) {
      const box = create('div', 'perm-group');
      const head = create('div', 'perm-group-head');
      head.appendChild(create('span', null, group.label));
      const toggle = create('button', null, '全选');
      toggle.type = 'button';
      toggle.disabled = locked;
      head.appendChild(toggle);
      const items = create('div', 'perm-items');
      const groupInputs = [];

      for (const item of group.items) {
        const label = create('label', 'perm-item' + (locked ? ' locked' : ''));
        const input = document.createElement('input');
        input.type = 'checkbox';
        input.dataset.bit = String(item.bit);
        input.checked = (Number(initialPerms) & item.bit) === item.bit;
        input.disabled = locked;
        const text = create('span', null, item.label);
        label.append(input, text);
        label.classList.toggle('checked', input.checked);
        input.addEventListener('change', () => {
          label.classList.toggle('checked', input.checked);
          sync();
        });
        items.appendChild(label);
        inputs.push(input);
        groupInputs.push(input);
      }

      toggle.addEventListener('click', () => {
        const allOn = groupInputs.every((input) => input.checked);
        for (const input of groupInputs) {
          input.checked = !allOn;
          input.dispatchEvent(new Event('change'));
        }
      });

      box.append(head, items);
      scroll.appendChild(box);
      groups.push({ toggle, inputs: groupInputs });
    }

    root.appendChild(scroll);

    function sync() {
      let checked = 0;
      for (const input of inputs) {
        if (input.checked) {
          checked += 1;
        }
      }
      countLabel.textContent = `已选 ${checked} / ${inputs.length} 位`;
      for (const group of groups) {
        const allOn = group.inputs.every((input) => input.checked);
        group.toggle.textContent = allOn ? '清空' : '全选';
      }
    }

    allBtn.addEventListener('click', () => {
      for (const input of inputs) {
        input.checked = true;
        input.dispatchEvent(new Event('change'));
      }
    });

    noneBtn.addEventListener('click', () => {
      for (const input of inputs) {
        input.checked = false;
        input.dispatchEvent(new Event('change'));
      }
    });

    function getValue() {
      let value = 0;
      for (const input of inputs) {
        if (input.checked) {
          value |= Number(input.dataset.bit);
        }
      }
      return value;
    }

    function setValue(value) {
      for (const input of inputs) {
        input.checked = (Number(value) & Number(input.dataset.bit)) === Number(input.dataset.bit);
        input.dispatchEvent(new Event('change'));
      }
    }

    sync();
    return { node: root, getValue, setValue };
  }

  /* ------------------------------ 用户管理 ------------------------------ */

  function roleBadge(role, roleLabel) {
    if (role === 'super') {
      return create('span', 'badge badge-accent', roleLabel || '超级管理员');
    }
    if (role === 'admin') {
      return create('span', 'badge badge-warn', roleLabel || '管理员');
    }
    return create('span', 'badge badge-off', roleLabel || '普通用户');
  }

  /* 用户列表客户端分页：每页 10 条 */
  const USER_PAGE_SIZE = 10;

  function renderUserPager(totalPages) {
    const pager = $('userPager');
    if (!pager) {
      return;
    }
    clear(pager);
    if (!totalPages || totalPages <= 1) {
      pager.hidden = true;
      return;
    }
    pager.hidden = false;

    const go = (page) => {
      state.userPage = page;
      renderUsers({ items: state.userItems, total: state.userItems.length });
    };

    const makeBtn = (text, page, extra, disabled) => {
      const btn = create('button', `page-btn${extra ? ` ${extra}` : ''}`, text);
      btn.type = 'button';
      if (disabled) {
        btn.disabled = true;
      } else {
        btn.addEventListener('click', () => go(page));
      }
      return btn;
    };

    pager.appendChild(makeBtn('上一页', state.userPage - 1, '', state.userPage <= 1));

    const pages = [];
    for (let i = 1; i <= totalPages; i += 1) {
      if (i === 1 || i === totalPages || Math.abs(i - state.userPage) <= 1) {
        pages.push(i);
      }
    }
    let lastPage = 0;
    for (const page of pages) {
      if (lastPage && page - lastPage > 1) {
        pager.appendChild(create('span', 'page-info', '…'));
      }
      pager.appendChild(makeBtn(String(page), page, page === state.userPage ? 'active' : '', false));
      lastPage = page;
    }

    pager.appendChild(makeBtn('下一页', state.userPage + 1, '', state.userPage >= totalPages));
    pager.appendChild(create('span', 'page-info', `第 ${state.userPage} / ${totalPages} 页`));
  }

  function renderUsers(data) {
    state.userItems = data.items || [];
    $('userSummary').textContent = `共 ${data.total} 个账号`;
    const body = $('userBody');
    clear(body);

    if (state.userItems.length === 0) {
      const row = document.createElement('tr');
      const cell = create('td', 'empty', '还没有任何账号');
      cell.colSpan = 6;
      row.appendChild(cell);
      body.appendChild(row);
      renderUserPager(0);
      return;
    }

    const totalPages = Math.max(1, Math.ceil(state.userItems.length / USER_PAGE_SIZE));
    state.userPage = Math.min(Math.max(1, state.userPage), totalPages);
    const start = (state.userPage - 1) * USER_PAGE_SIZE;
    const pageItems = state.userItems.slice(start, start + USER_PAGE_SIZE);

    const totalBits = state.catalog ? state.catalog.groups.reduce((sum, g) => sum + g.items.length, 0) : 21;

    for (const user of pageItems) {
      const row = document.createElement('tr');

      const nameCell = document.createElement('td');
      nameCell.dataset.label = '用户名';
      nameCell.appendChild(create('div', 'cell-main', user.username));
      nameCell.appendChild(create('div', 'cell-sub', `ID ${user.id}`));
      row.appendChild(nameCell);

      const roleCell = document.createElement('td');
      roleCell.dataset.label = '角色';
      roleCell.appendChild(roleBadge(user.role, user.roleLabel));
      row.appendChild(roleCell);

      const stateCell = document.createElement('td');
      stateCell.dataset.label = '状态';
      stateCell.appendChild(
        user.disabled ? create('span', 'badge badge-danger', '已禁用') : create('span', 'badge badge-on', '正常')
      );
      row.appendChild(stateCell);

      const permCell = document.createElement('td');
      permCell.dataset.label = '权限位';
      const permText = user.role === 'super' ? `全部 ${totalBits} 位` : `${countBits(user.perms)} / ${totalBits} 位`;
      permCell.appendChild(create('div', 'perm-count', permText));
      permCell.appendChild(create('div', 'cell-sub', `0x${Number(user.perms).toString(16).toUpperCase()}`));
      row.appendChild(permCell);

      const createdCell = create('td', 'cell-mono', formatDateTime(user.createdAt));
      createdCell.dataset.label = '创建时间';
      row.appendChild(createdCell);

      const actionCell = document.createElement('td');
      actionCell.className = 'col-actions';
      actionCell.dataset.label = '操作';
      const group = create('div', 'action-group');

      if (user.role === 'super') {
        group.appendChild(create('span', 'muted', '受保护'));
      } else {
        if (hasPerm('USER_EDIT')) {
          const editBtn = create('button', 'btn btn-ghost btn-sm', '编辑');
          editBtn.type = 'button';
          editBtn.addEventListener('click', () => openUserEditor(user));
          group.appendChild(editBtn);
        }
        if (hasPerm('USER_RESET_PWD')) {
          const pwdBtn = create('button', 'btn btn-ghost btn-sm', '重置密码');
          pwdBtn.type = 'button';
          pwdBtn.addEventListener('click', () => openResetPassword(user));
          group.appendChild(pwdBtn);
        }
        if (hasPerm('USER_DELETE')) {
          const delBtn = create('button', 'btn btn-danger btn-sm', '删除');
          delBtn.type = 'button';
          delBtn.addEventListener('click', () => confirmDeleteUser(user));
          group.appendChild(delBtn);
        }
        if (!group.firstChild) {
          group.appendChild(create('span', 'muted', '无可用操作'));
        }
      }
      actionCell.appendChild(group);
      row.appendChild(actionCell);
      body.appendChild(row);
    }

    renderUserPager(totalPages);
  }

  async function loadUsers() {
    const data = await api('/api/users');
    renderUsers(data);
    setConnState(true);
  }

  const USERNAME_PATTERN = /^[A-Za-z0-9_.-]{3,32}$/;

  function validatePassword(value) {
    if (typeof value !== 'string' || value.length < 8) {
      return '密码长度至少 8 位';
    }
    if (value.length > 128) {
      return '密码长度不能超过 128 位';
    }
    if (!/[A-Za-z]/.test(value) || !/[0-9]/.test(value)) {
      return '密码必须同时包含字母和数字';
    }
    return null;
  }

  function buildField(labelText, input, hint) {
    const label = document.createElement('label');
    label.className = 'field';
    label.appendChild(create('span', null, labelText));
    label.appendChild(input);
    if (hint) {
      label.appendChild(create('div', 'field-hint', hint));
    }
    return label;
  }

  function openUserEditor(user) {
    if (!state.catalog) {
      toast('权限目录尚未加载，请刷新页面后重试', 'error');
      return;
    }
    const isNew = !user;
    const isSelf = Boolean(user && state.me && user.id === state.me.id);
    const canChangeRole = !isSelf || (state.me && state.me.role === 'super');

    const form = create('div');
    const row = create('div', 'field-row');

    const usernameInput = document.createElement('input');
    usernameInput.type = 'text';
    usernameInput.maxLength = 32;
    usernameInput.placeholder = '3~32 位字母、数字、_ . -';
    usernameInput.value = isNew ? '' : user.username;
    row.appendChild(buildField('用户名', usernameInput));

    let passwordInput = null;
    if (isNew) {
      passwordInput = document.createElement('input');
      passwordInput.type = 'password';
      passwordInput.maxLength = 128;
      passwordInput.autocomplete = 'new-password';
      passwordInput.placeholder = '至少 8 位，含字母和数字';
      row.appendChild(buildField('初始密码', passwordInput));
    }
    form.appendChild(row);

    const roleSelect = document.createElement('select');
    for (const role of state.catalog.roles) {
      if (role.key === 'super') {
        continue;
      }
      const option = document.createElement('option');
      option.value = role.key;
      option.textContent = role.label;
      roleSelect.appendChild(option);
    }
    roleSelect.value = isNew ? 'user' : user.role === 'admin' ? 'admin' : 'user';
    roleSelect.disabled = !canChangeRole;
    form.appendChild(
      buildField(
        '角色',
        roleSelect,
        canChangeRole ? '管理员可管理用户，普通用户仅能使用被授权的功能' : '不能修改自己的角色'
      )
    );

    let disabledInput = null;
    if (!isNew) {
      const toggleLabel = create('label', 'perm-item' + (isSelf ? ' locked' : ''));
      disabledInput = document.createElement('input');
      disabledInput.type = 'checkbox';
      disabledInput.checked = Boolean(user.disabled);
      disabledInput.disabled = isSelf;
      toggleLabel.append(disabledInput, create('span', null, '禁用该账号（立即失效所有登录令牌）'));
      toggleLabel.classList.toggle('checked', disabledInput.checked);
      disabledInput.addEventListener('change', () => {
        toggleLabel.classList.toggle('checked', disabledInput.checked);
      });
      const holder = create('div', 'perm-group');
      holder.appendChild(toggleLabel);
      form.appendChild(holder);
    }

    const permsLocked = user
      ? user.role === 'super' || (isSelf && !(state.me && state.me.role === 'super'))
      : false;
    const initialPerms = isNew
      ? state.catalog.defaults.user
      : user.perms;
    const grid = createPermGrid(initialPerms, { locked: permsLocked });
    form.appendChild(grid.node);

    if (permsLocked) {
      form.appendChild(create('p', 'field-hint', '当前账号的权限位不可修改（超级管理员固定拥有全部权限，且不能修改自己的权限）。'));
    }

    const doSubmit = async ({ close, setBusy }) => {
      const username = usernameInput.value.trim();
      if (!USERNAME_PATTERN.test(username)) {
        toast('用户名需为 3~32 位字母、数字、下划线、点或短横线', 'error');
        usernameInput.focus();
        return;
      }
      if (passwordInput) {
        const passwordError = validatePassword(passwordInput.value);
        if (passwordError) {
          toast(passwordError, 'error');
          passwordInput.focus();
          return;
        }
      }
      setBusy(true);
      try {
        if (isNew) {
          await api('/api/users', {
            method: 'POST',
            body: {
              username,
              password: passwordInput.value,
              role: roleSelect.value,
              perms: grid.getValue(),
              disabled: false
            }
          });
          toast(`用户 ${username} 创建成功`, 'success');
        } else {
          const payload = { username };
          if (canChangeRole && roleSelect.value !== user.role) {
            payload.role = roleSelect.value;
          }
          if (!permsLocked) {
            payload.perms = grid.getValue();
          }
          if (disabledInput && !isSelf) {
            payload.disabled = disabledInput.checked;
          }
          await api(`/api/users/${user.id}`, { method: 'PUT', body: payload });
          toast(`用户 ${username} 已更新`, 'success');
        }
        close();
        await loadUsers();
      } finally {
        setBusy(false);
      }
    };

    openModal({
      title: isNew ? '新建用户' : `编辑用户 · ${user.username}`,
      wide: true,
      body: form,
      buttons: [
        { label: '取消', kind: 'btn-ghost', onClick: ({ close }) => close() },
        { label: isNew ? '创建用户' : '保存修改', kind: 'btn-primary', onClick: doSubmit }
      ]
    });
  }

  function openResetPassword(user) {
    const form = create('div');
    form.appendChild(
      create('p', 'modal-desc', `将为账号 ${user.username} 设置新密码，该账号所有已登录设备会立即失效。`)
    );

    const pwd1 = document.createElement('input');
    pwd1.type = 'password';
    pwd1.maxLength = 128;
    pwd1.autocomplete = 'new-password';
    pwd1.placeholder = '至少 8 位，含字母和数字';
    form.appendChild(buildField('新密码', pwd1));

    const pwd2 = document.createElement('input');
    pwd2.type = 'password';
    pwd2.maxLength = 128;
    pwd2.autocomplete = 'new-password';
    pwd2.placeholder = '请再次输入新密码';
    form.appendChild(buildField('确认新密码', pwd2));

    const submit = async ({ close, setBusy }) => {
      const passwordError = validatePassword(pwd1.value);
      if (passwordError) {
        toast(passwordError, 'error');
        pwd1.focus();
        return;
      }
      if (pwd1.value !== pwd2.value) {
        toast('两次输入的密码不一致', 'error');
        pwd2.focus();
        return;
      }
      setBusy(true);
      try {
        await api(`/api/users/${user.id}/reset-password`, {
          method: 'POST',
          body: { password: pwd1.value }
        });
        toast(`已重置 ${user.username} 的密码`, 'success');
        close();
      } finally {
        setBusy(false);
      }
    };

    openModal({
      title: '重置密码',
      body: form,
      buttons: [
        { label: '取消', kind: 'btn-ghost', onClick: ({ close }) => close() },
        { label: '确认重置', kind: 'btn-primary', onClick: submit }
      ]
    });
  }

  function confirmDeleteUser(user) {
    const body = create('p', 'modal-desc');
    body.textContent = `确定要删除账号「${user.username}」吗？该操作不可撤销，账号的登录令牌会立即失效。`;

    const submit = async ({ close, setBusy }) => {
      setBusy(true);
      try {
        await api(`/api/users/${user.id}`, { method: 'DELETE' });
        toast(`用户 ${user.username} 已删除`, 'success');
        close();
        await loadUsers();
      } finally {
        setBusy(false);
      }
    };

    openModal({
      title: '删除用户',
      body,
      buttons: [
        { label: '取消', kind: 'btn-ghost', onClick: ({ close }) => close() },
        { label: '确认删除', kind: 'btn-danger', onClick: submit }
      ]
    });
  }

  /* ------------------------------ 事件绑定 ------------------------------ */

  function bindEvents() {
    $('loginForm').addEventListener('submit', async (event) => {
      event.preventDefault();
      showFormError('loginError', '');
      const username = $('loginUser').value.trim();
      const password = $('loginPass').value;
      if (!username || !password) {
        showFormError('loginError', '请输入用户名和密码');
        return;
      }
      const button = $('loginBtn');
      button.disabled = true;
      button.setAttribute('aria-busy', 'true');
      try {
        const data = await api('/api/auth/login', {
          method: 'POST',
          auth: false,
          body: { username, password }
        });
        state.token = data.token;
        try {
          sessionStorage.setItem(TOKEN_KEY, data.token);
        } catch (storageErr) {
          console.warn('无法保存登录状态：', storageErr);
        }
        $('loginPass').value = '';
        setAuthStatus('online', '登录成功，正在进入后台…');
        toast(`欢迎回来，${data.user.username}`, 'success');
        await enterApp(data);
      } catch (err) {
        showFormError('loginError', err.message);
      } finally {
        button.disabled = false;
        button.removeAttribute('aria-busy');
      }
    });

    $('setupForm').addEventListener('submit', async (event) => {
      event.preventDefault();
      showFormError('setupError', '');
      const username = $('setupUser').value.trim();
      const password = $('setupPass').value;
      const confirm = $('setupPass2').value;
      if (!USERNAME_PATTERN.test(username)) {
        showFormError('setupError', '用户名需为 3~32 位字母、数字、下划线、点或短横线');
        return;
      }
      const passwordError = validatePassword(password);
      if (passwordError) {
        showFormError('setupError', passwordError);
        return;
      }
      if (password !== confirm) {
        showFormError('setupError', '两次输入的密码不一致');
        return;
      }
      const button = $('setupBtn');
      button.disabled = true;
      button.setAttribute('aria-busy', 'true');
      try {
        const data = await api('/api/setup/init', {
          method: 'POST',
          auth: false,
          body: { username, password }
        });
        state.token = data.token;
        try {
          sessionStorage.setItem(TOKEN_KEY, data.token);
        } catch (storageErr) {
          console.warn('无法保存登录状态：', storageErr);
        }
        $('setupPass').value = '';
        $('setupPass2').value = '';
        toast('超级管理员创建成功', 'success');
        await enterApp({
          user: data.user,
          roleLabel: '超级管理员',
          permissions: data.permissions
        });
      } catch (err) {
        showFormError('setupError', err.message);
      } finally {
        button.disabled = false;
        button.removeAttribute('aria-busy');
      }
    });

    $('logoutBtn').addEventListener('click', () => {
      doLogout();
    });

    const menuLogout = $('userMenuLogout');
    if (menuLogout) {
      menuLogout.addEventListener('click', () => {
        setUserMenu(false);
        doLogout();
      });
    }

    const collapseBtn = $('sidebarCollapse');
    if (collapseBtn) {
      collapseBtn.addEventListener('click', toggleCollapse);
    }

    const userBtn = $('userMenuBtn');
    if (userBtn) {
      userBtn.addEventListener('click', (event) => {
        event.stopPropagation();
        const menu = $('userMenu');
        setUserMenu(Boolean(menu && menu.hidden));
      });
    }

    // 点击菜单外部或按 Esc 关闭用户菜单
    document.addEventListener('click', (event) => {
      const menu = $('userMenu');
      if (!menu || menu.hidden) {
        return;
      }
      const wrap = menu.closest('.user-menu-wrap');
      if (!wrap || !wrap.contains(event.target)) {
        setUserMenu(false);
      }
    });

    const copyBtn = $('copyFingerprint');
    if (copyBtn) {
      copyBtn.addEventListener('click', async () => {
        const node = $('certFingerprint');
        const text = node ? node.textContent.trim() : '';
        if (!text || text === '-') {
          toast('证书指纹尚未加载完成', 'warn');
          return;
        }
        try {
          await copyText(text);
          toast('证书指纹已复制', 'success');
        } catch (err) {
          toast('复制失败，请手动选择指纹文本', 'error');
        }
      });
    }

    $('refreshBtn').addEventListener('click', () => {
      const button = $('refreshBtn');
      button.disabled = true;
      button.setAttribute('aria-busy', 'true');
      const hook = viewHooks[state.view];
      let task;
      if (hook && typeof hook.refresh === 'function') {
        try {
          task = Promise.resolve(hook.refresh());
        } catch (err) {
          task = Promise.reject(err);
        }
      } else {
        task =
          state.view === 'docker'
            ? loadDocker()
            : state.view === 'users'
              ? loadUsers()
              : refreshDashboard();
      }
      task
        .then(() => toast('数据已刷新', 'success'))
        .catch(handleViewError)
        .finally(() => {
          button.disabled = false;
          button.removeAttribute('aria-busy');
        });
    });

    $('addUserBtn').addEventListener('click', () => openUserEditor(null));

    const dashFullscreenBtn = $('dashFullscreenBtn');
    if (dashFullscreenBtn) {
      dashFullscreenBtn.addEventListener('click', toggleDashboardFullscreen);
    }
    const fullscreenElement = () =>
      'fullscreenElement' in document
        ? document.fullscreenElement
        : document.webkitFullscreenElement || null;
    const exitDashboardFullscreenOnLeave = () => {
      const el = fullscreenElement();
      if (!el) {
        dashFullscreenEngaged = false;
        if (dashFullscreenWatcher) {
          window.clearInterval(dashFullscreenWatcher);
          dashFullscreenWatcher = 0;
        }
      }
      syncDashboardFullscreen(Boolean(el));
    };
    document.addEventListener('fullscreenchange', exitDashboardFullscreenOnLeave);
    document.addEventListener('webkitfullscreenchange', exitDashboardFullscreenOnLeave);
    // 浏览器全屏 API 不可用（例如被 iframe 策略拒绝），或按 Esc 后浏览器未派发
    // fullscreenchange 时，Esc 也能退出大屏视图。
    document.addEventListener('keydown', (event) => {
      if (event.key !== 'Escape' || !isDashboardFullscreen()) {
        return;
      }
      const root = $('modalRoot');
      if (root && !root.hidden) {
        return; // 弹窗打开时优先由弹窗处理 Esc
      }
      if (!fullscreenElement()) {
        syncDashboardFullscreen(false);
        return;
      }
      // 真实全屏下按 Esc 由浏览器退出；部分环境（远程桌面、自动化）退出较慢
      // 或不派发 fullscreenchange，这里轮询兜底，确保大屏样式一定被回收。
      let guard = 0;
      const timer = window.setInterval(() => {
        guard += 1;
        if (!fullscreenElement()) {
          window.clearInterval(timer);
          syncDashboardFullscreen(false);
        } else if (guard > 20) {
          window.clearInterval(timer);
        }
      }, 100);
    });

    $('modalClose').addEventListener('click', closeModal);
    $('modalRoot').addEventListener('click', (event) => {
      const target = event.target;
      if (target instanceof HTMLElement && target.dataset && target.dataset.close === '1') {
        closeModal();
      }
    });

    window.addEventListener('hashchange', navigate);

    const themePickers = document.querySelectorAll('[data-theme-picker]');
    for (const picker of themePickers) {
      picker.addEventListener('change', () => {
        applyTheme(picker.value, document.documentElement.dataset.mode);
      });
    }

    const modeToggles = document.querySelectorAll('[data-mode-toggle]');
    for (const toggle of modeToggles) {
      toggle.addEventListener('click', () => {
        const nextMode = document.documentElement.dataset.mode === 'dark' ? 'light' : 'dark';
        applyTheme(document.documentElement.dataset.theme, nextMode);
      });
    }

    const accentDots = document.querySelectorAll('[data-accent]');
    for (const dot of accentDots) {
      dot.addEventListener('click', () => {
        applyAccent(dot.dataset.accent, true);
      });
    }

    const accentColor = $('accentColor');
    if (accentColor) {
      accentColor.addEventListener('input', () => applyAccent(accentColor.value, true));
      accentColor.addEventListener('change', () => applyAccent(accentColor.value, true));
    }

    const accentReset = $('accentReset');
    if (accentReset) {
      accentReset.addEventListener('click', () => {
        applyAccent('', true);
        toast('已恢复默认强调色', 'success');
      });
    }

    $('menuBtn').addEventListener('click', () => toggleSidebar());
    $('sidebarMask').addEventListener('click', () => toggleSidebar(false));
    window.addEventListener('keydown', (event) => {
      if (event.key !== 'Escape') {
        return;
      }
      setUserMenu(false);
      if ($('appView').classList.contains('sidebar-open')) {
        toggleSidebar(false);
      }
    });
    window.addEventListener('resize', () => {
      // 超过抽屉断点（900px）时自动收起抽屉
      if (window.innerWidth > 900) {
        toggleSidebar(false);
      }
    });
  }

  /* ------------------------------ 启动 ------------------------------ */

  async function boot() {
    loadTheme();
    loadAccent();
    // 恢复侧栏折叠偏好（不写入，避免覆盖用户设置）
    applyCollapse(readPreference(SIDEBAR_KEY) === '1', false);
    setUserMenu(false);
    bindEvents();
    setAuthStatus('', '正在检查服务状态…');
    try {
      const status = await api('/api/setup/status', { auth: false });
      if (!status.initialized) {
        setAuthStatus('offline', '系统尚未初始化，请创建超级管理员');
        showAuth('setup');
        return;
      }
      setAuthStatus('online', `服务正常 · 共有 ${status.userCount} 个账号`);
      if (!state.token) {
        showAuth('login');
        return;
      }
      try {
        const me = await api('/api/auth/me');
        setAuthStatus('online', `已恢复登录状态 · ${me.user.username}`);
        await enterApp(me);
      } catch (err) {
        if (err.status !== 401) {
          showFormError('loginError', err.message);
        }
        showAuth('login');
      }
    } catch (err) {
      setAuthStatus('offline', err.message);
      showAuth('login');
      showFormError('loginError', err.message);
    }
  }

  /**
   * 对外桥接对象：files.js / shares.js 在 app.js 之后加载，通过它复用请求、
   * 弹窗、提示等基础设施，并注册各自视图的挂载与刷新钩子。
   */
  window.FnosAdmin = {
    $,
    state,
    api,
    toast,
    openModal,
    closeModal,
    hasPerm,
    canManageUsers,
    formatBytes,
    formatDateTime,
    create,
    clear,
    handleViewError,
    setConnState,
    registerView
  };

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', boot);
  } else {
    boot();
  }
})();
