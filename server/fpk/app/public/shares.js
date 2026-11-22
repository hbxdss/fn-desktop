'use strict';

/**
 * 分享管理视图（侧栏「分享」）
 *
 * 功能：分享列表（文件名 / 路径 / 加密 / 下载次数 / 有效期）、新建分享弹窗、
 * 复制链接（剪贴板 + 兼容回退）、打开分享页、删除分享，
 * 管理员可切换「查看全部用户」。
 *
 * 所有请求复用 app.js 暴露的 window.FnosAdmin.api（自动携带 Bearer 令牌）。
 */

(function () {
  const A = window.FnosAdmin;
  if (!A) {
    console.warn('[分享] 未找到 FnosAdmin 桥接对象，分享管理视图未启用');
    return;
  }

  const $ = A.$;
  const create = A.create;
  const clear = A.clear;
  const api = A.api;
  const toast = A.toast;
  const openModal = A.openModal;
  const formatDateTime = A.formatDateTime;
  const hasPerm = A.hasPerm;

  /** 有效期预设（小时），null 表示永久 */
  const EXPIRE_PRESETS = [
    { key: '1d', label: '1 天', hours: 24 },
    { key: '7d', label: '7 天', hours: 24 * 7 },
    { key: '30d', label: '30 天', hours: 24 * 30 },
    { key: 'forever', label: '永久有效', hours: null },
    { key: 'custom', label: '自定义小时数', hours: 'custom' }
  ];

  const S = {
    bound: false,
    loaded: false,
    loading: false,
    items: [],
    all: false
  };

  /* ------------------------------ 基础工具 ------------------------------ */

  function isAdmin() {
    const me = A.state && A.state.me;
    return Boolean(me) && (me.role === 'admin' || me.role === 'super');
  }

  function showError(message) {
    const box = $('sharesError');
    if (!box) {
      return;
    }
    $('sharesErrorText').textContent = message || '分享列表加载失败';
    box.hidden = false;
  }

  function hideError() {
    const box = $('sharesError');
    if (box) {
      box.hidden = true;
    }
  }

  /** 分享的公开访问地址 */
  function shareUrlOf(share) {
    const origin = window.location.origin && window.location.origin !== 'null'
      ? window.location.origin
      : `${window.location.protocol}//${window.location.host}`;
    return `${origin}/s/${share.token}`;
  }

  /** 复制文本：优先使用异步剪贴板，失败时回退到 execCommand */
  async function copyText(text) {
    if (navigator.clipboard && typeof navigator.clipboard.writeText === 'function') {
      try {
        await navigator.clipboard.writeText(text);
        return true;
      } catch (err) {
        console.warn('[分享] 剪贴板接口不可用，改用兼容方案：', err && err.message);
      }
    }
    try {
      const area = document.createElement('textarea');
      area.value = text;
      area.setAttribute('readonly', 'readonly');
      area.style.position = 'fixed';
      area.style.top = '-1000px';
      area.style.left = '-1000px';
      area.style.opacity = '0';
      document.body.appendChild(area);
      area.select();
      area.setSelectionRange(0, text.length);
      const ok = document.execCommand('copy');
      document.body.removeChild(area);
      return Boolean(ok);
    } catch (err) {
      console.warn('[分享] 兼容复制失败：', err && err.message);
      return false;
    }
  }

  /** 统一的确认弹窗，返回 Promise<boolean> */
  function confirmAction(options) {
    const cfg = options || {};
    return new Promise((resolve) => {
      let settled = false;
      const finish = (value) => {
        if (!settled) {
          settled = true;
          resolve(value);
        }
      };
      const body = create('div');
      if (cfg.message) {
        body.appendChild(create('p', 'modal-desc', cfg.message));
      }
      openModal({
        title: cfg.title || '确认操作',
        body,
        buttons: [
          {
            label: '取消',
            kind: 'btn-ghost',
            onClick: ({ close }) => {
              finish(false);
              close();
            }
          },
          {
            label: cfg.confirmLabel || '确定',
            kind: cfg.danger ? 'btn-danger' : 'btn-primary',
            onClick: ({ close }) => {
              finish(true);
              close();
            }
          }
        ],
        onClose: () => finish(false)
      });
    });
  }

  /* ------------------------------ 列表渲染 ------------------------------ */

  function renderSummary() {
    const node = $('sharesSummary');
    if (!node) {
      return;
    }
    if (S.loading) {
      node.textContent = '加载中…';
      return;
    }
    node.textContent = `共 ${S.items.length} 条分享`;
  }

  /** 创建可复制的链接输入框 */
  function buildLinkRow(url) {
    const row = create('div', 'share-link-row');
    const input = document.createElement('input');
    input.type = 'text';
    input.className = 'share-link-input';
    input.value = url;
    input.readOnly = true;
    input.setAttribute('aria-label', '分享链接');
    row.appendChild(input);

    const copyButton = create('button', 'btn btn-primary btn-sm', '复制');
    copyButton.type = 'button';
    copyButton.addEventListener('click', async () => {
      const ok = await copyText(url);
      toast(ok ? '链接已复制到剪贴板' : '复制失败，请手动选中链接后复制', ok ? 'success' : 'error');
    });
    row.appendChild(copyButton);
    return row;
  }

  /** 行内操作按钮 */
  function rowButton(label, kind, handler) {
    const button = create('button', `btn ${kind || 'btn-ghost'} btn-sm`, label);
    button.type = 'button';
    button.addEventListener('click', (event) => {
      event.stopPropagation();
      handler(event);
    });
    return button;
  }

  function renderActions(share) {
    const group = create('div', 'action-group');
    const url = shareUrlOf(share);

    group.appendChild(
      rowButton('复制链接', 'btn-ghost', async () => {
        const ok = await copyText(url);
        toast(ok ? '链接已复制到剪贴板' : '复制失败，请手动选中链接后复制', ok ? 'success' : 'error');
      })
    );
    group.appendChild(
      rowButton('打开', 'btn-ghost', () => {
        window.open(url, '_blank', 'noopener');
      })
    );
    group.appendChild(rowButton('删除', 'btn-danger', () => deleteShare(share)));
    return group;
  }

  function renderRow(share) {
    const row = document.createElement('tr');
    row.className = 'files-row';
    row.dataset.shareId = String(share.id);

    // 名称（管理员查看全部时附带创建者）
    const nameCell = document.createElement('td');
    nameCell.dataset.label = '名称';
    const nameBox = create('div', 'files-name');
    const nameText = create('span', 'files-name-text', share.name || share.path || '(未命名)');
    nameText.title = share.path || share.name || '';
    nameBox.appendChild(nameText);
    nameCell.appendChild(nameBox);
    if (S.all && share.ownerName) {
      nameCell.appendChild(create('div', 'field-hint', `创建者：${share.ownerName}`));
    }
    row.appendChild(nameCell);

    // 路径
    const pathCell = create('td', 'cell-mono', share.path || '-');
    pathCell.dataset.label = '路径';
    row.appendChild(pathCell);

    // 是否加密
    const pwdCell = document.createElement('td');
    pwdCell.dataset.label = '加密';
    pwdCell.textContent = share.hasPassword ? '需要密码' : '公开';
    row.appendChild(pwdCell);

    // 已下载 / 上限
    const downloadsCell = document.createElement('td');
    downloadsCell.dataset.label = '已下载';
    downloadsCell.textContent = `${Number(share.downloads) || 0} / ${
      Number(share.maxDownloads) > 0 ? share.maxDownloads : '不限'
    }`;
    row.appendChild(downloadsCell);

    // 过期时间
    const expiresCell = document.createElement('td');
    expiresCell.dataset.label = '过期时间';
    expiresCell.className = 'cell-mono';
    if (share.expiresAt) {
      const expired = Number(share.expiresAt) <= Date.now();
      expiresCell.textContent = `${formatDateTime(share.expiresAt)}${expired ? '（已过期）' : ''}`;
    } else {
      expiresCell.textContent = '永久';
    }
    row.appendChild(expiresCell);

    // 创建时间
    const createdCell = document.createElement('td');
    createdCell.dataset.label = '创建时间';
    createdCell.className = 'cell-mono';
    createdCell.textContent = share.createdAt ? formatDateTime(share.createdAt) : '-';
    row.appendChild(createdCell);

    // 操作
    const actionsCell = document.createElement('td');
    actionsCell.className = 'col-actions';
    actionsCell.appendChild(renderActions(share));
    row.appendChild(actionsCell);

    return row;
  }

  /** 渲染整张表 */
  function renderRows() {
    const body = $('sharesBody');
    if (!body) {
      return;
    }
    clear(body);
    if (S.items.length === 0) {
      const row = document.createElement('tr');
      const cell = create('td', 'empty', '还没有创建任何分享链接，点击右上角「新建分享」开始');
      cell.colSpan = 7;
      row.appendChild(cell);
      body.appendChild(row);
      return;
    }
    for (const share of S.items) {
      body.appendChild(renderRow(share));
    }
  }

  /* ------------------------------ 数据加载 ------------------------------ */

  async function loadShares() {
    if (S.loading) {
      return;
    }
    S.loading = true;
    hideError();
    renderSummary();
    if (!S.loaded) {
      const body = $('sharesBody');
      if (body) {
        clear(body);
        const row = document.createElement('tr');
        const cell = create('td', 'empty', '正在加载分享列表…');
        cell.colSpan = 7;
        row.appendChild(cell);
        body.appendChild(row);
      }
    }
    try {
      const data = await api(S.all ? '/api/shares?all=1' : '/api/shares');
      S.items = Array.isArray(data.items) ? data.items : [];
      S.loaded = true;
      renderRows();
    } catch (err) {
      S.items = [];
      renderRows();
      showError((err && err.message) || '分享列表加载失败');
      if (err && err.status !== 401) {
        toast((err && err.message) || '分享列表加载失败', 'error');
      }
    } finally {
      S.loading = false;
      renderSummary();
    }
  }

  /* ------------------------------ 删除分享 ------------------------------ */

  async function deleteShare(share) {
    const confirmed = await confirmAction({
      title: '删除分享',
      message: `确定要删除「${share.name || share.path}」的分享链接吗？删除后链接立即失效。`,
      confirmLabel: '删除分享',
      danger: true
    });
    if (!confirmed) {
      return;
    }
    try {
      await api(`/api/shares/${share.id}`, { method: 'DELETE' });
      toast('分享链接已删除', 'success');
      await loadShares();
    } catch (err) {
      toast((err && err.message) || '删除分享失败', 'error');
    }
  }

  /* ------------------------------ 新建分享 ------------------------------ */

  /** 展示创建结果（链接 + 复制按钮） */
  function showShareResult(share) {
    const url = shareUrlOf(share);
    const body = create('div');
    body.appendChild(
      create(
        'p',
        'modal-desc',
        `「${share.name || share.path}」的分享链接已创建，任何拿到链接的人都可以访问。`
      )
    );

    const card = create('div', 'share-result');
    card.appendChild(buildLinkRow(url));
    const meta = create('div', 'share-meta');
    const tips = [];
    tips.push(share.hasPassword ? '已设置提取密码' : '无提取密码');
    tips.push(Number(share.maxDownloads) > 0 ? `最多下载 ${share.maxDownloads} 次` : '下载次数不限');
    tips.push(share.expiresAt ? `过期时间 ${formatDateTime(share.expiresAt)}` : '永久有效');
    meta.textContent = tips.join(' · ');
    card.appendChild(meta);
    body.appendChild(card);

    openModal({
      title: '分享创建成功',
      body,
      buttons: [{ label: '完成', kind: 'btn-primary' }]
    });
  }

  /** 新建分享弹窗（prefillPath 可预填要分享的路径） */
  function openCreate(prefillPath) {
    if (!hasPerm('FILE_SHARE')) {
      toast('没有创建分享的权限', 'warn');
      return;
    }

    const form = create('div');

    // 路径 + 浏览按钮
    const pathRow = create('div', 'field-row');
    const pathField = create('label', 'field');
    pathField.appendChild(create('span', null, '分享路径'));
    const pathInput = document.createElement('input');
    pathInput.type = 'text';
    pathInput.placeholder = '例如：/我的文件/照片.zip';
    pathInput.value = typeof prefillPath === 'string' ? prefillPath : '';
    pathInput.autocomplete = 'off';
    pathField.appendChild(pathInput);
    pathField.appendChild(create('div', 'field-hint', '填写服务端虚拟路径，也可以点击右侧按钮浏览目录。'));
    pathRow.appendChild(pathField);

    const browseWrap = create('div', 'field');
    browseWrap.appendChild(create('span', null, ' '));
    const browseButton = create('button', 'btn btn-ghost', '浏览目录');
    browseButton.type = 'button';
    browseButton.addEventListener('click', () => {
      const files = window.FnosFiles;
      if (!files || typeof files.pickPath !== 'function') {
        toast('文件模块尚未加载，请手动填写路径', 'warn');
        return;
      }
      const startPath = pathInput.value && pathInput.value.charAt(0) === '/'
        ? pathInput.value
        : files.currentPath();
      files.pickPath({
        title: '选择要分享的目录',
        startPath: typeof startPath === 'string' ? startPath : '/',
        confirmLabel: '选择此目录',
        onPick: (picked) => {
          pathInput.value = picked;
        }
      });
    });
    browseWrap.appendChild(browseButton);
    pathRow.appendChild(browseWrap);
    form.appendChild(pathRow);

    // 密码 + 下载次数上限
    const optionRow = create('div', 'field-row');
    const pwdField = create('label', 'field');
    pwdField.appendChild(create('span', null, '提取密码'));
    const pwdInput = document.createElement('input');
    pwdInput.type = 'password';
    pwdInput.maxLength = 128;
    pwdInput.autocomplete = 'new-password';
    pwdInput.placeholder = '留空表示无需密码';
    pwdField.appendChild(pwdInput);
    optionRow.appendChild(pwdField);

    const maxField = create('label', 'field');
    maxField.appendChild(create('span', null, '下载次数上限'));
    const maxInput = document.createElement('input');
    maxInput.type = 'number';
    maxInput.min = '0';
    maxInput.step = '1';
    maxInput.value = '0';
    maxInput.placeholder = '0 表示不限';
    maxField.appendChild(maxInput);
    maxField.appendChild(create('div', 'field-hint', '0 表示不限制下载次数。'));
    optionRow.appendChild(maxField);
    form.appendChild(optionRow);

    // 有效期
    const expireRow = create('div', 'field-row');
    const expireField = create('label', 'field');
    expireField.appendChild(create('span', null, '有效期'));
    const expireSelect = document.createElement('select');
    for (const preset of EXPIRE_PRESETS) {
      const option = document.createElement('option');
      option.value = preset.key;
      option.textContent = preset.label;
      if (preset.key === '7d') {
        option.selected = true;
      }
      expireSelect.appendChild(option);
    }
    expireField.appendChild(expireSelect);
    expireRow.appendChild(expireField);

    const customField = create('label', 'field');
    customField.appendChild(create('span', null, '自定义小时数'));
    const customInput = document.createElement('input');
    customInput.type = 'number';
    customInput.min = '1';
    customInput.step = '1';
    customInput.value = '12';
    customInput.disabled = true;
    customField.appendChild(customInput);
    customField.appendChild(create('div', 'field-hint', '最少 1 小时，最长一年。'));
    expireRow.appendChild(customField);
    form.appendChild(expireRow);

    expireSelect.addEventListener('change', () => {
      customInput.disabled = expireSelect.value !== 'custom';
    });

    const error = create('div', 'form-error');
    error.hidden = true;
    form.appendChild(error);

    /** 读取表单并校验，返回请求体或 null */
    const readForm = () => {
      const path = pathInput.value.trim();
      if (!path) {
        error.textContent = '请填写要分享的路径';
        error.hidden = false;
        pathInput.focus();
        return null;
      }
      if (path.charAt(0) !== '/') {
        error.textContent = '虚拟路径必须以 / 开头';
        error.hidden = false;
        pathInput.focus();
        return null;
      }
      if (path.length > 1 && path.replace(/\/+$/, '') === '') {
        error.textContent = '不能分享根目录';
        error.hidden = false;
        return null;
      }

      const payload = { path };
      const password = pwdInput.value.trim();
      if (password.length > 0) {
        payload.password = password;
      }

      const preset = EXPIRE_PRESETS.find((item) => item.key === expireSelect.value);
      if (preset && preset.key === 'custom') {
        const hours = Number(customInput.value);
        if (!Number.isFinite(hours) || hours <= 0) {
          error.textContent = '自定义有效期必须是大于 0 的小时数';
          error.hidden = false;
          customInput.focus();
          return null;
        }
        payload.expiresInHours = hours;
      } else if (preset && preset.hours !== null) {
        payload.expiresInHours = preset.hours;
      }

      const maxDownloads = Number(maxInput.value || '0');
      if (!Number.isFinite(maxDownloads) || maxDownloads < 0 || Math.floor(maxDownloads) !== maxDownloads) {
        error.textContent = '下载次数上限必须是非负整数（0 表示不限）';
        error.hidden = false;
        maxInput.focus();
        return null;
      }
      payload.maxDownloads = maxDownloads;
      return payload;
    };

    openModal({
      title: '新建分享',
      body: form,
      buttons: [
        { label: '取消', kind: 'btn-ghost' },
        {
          label: '创建分享',
          kind: 'btn-primary',
          onClick: ({ close, setBusy }) => {
            const payload = readForm();
            if (!payload) {
              return;
            }
            error.hidden = true;
            setBusy(true);
            api('/api/shares', { method: 'POST', body: payload })
              .then((data) => {
                close();
                if (data && data.share) {
                  showShareResult(data.share);
                  loadShares();
                } else {
                  toast('分享已创建，但未返回链接信息', 'warn');
                }
              })
              .catch((err) => {
                setBusy(false);
                error.textContent = (err && err.message) || '创建分享失败';
                error.hidden = false;
                toast((err && err.message) || '创建分享失败', 'error');
              });
          }
        }
      ]
    });
  }

  /* ------------------------------ 事件绑定 ------------------------------ */

  function bindEvents() {
    if (S.bound) {
      return;
    }
    S.bound = true;

    const createButton = $('sharesCreateBtn');
    if (createButton) {
      createButton.addEventListener('click', () => openCreate(''));
    }

    const retryButton = $('sharesRetry');
    if (retryButton) {
      retryButton.addEventListener('click', () => loadShares());
    }

    const allWrap = $('sharesAllWrap');
    const allToggle = $('sharesAllToggle');
    if (allWrap && allToggle) {
      allWrap.hidden = !isAdmin();
      allToggle.addEventListener('change', () => {
        S.all = allToggle.checked;
        S.loaded = false;
        loadShares();
      });
    }
  }

  /* ------------------------------ 视图钩子 ------------------------------ */

  async function mount() {
    bindEvents();
    await loadShares();
  }

  function refresh() {
    S.loaded = false;
    return loadShares();
  }

  A.registerView('shares', { mount, refresh });

  // 暴露给 files.js：文件列表中的「分享」按钮直接打开新建分享弹窗
  window.FnosShares = {
    openCreate
  };
})();
