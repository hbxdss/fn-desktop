'use strict';

/**
 * 飞牛终端 · Web 管理后台 —— 「审计日志」与「系统设置」两个视图
 * 依赖 app.js 暴露的 window.FnosAdmin 基础设施，不引用任何外部资源。
 */

(function () {
  const A = window.FnosAdmin;
  if (!A) {
    return;
  }

  const { $, api, toast, create, clear, formatDateTime, registerView, openModal, state } = A;

  /** 与 app.js 中的 TOKEN_KEY 保持一致：修改密码后需要替换本地令牌 */
  const TOKEN_KEY = 'fnos_admin_token';

  /** 审计动作码 → 中文文案（未收录的动作码原样展示） */
  const ACTION_LABELS = {
    LOGIN: '登录',
    LOGOUT: '退出登录',
    LOGIN_FAILED: '登录失败',
    SETUP: '初始化',
    PASSWORD_CHANGE: '修改密码',
    USER_CREATE: '新建用户',
    USER_UPDATE: '修改用户',
    USER_DELETE: '删除用户',
    USER_PASSWORD_RESET: '重置密码',
    FILE_UPLOAD: '上传文件',
    FILE_DELETE: '删除文件',
    FILE_RENAME: '重命名',
    FILE_MOVE: '移动文件',
    FILE_COPY: '复制文件',
    FILE_COMPRESS: '压缩文件',
    FILE_EXTRACT: '解压文件',
    FILE_DOWNLOAD: '下载文件',
    SHARE_CREATE: '创建分享',
    SHARE_DELETE: '删除分享',
    DOCKER_CREATE: '创建容器',
    DOCKER_START: '启动容器',
    DOCKER_STOP: '停止容器',
    DOCKER_RESTART: '重启容器',
    DOCKER_DELETE: '删除容器',
    DOCKER_IMAGE_PULL: '拉取镜像',
    DOCKER_IMAGE_DELETE: '删除镜像',
    DOCKER_VOLUME_CREATE: '创建卷',
    DOCKER_VOLUME_DELETE: '删除卷',
    DOCKER_NETWORK_CREATE: '创建网络',
    DOCKER_NETWORK_DELETE: '删除网络'
  };

  const AUDIT_LIMIT = 100;
  let auditTotal = 0;
  let settingsBound = false;

  /* ------------------------------ 审计日志 ------------------------------ */

  function actionText(action) {
    if (!action) {
      return '-';
    }
    return ACTION_LABELS[action] || action;
  }

  function renderAuditEmpty() {
    const body = $('auditBody');
    if (!body) {
      return;
    }
    clear(body);
    const tr = create('tr');
    const td = create('td', 'empty', '还没有任何操作记录。登录、上传文件或操作容器后会在这里留下痕迹。');
    td.colSpan = 5;
    tr.appendChild(td);
    body.appendChild(tr);
  }

  function renderAudit(items) {
    const body = $('auditBody');
    if (!body) {
      return;
    }
    clear(body);
    if (!Array.isArray(items) || items.length === 0) {
      renderAuditEmpty();
      return;
    }
    const fragment = document.createDocumentFragment();
    for (const item of items) {
      const tr = create('tr');

      const time = create('td', 'cell-sub', formatDateTime(item.createdAt));
      time.dataset.label = '时间';
      tr.appendChild(time);

      const user = create('td');
      user.appendChild(create('strong', null, item.username || '系统'));
      user.dataset.label = '账号';
      tr.appendChild(user);

      const action = create('td');
      action.appendChild(create('span', 'tag', actionText(item.action)));
      action.dataset.label = '动作';
      tr.appendChild(action);

      const detail = create('td', 'cell-sub', item.detail || '-');
      detail.dataset.label = '详情';
      tr.appendChild(detail);

      const ip = create('td', 'cell-sub cell-mono', item.ip || '-');
      ip.dataset.label = '来源 IP';
      tr.appendChild(ip);

      fragment.appendChild(tr);
    }
    body.appendChild(fragment);
  }

  async function loadAudit() {
    const summary = $('auditSummary');
    if (summary) {
      summary.textContent = '加载中…';
    }
    try {
      const data = await api(`/api/audit?limit=${AUDIT_LIMIT}`);
      auditTotal = Number(data.total) || 0;
      renderAudit(data.items);
      if (summary) {
        summary.textContent =
          auditTotal > AUDIT_LIMIT
            ? `共 ${auditTotal} 条 · 显示最近 ${AUDIT_LIMIT} 条`
            : `共 ${auditTotal} 条`;
      }
    } catch (err) {
      if (summary) {
        summary.textContent = '读取失败';
      }
      renderAuditEmpty();
      throw err;
    }
  }

  function bindAudit() {
    const button = $('auditRefreshBtn');
    if (button && button.dataset.bound !== '1') {
      button.dataset.bound = '1';
      button.addEventListener('click', () => {
        button.disabled = true;
        loadAudit()
          .then(() => toast('审计日志已刷新', 'success'))
          .catch((err) => {
            if (err && err.status !== 401) {
              toast(err.message || '审计日志读取失败', 'error');
            }
          })
          .finally(() => {
            button.disabled = false;
          });
      });
    }
  }

  /* ------------------------------ 系统设置 ------------------------------ */

  function fillSettingsInfo(info) {
    const list = $('settingsInfo');
    if (!list || !info) {
      return;
    }
    clear(list);
    const rows = [
      ['主机名', info.hostname || '-'],
      ['运行平台', `${info.platform || '-'} ${info.release || ''}`.trim()],
      ['系统架构', info.arch || '-'],
      ['处理器', `${info.cpuModel || '-'}（${info.cpuCores || 0} 核）`],
      ['Node 版本', info.nodeVersion || '-'],
      ['服务版本', info.serverVersion ? `v${info.serverVersion}` : '-'],
      ['数据目录', info.dataDir || '-'],
      ['文件根目录', info.filesRoot || '-']
    ];
    for (const [key, value] of rows) {
      const row = create('div');
      row.appendChild(create('dt', null, key));
      const dd = create('dd', 'cell-mono', value);
      row.appendChild(dd);
      list.appendChild(row);
    }
  }

  async function loadSettings() {
    const version = $('aboutVersion');
    const roleTag = $('settingsRoleTag');
    if (roleTag) {
      roleTag.textContent = state.roleLabel || (state.me ? state.me.role : '当前账号');
    }
    try {
      const info = await api('/api/system/info');
      fillSettingsInfo(info);
      if (version && info.serverVersion) {
        version.textContent = `v${info.serverVersion}`;
      }
    } catch (err) {
      const list = $('settingsInfo');
      if (list) {
        clear(list);
        const row = create('div');
        row.appendChild(create('dt', null, '状态'));
        row.appendChild(create('dd', null, err.message || '读取失败'));
        list.appendChild(row);
      }
      throw err;
    }
  }

  function showPwdError(message) {
    const node = $('pwdError');
    if (!node) {
      return;
    }
    if (!message) {
      node.hidden = true;
      node.textContent = '';
      return;
    }
    node.hidden = false;
    node.textContent = message;
  }

  async function submitPassword(event) {
    event.preventDefault();
    const oldNode = $('pwdOld');
    const newNode = $('pwdNew');
    const againNode = $('pwdNew2');
    const button = $('pwdSubmit');
    if (!oldNode || !newNode || !againNode || !button) {
      return;
    }
    const oldPassword = oldNode.value;
    const newPassword = newNode.value;
    const again = againNode.value;

    showPwdError('');
    if (!oldPassword) {
      showPwdError('请输入当前密码');
      oldNode.focus();
      return;
    }
    if (newPassword.length < 8) {
      showPwdError('新密码至少需要 8 位字符');
      newNode.focus();
      return;
    }
    if (!/[A-Za-z]/.test(newPassword) || !/[0-9]/.test(newPassword)) {
      showPwdError('新密码需同时包含字母与数字');
      newNode.focus();
      return;
    }
    if (newPassword !== again) {
      showPwdError('两次输入的新密码不一致');
      againNode.focus();
      return;
    }
    if (newPassword === oldPassword) {
      showPwdError('新密码不能与当前密码相同');
      newNode.focus();
      return;
    }

    button.disabled = true;
    button.setAttribute('aria-busy', 'true');
    try {
      const result = await api('/api/auth/password', {
        method: 'POST',
        body: { oldPassword, newPassword }
      });
      if (result && result.token) {
        state.token = result.token;
        try {
          window.sessionStorage.setItem(TOKEN_KEY, result.token);
        } catch (storageErr) {
          console.warn('无法保存新的登录令牌，刷新页面后需要重新登录：', storageErr);
        }
      }
      oldNode.value = '';
      newNode.value = '';
      againNode.value = '';
      toast(result && result.message ? result.message : '密码已更新', 'success');
    } catch (err) {
      if (err && err.status !== 401) {
        showPwdError(err.message || '密码修改失败');
      }
    } finally {
      button.disabled = false;
      button.removeAttribute('aria-busy');
    }
  }

  function bindSettings() {
    if (settingsBound) {
      return;
    }
    const form = $('pwdForm');
    if (form) {
      form.addEventListener('submit', submitPassword);
      // 用户重新输入时清除上一次的校验提示，避免旧错误残留
      for (const id of ['pwdOld', 'pwdNew', 'pwdNew2']) {
        const input = $(id);
        if (input) {
          input.addEventListener('input', () => showPwdError(''));
        }
      }
      settingsBound = true;
    }
  }

  /* ------------------------------ 存储挂载 ------------------------------ */

  /** 挂载点可用状态文案 */
  function mountStateText(item) {
    if (!item) {
      return '-';
    }
    if (!item.exists) {
      return '目录不存在';
    }
    if (!item.readable) {
      return item.error || '无读取权限';
    }
    if (item.readonly) {
      return '只读挂载';
    }
    return item.writable ? '读写正常' : '无写入权限';
  }

  function mountStateClass(item) {
    if (!item || !item.exists || !item.readable) {
      return 'is-error';
    }
    if (item.readonly || !item.writable) {
      return 'is-warn';
    }
    return 'is-ok';
  }

  function renderMounts(items, needsSetup) {
    const list = $('mountList');
    const tag = $('mountCountTag');
    if (tag) {
      tag.textContent = `${items.length} 个挂载点`;
    }
    if (!list) {
      return;
    }
    clear(list);
    if (items.length === 0) {
      const hint = create(
        'p',
        'settings-hint',
        '还没有挂载任何目录。先到飞牛「设置 → 应用 → 飞牛终端 → 文件夹权限」授予目录权限，'
        + '再点上方「自动挂载 /vol1」或手动填写路径添加挂载。'
      );
      if (needsSetup) {
        hint.classList.add('is-alert');
      }
      list.appendChild(hint);
      return;
    }
    for (const item of items) {
      const row = create('div', 'mount-item');
      const main = create('div', 'mount-item-main');
      const title = create('div', 'mount-item-title');
      title.appendChild(create('strong', null, item.name));
      title.appendChild(create('span', 'mount-item-path', item.absPath || '-'));
      main.appendChild(title);
      const meta = create('div', 'mount-item-meta');
      meta.appendChild(create('code', 'mount-item-virtual', `/m/${item.name}`));
      meta.appendChild(create('span', `mount-state ${mountStateClass(item)}`, mountStateText(item)));
      if (item.error && item.readable) {
        meta.appendChild(create('span', 'mount-item-msg', item.error));
      }
      main.appendChild(meta);
      row.appendChild(main);

      const remove = create('button', 'btn btn-danger btn-sm', '卸载');
      remove.type = 'button';
      remove.addEventListener('click', () => {
        remove.disabled = true;
        removeMount(item)
          .catch(() => {
            remove.disabled = false;
          });
      });
      row.appendChild(remove);
      list.appendChild(row);
    }
  }

  function showMountError(message) {
    const node = $('mountError');
    if (!node) {
      return;
    }
    if (!message) {
      node.hidden = true;
      node.textContent = '';
      return;
    }
    node.hidden = false;
    node.textContent = message;
  }

  async function loadMounts() {
    const list = $('mountList');
    try {
      const data = await api('/api/storage/mounts');
      const items = Array.isArray(data.items) ? data.items : [];
      renderMounts(items, Boolean(data.needsSetup));
      return data;
    } catch (err) {
      if (list) {
        clear(list);
        list.appendChild(create('p', 'form-error', (err && err.message) || '挂载点读取失败'));
      }
      const tag = $('mountCountTag');
      if (tag) {
        tag.textContent = '读取失败';
      }
      throw err;
    }
  }

  async function addMount() {
    const nameNode = $('mountName');
    const pathNode = $('mountPath');
    const readonlyNode = $('mountReadonly');
    const button = $('mountAddBtn');
    const name = nameNode ? nameNode.value.trim() : '';
    const absPath = pathNode ? pathNode.value.trim() : '';
    showMountError('');
    if (!name) {
      showMountError('请填写挂载名称');
      if (nameNode) {
        nameNode.focus();
      }
      return;
    }
    if (!absPath || absPath.charAt(0) !== '/') {
      showMountError('挂载路径必须是以 / 开头的 NAS 绝对路径');
      if (pathNode) {
        pathNode.focus();
      }
      return;
    }
    if (button) {
      button.disabled = true;
      button.setAttribute('aria-busy', 'true');
    }
    try {
      await api('/api/storage/mounts', {
        method: 'POST',
        body: { name, absPath, readonly: Boolean(readonlyNode && readonlyNode.checked) }
      });
      if (nameNode) {
        nameNode.value = '';
      }
      toast(`已挂载 ${absPath} → /m/${name}`, 'success');
      await loadMounts();
    } catch (err) {
      if (err && err.status !== 401) {
        showMountError((err && err.message) || '挂载失败');
      }
    } finally {
      if (button) {
        button.disabled = false;
        button.removeAttribute('aria-busy');
      }
    }
  }

  async function removeMount(item) {
    try {
      await api(`/api/storage/mounts?id=${encodeURIComponent(item.id)}`, { method: 'DELETE' });
      toast(`已卸载 ${item.absPath || item.name}`, 'success');
      await loadMounts();
    } catch (err) {
      if (err && err.status !== 401) {
        toast((err && err.message) || '卸载失败', 'error');
      }
      throw err;
    }
  }

  async function autoMount() {
    const button = $('mountAutoBtn');
    if (button) {
      button.disabled = true;
      button.setAttribute('aria-busy', 'true');
    }
    showMountError('');
    try {
      const result = await api('/api/storage/mounts/auto', { method: 'POST' });
      const added = Array.isArray(result.added) ? result.added : [];
      const skipped = Array.isArray(result.skipped) ? result.skipped : [];
      if (added.length === 0) {
        showMountError(
          `没有新增挂载点。${(skipped[0] && skipped[0].reason) || '请先在飞牛「设置 → 应用 → 飞牛终端 → 文件夹权限」授予 /vol1 及子目录权限。'}`
        );
      } else {
        toast(`已自动挂载 ${added.length} 个目录`, 'success');
      }
      await loadMounts();
    } catch (err) {
      if (err && err.status !== 401) {
        showMountError((err && err.message) || '自动挂载失败');
      }
    } finally {
      if (button) {
        button.disabled = false;
        button.removeAttribute('aria-busy');
      }
    }
  }

  /** 把选中的绝对路径填回输入框，并根据是否有写权限勾选只读 */
  function applyMountPath(absPath, writable) {
    const pathNode = $('mountPath');
    const nameNode = $('mountName');
    const readonlyNode = $('mountReadonly');
    if (pathNode) {
      pathNode.value = absPath;
    }
    if (nameNode && !nameNode.value.trim()) {
      const base = absPath.split('/').filter(Boolean).pop() || 'mount';
      nameNode.value = base;
    }
    if (readonlyNode && writable === false) {
      readonlyNode.checked = true;
    }
  }

  /** 目录浏览器的快捷路径与无权限提示文案 */
  const BROWSE_QUICK_PATHS = ['/vol1', '/vol1/1000', '/vol1/@appdata/fnos-terminal', '/vol1/@appcenter/fnos-terminal', '/tmp'];
  const PERMISSION_HINT =
    '应用账号没有读取该目录的权限。请到飞牛「设置 → 应用 → 飞牛终端 → 文件夹权限」把该文件夹加入允许列表并授予「读写」权限，然后点上方「前往」重新加载。';

  /** 目录浏览器（弹窗内逐级进入，确定后回填路径） */
  async function showMountBrowser(basePath) {
    let currentPath = String(basePath || '').trim();
    if (!currentPath || currentPath.charAt(0) !== '/') {
      currentPath = '/vol1';
    }

    const body = create('div', 'mount-browser');

    // 手动输入 + 前往
    const nav = create('div', 'mount-browser-nav');
    const pathInput = document.createElement('input');
    pathInput.type = 'text';
    pathInput.className = 'input mount-browser-input';
    pathInput.value = currentPath;
    pathInput.placeholder = '/vol1/1000';
    pathInput.setAttribute('aria-label', 'NAS 目录绝对路径');
    const goBtn = create('button', 'btn btn-ghost btn-sm', '前往');
    goBtn.type = 'button';
    nav.append(pathInput, goBtn);
    body.appendChild(nav);

    // 常用路径快捷入口
    const quick = create('div', 'mount-browser-quick');
    body.appendChild(quick);

    const current = create('p', 'mount-browser-path', `当前目录：${currentPath}`);
    body.appendChild(current);

    const list = create('div', 'mount-browser-list');
    body.appendChild(list);

    openModal({
      title: '选择 NAS 目录',
      body,
      wide: true,
      buttons: [
        {
          label: '选择当前目录',
          kind: 'btn-primary',
          onClick: ({ close }) => {
            applyMountPath(currentPath, null);
            close();
          }
        },
        { label: '取消' }
      ]
    });

    function renderQuick() {
      clear(quick);
      for (const path of BROWSE_QUICK_PATHS) {
        if (path === currentPath) {
          continue;
        }
        const chip = create('button', 'mount-quick-chip', path);
        chip.type = 'button';
        chip.addEventListener('click', () => {
          load(path);
        });
        quick.appendChild(chip);
      }
    }

    function renderError(message) {
      clear(list);
      const box = create('div', 'mount-browser-error');
      box.appendChild(create('p', 'form-error', message || '目录读取失败'));
      box.appendChild(create('p', 'settings-hint', PERMISSION_HINT));
      const retry = create('button', 'btn btn-ghost btn-sm', '重新加载');
      retry.type = 'button';
      retry.addEventListener('click', () => {
        load(currentPath);
      });
      box.appendChild(retry);
      list.appendChild(box);
    }

    async function load(target) {
      currentPath = target;
      pathInput.value = target;
      current.textContent = `当前目录：${target}`;
      renderQuick();
      clear(list);
      list.appendChild(create('p', 'settings-hint', '正在读取目录…'));

      let data = null;
      try {
        data = await api(`/api/storage/folders?path=${encodeURIComponent(target)}`);
      } catch (err) {
        if (err && err.status === 401) {
          return;
        }
        renderError((err && err.message) || '目录读取失败（通常是还没有授予文件夹权限）');
        return;
      }

      clear(list);
      const parent = target.replace(/\/+$/, '').split('/').slice(0, -1).join('/') || '/';
      if (target !== '/') {
        const up = create('button', 'mount-browser-item up', '.. 返回上级');
        up.type = 'button';
        up.addEventListener('click', () => {
          load(parent);
        });
        list.appendChild(up);
      }
      const items = Array.isArray(data.items) ? data.items : [];
      if (items.length === 0) {
        list.appendChild(create('p', 'settings-hint', '该目录下没有子文件夹，可直接「选择当前目录」。'));
        return;
      }
      for (const item of items) {
        const row = create('button', 'mount-browser-item');
        row.type = 'button';
        row.appendChild(create('span', 'mount-browser-name', item.name));
        const state = !item.readable ? '无读取权限' : item.writable ? '可读写' : '只读';
        row.appendChild(
          create('span', `mount-state ${item.readable ? (item.writable ? 'is-ok' : 'is-warn') : 'is-error'}`, state)
        );
        if (item.mounted) {
          row.appendChild(create('span', 'tag', '已挂载'));
        }
        row.addEventListener('click', () => {
          applyMountPath(item.path, item.writable);
          load(item.path);
        });
        list.appendChild(row);
      }
    }

    goBtn.addEventListener('click', () => {
      const value = pathInput.value.trim();
      if (!value || value.charAt(0) !== '/') {
        renderError('目录路径必须是以 / 开头的 NAS 绝对路径');
        pathInput.focus();
        return;
      }
      load(value);
    });
    pathInput.addEventListener('keydown', (event) => {
      if (event.key === 'Enter') {
        event.preventDefault();
        goBtn.click();
      }
    });

    await load(currentPath);
  }

  function bindMounts() {
    const add = $('mountAddBtn');
    if (add && add.dataset.bound !== '1') {
      add.dataset.bound = '1';
      add.addEventListener('click', () => {
        addMount();
      });
    }
    const auto = $('mountAutoBtn');
    if (auto && auto.dataset.bound !== '1') {
      auto.dataset.bound = '1';
      auto.addEventListener('click', () => {
        autoMount();
      });
    }
    const refresh = $('mountRefreshBtn');
    if (refresh && refresh.dataset.bound !== '1') {
      refresh.dataset.bound = '1';
      refresh.addEventListener('click', () => {
        loadMounts()
          .then(() => toast('挂载点已刷新', 'success'))
          .catch((err) => {
            if (err && err.status !== 401) {
              toast((err && err.message) || '挂载点刷新失败', 'error');
            }
          });
      });
    }
    const browse = $('mountBrowseBtn');
    if (browse && browse.dataset.bound !== '1') {
      browse.dataset.bound = '1';
      browse.addEventListener('click', () => {
        const pathNode = $('mountPath');
        const start = (pathNode && pathNode.value.trim()) || '/vol1';
        showMountBrowser(start.charAt(0) === '/' ? start : '/vol1');
      });
    }
    const pathNode = $('mountPath');
    if (pathNode) {
      pathNode.addEventListener('input', () => showMountError(''));
    }
  }

  /* ------------------------------ 视图注册 ------------------------------ */

  registerView('audit', {
    mount() {
      bindAudit();
      return loadAudit();
    },
    refresh() {
      return loadAudit();
    }
  });

  registerView('settings', {
    mount() {
      bindSettings();
      bindMounts();
      return Promise.all([
        loadSettings(),
        loadMounts().catch(() => {})
      ]);
    },
    refresh() {
      return Promise.all([
        loadSettings(),
        loadMounts().catch(() => {})
      ]);
    }
  });
})();
