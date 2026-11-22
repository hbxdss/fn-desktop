'use strict';

/**
 * Docker 管理视图（容器 / 镜像 / 卷 / 网络）
 * - 依赖 app.js 暴露的 window.FnosAdmin 桥接对象，无任何外部库；
 * - 全部事件在此文件绑定（CSP 禁止内联脚本），不使用 alert/confirm/prompt；
 * - 颜色一律取自 style.css 的 CSS 变量，四个主题 × 明暗模式均可正常显示。
 */
(function () {
  const A = window.FnosAdmin;
  if (!A) {
    console.warn('[Docker] 未找到 FnosAdmin 桥接对象，Docker 视图不可用');
    return;
  }

  const { $, api, toast, openModal, closeModal, hasPerm, formatBytes, create } = A;

  /* ------------------------------ 常量 ------------------------------ */

  const TABS = ['containers', 'images', 'volumes', 'networks'];
  const AUTO_INTERVAL = 10000;
  const STATS_INTERVAL = 3000;
  const LOG_POLL_INTERVAL = 3000;

  const STATE_LABELS = {
    running: '运行中',
    paused: '已暂停',
    restarting: '重启中',
    exited: '已停止',
    created: '已创建',
    dead: '已失效',
    removing: '删除中',
    unknown: '未知'
  };
  const STATE_TONE = {
    running: 'ok',
    paused: 'warn',
    restarting: 'warn',
    exited: 'mute',
    created: 'mute',
    dead: 'danger',
    removing: 'mute',
    unknown: 'mute'
  };
  const RESTART_OPTIONS = [
    { value: 'no', label: 'no（不自动重启）' },
    { value: 'always', label: 'always（总是重启）' },
    { value: 'unless-stopped', label: 'unless-stopped（手动停止除外）' },
    { value: 'on-failure', label: 'on-failure（异常退出时重启）' }
  ];
  const NETWORK_DRIVERS = ['bridge', 'host', 'none', 'overlay', 'macvlan', 'ipvlan'];

  /** 与后端一致的校验规则，仅用于前端即时提示 */
  const IMAGE_REF = /^[a-zA-Z0-9][a-zA-Z0-9_.\-/:@]{0,254}$/;
  const NAME_REF = /^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,127}$/;
  const ENV_KEY_REF = /^[A-Za-z_][A-Za-z0-9_]*$/;
  const SENSITIVE_KEY = /(pass|pwd|secret|token|key|auth|credential)/i;

  /* ------------------------------ 状态 ------------------------------ */

  const S = {
    mounted: false,
    tab: 'containers',
    available: null,
    overview: null,
    containers: [],
    images: [],
    volumes: [],
    networks: [],
    counts: { containers: null, images: null, volumes: null, networks: null },
    filters: { q: '', state: 'all' },
    expandPorts: false,
    selected: new Set(),
    paused: false,
    timer: null,
    busy: false,
    pulling: false,
    loaded: { containers: false, images: false, volumes: false, networks: false },
    drawer: {
      open: false,
      id: '',
      inspect: null,
      ws: null,
      wsOpened: false,
      polling: null,
      statsTimer: null,
      logs: [],
      tail: 200,
      autoScroll: true,
      batchBusy: false
    }
  };

  /* ------------------------------ 基础工具 ------------------------------ */

  /** HTML 转义：容器名、镜像名等来自 Docker，必须转义后再拼接 */
  function esc(value) {
    return String(value === undefined || value === null ? '' : value).replace(/[&<>"']/g, (ch) => {
      const map = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };
      return map[ch];
    });
  }

  /** 生成内联 SVG 图标（复用页面 symbol 精灵） */
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

  /** 权限判断的安全包装：桥接对象缺少该方法时默认放行并由后端把关 */
  function can(key) {
    return typeof hasPerm === 'function' ? hasPerm(key) : true;
  }

  /** 把 Docker 返回的时间文本裁成 "YYYY-MM-DD HH:mm" */
  function prettyTime(text) {
    const raw = String(text || '').trim();
    if (!raw) {
      return '-';
    }
    const match = raw.match(/^(\d{4}-\d{2}-\d{2})[ T](\d{2}:\d{2})/);
    if (match) {
      return `${match[1]} ${match[2]}`;
    }
    const epoch = Number(raw);
    if (Number.isFinite(epoch) && epoch > 0) {
      return A.formatDateTime ? A.formatDateTime(epoch) : raw;
    }
    return raw;
  }

  function sizeText(bytes) {
    return typeof formatBytes === 'function' ? formatBytes(bytes) : String(bytes || 0);
  }

  /** 按钮 loading：操作期间禁用并显示忙碌态，防止重复点击 */
  async function withBusy(button, task) {
    if (!button || button.dataset.busy === '1') {
      return undefined;
    }
    button.dataset.busy = '1';
    button.setAttribute('aria-busy', 'true');
    button.disabled = true;
    try {
      return await task();
    } finally {
      button.dataset.busy = '';
      button.removeAttribute('aria-busy');
      button.disabled = false;
    }
  }

  function setInlineError(node, message) {
    if (!node) {
      return;
    }
    node.textContent = message || '';
    node.hidden = !message;
  }

  /* ------------------------------ 标签页与 URL hash ------------------------------ */

  function currentView() {
    return (location.hash || '#/dashboard').replace(/^#\/?/, '').split('?')[0] || 'dashboard';
  }

  /** 从 #/docker?tab=images 读取当前标签页 */
  function readTabFromHash() {
    const raw = (location.hash || '').replace(/^#\/?/, '');
    const index = raw.indexOf('?');
    if (index < 0) {
      return 'containers';
    }
    const params = new URLSearchParams(raw.slice(index + 1));
    const tab = params.get('tab');
    return TABS.indexOf(tab) >= 0 ? tab : 'containers';
  }

  /** 写回 URL，使用 replaceState 避免触发 hashchange 重新挂载视图 */
  function syncHash() {
    const target = `#/docker?tab=${S.tab}`;
    if (location.hash !== target) {
      try {
        history.replaceState(null, '', target);
      } catch (replaceErr) {
        console.warn('[Docker] 更新地址栏失败：', replaceErr && replaceErr.message);
      }
    }
  }

  function renderTabs() {
    const tabs = document.querySelectorAll('#dockerTabs .docker-tab');
    for (const tab of tabs) {
      const active = tab.dataset.dtab === S.tab;
      tab.classList.toggle('is-active', active);
      tab.setAttribute('aria-selected', active ? 'true' : 'false');
    }
    const panes = document.querySelectorAll('#dockerPanel .docker-pane');
    for (const pane of panes) {
      pane.hidden = pane.dataset.dpane !== S.tab;
    }
  }

  function renderCounts() {
    const map = {
      dockerCountContainers: S.containers.length,
      dockerCountImages: S.images.length,
      dockerCountVolumes: S.volumes.length,
      dockerCountNetworks: S.networks.length
    };
    for (const [id, value] of Object.entries(map)) {
      const node = $(id);
      if (node) {
        node.textContent = S.available ? String(value) : '-';
      }
    }
  }

  function setTab(tab) {
    if (TABS.indexOf(tab) < 0 || S.tab === tab) {
      return;
    }
    S.tab = tab;
    syncHash();
    renderTabs();
    loadTab(tab, false).catch((err) => toast(err.message || '加载失败', 'error'));
  }

  /* ------------------------------ 状态条 / 不可用提示 ------------------------------ */

  function renderStatus() {
    const dot = $('dockerStatusDot');
    const text = $('dockerStatusText');
    const summary = $('dockerSummary');
    const metrics = $('dockerMetrics');
    const info = S.overview || null;

    if (dot) {
      dot.className = `docker-status-dot ${S.available ? 'is-ok' : 'is-off'}`;
    }
    if (text) {
      text.textContent = S.available
        ? 'Docker 运行中'
        : S.available === null
          ? '正在连接 Docker…'
          : 'Docker 服务不可用';
    }
    if (summary) {
      summary.textContent = S.available && info && info.version
        ? `版本 ${info.version}`
        : '-';
    }
    if (!metrics) {
      return;
    }
    if (!S.available || !info) {
      metrics.textContent = '';
      return;
    }
    const stats = info.stats || {};
    const items = [
      { label: '容器（运行/总）', value: `${stats.running || 0} / ${stats.containers || 0}` },
      { label: '镜像数量', value: String(stats.images || 0) },
      { label: '镜像占用', value: sizeText(stats.imagesSize || 0) },
      { label: '卷 / 网络', value: `${stats.volumes || 0} / ${stats.networks || 0}` }
    ];
    metrics.innerHTML = items
      .map(
        (item) =>
          `<div class="docker-metric"><span class="docker-metric-label">${esc(item.label)}</span>` +
          `<strong class="docker-metric-value">${esc(item.value)}</strong></div>`
      )
      .join('');
  }

  /** 切换"不可用"整页提示 */
  function showUnavailable(unavailable, message, hint) {
    const card = $('dockerError');
    const panel = $('dockerPanel');
    if (card) {
      card.hidden = !unavailable;
      const title = $('dockerErrorText');
      const sub = $('dockerErrorHint');
      if (title) {
        title.textContent = message || '未检测到 Docker 服务';
      }
      if (sub) {
        sub.textContent = hint || '请先在飞牛中安装并启动 Docker';
      }
    }
    if (panel) {
      panel.hidden = Boolean(unavailable);
    }
  }

  async function loadOverview() {
    try {
      const data = await api('/api/docker/overview');
      S.available = true;
      S.overview = data;
      S.counts = (data && data.stats) || S.counts;
      showUnavailable(false);
      renderStatus();
      renderCounts();
      return true;
    } catch (err) {
      S.available = false;
      S.overview = null;
      renderStatus();
      const isUnavailable = err && (err.status === 503 || err.code === 'DOCKER_UNAVAILABLE');
      showUnavailable(
        true,
        isUnavailable ? '未检测到 Docker 服务' : err && err.message ? err.message : 'Docker 服务不可用',
        isUnavailable
          ? '请先在飞牛中安装并启动 Docker，完成后点击下方「重试」'
          : '请检查 Docker 守护进程状态后重试'
      );
      renderEmptyTables('Docker 服务不可用');
      renderCounts();
      return false;
    }
  }

  /** 不可用时清空四张表，保证界面不残留骨架屏 */
  function renderEmptyTables(text) {
    const targets = ['dockerBody', 'dockerImageBody', 'dockerVolumeBody', 'dockerNetworkBody'];
    const spans = { dockerBody: 7, dockerImageBody: 5, dockerVolumeBody: 5, dockerNetworkBody: 6 };
    for (const id of targets) {
      const body = $(id);
      if (body) {
        body.innerHTML = `<tr><td class="empty" colspan="${spans[id]}">${esc(text)}</td></tr>`;
      }
    }
  }

  /* ------------------------------ 容器列表 ------------------------------ */

  async function fetchContainers() {
    // 仅"运行中"筛选时只需要运行中的容器，其余情况需要全部容器
    const all = S.filters.state === 'running' ? '0' : '1';
    const data = await api(`/api/docker/containers?all=${all}`);
    S.containers = Array.isArray(data.items) ? data.items : [];
    S.loaded.containers = true;
    // 清理已不存在的选中项，避免批量操作误伤
    const ids = new Set(S.containers.map((item) => item.id));
    for (const id of Array.from(S.selected)) {
      if (!ids.has(id)) {
        S.selected.delete(id);
      }
    }
  }

  function matchState(container) {
    const filter = S.filters.state;
    if (filter === 'all') {
      return true;
    }
    if (filter === 'running') {
      return container.state === 'running';
    }
    if (filter === 'paused') {
      return container.state === 'paused';
    }
    if (filter === 'exited') {
      return container.state !== 'running' && container.state !== 'paused' && container.state !== 'restarting';
    }
    return true;
  }

  function filteredContainers() {
    const query = S.filters.q.trim().toLowerCase();
    return S.containers.filter((container) => {
      if (!matchState(container)) {
        return false;
      }
      if (!query) {
        return true;
      }
      return (
        String(container.name || '').toLowerCase().includes(query) ||
        String(container.image || '').toLowerCase().includes(query)
      );
    });
  }

  const BADGE_CLASS = {
    ok: 'badge-on',
    warn: 'badge-warn',
    danger: 'badge-danger',
    mute: 'badge-off'
  };

  function portChipsHtml(container) {
    const ports = Array.isArray(container.ports) ? container.ports : [];
    if (!ports.length) {
      return '<span class="docker-port-none">无端口映射</span>';
    }
    const shown = S.expandPorts ? ports : ports.slice(0, 2);
    const chips = shown
      .map((port) => {
        const text = port.host
          ? `${port.host} → ${port.container}/${port.protocol}`
          : `${port.container}/${port.protocol}`;
        return `<span class="docker-chip" title="${esc(text)}">${esc(text)}</span>`;
      })
      .join('');
    const rest = ports.length - shown.length;
    return chips + (rest > 0 ? `<span class="docker-chip docker-chip-more">+${rest}</span>` : '');
  }

  function rowActionsHtml(container) {
    const running = container.state === 'running';
    const paused = container.state === 'paused';
    const parts = [];
    if (running) {
      if (can('DOCKER_STOP')) {
        parts.push(
          `<button class="btn btn-sm" type="button" data-dact="stop" data-id="${esc(container.id)}" title="停止容器">` +
            '<svg class="icon"><use href="#i-stop"></use></svg>停止</button>'
        );
      }
    } else if (paused) {
      if (can('DOCKER_START')) {
        parts.push(
          `<button class="btn btn-sm" type="button" data-dact="unpause" data-id="${esc(container.id)}" title="恢复容器">` +
            '<svg class="icon"><use href="#i-play"></use></svg>恢复</button>'
        );
      }
    } else if (can('DOCKER_START')) {
      parts.push(
        `<button class="btn btn-sm" type="button" data-dact="start" data-id="${esc(container.id)}" title="启动容器">` +
          '<svg class="icon"><use href="#i-play"></use></svg>启动</button>'
      );
    }
    if (can('DOCKER_START')) {
      parts.push(
        `<button class="btn btn-ghost btn-sm" type="button" data-dact="restart" data-id="${esc(container.id)}" title="重启容器">` +
          '<svg class="icon"><use href="#i-restart"></use></svg>重启</button>'
      );
    }
    parts.push(
      `<button class="btn btn-ghost btn-sm" type="button" data-dact="detail" data-id="${esc(container.id)}" title="查看详情">` +
        '详情</button>'
    );
    parts.push(
      `<button class="icon-btn" type="button" data-dact="more" data-id="${esc(container.id)}" aria-label="更多操作" title="更多操作">` +
        '<svg class="icon"><use href="#i-more"></use></svg></button>'
    );
    return parts.join('');
  }

  function renderContainers() {
    const body = $('dockerBody');
    if (!body) {
      return;
    }
    if (!S.available) {
      return;
    }
    const list = filteredContainers();
    if (!list.length) {
      const text = S.containers.length ? '没有符合筛选条件的容器' : '暂无容器，点击「创建容器」开始使用';
      body.innerHTML = `<tr><td class="empty" colspan="7">${esc(text)}</td></tr>`;
      updateBatchBar();
      return;
    }
    body.innerHTML = list
      .map((container) => {
        const tone = STATE_TONE[container.state] || 'mute';
        const label = STATE_LABELS[container.state] || container.state || '未知';
        const selected = S.selected.has(container.id);
        return (
          `<tr class="${selected ? 'selected' : ''}" data-id="${esc(container.id)}">` +
          `<td class="col-check" data-label="选择"><input type="checkbox" data-dsel="${esc(container.id)}"` +
          `${selected ? ' checked' : ''} aria-label="选择容器 ${esc(container.name)}"></td>` +
          '<td data-label="容器">' +
          `<button class="docker-name-btn" type="button" data-ddetail="${esc(container.id)}" title="查看容器详情">` +
          `<span class="docker-name-dot is-${tone}"></span>` +
          `<span class="docker-name-text">${esc(container.name)}</span></button>` +
          `<span class="cell-sub cell-mono docker-name-id">${esc(container.shortId)}</span></td>` +
          `<td data-label="镜像"><span class="docker-cell-text">${esc(container.image)}</span></td>` +
          `<td data-label="端口映射"><div class="docker-ports">${portChipsHtml(container)}</div></td>` +
          `<td data-label="状态"><span class="badge ${BADGE_CLASS[tone]}">${esc(label)}</span>` +
          `<span class="cell-sub docker-status-sub">${esc(container.status)}</span></td>` +
          `<td data-label="创建时间"><span class="cell-sub">${esc(prettyTime(container.created))}</span></td>` +
          `<td class="col-actions" data-label="操作"><div class="action-group">${rowActionsHtml(container)}</div></td>` +
          '</tr>'
        );
      })
      .join('');
    updateBatchBar();
  }

  function updateBatchBar() {
    const bar = $('dockerBatchBar');
    const info = $('dockerBatchInfo');
    if (info) {
      info.textContent = `已选择 ${S.selected.size} 项`;
    }
    if (bar) {
      bar.hidden = S.selected.size === 0;
    }
    const all = $('dockerSelectAll');
    if (all) {
      const list = filteredContainers();
      const allChecked = list.length > 0 && list.every((item) => S.selected.has(item.id));
      all.checked = allChecked;
      all.indeterminate = !allChecked && list.some((item) => S.selected.has(item.id));
    }
  }

  /* ------------------------------ 容器操作 ------------------------------ */

  const ACTION_LABELS = {
    start: '启动',
    stop: '停止',
    restart: '重启',
    pause: '暂停',
    unpause: '恢复',
    kill: '强制结束'
  };

  async function containerAction(id, action, button) {
    const label = ACTION_LABELS[action] || action;
    await withBusy(button, async () => {
      try {
        await api(`/api/docker/containers/${encodeURIComponent(id)}/${action}`, {
          method: 'POST',
          body: {}
        });
        toast(`${label}容器成功`, 'success');
        await refreshCurrent(true);
      } catch (err) {
        toast(err.message || `${label}容器失败`, 'error');
      }
    });
  }

  function findContainer(id) {
    return S.containers.find((item) => item.id === id) || null;
  }

  function runRowAction(action, id, button) {
    const container = findContainer(id);
    if (!container) {
      toast('容器已不存在，请刷新后重试', 'warn');
      return;
    }
    if (ACTION_LABELS[action]) {
      containerAction(id, action, button).catch((err) => toast(err.message || '操作失败', 'error'));
      return;
    }
    if (action === 'detail') {
      openDrawer(id).catch((err) => toast(err.message || '读取容器详情失败', 'error'));
    } else if (action === 'logs') {
      openDrawer(id, true).catch((err) => toast(err.message || '读取容器日志失败', 'error'));
    } else if (action === 'more') {
      openMoreMenu(container);
    } else if (action === 'delete') {
      openDeleteContainersModal([container]);
    } else if (action === 'rename') {
      openRenameModal(container);
    } else if (action === 'policy') {
      openPolicyModal(container);
    }
  }

  /** 行内「更多」菜单：把低频操作收进模态列表 */
  function openMoreMenu(container) {
    const body = create('div', 'docker-menu');
    const items = [];
    if (container.state === 'running' && can('DOCKER_STOP')) {
      items.push({ label: '暂停容器', act: 'pause' });
    }
    if (container.state === 'paused' && can('DOCKER_START')) {
      items.push({ label: '恢复容器', act: 'unpause' });
    }
    if (can('DOCKER_VIEW')) {
      items.push({ label: '查看实时日志', act: 'logs' });
    }
    if (can('DOCKER_CREATE')) {
      items.push({ label: '重命名容器', act: 'rename' });
      items.push({ label: '更新重启策略', act: 'policy' });
    }
    if (can('DOCKER_STOP')) {
      items.push({ label: '强制结束容器（kill）', act: 'kill', danger: true });
    }
    if (can('DOCKER_DELETE')) {
      items.push({ label: '删除容器', act: 'delete', danger: true });
    }
    if (!items.length) {
      toast('当前账号没有可执行的容器操作权限', 'warn');
      return;
    }
    for (const item of items) {
      const btn = create('button', `docker-menu-item${item.danger ? ' is-danger' : ''}`, item.label);
      btn.type = 'button';
      btn.addEventListener('click', () => {
        closeModal();
        runRowAction(item.act, container.id, null);
      });
      body.appendChild(btn);
    }
    openModal({
      title: `容器操作 · ${container.name}`,
      body,
      buttons: [{ label: '关闭', kind: 'btn-ghost', onClick: ({ close }) => close() }]
    });
  }

  /* ------------------------------ 通用表单工具 ------------------------------ */

  function mkInput(type, id, placeholder, value) {
    const input = document.createElement('input');
    input.type = type;
    if (id) {
      input.id = id;
    }
    input.className = 'docker-input';
    if (placeholder) {
      input.placeholder = placeholder;
    }
    if (value !== undefined) {
      input.value = value;
    }
    input.autocomplete = 'off';
    return input;
  }

  function mkSelect(options, id) {
    const select = document.createElement('select');
    select.className = 'docker-select';
    if (id) {
      select.id = id;
    }
    for (const item of options) {
      const option = document.createElement('option');
      option.value = item.value;
      option.textContent = item.label;
      select.appendChild(option);
    }
    return select;
  }

  function mkButton(className, iconName, text) {
    const button = create('button', className);
    button.type = 'button';
    if (iconName) {
      button.appendChild(svgIcon(iconName));
    }
    if (text) {
      button.appendChild(document.createTextNode(text));
    }
    return button;
  }

  function fieldWrap(labelText, control, hintText) {
    const field = create('label', 'field');
    field.appendChild(create('span', null, labelText));
    field.appendChild(control);
    if (hintText) {
      field.appendChild(create('small', 'field-hint', hintText));
    }
    return field;
  }

  function checkboxRow(id, label, note) {
    const wrap = create('label', 'docker-check');
    const input = document.createElement('input');
    input.type = 'checkbox';
    input.id = id;
    const text = create('span', null, label);
    wrap.append(input, text);
    if (note) {
      wrap.appendChild(create('small', 'docker-hint', note));
    }
    return { wrap, input };
  }

  function mkRowRemove() {
    const button = mkButton('icon-btn docker-row-remove', 'i-close');
    button.setAttribute('aria-label', '移除该行');
    button.title = '移除该行';
    return button;
  }

  /* ------------------------------ 删除 / 重命名 / 重启策略 ------------------------------ */

  function openDeleteContainersModal(containers) {
    if (!containers.length) {
      return;
    }
    const batch = containers.length > 1;
    const body = create('div', 'docker-modal-form');
    body.appendChild(
      create(
        'p',
        'modal-desc',
        batch
          ? `确定要删除选中的 ${containers.length} 个容器吗？该操作不可恢复。`
          : `确定要删除容器「${containers[0].name}」吗？该操作不可恢复。`
      )
    );
    if (!batch) {
      body.appendChild(create('p', 'docker-hint', `镜像：${containers[0].image}`));
    }
    const force = checkboxRow('dockerDelForce', '强制删除', '运行中的容器会被强制结束');
    const volumes = checkboxRow('dockerDelVolumes', '同时删除匿名卷', '容器创建时自动生成的匿名卷会被一并清理');
    body.append(force.wrap, volumes.wrap);

    openModal({
      title: batch ? '批量删除容器' : '删除容器',
      body,
      wide: batch,
      buttons: [
        { label: '取消', kind: 'btn-ghost', onClick: ({ close }) => close() },
        {
          label: '删除',
          kind: 'btn-danger',
          onClick: async ({ close, setBusy }) => {
            setBusy(true);
            const query = `?force=${force.input.checked ? '1' : '0'}&volumes=${volumes.input.checked ? '1' : '0'}`;
            let success = 0;
            const failures = [];
            for (const item of containers) {
              try {
                await api(`/api/docker/containers/${encodeURIComponent(item.id)}${query}`, { method: 'DELETE' });
                success += 1;
              } catch (err) {
                failures.push(`${item.name}：${err.message || '删除失败'}`);
              }
            }
            setBusy(false);
            close();
            if (success > 0) {
              toast(`已删除 ${success} 个容器`, 'success');
            }
            if (failures.length > 0) {
              toast(
                failures.length > 1 ? `${failures[0]}（共 ${failures.length} 项失败）` : failures[0],
                'error'
              );
            }
            S.selected.clear();
            S.drawer && S.drawer.open && closeDrawerImmediate();
            await refreshCurrent(true);
          }
        }
      ]
    });
  }

  function openRenameModal(container) {
    const body = create('div', 'docker-modal-form');
    const input = mkInput('text', 'dockerRenameInput', '新的容器名称', container.name);
    body.appendChild(
      fieldWrap('新的容器名称', input, '以字母或数字开头，仅允许字母、数字、下划线、点和短横线')
    );
    const error = create('div', 'docker-form-error');
    error.hidden = true;
    body.appendChild(error);

    openModal({
      title: '重命名容器',
      body,
      buttons: [
        { label: '取消', kind: 'btn-ghost', onClick: ({ close }) => close() },
        {
          label: '保存',
          kind: 'btn-primary',
          onClick: async ({ close, setBusy }) => {
            const name = input.value.trim();
            if (!NAME_REF.test(name)) {
              setInlineError(error, '名称不合法：需以字母或数字开头，仅允许字母、数字、下划线、点和短横线');
              input.focus();
              return;
            }
            if (name === container.name) {
              close();
              return;
            }
            setBusy(true);
            try {
              await api(`/api/docker/containers/${encodeURIComponent(container.id)}/rename`, {
                method: 'POST',
                body: { name }
              });
              close();
              toast('重命名成功', 'success');
              await refreshCurrent(true);
            } catch (err) {
              setBusy(false);
              setInlineError(error, err.message || '重命名失败');
            }
          }
        }
      ]
    });
  }

  function openPolicyModal(container) {
    const body = create('div', 'docker-modal-form');
    const select = mkSelect(RESTART_OPTIONS, 'dockerPolicySelect');
    select.classList.add('docker-select-block');
    body.appendChild(fieldWrap('重启策略', select, '调整后立即生效，无需重建容器'));
    const error = create('div', 'docker-form-error');
    error.hidden = true;
    body.appendChild(error);

    // 读取当前策略（列表接口不含该字段），失败时保持默认 no
    api(`/api/docker/containers/${encodeURIComponent(container.id)}`)
      .then((data) => {
        const policy = data && data.restartPolicy ? data.restartPolicy : 'no';
        if (RESTART_OPTIONS.some((item) => item.value === policy)) {
          select.value = policy;
        }
      })
      .catch(() => {
        /* 读取失败不阻塞修改 */
      });

    openModal({
      title: '更新重启策略',
      body,
      buttons: [
        { label: '取消', kind: 'btn-ghost', onClick: ({ close }) => close() },
        {
          label: '保存',
          kind: 'btn-primary',
          onClick: async ({ close, setBusy }) => {
            setBusy(true);
            try {
              await api(`/api/docker/containers/${encodeURIComponent(container.id)}/update`, {
                method: 'POST',
                body: { restartPolicy: select.value }
              });
              close();
              toast('重启策略已更新', 'success');
              await refreshCurrent(true);
            } catch (err) {
              setBusy(false);
              setInlineError(error, err.message || '更新重启策略失败');
            }
          }
        }
      ]
    });
  }

  /* ------------------------------ 创建容器 ------------------------------ */

  function fillImageSelect(select) {
    const current = select.value;
    select.innerHTML = '';
    const empty = document.createElement('option');
    empty.value = '';
    empty.textContent = '选择已有镜像…';
    select.appendChild(empty);
    for (const image of S.images) {
      if (!image.repoTag || image.repoTag === '<none>') {
        continue;
      }
      const option = document.createElement('option');
      option.value = image.repoTag;
      option.textContent = `${image.repoTag}（${image.size || ''}）`;
      select.appendChild(option);
    }
    select.value = current;
  }

  function openCreateModal() {
    if (!can('DOCKER_CREATE')) {
      toast('当前账号没有创建容器的权限', 'warn');
      return;
    }
    const body = create('div', 'docker-modal-form docker-create-form');

    /* 镜像 */
    const imageInput = mkInput('text', 'ctrImage', 'nginx:latest');
    const imageSelect = mkSelect([], 'ctrImagePick');
    fillImageSelect(imageSelect);
    imageSelect.addEventListener('change', () => {
      if (imageSelect.value) {
        imageInput.value = imageSelect.value;
      }
    });
    const pullBtn = mkButton('btn btn-ghost btn-sm', 'i-download', '拉取镜像');
    pullBtn.id = 'ctrPullBtn';
    const pullStatus = create('span', 'docker-pull-status');
    pullStatus.hidden = true;
    const imageRow = create('div', 'docker-inline');
    imageRow.append(imageInput, imageSelect, pullBtn);
    const imageField = fieldWrap('镜像 *', imageRow, '可输入任意镜像引用，或从已有镜像中选择后拉取');
    imageField.appendChild(pullStatus);
    pullBtn.addEventListener('click', () => {
      const name = imageInput.value.trim();
      if (!name) {
        setInlineError(errorNode, '请先填写要拉取的镜像名称');
        imageInput.focus();
        return;
      }
      pullImage(name, pullBtn, pullStatus, () => fillImageSelect(imageSelect)).catch(() => {});
    });

    /* 基础信息 */
    const nameInput = mkInput('text', 'ctrName', '留空则由 Docker 自动命名');
    const cmdInput = mkInput('text', 'ctrCmd', '可选，如 nginx -g "daemon off;"');
    const policySelect = mkSelect(RESTART_OPTIONS, 'ctrPolicy');
    const networkSelect = mkSelect(
      [{ value: '', label: '默认网络' }].concat(
        S.networks.filter((item) => item.name).map((item) => ({ value: item.name, label: item.name }))
      ),
      'ctrNetwork'
    );
    const autoRemove = checkboxRow('ctrAutoRemove', '自动删除容器（--rm）', '容器退出后自动删除，不能与重启策略同时使用');

    /* 动态行容器 */
    const portsBox = create('div', 'docker-rows');
    portsBox.id = 'ctrPorts';
    const volumesBox = create('div', 'docker-rows');
    volumesBox.id = 'ctrVolumes';
    const envBox = create('div', 'docker-rows');
    envBox.id = 'ctrEnv';

    const addPort = () => {
      const row = create('div', 'docker-row docker-row-port');
      const host = mkInput('number', '', '宿主端口（可留空）');
      host.dataset.f = 'host';
      host.min = '1';
      host.max = '65535';
      const target = mkInput('number', '', '容器端口');
      target.dataset.f = 'container';
      target.min = '1';
      target.max = '65535';
      const protocol = mkSelect(
        [
          { value: 'tcp', label: 'tcp' },
          { value: 'udp', label: 'udp' },
          { value: 'sctp', label: 'sctp' }
        ],
        ''
      );
      protocol.dataset.f = 'protocol';
      const remove = mkRowRemove();
      remove.addEventListener('click', () => row.remove());
      row.append(host, target, protocol, remove);
      portsBox.appendChild(row);
    };

    const addVolume = () => {
      const row = create('div', 'docker-row docker-row-volume');
      const host = mkInput('text', '', '/data 宿主路径');
      host.dataset.f = 'host';
      const target = mkInput('text', '', '/app/data 容器路径');
      target.dataset.f = 'container';
      const roWrap = create('label', 'docker-check docker-check-inline');
      const ro = document.createElement('input');
      ro.type = 'checkbox';
      ro.dataset.f = 'ro';
      roWrap.append(ro, create('span', null, '只读'));
      const remove = mkRowRemove();
      remove.addEventListener('click', () => row.remove());
      row.append(host, target, roWrap, remove);
      volumesBox.appendChild(row);
    };

    const addEnv = () => {
      const row = create('div', 'docker-row docker-row-env');
      const input = mkInput('text', '', 'KEY=VALUE');
      input.dataset.f = 'value';
      const remove = mkRowRemove();
      remove.addEventListener('click', () => row.remove());
      row.append(input, remove);
      envBox.appendChild(row);
    };

    const addPortBtn = mkButton('btn btn-ghost btn-sm', 'i-plus', '添加端口映射');
    const addVolumeBtn = mkButton('btn btn-ghost btn-sm', 'i-plus', '添加卷映射');
    const addEnvBtn = mkButton('btn btn-ghost btn-sm', 'i-plus', '添加环境变量');
    addPortBtn.addEventListener('click', addPort);
    addVolumeBtn.addEventListener('click', addVolume);
    addEnvBtn.addEventListener('click', addEnv);

    const errorNode = create('div', 'docker-form-error');
    errorNode.id = 'ctrFormError';
    errorNode.hidden = true;

    // 自动删除与重启策略互斥，勾选后锁定策略为 no
    autoRemove.input.addEventListener('change', () => {
      if (autoRemove.input.checked) {
        policySelect.value = 'no';
        policySelect.disabled = true;
      } else {
        policySelect.disabled = false;
      }
    });

    body.append(
      imageField,
      fieldWrap('容器名称', nameInput, '以字母或数字开头，仅允许字母、数字、下划线、点和短横线'),
      fieldWrap('启动命令', cmdInput, '无需固定入口时可留空'),
      fieldWrap('重启策略', policySelect),
      fieldWrap('网络', networkSelect),
      autoRemove.wrap
    );

    const portsSection = create('div', 'docker-fieldset');
    portsSection.appendChild(create('div', 'docker-fieldset-head', '端口映射'));
    portsSection.appendChild(portsBox);
    portsSection.appendChild(addPortBtn);
    body.appendChild(portsSection);

    const volumesSection = create('div', 'docker-fieldset');
    volumesSection.appendChild(create('div', 'docker-fieldset-head', '卷映射'));
    volumesSection.appendChild(volumesBox);
    volumesSection.appendChild(addVolumeBtn);
    body.appendChild(volumesSection);

    const envSection = create('div', 'docker-fieldset');
    envSection.appendChild(create('div', 'docker-fieldset-head', '环境变量'));
    envSection.appendChild(envBox);
    envSection.appendChild(addEnvBtn);
    body.appendChild(envSection);

    body.appendChild(errorNode);

    addPort();
    addVolume();
    addEnv();

    const refs = {
      image: imageInput,
      name: nameInput,
      cmd: cmdInput,
      policy: policySelect,
      network: networkSelect,
      autoRemove: autoRemove.input,
      ports: portsBox,
      volumes: volumesBox,
      env: envBox
    };

    openModal({
      title: '创建容器',
      body,
      wide: true,
      buttons: [
        { label: '取消', kind: 'btn-ghost', onClick: ({ close }) => close() },
        {
          label: '创建',
          kind: 'btn-primary',
          onClick: async ({ close, setBusy }) => {
            const result = collectCreatePayload(refs);
            if (!result.ok) {
              setInlineError(errorNode, result.errors[0]);
              const firstRow = errorNode.previousElementSibling;
              if (firstRow && typeof firstRow.scrollIntoView === 'function') {
                errorNode.scrollIntoView({ block: 'nearest' });
              }
              return;
            }
            setBusy(true);
            try {
              const data = await api('/api/docker/containers', { method: 'POST', body: result.payload });
              close();
              toast(`容器「${data.name || result.payload.image}」创建成功`, 'success');
              await refreshCurrent(true);
            } catch (err) {
              setBusy(false);
              setInlineError(errorNode, err.message || '创建容器失败');
              errorNode.scrollIntoView({ block: 'nearest' });
            }
          }
        }
      ]
    });
  }

  /** 读取并校验创建表单，返回 {ok, errors, payload} */
  function collectCreatePayload(refs) {
    const errors = [];
    const image = refs.image.value.trim();
    if (!image) {
      errors.push('请填写镜像名称');
    } else if (!IMAGE_REF.test(image) || image.includes('..') || image.includes('//')) {
      errors.push('镜像名称不合法，请填写如 nginx:latest 的引用');
    }

    const name = refs.name.value.trim();
    if (name && !NAME_REF.test(name)) {
      errors.push('容器名称不合法：需以字母或数字开头，仅允许字母、数字、下划线、点和短横线');
    }

    const ports = [];
    for (const row of refs.ports.children) {
      const hostInput = row.querySelector('[data-f="host"]');
      const targetInput = row.querySelector('[data-f="container"]');
      const protocolInput = row.querySelector('[data-f="protocol"]');
      const host = hostInput.value.trim();
      const target = targetInput.value.trim();
      const protocol = protocolInput ? protocolInput.value : 'tcp';
      if (!host && !target) {
        continue;
      }
      if (!target) {
        errors.push('端口映射：容器端口不能为空');
        continue;
      }
      if (!/^\d+$/.test(target) || Number(target) < 1 || Number(target) > 65535) {
        errors.push('端口映射：容器端口必须是 1~65535 的整数');
        continue;
      }
      if (host) {
        if (!/^\d+$/.test(host) || Number(host) < 1 || Number(host) > 65535) {
          errors.push('端口映射：宿主端口必须是 1~65535 的整数');
          continue;
        }
      }
      ports.push({ host: host ? Number(host) : null, container: Number(target), protocol });
    }

    const volumes = [];
    for (const row of refs.volumes.children) {
      const hostInput = row.querySelector('[data-f="host"]');
      const targetInput = row.querySelector('[data-f="container"]');
      const roInput = row.querySelector('[data-f="ro"]');
      const host = hostInput.value.trim();
      const target = targetInput.value.trim();
      if (!host && !target) {
        continue;
      }
      if (!host) {
        errors.push('卷映射：宿主路径不能为空');
        continue;
      }
      if (!host.startsWith('/')) {
        errors.push('卷映射：宿主路径必须以 / 开头');
        continue;
      }
      if (host.split('/').includes('..')) {
        errors.push('卷映射：宿主路径不能包含 ..');
        continue;
      }
      if (!target) {
        errors.push('卷映射：容器路径不能为空');
        continue;
      }
      if (!target.startsWith('/')) {
        errors.push('卷映射：容器路径必须以 / 开头');
        continue;
      }
      if (target.split('/').includes('..')) {
        errors.push('卷映射：容器路径不能包含 ..');
        continue;
      }
      volumes.push({ host, container: target, ro: Boolean(roInput && roInput.checked) });
    }

    const env = [];
    for (const row of refs.env.children) {
      const input = row.querySelector('[data-f="value"]');
      const value = input.value.trim();
      if (!value) {
        continue;
      }
      const index = value.indexOf('=');
      const key = index > 0 ? value.slice(0, index) : '';
      if (!ENV_KEY_REF.test(key)) {
        errors.push(`环境变量格式不正确：${value}，应为 KEY=VALUE`);
        continue;
      }
      env.push(value);
    }

    const policy = refs.policy.value;
    const autoRemove = Boolean(refs.autoRemove.checked);
    if (autoRemove && policy !== 'no') {
      errors.push('自动删除（--rm）不能与重启策略同时使用，请将重启策略设为 no');
    }

    const payload = {
      image,
      name: name || undefined,
      restartPolicy: policy || 'no',
      autoRemove,
      network: refs.network.value || undefined,
      ports,
      volumes,
      env
    };
    const cmd = refs.cmd.value.trim();
    if (cmd) {
      payload.cmd = cmd;
    }
    return { ok: errors.length === 0, errors, payload };
  }

  /** 拉取镜像：显示进度与耗时，成功后刷新镜像列表 */
  async function pullImage(name, button, statusNode, onDone) {
    if (S.pulling) {
      toast('已有镜像正在拉取，请稍后重试', 'warn');
      return;
    }
    if (!IMAGE_REF.test(name)) {
      if (statusNode) {
        statusNode.hidden = false;
        statusNode.className = 'docker-pull-status is-error';
        statusNode.textContent = '镜像名称不合法，请填写如 nginx:latest 的引用';
      }
      return;
    }
    S.pulling = true;
    const started = Date.now();
    if (button) {
      button.disabled = true;
      button.setAttribute('aria-busy', 'true');
    }
    if (statusNode) {
      statusNode.hidden = false;
      statusNode.className = 'docker-pull-status is-busy';
      statusNode.textContent = `正在拉取 ${name}…`;
    }
    try {
      await api('/api/docker/images/pull', { method: 'POST', body: { image: name } });
      const seconds = ((Date.now() - started) / 1000).toFixed(1);
      if (statusNode) {
        statusNode.className = 'docker-pull-status is-ok';
        statusNode.textContent = `已拉取 ${name}，耗时 ${seconds} 秒`;
      }
      toast(`镜像 ${name} 拉取完成`, 'success');
      await fetchImages().catch(() => {});
      renderImages();
      renderCounts();
      if (typeof onDone === 'function') {
        onDone();
      }
    } catch (err) {
      if (statusNode) {
        statusNode.className = 'docker-pull-status is-error';
        statusNode.textContent = err.message || '拉取镜像失败';
      }
      toast(err.message || '拉取镜像失败', 'error');
    } finally {
      S.pulling = false;
      if (button) {
        button.disabled = false;
        button.removeAttribute('aria-busy');
      }
    }
  }

  /* ------------------------------ 镜像列表 ------------------------------ */

  async function fetchImages() {
    const data = await api('/api/docker/images');
    S.images = Array.isArray(data.items) ? data.items : [];
    S.loaded.images = true;
    renderCounts();
  }

  function imageRepoText(image) {
    return image.repoTag && image.repoTag !== '<none>' ? image.repoTag : '无标签（悬空镜像）';
  }

  function renderImages() {
    const body = $('dockerImageBody');
    if (!body || !S.available) {
      return;
    }
    if (!S.images.length) {
      body.innerHTML =
        '<tr><td class="empty" colspan="5">暂无镜像，可在上方输入镜像名称后点击「拉取镜像」</td></tr>';
      return;
    }
    body.innerHTML = S.images
      .map((image) => {
        const actions = can('DOCKER_DELETE')
          ? `<button class="btn btn-danger btn-sm" type="button" data-diact="delete" data-id="${esc(image.id)}">` +
            '<svg class="icon"><use href="#i-trash"></use></svg>删除</button>'
          : '';
        return (
          `<tr data-id="${esc(image.id)}">` +
          `<td data-label="仓库标签"><span class="docker-cell-text">${esc(imageRepoText(image))}</span></td>` +
          `<td data-label="镜像 ID"><span class="cell-mono">${esc(image.shortId || '')}</span></td>` +
          `<td data-label="大小"><span class="cell-sub">${esc(image.size || sizeText(image.sizeBytes))}</span></td>` +
          `<td data-label="创建时间"><span class="cell-sub">${esc(image.createdSince || prettyTime(image.created))}</span></td>` +
          `<td class="col-actions" data-label="操作"><div class="action-group">${actions}</div></td>` +
          '</tr>'
        );
      })
      .join('');
  }

  /** 删除镜像：可选强制删除 */
  function openDeleteImageModal(image) {
    const body = create('div', 'docker-modal-form');
    body.appendChild(
      create('p', 'modal-desc', `确定要删除镜像「${imageRepoText(image)}」吗？该操作不可恢复。`)
    );
    const force = checkboxRow('dockerImageForce', '强制删除', '镜像仍被容器使用时需要强制删除');
    body.appendChild(force.wrap);

    openModal({
      title: '删除镜像',
      body,
      buttons: [
        { label: '取消', kind: 'btn-ghost', onClick: ({ close }) => close() },
        {
          label: '删除',
          kind: 'btn-danger',
          onClick: async ({ close, setBusy }) => {
            setBusy(true);
            try {
              await api(
                `/api/docker/images/${encodeURIComponent(image.id)}?force=${force.input.checked ? '1' : '0'}`,
                { method: 'DELETE' }
              );
              close();
              toast('镜像已删除', 'success');
              await refreshCurrent(true);
            } catch (err) {
              setBusy(false);
              toast(err.message || '删除镜像失败', 'error');
            }
          }
        }
      ]
    });
  }

  /** 清理未使用镜像 */
  function openPruneImagesModal() {
    const body = create('div', 'docker-modal-form');
    body.appendChild(
      create('p', 'modal-desc', '将清理当前没有被任何容器使用的镜像。清理后需要重新拉取才能使用。')
    );
    const all = checkboxRow('dockerPruneAll', '同时清理所有未使用镜像（-a）', '默认只清理悬空镜像');
    body.appendChild(all.wrap);

    openModal({
      title: '清理未使用镜像',
      body,
      buttons: [
        { label: '取消', kind: 'btn-ghost', onClick: ({ close }) => close() },
        {
          label: '开始清理',
          kind: 'btn-warn',
          onClick: async ({ close, setBusy }) => {
            setBusy(true);
            try {
              const data = await api('/api/docker/images/prune', {
                method: 'POST',
                body: { all: all.input.checked }
              });
              close();
              const reclaimed = sizeText(data.reclaimedBytes || 0);
              toast(`清理完成，释放 ${reclaimed} 空间`, 'success');
              await refreshCurrent(true);
            } catch (err) {
              setBusy(false);
              toast(err.message || '清理镜像失败', 'error');
            }
          }
        }
      ]
    });
  }

  /* ------------------------------ 数据卷 ------------------------------ */

  async function fetchVolumes() {
    const data = await api('/api/docker/volumes');
    S.volumes = Array.isArray(data.items) ? data.items : [];
    S.loaded.volumes = true;
  }

  function renderVolumes() {
    const body = $('dockerVolumeBody');
    if (!body || !S.available) {
      return;
    }
    if (!S.volumes.length) {
      body.innerHTML = '<tr><td class="empty" colspan="5">暂无数据卷，可在上方输入名称后创建</td></tr>';
      return;
    }
    body.innerHTML = S.volumes
      .map((volume) => {
        const actions = can('DOCKER_DELETE')
          ? `<button class="btn btn-danger btn-sm" type="button" data-dvact="delete" data-name="${esc(volume.name)}">` +
            '<svg class="icon"><use href="#i-trash"></use></svg>删除</button>'
          : '';
        return (
          `<tr data-name="${esc(volume.name)}">` +
          `<td data-label="名称"><span class="docker-cell-text">${esc(volume.name)}</span></td>` +
          `<td data-label="驱动"><span class="cell-sub">${esc(volume.driver || 'local')}</span></td>` +
          `<td data-label="作用域"><span class="cell-sub">${esc(volume.scope || 'local')}</span></td>` +
          `<td data-label="挂载点"><span class="cell-mono docker-mount-path" title="${esc(volume.mountpoint || '')}">${esc(volume.mountpoint || '-')}</span></td>` +
          `<td class="col-actions" data-label="操作"><div class="action-group">${actions}</div></td>` +
          '</tr>'
        );
      })
      .join('');
  }

  async function createVolume() {
    const nameNode = $('dockerVolumeName');
    const driverNode = $('dockerVolumeDriver');
    const errorNode = $('dockerVolumeError');
    const button = $('dockerVolumeCreate');
    setInlineError(errorNode, '');
    if (!nameNode) {
      return;
    }
    const name = nameNode.value.trim();
    if (!NAME_REF.test(name)) {
      setInlineError(errorNode, '名称不合法：需以字母或数字开头，仅允许字母、数字、下划线、点和短横线');
      nameNode.focus();
      return;
    }
    await withBusy(button, async () => {
      try {
        await api('/api/docker/volumes', {
          method: 'POST',
          body: { name, driver: driverNode && driverNode.value ? driverNode.value : 'local' }
        });
        nameNode.value = '';
        toast(`数据卷「${name}」创建成功`, 'success');
        await refreshCurrent(true);
      } catch (err) {
        setInlineError(errorNode, err.message || '创建数据卷失败');
      }
    });
  }

  function openDeleteVolumeModal(volume) {
    const body = create('div', 'docker-modal-form');
    body.appendChild(create('p', 'modal-desc', `确定要删除数据卷「${volume.name}」吗？`));
    body.appendChild(
      create('p', 'docker-warn-text', '注意：卷内的数据将被永久删除且无法恢复，请确认已完成备份。')
    );
    const force = checkboxRow('dockerVolumeForce', '强制删除', '即使有容器仍在使用，也强制移除该卷');
    body.appendChild(force.wrap);

    openModal({
      title: '删除数据卷',
      body,
      buttons: [
        { label: '取消', kind: 'btn-ghost', onClick: ({ close }) => close() },
        {
          label: '删除',
          kind: 'btn-danger',
          onClick: async ({ close, setBusy }) => {
            setBusy(true);
            try {
              await api(
                `/api/docker/volumes/${encodeURIComponent(volume.name)}?force=${force.input.checked ? '1' : '0'}`,
                { method: 'DELETE' }
              );
              close();
              toast('数据卷已删除', 'success');
              await refreshCurrent(true);
            } catch (err) {
              setBusy(false);
              toast(err.message || '删除数据卷失败', 'error');
            }
          }
        }
      ]
    });
  }

  /* ------------------------------ 网络 ------------------------------ */

  async function fetchNetworks() {
    const data = await api('/api/docker/networks');
    S.networks = Array.isArray(data.items) ? data.items : [];
    S.loaded.networks = true;
  }

  function renderNetworks() {
    const body = $('dockerNetworkBody');
    if (!body || !S.available) {
      return;
    }
    if (!S.networks.length) {
      body.innerHTML = '<tr><td class="empty" colspan="6">暂无网络，可在上方输入名称后创建</td></tr>';
      return;
    }
    body.innerHTML = S.networks
      .map((network) => {
        const flags = [];
        if (network.ipv6) {
          flags.push('<span class="tag">IPv6</span>');
        }
        if (network.internal) {
          flags.push('<span class="tag">内部网络</span>');
        }
        const actions = can('DOCKER_DELETE')
          ? `<button class="btn btn-danger btn-sm" type="button" data-dnact="delete" data-name="${esc(network.name)}">` +
            '<svg class="icon"><use href="#i-trash"></use></svg>删除</button>'
          : '';
        return (
          `<tr data-name="${esc(network.name)}">` +
          `<td data-label="名称"><span class="docker-cell-text">${esc(network.name)}</span></td>` +
          `<td data-label="驱动"><span class="cell-sub">${esc(network.driver || 'bridge')}</span></td>` +
          `<td data-label="作用域"><span class="cell-sub">${esc(network.scope || 'local')}</span></td>` +
          `<td data-label="网络 ID"><span class="cell-mono">${esc(network.shortId || '')}</span></td>` +
          `<td data-label="属性">${flags.join(' ') || '<span class="cell-sub">-</span>'}</td>` +
          `<td class="col-actions" data-label="操作"><div class="action-group">${actions}</div></td>` +
          '</tr>'
        );
      })
      .join('');
  }

  async function createNetwork() {
    const nameNode = $('dockerNetworkName');
    const driverNode = $('dockerNetworkDriver');
    const errorNode = $('dockerNetworkError');
    const button = $('dockerNetworkCreate');
    setInlineError(errorNode, '');
    if (!nameNode) {
      return;
    }
    const name = nameNode.value.trim();
    if (!NAME_REF.test(name)) {
      setInlineError(errorNode, '名称不合法：需以字母或数字开头，仅允许字母、数字、下划线、点和短横线');
      nameNode.focus();
      return;
    }
    await withBusy(button, async () => {
      try {
        await api('/api/docker/networks', {
          method: 'POST',
          body: { name, driver: driverNode && driverNode.value ? driverNode.value : 'bridge' }
        });
        nameNode.value = '';
        toast(`网络「${name}」创建成功`, 'success');
        await refreshCurrent(true);
      } catch (err) {
        setInlineError(errorNode, err.message || '创建网络失败');
      }
    });
  }

  function openDeleteNetworkModal(network) {
    const body = create('div', 'docker-modal-form');
    body.appendChild(create('p', 'modal-desc', `确定要删除网络「${network.name}」吗？`));
    body.appendChild(create('p', 'docker-hint', '仍有容器连接到该网络时删除会失败，请先断开相关容器。'));

    openModal({
      title: '删除网络',
      body,
      buttons: [
        { label: '取消', kind: 'btn-ghost', onClick: ({ close }) => close() },
        {
          label: '删除',
          kind: 'btn-danger',
          onClick: async ({ close, setBusy }) => {
            setBusy(true);
            try {
              await api(`/api/docker/networks/${encodeURIComponent(network.name)}`, { method: 'DELETE' });
              close();
              toast('网络已删除', 'success');
              await refreshCurrent(true);
            } catch (err) {
              setBusy(false);
              toast(err.message || '删除网络失败', 'error');
            }
          }
        }
      ]
    });
  }

  /* ------------------------------ 刷新与自动轮询 ------------------------------ */

  let refreshing = false;

  async function loadTab(tab, silent) {
    const name = TABS.indexOf(tab) >= 0 ? tab : 'containers';
    try {
      if (name === 'containers') {
        await fetchContainers();
        renderContainers();
      } else if (name === 'images') {
        await fetchImages();
        renderImages();
      } else if (name === 'volumes') {
        await fetchVolumes();
        renderVolumes();
      } else {
        await fetchNetworks();
        renderNetworks();
      }
      renderCounts();
    } catch (err) {
      if (!silent) {
        throw err;
      }
      console.warn('[Docker] 静默刷新失败：', err && err.message);
      if (err && (err.status === 503 || err.code === 'DOCKER_UNAVAILABLE')) {
        S.available = false;
        renderStatus();
        showUnavailable(
          true,
          '未检测到 Docker 服务',
          '请先在飞牛中安装并启动 Docker，完成后点击下方「重试」'
        );
        renderEmptyTables('Docker 服务不可用');
        stopPolling();
      }
    }
  }

  /** 立即刷新：状态条 + 当前标签页（silent=true 时不抛错，用于轮询） */
  async function refreshCurrent(silent) {
    if (refreshing) {
      return false;
    }
    refreshing = true;
    try {
      const ok = await loadOverview();
      if (!ok) {
        renderEmptyTables('Docker 服务不可用');
        // 手动刷新（非静默）时抛出错误，由顶部刷新按钮统一提示失败
        if (!silent) {
          throw new Error('Docker 服务不可用，请先在飞牛中安装并启动 Docker');
        }
        return false;
      }
      await loadTab(S.tab, Boolean(silent));
      return true;
    } finally {
      refreshing = false;
    }
  }

  function stopPolling() {
    if (S.timer) {
      clearInterval(S.timer);
      S.timer = null;
    }
  }

  /** 每 10 秒静默刷新当前标签页；页面隐藏或手动暂停时停止 */
  function startPolling() {
    stopPolling();
    if (!S.mounted || S.paused || document.hidden || currentView() !== 'docker') {
      return;
    }
    S.timer = setInterval(() => {
      if (!S.mounted || S.paused || document.hidden || currentView() !== 'docker') {
        return;
      }
      refreshCurrent(true).catch(() => {});
    }, AUTO_INTERVAL);
  }

  /* ------------------------------ 容器详情抽屉 ------------------------------ */

  /** 复制文本：优先使用剪贴板 API，失败时提示手动复制 */
  async function copyText(text, message) {
    try {
      if (navigator.clipboard && typeof navigator.clipboard.writeText === 'function') {
        await navigator.clipboard.writeText(text);
        toast(message || '已复制', 'success');
        return;
      }
      throw new Error('clipboard unavailable');
    } catch (copyErr) {
      toast('当前浏览器不允许自动复制，请手动选择文本复制', 'warn');
    }
  }

  function drawerSection(titleText) {
    const section = create('section', 'docker-drawer-section');
    const head = create('div', 'docker-drawer-section-head');
    head.appendChild(create('h4', 'docker-drawer-section-title', titleText));
    section.appendChild(head);
    return { section, head };
  }

  /** 基本信息网格 */
  function buildInfoGrid(info) {
    const grid = create('div', 'docker-info-grid');
    const addItem = (label, value, options) => {
      const item = create('div', 'docker-info-item');
      item.appendChild(create('span', 'docker-info-label', label));
      const valueNode = create('div', 'docker-info-value', value || '-');
      if (options && options.mono) {
        valueNode.classList.add('cell-mono');
      }
      if (options && options.copy && value) {
        const copyBtn = mkButton('icon-btn docker-copy-btn', 'i-copy');
        copyBtn.title = '复制';
        copyBtn.setAttribute('aria-label', `复制${label}`);
        copyBtn.addEventListener('click', () => {
          copyText(value, `${label}已复制`).catch(() => {});
        });
        valueNode.appendChild(copyBtn);
      }
      item.appendChild(valueNode);
      grid.appendChild(item);
    };

    addItem('容器 ID', info.id, { mono: true, copy: true });
    addItem('镜像', info.image);
    addItem('启动命令', (info.cmd || []).join(' ') || '（使用镜像默认入口）');
    addItem('创建时间', prettyTime(info.created));
    addItem('重启策略', info.restartPolicy || 'no');
    addItem(
      '运行状态',
      `${STATE_LABELS[info.state] || info.state || '未知'}${info.exitCode ? `（退出码 ${info.exitCode}）` : ''}`
    );
    if (info.ipAddress) {
      addItem('容器 IP', info.ipAddress, { mono: true, copy: true });
    }
    return grid;
  }

  function buildPortsTable(ports) {
    const wrap = create('div', 'table-wrap');
    const table = create('table', 'table docker-detail-table');
    const rows = (ports || [])
      .map(
        (port) =>
          `<tr><td data-label="宿主端口">${esc(port.host ? String(port.host) : '随机')}</td>` +
          `<td data-label="容器端口">${esc(String(port.container || ''))}</td>` +
          `<td data-label="协议">${esc(port.protocol || 'tcp')}</td>` +
          `<td data-label="宿主 IP">${esc(port.hostIp || '0.0.0.0')}</td></tr>`
      )
      .join('');
    table.innerHTML =
      '<thead><tr><th>宿主端口</th><th>容器端口</th><th>协议</th><th>宿主 IP</th></tr></thead>' +
      `<tbody>${rows || '<tr><td class="empty" colspan="4">无端口映射</td></tr>'}</tbody>`;
    wrap.appendChild(table);
    return wrap;
  }

  function buildMountsTable(mounts) {
    const wrap = create('div', 'table-wrap');
    const table = create('table', 'table docker-detail-table');
    const rows = (mounts || [])
      .map(
        (mount) =>
          `<tr><td data-label="类型">${esc(mount.type || 'bind')}</td>` +
          `<td data-label="宿主路径"><span class="cell-mono docker-mount-path" title="${esc(mount.source || '')}">${esc(mount.source || '-')}</span></td>` +
          `<td data-label="容器路径"><span class="cell-mono docker-mount-path" title="${esc(mount.destination || '')}">${esc(mount.destination || '-')}</span></td>` +
          `<td data-label="读写">${mount.ro ? '只读' : '可读写'}</td></tr>`
      )
      .join('');
    table.innerHTML =
      '<thead><tr><th>类型</th><th>宿主路径</th><th>容器路径</th><th>读写</th></tr></thead>' +
      `<tbody>${rows || '<tr><td class="empty" colspan="4">无挂载</td></tr>'}</tbody>`;
    wrap.appendChild(table);
    return wrap;
  }

  /** 环境变量：敏感键默认打码，可点击显示 */
  function buildEnvList(envList) {
    const box = create('div', 'docker-env-list');
    if (!envList || !envList.length) {
      box.appendChild(create('p', 'docker-hint', '无环境变量'));
      return box;
    }
    for (const entry of envList) {
      const raw = String(entry || '');
      const index = raw.indexOf('=');
      const key = index > 0 ? raw.slice(0, index) : raw;
      const value = index > 0 ? raw.slice(index + 1) : '';
      const row = create('div', 'docker-env-row');
      row.appendChild(create('span', 'docker-env-key', key));
      const sensitive = SENSITIVE_KEY.test(key);
      const valueNode = create('span', 'docker-env-value', sensitive ? '••••••' : value);
      row.appendChild(valueNode);
      if (sensitive) {
        const toggle = mkButton('btn btn-ghost btn-sm', '', '显示');
        toggle.addEventListener('click', () => {
          const hidden = toggle.dataset.shown !== '1';
          toggle.dataset.shown = hidden ? '1' : '0';
          valueNode.textContent = hidden ? value : '••••••';
          toggle.textContent = hidden ? '隐藏' : '显示';
        });
        row.appendChild(toggle);
      }
      box.appendChild(row);
    }
    return box;
  }

  function buildNetworkChips(networks) {
    const box = create('div', 'docker-chips');
    if (!networks || !networks.length) {
      box.appendChild(create('span', 'docker-port-none', '未连接自定义网络'));
      return box;
    }
    for (const name of networks) {
      box.appendChild(create('span', 'docker-chip', name));
    }
    return box;
  }

  function buildLogSection() {
    const { section, head } = drawerSection('实时日志');
    const tools = create('div', 'docker-log-tools');

    const tailSelect = mkSelect(
      [
        { value: '100', label: '最近 100 行' },
        { value: '500', label: '最近 500 行' },
        { value: '2000', label: '最近 2000 行' }
      ],
      'dockerLogTail'
    );
    tailSelect.value = String(S.drawer.tail);
    tailSelect.addEventListener('change', () => {
      S.drawer.tail = Number(tailSelect.value) || 200;
      startLogs();
    });

    const auto = checkboxRow('dockerLogAutoScroll', '自动滚动');
    auto.input.checked = S.drawer.autoScroll;
    auto.input.addEventListener('change', () => {
      S.drawer.autoScroll = auto.input.checked;
      if (S.drawer.autoScroll) {
        renderLogs();
      }
    });

    const clearBtn = mkButton('btn btn-ghost btn-sm', '', '清屏');
    clearBtn.id = 'dockerLogClear';
    clearBtn.addEventListener('click', () => {
      S.drawer.logs = [];
      renderLogs();
    });

    const downloadBtn = mkButton('btn btn-ghost btn-sm', 'i-download', '下载日志');
    downloadBtn.id = 'dockerLogDownload';
    downloadBtn.addEventListener('click', downloadLogs);

    tools.append(tailSelect, auto.wrap, clearBtn, downloadBtn);
    head.appendChild(tools);

    const pre = document.createElement('pre');
    pre.className = 'docker-log';
    pre.id = 'dockerLogs';
    pre.setAttribute('aria-live', 'polite');
    pre.dataset.empty = '1';
    section.appendChild(pre);

    const status = create('div', 'docker-log-status');
    status.id = 'dockerLogStatus';
    section.appendChild(status);
    return section;
  }

  /** 渲染整个抽屉内容；日志与资源占用分别由各自的定时器局部更新 */
  function renderDrawer() {
    const body = $('dockerDrawerBody');
    const info = S.drawer.inspect;
    const container = findContainer(S.drawer.id);
    const titleNode = $('dockerDrawerTitle');
    const subNode = $('dockerDrawerSub');

    const displayName = (info && info.name) || (container && container.name) || '容器详情';
    const stateText = info
      ? STATE_LABELS[info.state] || info.state || ''
      : container
        ? STATE_LABELS[container.state] || container.state || ''
        : '';
    const shortId = (info && info.shortId) || (container && container.shortId) || '';
    if (titleNode) {
      titleNode.textContent = displayName;
    }
    if (subNode) {
      subNode.textContent = [shortId, stateText].filter(Boolean).join(' · ');
    }
    if (!body) {
      return;
    }
    clear(body);

    if (!info) {
      body.appendChild(create('p', 'docker-hint', '正在读取容器详情…'));
      return;
    }

    const infoSection = drawerSection('基本信息');
    infoSection.section.appendChild(buildInfoGrid(info));
    body.appendChild(infoSection.section);

    const statsSection = drawerSection('资源占用');
    const statsBox = create('div', 'docker-stats');
    statsBox.id = 'dockerStats';
    statsBox.appendChild(create('p', 'docker-hint', '每 3 秒刷新一次…'));
    statsSection.section.appendChild(statsBox);
    body.appendChild(statsSection.section);

    const portSection = drawerSection('端口映射');
    portSection.section.appendChild(buildPortsTable(info.ports));
    body.appendChild(portSection.section);

    const mountSection = drawerSection('挂载');
    mountSection.section.appendChild(buildMountsTable(info.mounts));
    body.appendChild(mountSection.section);

    const envSection = drawerSection('环境变量');
    envSection.section.appendChild(buildEnvList(info.env));
    body.appendChild(envSection.section);

    const networkSection = drawerSection('网络');
    networkSection.section.appendChild(buildNetworkChips(info.networks));
    body.appendChild(networkSection.section);

    body.appendChild(buildLogSection());
    renderLogs();
    loadStats();
  }

  /* ------------------------------ 实时日志 ------------------------------ */

  function setLogStatus(text) {
    const node = $('dockerLogStatus');
    if (node) {
      node.textContent = text || '';
    }
  }

  function renderLogs() {
    const pre = $('dockerLogs');
    if (!pre) {
      return;
    }
    const text = S.drawer.logs.join('');
    if (text) {
      pre.textContent = text;
      pre.dataset.empty = '0';
    } else {
      pre.textContent = '正在等待日志输出…';
      pre.dataset.empty = '1';
    }
    if (S.drawer.autoScroll) {
      pre.scrollTop = pre.scrollHeight;
    }
  }

  /** 追加日志片段，总量超出上限时丢弃最早内容 */
  function appendLog(chunk) {
    if (!chunk) {
      return;
    }
    const text = String(chunk).replace(/\r/g, '');
    S.drawer.logs.push(text);
    let total = 0;
    for (const item of S.drawer.logs) {
      total += item.length;
    }
    while (total > 400000 && S.drawer.logs.length > 1) {
      total -= S.drawer.logs.shift().length;
    }
    const pre = $('dockerLogs');
    if (!pre) {
      return;
    }
    if (pre.dataset.empty === '1') {
      pre.textContent = '';
      pre.dataset.empty = '0';
    }
    pre.textContent += text;
    if (pre.textContent.length > 400000) {
      pre.textContent = pre.textContent.slice(-300000);
    }
    if (S.drawer.autoScroll) {
      pre.scrollTop = pre.scrollHeight;
    }
  }

  function downloadLogs() {
    const text = S.drawer.logs.join('');
    if (!text) {
      toast('当前没有可下载的日志', 'warn');
      return;
    }
    const container = findContainer(S.drawer.id);
    const name = ((S.drawer.inspect && S.drawer.inspect.name) || (container && container.name) || 'container').replace(
      /[^a-zA-Z0-9_.-]/g,
      '_'
    );
    const blob = new Blob([text], { type: 'text/plain;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = `${name}-logs.txt`;
    document.body.appendChild(link);
    link.click();
    link.remove();
    window.setTimeout(() => URL.revokeObjectURL(url), 2000);
    toast('日志已开始下载', 'success');
  }

  function stopLogs() {
    S.drawer.gen = (S.drawer.gen || 0) + 1;
    if (S.drawer.ws) {
      try {
        S.drawer.ws.close();
      } catch (wsErr) {
        /* 忽略关闭异常 */
      }
      S.drawer.ws = null;
    }
    if (S.drawer.polling) {
      clearInterval(S.drawer.polling);
      S.drawer.polling = null;
    }
    S.drawer.wsOpened = false;
    setLogStatus('');
  }

  /** 优先 WebSocket 实时日志，失败时降级为 3 秒轮询 */
  function startLogs() {
    stopLogs();
    const id = S.drawer.id;
    if (!id) {
      return;
    }
    S.drawer.logs = [];
    renderLogs();
    const token = A.state && A.state.token ? A.state.token : '';
    if (!token || typeof WebSocket !== 'function') {
      startLogPolling();
      return;
    }
    const gen = S.drawer.gen;
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    const url =
      `${proto}//${location.host}/ws/docker/logs?id=${encodeURIComponent(id)}` +
      `&token=${encodeURIComponent(token)}&tail=${S.drawer.tail}`;
    let ws;
    try {
      ws = new WebSocket(url);
    } catch (wsErr) {
      startLogPolling();
      return;
    }
    S.drawer.ws = ws;
    let fellBack = false;
    const fallback = () => {
      if (fellBack || S.drawer.gen !== gen) {
        return;
      }
      fellBack = true;
      try {
        ws.close();
      } catch (closeErr) {
        /* 忽略关闭异常 */
      }
      if (S.drawer.ws === ws) {
        S.drawer.ws = null;
      }
      startLogPolling();
    };

    ws.onmessage = (event) => {
      if (S.drawer.gen !== gen) {
        return;
      }
      let payload;
      try {
        payload = JSON.parse(event.data);
      } catch (parseErr) {
        appendLog(String(event.data));
        return;
      }
      if (payload.type === 'start') {
        S.drawer.wsOpened = true;
        setLogStatus('已连接实时日志流');
      } else if (payload.type === 'data') {
        S.drawer.wsOpened = true;
        appendLog(payload.data || '');
      } else if (payload.type === 'error') {
        appendLog(`${payload.message || '日志流出错'}\n`);
        setLogStatus(payload.message || '日志流出错');
      } else if (payload.type === 'end') {
        setLogStatus(payload.message || '日志流已结束');
      }
    };
    ws.onerror = () => {
      fallback();
    };
    ws.onclose = () => {
      if (S.drawer.gen !== gen) {
        return;
      }
      if (S.drawer.ws === ws) {
        S.drawer.ws = null;
      }
      if (!fellBack && !S.drawer.wsOpened) {
        fallback();
      }
    };
  }

  function startLogPolling() {
    const id = S.drawer.id;
    if (!id) {
      return;
    }
    const gen = S.drawer.gen;
    if (S.drawer.polling) {
      clearInterval(S.drawer.polling);
    }
    setLogStatus('实时连接不可用，已切换为 3 秒轮询');
    const run = async () => {
      if (S.drawer.gen !== gen || !S.drawer.open || S.drawer.id !== id || document.hidden) {
        return;
      }
      try {
        const data = await api(
          `/api/docker/containers/${encodeURIComponent(id)}/logs?tail=${S.drawer.tail}`
        );
        if (S.drawer.gen !== gen) {
          return;
        }
        let text = '';
        if (data && typeof data.text === 'string') {
          text = data.text;
        } else if (data && Array.isArray(data.logs)) {
          text = data.logs
            .map((line) => (line && typeof line === 'object' ? line.line || '' : String(line)))
            .join('\n');
        }
        S.drawer.logs = text ? [text.replace(/\n+$/, '') + '\n'] : [];
        renderLogs();
      } catch (err) {
        if (S.drawer.gen === gen) {
          setLogStatus(err.message || '读取日志失败');
        }
      }
    };
    run().catch(() => {});
    S.drawer.polling = setInterval(() => {
      run().catch(() => {});
    }, LOG_POLL_INTERVAL);
  }

  /* ------------------------------ 资源占用 ------------------------------ */

  function statBar(label, valueText, percent) {
    const width = Math.max(0, Math.min(100, Number(percent) || 0));
    return (
      `<div class="docker-stat"><div class="docker-stat-head">` +
      `<span>${esc(label)}</span><strong>${esc(valueText)}</strong></div>` +
      `<div class="docker-bar"><span style="width:${width.toFixed(1)}%"></span></div></div>`
    );
  }

  function renderStats(data) {
    const box = $('dockerStats');
    if (!box) {
      return;
    }
    if (!data || data.available === false) {
      box.innerHTML = `<p class="docker-hint">${esc((data && data.message) || '资源占用信息暂不可用')}</p>`;
      return;
    }
    const cpu = Number(data.cpuPercent) || 0;
    const memUsage = Number(data.memUsage) || 0;
    const memLimit = Number(data.memLimit) || 0;
    const memPercent = Number(data.memPercent) || (memLimit ? (memUsage / memLimit) * 100 : 0);
    const netIn = Number(data.netIn) || 0;
    const netOut = Number(data.netOut) || 0;
    const blockRead = Number(data.blockRead) || 0;
    const blockWrite = Number(data.blockWrite) || 0;
    const netMax = Math.max(netIn, netOut, 1);
    const diskMax = Math.max(blockRead, blockWrite, 1);

    box.innerHTML =
      statBar('CPU 使用率', `${cpu.toFixed(1)}%`, cpu) +
      statBar('内存占用', `${sizeText(memUsage)} / ${sizeText(memLimit)}（${memPercent.toFixed(1)}%）`, memPercent) +
      statBar('网络接收', sizeText(netIn), (netIn / netMax) * 100) +
      statBar('网络发送', sizeText(netOut), (netOut / netMax) * 100) +
      statBar('磁盘读取', sizeText(blockRead), (blockRead / diskMax) * 100) +
      statBar('磁盘写入', sizeText(blockWrite), (blockWrite / diskMax) * 100) +
      `<p class="docker-hint">进程数 ${esc(String(data.pids == null ? '-' : data.pids))} · ` +
      '网络与磁盘条形按相对刻度显示</p>';
  }

  async function loadStats() {
    const id = S.drawer.id;
    if (!id || !S.drawer.open) {
      return;
    }
    const box = $('dockerStats');
    if (!box) {
      return;
    }
    try {
      const data = await api(`/api/docker/containers/${encodeURIComponent(id)}/stats`);
      if (S.drawer.id !== id || !S.drawer.open) {
        return;
      }
      renderStats(data);
    } catch (err) {
      if (box && S.drawer.id === id) {
        box.innerHTML = `<p class="docker-hint">${esc(err.message || '读取资源占用失败')}</p>`;
      }
    }
  }

  function stopStats() {
    if (S.drawer.statsTimer) {
      clearInterval(S.drawer.statsTimer);
      S.drawer.statsTimer = null;
    }
  }

  function startStats() {
    stopStats();
    loadStats().catch(() => {});
    S.drawer.statsTimer = setInterval(() => {
      if (!S.drawer.open || document.hidden) {
        return;
      }
      loadStats().catch(() => {});
    }, STATS_INTERVAL);
  }

  /* ------------------------------ 抽屉开关 ------------------------------ */

  function stopDrawerTimers() {
    stopStats();
    stopLogs();
  }

  function closeDrawerImmediate() {
    S.drawer.open = false;
    S.drawer.id = '';
    S.drawer.inspect = null;
    S.drawer.logs = [];
    stopDrawerTimers();
    const drawer = $('dockerDrawer');
    const mask = $('dockerDrawerMask');
    if (drawer) {
      drawer.classList.remove('is-open');
      drawer.hidden = true;
    }
    if (mask) {
      mask.classList.remove('is-open');
      mask.hidden = true;
    }
    document.body.classList.remove('docker-drawer-open');
  }

  function closeDrawer() {
    if (!S.drawer.open) {
      return;
    }
    S.drawer.open = false;
    S.drawer.id = '';
    S.drawer.inspect = null;
    stopDrawerTimers();
    const drawer = $('dockerDrawer');
    const mask = $('dockerDrawerMask');
    if (drawer) {
      drawer.classList.remove('is-open');
    }
    if (mask) {
      mask.classList.remove('is-open');
    }
    document.body.classList.remove('docker-drawer-open');
    window.setTimeout(() => {
      if (S.drawer.open) {
        return;
      }
      if (drawer) {
        drawer.hidden = true;
      }
      if (mask) {
        mask.hidden = true;
      }
    }, 220);
  }

  async function openDrawer(id, focusLogs) {
    if (!can('DOCKER_VIEW')) {
      toast('当前账号没有查看容器详情的权限', 'warn');
      return;
    }
    const container = findContainer(id);
    if (!container) {
      toast('容器已不存在，请刷新后重试', 'warn');
      return;
    }
    closeDrawerImmediate();
    S.drawer.open = true;
    S.drawer.id = id;
    S.drawer.inspect = null;
    S.drawer.logs = [];
    S.drawer.tail = 200;
    S.drawer.autoScroll = true;
    S.drawer.wsOpened = false;
    S.drawer.gen = (S.drawer.gen || 0) + 1;

    const drawer = $('dockerDrawer');
    const mask = $('dockerDrawerMask');
    if (drawer) {
      drawer.hidden = false;
      drawer.scrollTop = 0;
    }
    if (mask) {
      mask.hidden = false;
    }
    document.body.classList.add('docker-drawer-open');
    window.requestAnimationFrame(() => {
      if (!S.drawer.open) {
        return;
      }
      if (drawer) {
        drawer.classList.add('is-open');
      }
      if (mask) {
        mask.classList.add('is-open');
      }
    });
    renderDrawer();

    try {
      const info = await api(`/api/docker/containers/${encodeURIComponent(id)}`);
      if (!S.drawer.open || S.drawer.id !== id) {
        return;
      }
      S.drawer.inspect = info;
      renderDrawer();
    } catch (err) {
      if (!S.drawer.open || S.drawer.id !== id) {
        return;
      }
      const body = $('dockerDrawerBody');
      if (body) {
        clear(body);
        body.appendChild(create('p', 'docker-hint', err.message || '读取容器详情失败'));
      }
      toast(err.message || '读取容器详情失败', 'error');
      return;
    }

    startStats();
    startLogs();
    if (focusLogs) {
      const target = $('dockerLogs');
      if (target && typeof target.scrollIntoView === 'function') {
        target.scrollIntoView({ block: 'nearest' });
      }
    }
  }

  /* ------------------------------ 挂载与轮询入口 ------------------------------ */

  async function retryLoad() {
    const ok = await loadOverview();
    if (!ok) {
      renderEmptyTables('Docker 服务不可用');
      return false;
    }
    await loadTab(S.tab, true);
    return true;
  }

  async function mount() {
    S.mounted = true;
    S.tab = readTabFromHash();
    S.selected.clear();
    S.filters.q = '';
    S.filters.state = 'all';
    const search = $('dockerSearch');
    if (search) {
      search.value = '';
    }
    const filter = $('dockerStateFilter');
    if (filter) {
      filter.value = 'all';
    }
    const auto = $('dockerAutoRefresh');
    if (auto) {
      auto.checked = !S.paused;
    }
    closeDrawerImmediate();
    renderTabs();
    renderCounts();
    await retryLoad();
    startPolling();
  }

  /* ------------------------------ 事件绑定 ------------------------------ */

  let inited = false;

  /** 批量操作：顺序执行，避免同时向 Docker 发起大量请求 */
  async function batchAction(action) {
    const list = S.containers.filter((item) => S.selected.has(item.id));
    if (!list.length) {
      toast('请先选择要操作的容器', 'warn');
      return;
    }
    if (action === 'delete') {
      openDeleteContainersModal(list);
      return;
    }
    const label = ACTION_LABELS[action] || action;
    let success = 0;
    const failures = [];
    for (const container of list) {
      try {
        await api(`/api/docker/containers/${encodeURIComponent(container.id)}/${action}`, {
          method: 'POST',
          body: {}
        });
        success += 1;
      } catch (err) {
        failures.push(`${container.name}：${err.message || '操作失败'}`);
      }
    }
    if (success > 0) {
      toast(`已${label} ${success} 个容器`, 'success');
    }
    if (failures.length > 0) {
      toast(
        failures.length > 1 ? `${failures[0]}（共 ${failures.length} 项失败）` : failures[0],
        'error'
      );
    }
    await refreshCurrent(true);
  }

  function init() {
    if (inited) {
      return;
    }
    inited = true;

    const retryBtn = $('dockerRetry');
    if (retryBtn) {
      retryBtn.addEventListener('click', () => {
        withBusy(retryBtn, async () => {
          const ok = await retryLoad();
          if (ok) {
            startPolling();
          }
        }).catch((err) => toast(err.message || '重新连接失败', 'error'));
      });
    }

    const refreshBtn = $('dockerRefreshBtn');
    if (refreshBtn) {
      refreshBtn.addEventListener('click', () => {
        withBusy(refreshBtn, async () => {
          const ok = await refreshCurrent(false);
          if (ok) {
            startPolling();
          }
        }).catch((err) => toast(err.message || '刷新失败', 'error'));
      });
    }

    const autoRefresh = $('dockerAutoRefresh');
    if (autoRefresh) {
      autoRefresh.addEventListener('change', () => {
        S.paused = !autoRefresh.checked;
        if (S.paused) {
          stopPolling();
        } else {
          startPolling();
          refreshCurrent(true).catch(() => {});
        }
      });
    }

    const tabs = $('dockerTabs');
    if (tabs) {
      tabs.addEventListener('click', (event) => {
        const tab = event.target.closest('[data-dtab]');
        if (tab) {
          setTab(tab.dataset.dtab);
        }
      });
    }

    const search = $('dockerSearch');
    if (search) {
      search.addEventListener('input', () => {
        S.filters.q = search.value;
        renderContainers();
      });
    }

    const stateFilter = $('dockerStateFilter');
    if (stateFilter) {
      stateFilter.addEventListener('change', () => {
        S.filters.state = stateFilter.value;
        fetchContainers()
          .then(() => renderContainers())
          .catch((err) => toast(err.message || '筛选容器失败', 'error'));
      });
    }

    const expandBtn = $('dockerExpandPortsBtn');
    if (expandBtn) {
      expandBtn.addEventListener('click', () => {
        S.expandPorts = !S.expandPorts;
        expandBtn.setAttribute('aria-expanded', S.expandPorts ? 'true' : 'false');
        expandBtn.replaceChildren(
          svgIcon('i-chevron'),
          document.createTextNode(S.expandPorts ? '收起端口映射' : '展开全部端口')
        );
        renderContainers();
      });
    }

    const createBtn = $('dockerCreateBtn');
    if (createBtn) {
      createBtn.addEventListener('click', openCreateModal);
    }

    const selectAll = $('dockerSelectAll');
    if (selectAll) {
      selectAll.addEventListener('change', () => {
        const list = filteredContainers();
        for (const container of list) {
          if (selectAll.checked) {
            S.selected.add(container.id);
          } else {
            S.selected.delete(container.id);
          }
        }
        renderContainers();
      });
    }

    const containerBody = $('dockerBody');
    if (containerBody) {
      containerBody.addEventListener('change', (event) => {
        const box = event.target.closest('[data-dsel]');
        if (!box) {
          return;
        }
        const id = box.dataset.dsel;
        if (box.checked) {
          S.selected.add(id);
        } else {
          S.selected.delete(id);
        }
        const row = box.closest('tr');
        if (row) {
          row.classList.toggle('selected', box.checked);
        }
        updateBatchBar();
      });
      containerBody.addEventListener('click', (event) => {
        const detail = event.target.closest('[data-ddetail]');
        if (detail) {
          openDrawer(detail.dataset.ddetail, false).catch((err) =>
            toast(err.message || '打开容器详情失败', 'error')
          );
          return;
        }
        const actionBtn = event.target.closest('[data-dact]');
        if (actionBtn) {
          runRowAction(actionBtn.dataset.dact, actionBtn.dataset.id, actionBtn);
        }
      });
    }

    const batchMap = {
      dockerBatchStart: 'start',
      dockerBatchStop: 'stop',
      dockerBatchRestart: 'restart'
    };
    for (const [id, action] of Object.entries(batchMap)) {
      const button = $(id);
      if (button) {
        button.addEventListener('click', () => {
          withBusy(button, () => batchAction(action)).catch((err) =>
            toast(err.message || '批量操作失败', 'error')
          );
        });
      }
    }
    const batchDelete = $('dockerBatchDelete');
    if (batchDelete) {
      batchDelete.addEventListener('click', () => {
        const list = S.containers.filter((item) => S.selected.has(item.id));
        if (!list.length) {
          toast('请先选择要删除的容器', 'warn');
          return;
        }
        openDeleteContainersModal(list);
      });
    }
    const batchClear = $('dockerBatchClear');
    if (batchClear) {
      batchClear.addEventListener('click', () => {
        S.selected.clear();
        renderContainers();
      });
    }

    /* 镜像 */
    const pullBtn = $('dockerPullBtn');
    const imageInput = $('dockerImageInput');
    const pullStatus = $('dockerPullStatus');
    if (pullBtn) {
      pullBtn.addEventListener('click', () => {
        const name = imageInput ? imageInput.value.trim() : '';
        if (!name) {
          toast('请输入要拉取的镜像名称', 'warn');
          if (imageInput) {
            imageInput.focus();
          }
          return;
        }
        pullImage(name, pullBtn, pullStatus).catch(() => {});
      });
    }
    if (imageInput) {
      imageInput.addEventListener('keydown', (event) => {
        if (event.key === 'Enter') {
          event.preventDefault();
          if (pullBtn) {
            pullBtn.click();
          }
        }
      });
    }
    const pruneBtn = $('dockerPruneBtn');
    if (pruneBtn) {
      pruneBtn.addEventListener('click', openPruneImagesModal);
    }
    const imageBody = $('dockerImageBody');
    if (imageBody) {
      imageBody.addEventListener('click', (event) => {
        const button = event.target.closest('[data-diact="delete"]');
        if (!button) {
          return;
        }
        const image = S.images.find((item) => item.id === button.dataset.id);
        if (image) {
          openDeleteImageModal(image);
        }
      });
    }

    /* 数据卷 */
    const volumeCreate = $('dockerVolumeCreate');
    const volumeName = $('dockerVolumeName');
    if (volumeCreate) {
      volumeCreate.addEventListener('click', () => {
        createVolume().catch((err) => toast(err.message || '创建数据卷失败', 'error'));
      });
    }
    if (volumeName) {
      volumeName.addEventListener('keydown', (event) => {
        if (event.key === 'Enter') {
          event.preventDefault();
          if (volumeCreate) {
            volumeCreate.click();
          }
        }
      });
    }
    const volumeBody = $('dockerVolumeBody');
    if (volumeBody) {
      volumeBody.addEventListener('click', (event) => {
        const button = event.target.closest('[data-dvact="delete"]');
        if (!button) {
          return;
        }
        const volume = S.volumes.find((item) => item.name === button.dataset.name);
        if (volume) {
          openDeleteVolumeModal(volume);
        }
      });
    }

    /* 网络 */
    const networkCreate = $('dockerNetworkCreate');
    const networkName = $('dockerNetworkName');
    if (networkCreate) {
      networkCreate.addEventListener('click', () => {
        createNetwork().catch((err) => toast(err.message || '创建网络失败', 'error'));
      });
    }
    if (networkName) {
      networkName.addEventListener('keydown', (event) => {
        if (event.key === 'Enter') {
          event.preventDefault();
          if (networkCreate) {
            networkCreate.click();
          }
        }
      });
    }
    const networkBody = $('dockerNetworkBody');
    if (networkBody) {
      networkBody.addEventListener('click', (event) => {
        const button = event.target.closest('[data-dnact="delete"]');
        if (!button) {
          return;
        }
        const network = S.networks.find((item) => item.name === button.dataset.name);
        if (network) {
          openDeleteNetworkModal(network);
        }
      });
    }

    /* 抽屉 */
    const drawerClose = $('dockerDrawerClose');
    if (drawerClose) {
      drawerClose.addEventListener('click', closeDrawer);
    }
    const drawerMask = $('dockerDrawerMask');
    if (drawerMask) {
      drawerMask.addEventListener('click', closeDrawer);
    }

    /* 页面可见性与路由变化 */
    document.addEventListener('visibilitychange', () => {
      if (document.hidden) {
        stopPolling();
        return;
      }
      if (S.mounted && currentView() === 'docker') {
        startPolling();
        refreshCurrent(true).catch(() => {});
      }
    });

    window.addEventListener('hashchange', () => {
      if (currentView() === 'docker') {
        return;
      }
      S.mounted = false;
      stopPolling();
      closeDrawerImmediate();
    });

    document.addEventListener('keydown', (event) => {
      if (event.key !== 'Escape' || !S.drawer.open) {
        return;
      }
      const modalRoot = $('modalRoot');
      if (modalRoot && !modalRoot.hidden) {
        return;
      }
      event.preventDefault();
      closeDrawer();
    });
  }

  A.registerView('docker', {
    mount,
    refresh: () => refreshCurrent(false)
  });

  init();
})();
