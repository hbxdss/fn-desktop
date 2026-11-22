'use strict';

/**
 * 文件管理视图（侧栏「文件」）
 *
 * 功能：目录浏览、面包屑、新建文件夹、拖拽/分块上传、递归搜索、
 * 收藏 / 最近访问 / 回收站、自定义右键菜单、批量操作、压缩解压、
 * 在线预览（图片/视频/音频/PDF/文本）与下载。
 *
 * 所有请求复用 app.js 暴露的 window.FnosAdmin.api（自动携带 Bearer 令牌）。
 * 媒体预览依赖 sw.js 注入 Authorization 头，Service Worker 不可用时自动降级。
 */

(function () {
  const A = window.FnosAdmin;
  if (!A) {
    console.warn('[文件] 未找到 FnosAdmin 桥接对象，文件管理视图未启用');
    return;
  }

  const $ = A.$;
  const create = A.create;
  const clear = A.clear;
  const api = A.api;
  const toast = A.toast;
  const openModal = A.openModal;
  const formatBytes = A.formatBytes;
  const formatDateTime = A.formatDateTime;
  const hasPerm = A.hasPerm;

  /** 分块上传阈值：8MB */
  const CHUNK_SIZE = 8 * 1024 * 1024;
  /** 文本预览最多显示的字符数，超出部分截断提示 */
  const TEXT_PREVIEW_LIMIT = 200000;

  const IMAGE_EXTS = new Set(['png', 'jpg', 'jpeg', 'gif', 'webp', 'bmp', 'ico', 'svg']);
  const AUDIO_EXTS = new Set(['mp3', 'wav', 'flac', 'aac', 'ogg', 'oga', 'm4a', 'opus', 'wma']);
  const VIDEO_EXTS = new Set(['mp4', 'webm', 'ogv', 'mov', 'mkv', 'avi', 'm4v']);
  const TEXT_EXTS = new Set([
    'txt', 'log', 'md', 'markdown', 'json', 'xml', 'csv', 'tsv', 'ini', 'conf', 'cfg', 'yml', 'yaml',
    'html', 'htm', 'css', 'js', 'mjs', 'cjs', 'ts', 'jsx', 'tsx', 'vue', 'sh', 'bash', 'bat', 'cmd',
    'ps1', 'py', 'java', 'c', 'h', 'cpp', 'hpp', 'go', 'rs', 'sql', 'toml', 'env', 'properties',
    'srt', 'lrc'
  ]);

  const COLLATOR = new Intl.Collator('zh-CN', { numeric: true, sensitivity: 'base' });

  /**
   * 视图状态
   * mode: browse / favorites / recent / trash / search
   */
  const F = {
    bound: false,
    mode: 'browse',
    cwd: '/',
    parent: null,
    items: [],
    selected: new Set(),
    favorites: new Set(),
    sortKey: 'name',
    sortDir: 'asc',
    search: { base: '/', keyword: '' },
    uploads: [],
    uploadSeq: 0,
    uploadBusy: false,
    swReady: false,
    dragDepth: 0
  };

  /* ------------------------------ 基础工具 ------------------------------ */

  /** 生成内联 SVG 图标节点（复用 index.html 中的 sprite） */
  function icon(name, className) {
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('class', className || 'icon');
    svg.setAttribute('aria-hidden', 'true');
    const use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
    use.setAttribute('href', `#${name}`);
    svg.appendChild(use);
    return svg;
  }

  /** 规范化虚拟路径：保证以 / 开头，去掉结尾多余的 / */
  function normalizePath(raw) {
    let value = typeof raw === 'string' ? raw.trim() : '';
    if (!value) {
      return '/';
    }
    value = value.replace(/\\/g, '/');
    if (value.charAt(0) !== '/') {
      value = `/${value}`;
    }
    value = value.replace(/\/{2,}/g, '/');
    if (value.length > 1 && value.endsWith('/')) {
      value = value.slice(0, -1);
    }
    return value;
  }

  /** 拼接虚拟路径 */
  function joinPath(dir, name) {
    const base = normalizePath(dir);
    if (base === '/') {
      return `/${name}`;
    }
    return `${base}/${name}`;
  }

  /** 取上级目录，根目录返回 null */
  function parentOf(raw) {
    const value = normalizePath(raw);
    if (value === '/') {
      return null;
    }
    const index = value.lastIndexOf('/');
    return index <= 0 ? '/' : value.slice(0, index);
  }

  /** 取扩展名（小写，不含点） */
  function extOf(name) {
    const text = String(name || '');
    const index = text.lastIndexOf('.');
    return index > 0 ? text.slice(index + 1).toLowerCase() : '';
  }

  function isDirItem(item) {
    return Boolean(item && item.isDir);
  }

  /** 选中集合的键：回收站条目同名同路径可能重复，用 id 区分 */
  function itemKey(item) {
    if (F.mode === 'trash') {
      return `t:${item.id}`;
    }
    return item.path || item.name || '';
  }

  /** 当前选中的条目列表 */
  function selectedItems() {
    return F.items.filter((item) => F.selected.has(itemKey(item)));
  }

  function isAdmin() {
    const me = A.state && A.state.me;
    return Boolean(me) && (me.role === 'admin' || me.role === 'super');
  }

  function showError(err) {
    const box = $('filesError');
    $('filesErrorText').textContent = (err && err.message) || '文件列表加载失败';
    box.hidden = false;
  }

  function hideError() {
    $('filesError').hidden = true;
  }

  /** 表格占位行（加载中 / 空目录 / 错误） */
  function showPlaceholder(text) {
    const body = $('filesBody');
    clear(body);
    const row = document.createElement('tr');
    const cell = create('td', 'empty', text);
    cell.colSpan = currentColumns().length + 1;
    row.appendChild(cell);
    body.appendChild(row);
    const head = $('filesHead');
    clear(head);
    clear($('filesBatchBar'));
    $('filesBatchBar').hidden = true;
  }

  /* ------------------------------ 排序 ------------------------------ */

  /** 当前模式的列定义（第一列固定为复选框） */
  function currentColumns() {
    const nameCol = { key: 'name', label: '名称', sortKey: 'name' };
    const sizeCol = { key: 'size', label: '大小', sortKey: 'size' };
    const pathCol = { key: 'path', label: '所在路径', sortKey: 'path' };
    const timeCol = { key: 'time', label: '修改时间', sortKey: 'mtime' };
    const actionsCol = { key: 'actions', label: '操作' };

    if (F.mode === 'favorites') {
      return [nameCol, pathCol, { key: 'time', label: '收藏时间' }, actionsCol];
    }
    if (F.mode === 'recent') {
      return [nameCol, pathCol, { key: 'time', label: '访问时间' }, actionsCol];
    }
    if (F.mode === 'trash') {
      return [nameCol, { key: 'path', label: '原路径', sortKey: 'path' }, sizeCol, { key: 'time', label: '删除时间' }, actionsCol];
    }
    if (F.mode === 'search') {
      return [nameCol, pathCol, sizeCol, timeCol, actionsCol];
    }
    return [nameCol, sizeCol, timeCol, actionsCol];
  }

  /** 条目的时间字段：不同模式取不同来源 */
  function itemTime(item) {
    if (F.mode === 'favorites') {
      return Number(item.createdAt) || 0;
    }
    if (F.mode === 'recent') {
      return Number(item.openedAt) || 0;
    }
    if (F.mode === 'trash') {
      return Number(item.deletedAt) || 0;
    }
    return Number(item.mtime) || 0;
  }

  /** 文件夹优先 + 当前排序字段 */
  function sortItems(items) {
    const key = F.sortKey;
    const direction = F.sortDir === 'desc' ? -1 : 1;
    return items.slice().sort((a, b) => {
      const aDir = isDirItem(a);
      const bDir = isDirItem(b);
      if (aDir !== bDir) {
        return aDir ? -1 : 1;
      }
      let result = 0;
      if (key === 'size') {
        result = (Number(a.size) || 0) - (Number(b.size) || 0);
      } else if (key === 'mtime') {
        result = itemTime(a) - itemTime(b);
      } else if (key === 'path') {
        result = COLLATOR.compare(String(a.path || ''), String(b.path || ''));
      } else {
        result = COLLATOR.compare(String(a.name || ''), String(b.name || ''));
      }
      if (result === 0) {
        result = COLLATOR.compare(String(a.name || ''), String(b.name || ''));
      }
      return result * direction;
    });
  }

  /* ------------------------------ 列表渲染 ------------------------------ */

  /** 表头：全选框 + 可排序列表头 */
  function renderHead() {
    const head = $('filesHead');
    clear(head);
    const row = document.createElement('tr');

    const checkTh = document.createElement('th');
    checkTh.className = 'col-check';
    const selectAll = document.createElement('input');
    selectAll.type = 'checkbox';
    selectAll.id = 'filesSelectAll';
    selectAll.setAttribute('aria-label', '全选当前列表');
    selectAll.disabled = F.items.length === 0;
    selectAll.addEventListener('change', () => {
      if (selectAll.checked) {
        for (const item of F.items) {
          F.selected.add(itemKey(item));
        }
      } else {
        F.selected.clear();
      }
      syncSelectionDom();
      updateBatchBar();
    });
    checkTh.appendChild(selectAll);
    row.appendChild(checkTh);

    for (const column of currentColumns()) {
      const th = document.createElement('th');
      if (column.key === 'actions') {
        th.className = 'col-actions';
      }
      if (column.sortKey) {
        const button = create('button', 'sort-btn', column.label);
        button.type = 'button';
        const active = F.sortKey === column.sortKey;
        const arrow = create('span', 'sort-arrow', active ? (F.sortDir === 'asc' ? '▲' : '▼') : '↕');
        button.appendChild(arrow);
        button.addEventListener('click', () => {
          if (F.sortKey === column.sortKey) {
            F.sortDir = F.sortDir === 'asc' ? 'desc' : 'asc';
          } else {
            F.sortKey = column.sortKey;
            F.sortDir = 'asc';
          }
          renderHead();
          renderBody();
        });
        th.appendChild(button);
      } else {
        th.textContent = column.label;
      }
      row.appendChild(th);
    }
    head.appendChild(row);
  }

  function emptyText() {
    if (F.mode === 'favorites') {
      return '还没有收藏任何文件';
    }
    if (F.mode === 'recent') {
      return '最近没有访问记录';
    }
    if (F.mode === 'trash') {
      return '回收站是空的';
    }
    if (F.mode === 'search') {
      return '没有找到匹配的文件';
    }
    if (F.cwd === '/') {
      return '根目录为空。若 NAS 上的共享文件夹没有显示，请在「系统设置 → 存储挂载」中挂载，'
        + '并先到飞牛「设置 → 应用 → 飞牛终端 → 文件夹权限」授予对应目录的读写权限。';
    }
    return '当前目录为空，可以把文件拖到这里上传';
  }

  function renderBody() {
    const body = $('filesBody');
    clear(body);
    if (F.items.length === 0) {
      const row = document.createElement('tr');
      const cell = create('td', 'empty', emptyText());
      cell.colSpan = currentColumns().length + 1;
      row.appendChild(cell);
      body.appendChild(row);
      return;
    }
    for (const item of sortItems(F.items)) {
      body.appendChild(renderRow(item));
    }
    syncSelectionDom();
  }

  /** 单个文件或目录名按钮（单击进入 / 双击预览） */
  function renderNameCell(item) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = `files-name files-name-link${isDirItem(item) ? '' : ' file'}`;
    const mount = item.isMount ? item : null;
    button.appendChild(icon(mount ? 'i-server' : (isDirItem(item) ? 'i-folder' : 'i-file')));
    const text = create('span', 'files-name-text', item.name || item.path || '(未命名)');
    text.title = item.path || item.name || '';
    button.appendChild(text);

    if (mount) {
      const badge = create('span', 'files-badge', mount.readonly ? '挂载 · 只读' : '挂载');
      badge.title = `挂载路径：${mount.mountAbsPath || mount.absPath || '-'}`
        + (mount.error ? `\n状态：${mount.error}` : '');
      button.appendChild(badge);
    }

    if (F.mode === 'trash') {
      button.addEventListener('click', () => selectOnly(item));
    } else if (isDirItem(item)) {
      button.addEventListener('click', () => openDir(item));
    } else {
      button.addEventListener('click', () => selectOnly(item));
      button.addEventListener('dblclick', () => openPreview(item));
    }
    return button;
  }

  /** 只读挂载点下的条目不允许写操作 */
  function isReadonlyMountItem(item) {
    return Boolean(item && item.isMount && item.readonly);
  }

  /** 行内操作按钮 */
  function actionButton(iconName, label, handler, extraClass) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = `btn btn-ghost btn-sm${extraClass ? ` ${extraClass}` : ''}`;
    button.title = label;
    button.setAttribute('aria-label', label);
    if (iconName) {
      button.appendChild(icon(iconName));
    } else {
      button.textContent = label;
    }
    button.addEventListener('click', (event) => {
      event.stopPropagation();
      handler(event);
    });
    return button;
  }

  function renderRowActions(item) {
    const group = create('div', 'action-group');

    if (F.mode === 'trash') {
      const restore = create('button', 'btn btn-ghost btn-sm', '恢复');
      restore.type = 'button';
      restore.addEventListener('click', (event) => {
        event.stopPropagation();
        restoreItems([item.id]);
      });
      const purge = create('button', 'btn btn-danger btn-sm', '彻底删除');
      purge.type = 'button';
      purge.addEventListener('click', (event) => {
        event.stopPropagation();
        purgeItems([item.id]);
      });
      group.append(restore, purge);
      return group;
    }

    // 收藏 / 最近访问：提供「所在目录」入口
    if (F.mode === 'favorites' || F.mode === 'recent') {
      const locate = create('button', 'btn btn-ghost btn-sm', '所在目录');
      locate.type = 'button';
      locate.title = `跳转到 ${parentOf(item.path) || '/'}`;
      locate.addEventListener('click', (event) => {
        event.stopPropagation();
        locateItem(item);
      });
      group.appendChild(locate);
    }

    const favorited = F.favorites.has(item.path);
    if (isDirItem(item)) {
      group.appendChild(actionButton('i-folder', '打开', () => openDir(item)));
      group.appendChild(actionButton('i-download', '打包下载（zip）', () => downloadPath(item)));
    } else {
      group.appendChild(actionButton('i-download', '下载', () => downloadPath(item)));
      group.appendChild(actionButton('i-file', '预览', () => openPreview(item)));
    }
    if (isReadonlyMountItem(item)) {
      // 只读挂载点：允许浏览/下载/收藏/分享，但不提供重命名和删除
      group.appendChild(
        actionButton('i-star', favorited ? '取消收藏' : '收藏', () => toggleFavorite(item), favorited ? 'is-fav' : '')
      );
      group.appendChild(actionButton('i-share', '分享', () => shareItem(item)));
      return group;
    }
    group.appendChild(actionButton(null, '重命名', () => renameItem(item)));
    group.appendChild(
      actionButton('i-star', favorited ? '取消收藏' : '收藏', () => toggleFavorite(item), favorited ? 'is-fav' : '')
    );
    group.appendChild(actionButton('i-share', '分享', () => shareItem(item)));
    group.appendChild(
      actionButton(
        'i-trash',
        '删除（按住 Shift 点击可彻底删除）',
        (event) => deleteItem(item, Boolean(event && event.shiftKey)),
        'is-danger'
      )
    );
    return group;
  }

  function renderRow(item) {
    const row = document.createElement('tr');
    row.className = 'files-row';
    const key = itemKey(item);
    row.dataset.key = key;
    if (F.selected.has(key)) {
      row.classList.add('selected');
    }

    row.addEventListener('contextmenu', (event) => {
      event.preventDefault();
      event.stopPropagation();
      if (!F.selected.has(key)) {
        selectOnly(item);
      }
      showRowMenu(item, event.clientX, event.clientY);
    });

    row.addEventListener('click', (event) => {
      const target = event.target;
      if (!(target instanceof HTMLElement)) {
        return;
      }
      if (target.closest('button') || target.closest('input')) {
        return;
      }
      selectOnly(item);
    });

    const checkCell = document.createElement('td');
    checkCell.className = 'col-check';
    const box = document.createElement('input');
    box.type = 'checkbox';
    box.checked = F.selected.has(key);
    box.setAttribute('aria-label', `选择 ${item.name || item.path}`);
    box.addEventListener('change', () => {
      if (box.checked) {
        F.selected.add(key);
      } else {
        F.selected.delete(key);
      }
      row.classList.toggle('selected', box.checked);
      syncSelectionDom();
      updateBatchBar();
    });
    checkCell.appendChild(box);
    row.appendChild(checkCell);

    for (const column of currentColumns()) {
      const cell = document.createElement('td');
      cell.dataset.label = column.label;
      if (column.key === 'actions') {
        cell.className = 'col-actions';
        cell.appendChild(renderRowActions(item));
      } else if (column.key === 'name') {
        cell.appendChild(renderNameCell(item));
      } else if (column.key === 'path') {
        cell.className = 'cell-mono';
        cell.textContent = item.path || '-';
      } else if (column.key === 'size') {
        cell.textContent = isDirItem(item) ? '—' : formatBytes(item.size);
      } else {
        const time = itemTime(item);
        cell.className = 'cell-mono';
        cell.textContent = time > 0 ? formatDateTime(time) : '-';
      }
      row.appendChild(cell);
    }
    return row;
  }

  /** 只选中某一项 */
  function selectOnly(item) {
    F.selected.clear();
    F.selected.add(itemKey(item));
    syncSelectionDom();
    updateBatchBar();
  }

  /** 收藏 / 最近访问条目：跳转到所在目录并选中该项 */
  async function locateItem(item) {
    const dir = parentOf(item.path);
    if (!dir) {
      toast('该文件已经在根目录下', 'info');
      return;
    }
    await loadBrowse(dir);
    const key = item.path;
    if (F.items.some((entry) => entry.path === key)) {
      F.selected.add(key);
      syncSelectionDom();
      updateBatchBar();
    }
  }

  /** 把选中状态同步到已有 DOM（避免整页重绘） */
  function syncSelectionDom() {
    const body = $('filesBody');
    const rows = body.querySelectorAll('tr[data-key]');
    for (const row of rows) {
      const selected = F.selected.has(row.dataset.key);
      row.classList.toggle('selected', selected);
      const box = row.querySelector('input[type="checkbox"]');
      if (box) {
        box.checked = selected;
      }
    }
    const all = $('filesSelectAll');
    if (all) {
      all.checked = F.items.length > 0 && F.selected.size === F.items.length;
      all.indeterminate = F.selected.size > 0 && F.selected.size < F.items.length;
    }
  }

  /* ------------------------------ 面包屑与工具栏 ------------------------------ */

  function crumbButton(label, path) {
    const button = create('button', 'crumb', label);
    button.type = 'button';
    button.title = path;
    button.addEventListener('click', () => {
      loadBrowse(path);
    });
    return button;
  }

  function renderBreadcrumb() {
    const nav = $('filesBreadcrumb');
    clear(nav);

    if (F.mode === 'favorites') {
      nav.appendChild(create('span', 'crumb current', '我的收藏'));
      return;
    }
    if (F.mode === 'recent') {
      nav.appendChild(create('span', 'crumb current', '最近访问'));
      return;
    }
    if (F.mode === 'trash') {
      nav.appendChild(create('span', 'crumb current', '回收站'));
      return;
    }

    const base = F.mode === 'search' ? F.search.base : F.cwd;
    const segments = normalizePath(base).split('/').filter(Boolean);
    nav.appendChild(crumbButton('根目录', '/'));
    let accumulated = '';
    for (const segment of segments) {
      accumulated += `/${segment}`;
      nav.appendChild(create('span', 'crumb-sep', '/'));
      if (accumulated === base && F.mode !== 'search') {
        nav.appendChild(create('span', 'crumb current', segment));
      } else {
        nav.appendChild(crumbButton(segment, accumulated));
      }
    }
    if (F.mode === 'search') {
      nav.appendChild(create('span', 'crumb-sep', '›'));
      nav.appendChild(create('span', 'crumb current', `搜索结果：${F.search.keyword}`));
    }
  }

  /** 按模式显隐工具栏按钮 */
  function syncToolbar() {
    const browse = F.mode === 'browse';
    const searchMode = F.mode === 'search';
    $('filesMkdir').hidden = !(browse && hasPerm('FILE_WRITE'));
    $('filesUploadBtn').hidden = !(browse && hasPerm('FILE_UPLOAD'));
    const canZip = hasPerm('FILE_READ') && F.mode !== 'trash';
    $('filesCompress').hidden = !canZip;
    $('filesExtract').hidden = !canZip;
    $('filesSearchRow').hidden = !(browse || searchMode);
    $('filesTrashClear').hidden = !(F.mode === 'trash' && isAdmin());
    $('filesSearchNote').hidden = !searchMode;
    if (searchMode) {
      $('filesSearchInput').value = F.search.keyword;
    }
  }

  /** 同步工具栏「压缩 / 解压」按钮的可用状态 */
  function syncFileActionButtons() {
    const compress = $('filesCompress');
    const extract = $('filesExtract');
    if (!compress || !extract) {
      return;
    }
    const items = F.mode === 'trash' ? [] : selectedItems();
    compress.disabled = items.length === 0;
    compress.title = items.length === 0 ? '请先选择要压缩的文件' : `压缩选中的 ${items.length} 项为 zip`;
    const singleZip =
      items.length === 1 && !isDirItem(items[0]) && extOf(items[0].name) === 'zip';
    extract.disabled = !singleZip;
    extract.title = singleZip ? `解压 ${items[0].name}` : '请选择一个 .zip 压缩包';
  }

  /** 批量操作条 */
  function updateBatchBar() {
    syncFileActionButtons();
    const bar = $('filesBatchBar');
    clear(bar);
    const items = selectedItems();
    if (items.length === 0) {
      bar.hidden = true;
      return;
    }
    bar.hidden = false;
    bar.appendChild(create('span', 'batch-info', `已选择 ${items.length} 项`));

    const addButton = (label, handler, kind) => {
      const button = create('button', `btn ${kind || 'btn-ghost'} btn-sm`, label);
      button.type = 'button';
      button.addEventListener('click', handler);
      bar.appendChild(button);
    };

    if (F.mode === 'trash') {
      addButton('恢复', () => restoreItems(items.map((item) => item.id)), 'btn-primary');
      addButton('彻底删除', () => purgeItems(items.map((item) => item.id)), 'btn-danger');
    } else {
      addButton('压缩', () => openCompressDialog(items.map((item) => item.path), items.map((item) => item.name)));
      addButton('下载', () => batchDownload(items));
      addButton('复制', () => transferItems(items.map((item) => item.path), 'copy'));
      addButton('移动', () => transferItems(items.map((item) => item.path), 'move'));
      addButton('删除', () => batchDelete(items), 'btn-danger');
      if (F.mode === 'favorites') {
        addButton('取消收藏', () => batchFavorite(items, false));
      } else {
        addButton('收藏', () => batchFavorite(items, true));
      }
    }

    const clearButton = create('button', 'btn btn-ghost btn-sm', '取消选择');
    clearButton.type = 'button';
    clearButton.addEventListener('click', () => {
      F.selected.clear();
      syncSelectionDom();
      updateBatchBar();
    });
    bar.appendChild(clearButton);
  }

  /* ------------------------------ 数据加载 ------------------------------ */

  /** 进入某个目录 */
  function openDir(item) {
    recordRecent(item.path);
    loadBrowse(item.path);
  }

  /** 浏览目录 */
  async function loadBrowse(path) {
    const target = normalizePath(path);
    F.mode = 'browse';
    F.search = { base: '/', keyword: '' };
    F.selected.clear();
    hideError();
    showPlaceholder('正在加载文件列表…');
    try {
      const data = await api(`/api/files?path=${encodeURIComponent(target)}`);
      F.cwd = normalizePath(data.path || target);
      F.parent = data.parent || parentOf(F.cwd);
      F.items = Array.isArray(data.items) ? data.items : [];
      renderBreadcrumb();
      syncToolbar();
      renderHead();
      renderBody();
      updateBatchBar();
      loadFavorites().catch(() => {});
    } catch (err) {
      showPlaceholder((err && err.message) || '目录加载失败');
      showError(err);
    }
    return F.cwd;
  }

  /** 拉取收藏路径集合，用于行内星标状态 */
  async function loadFavorites() {
    try {
      const data = await api('/api/files/favorites');
      const items = Array.isArray(data.items) ? data.items : [];
      F.favorites = new Set(items.map((item) => item.path));
      if (F.mode === 'browse' || F.mode === 'search' || F.mode === 'recent') {
        renderBody();
        updateBatchBar();
      }
    } catch (err) {
      if (err && err.status !== 401) {
        console.warn('[文件] 收藏列表加载失败：', err.message);
      }
    }
  }

  /** 记录最近访问（失败不影响主流程） */
  function recordRecent(path) {
    if (!path) {
      return;
    }
    api('/api/files/recent', { method: 'POST', body: { path } }).catch((err) => {
      if (err && err.status !== 401) {
        console.warn('[文件] 记录最近访问失败：', err.message);
      }
    });
  }

  async function enterFavorites() {
    F.mode = 'favorites';
    F.selected.clear();
    hideError();
    showPlaceholder('正在加载收藏…');
    try {
      const data = await api('/api/files/favorites');
      F.items = Array.isArray(data.items) ? data.items : [];
      F.favorites = new Set(F.items.map((item) => item.path));
      renderBreadcrumb();
      syncToolbar();
      renderHead();
      renderBody();
      updateBatchBar();
    } catch (err) {
      showPlaceholder((err && err.message) || '收藏列表加载失败');
      showError(err);
    }
  }

  async function enterRecent() {
    F.mode = 'recent';
    F.selected.clear();
    hideError();
    showPlaceholder('正在加载最近访问…');
    try {
      const data = await api('/api/files/recent');
      F.items = Array.isArray(data.items) ? data.items : [];
      renderBreadcrumb();
      syncToolbar();
      renderHead();
      renderBody();
      updateBatchBar();
      loadFavorites().catch(() => {});
    } catch (err) {
      showPlaceholder((err && err.message) || '最近访问加载失败');
      showError(err);
    }
  }

  async function enterTrash() {
    F.mode = 'trash';
    F.selected.clear();
    hideError();
    showPlaceholder('正在加载回收站…');
    try {
      const data = await api('/api/files/trash');
      F.items = Array.isArray(data.items) ? data.items : [];
      renderBreadcrumb();
      syncToolbar();
      renderHead();
      renderBody();
      updateBatchBar();
    } catch (err) {
      showPlaceholder((err && err.message) || '回收站加载失败');
      showError(err);
    }
  }

  /** 递归搜索当前目录 */
  async function runSearch() {
    const keyword = $('filesSearchInput').value.trim();
    if (!keyword) {
      toast('请输入要搜索的关键字', 'warn');
      $('filesSearchInput').focus();
      return;
    }
    const base = F.mode === 'search' ? F.search.base : F.cwd;
    F.selected.clear();
    hideError();
    showPlaceholder('正在搜索…');
    try {
      const data = await api(
        `/api/files/search?path=${encodeURIComponent(base)}&q=${encodeURIComponent(keyword)}&limit=200`
      );
      F.mode = 'search';
      F.search = { base: normalizePath(data.path || base), keyword: data.q || keyword };
      F.items = Array.isArray(data.items) ? data.items : [];
      renderBreadcrumb();
      syncToolbar();
      renderHead();
      renderBody();
      updateBatchBar();
      $('filesSearchNote').hidden = false;
      $('filesSearchText').textContent = `“${F.search.keyword}” 在 ${F.search.base} 下找到 ${
        F.items.length
      } 个结果${data.truncated ? '（结果过多已截断）' : ''}`;
      loadFavorites().catch(() => {});
    } catch (err) {
      showPlaceholder((err && err.message) || '搜索失败');
      showError(err);
    }
  }

  function exitSearch() {
    loadBrowse(F.search.base || F.cwd || '/');
  }

  /** 切换左侧快捷入口 */
  function switchMode(mode) {
    const items = document.querySelectorAll('.files-side-item');
    for (const item of items) {
      item.classList.toggle('active', item.dataset.fmode === mode);
    }
    if (mode === 'favorites') {
      return enterFavorites();
    }
    if (mode === 'recent') {
      return enterRecent();
    }
    if (mode === 'trash') {
      return enterTrash();
    }
    return loadBrowse(F.cwd || '/');
  }

  /** 重新加载当前模式的数据 */
  function reloadCurrent() {
    if (F.mode === 'favorites') {
      return enterFavorites();
    }
    if (F.mode === 'recent') {
      return enterRecent();
    }
    if (F.mode === 'trash') {
      return enterTrash();
    }
    if (F.mode === 'search') {
      return runSearch();
    }
    return loadBrowse(F.cwd || '/');
  }

  /* ------------------------------ 右键菜单 ------------------------------ */

  function hideMenu() {
    const menu = $('fileMenu');
    if (!menu) {
      return;
    }
    menu.hidden = true;
    clear(menu);
  }

  /** 在指定坐标弹出菜单，自动避免超出视口 */
  function showMenuAt(entries, x, y) {
    const menu = $('fileMenu');
    if (!menu || entries.length === 0) {
      return;
    }
    clear(menu);
    for (const entry of entries) {
      if (entry.separator) {
        menu.appendChild(create('div', 'file-menu-sep'));
        continue;
      }
      const button = create('button', `file-menu-item${entry.danger ? ' danger' : ''}`, entry.label);
      button.type = 'button';
      button.addEventListener('click', () => {
        hideMenu();
        entry.action();
      });
      menu.appendChild(button);
    }
    menu.hidden = false;
    menu.style.left = '0px';
    menu.style.top = '0px';
    const rect = menu.getBoundingClientRect();
    const left = Math.max(8, Math.min(x, window.innerWidth - rect.width - 8));
    const top = Math.max(8, Math.min(y, window.innerHeight - rect.height - 8));
    menu.style.left = `${left}px`;
    menu.style.top = `${top}px`;
  }

  function menuEntry(label, action, options) {
    const config = options || {};
    return { label, action, danger: Boolean(config.danger) };
  }

  /** 条目右键菜单 */
  function showRowMenu(item, x, y) {
    if (F.mode === 'trash') {
      showMenuAt(
        [
          menuEntry('恢复', () => restoreItems([item.id])),
          menuEntry('彻底删除', () => purgeItems([item.id]), { danger: true })
        ],
        x,
        y
      );
      return;
    }

    const isDir = isDirItem(item);
    const entries = [
      menuEntry(isDir ? '打开文件夹' : '预览', () => (isDir ? openDir(item) : openPreview(item)))
    ];
    if (!isDir) {
      entries.push(menuEntry('下载', () => downloadPath(item)));
    }
    if (F.mode === 'favorites' || F.mode === 'recent') {
      entries.push(menuEntry('跳到所在目录', () => locateItem(item)));
    }
    entries.push(menuEntry('重命名', () => renameItem(item)));
    entries.push(menuEntry('复制', () => setClipboard([item.path], 'copy')));
    entries.push(menuEntry('剪切', () => setClipboard([item.path], 'cut')));
    entries.push(menuEntry('移动到…', () => transferItems([item.path], 'move')));
    entries.push(menuEntry('压缩为 zip', () => openCompressDialog([item.path], [item.name])));
    if (!isDir && extOf(item.name) === 'zip') {
      entries.push(menuEntry('解压', () => extractItem(item)));
    }
    entries.push(menuEntry(F.favorites.has(item.path) ? '取消收藏' : '收藏', () => toggleFavorite(item)));
    entries.push(menuEntry('分享', () => shareItem(item)));
    entries.push(menuEntry('删除', () => deleteItem(item, false)));
    entries.push(menuEntry('彻底删除', () => deleteItem(item, true), { danger: true }));
    showMenuAt(entries, x, y);
  }

  /** 列表空白处右键菜单 */
  function showBlankMenu(x, y) {
    const entries = [];
    if (F.mode === 'browse') {
      if (hasPerm('FILE_WRITE')) {
        entries.push(menuEntry('新建文件夹', () => openMkdirDialog()));
      }
      if (hasPerm('FILE_UPLOAD')) {
        entries.push(menuEntry('上传文件', () => $('filesFileInput').click()));
      }
      if (F.clipboard && F.clipboard.paths.length > 0) {
        entries.push(menuEntry(`粘贴（${F.clipboard.paths.length} 项）`, () => pasteClipboard()));
      }
    }
    if (F.mode === 'trash' && isAdmin()) {
      entries.push(menuEntry('清空回收站', () => clearTrash(), { danger: true }));
    }
    showMenuAt(entries, x, y);
  }

  /** 剪贴板（前端内的复制/剪切，供右键粘贴使用） */
  function setClipboard(paths, action) {
    if (!Array.isArray(paths) || paths.length === 0) {
      return;
    }
    F.clipboard = { paths: paths.slice(), action: action === 'cut' ? 'cut' : 'copy' };
    toast(action === 'cut' ? '已剪切，可在目标目录右键粘贴' : '已复制，可在目标目录右键粘贴', 'success');
  }

  async function pasteClipboard() {
    if (!F.clipboard || F.clipboard.paths.length === 0) {
      toast('剪贴板为空', 'warn');
      return;
    }
    const action = F.clipboard.action === 'cut' ? 'move' : 'copy';
    const ok = await applyBatch(action, F.clipboard.paths, F.cwd);
    if (ok && action === 'move') {
      F.clipboard = null;
    }
  }

  /* ------------------------------ 通用确认 / 输入弹窗 ------------------------------ */

  /** 确认对话框，返回 Promise<boolean> */
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
      if (cfg.detail) {
        body.appendChild(create('p', 'field-hint', cfg.detail));
      }
      openModal({
        title: cfg.title || '确认操作',
        body,
        buttons: [
          {
            label: cfg.cancelLabel || '取消',
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

  /** 文本输入对话框，返回 Promise<string|null>（取消为 null） */
  function promptText(options) {
    const cfg = options || {};
    return new Promise((resolve) => {
      let settled = false;
      const finish = (value) => {
        if (!settled) {
          settled = true;
          resolve(value);
        }
      };

      const form = create('div');
      const field = create('label', 'field');
      field.appendChild(create('span', null, cfg.label || '名称'));
      const input = document.createElement('input');
      input.type = 'text';
      input.value = cfg.value || '';
      input.maxLength = cfg.maxLength || 255;
      input.autocomplete = 'off';
      if (cfg.placeholder) {
        input.placeholder = cfg.placeholder;
      }
      field.appendChild(input);
      if (cfg.hint) {
        field.appendChild(create('div', 'field-hint', cfg.hint));
      }
      form.appendChild(field);

      const error = create('div', 'form-error');
      error.hidden = true;
      form.appendChild(error);

      /** 校验并提交，返回是否已提交 */
      const submit = () => {
        const value = input.value.trim();
        if (!value) {
          error.textContent = cfg.requiredMessage || '内容不能为空';
          error.hidden = false;
          input.focus();
          return false;
        }
        if (typeof cfg.validate === 'function') {
          const message = cfg.validate(value);
          if (message) {
            error.textContent = message;
            error.hidden = false;
            input.focus();
            return false;
          }
        }
        finish(value);
        return true;
      };

      input.addEventListener('keydown', (event) => {
        if (event.key === 'Enter') {
          event.preventDefault();
          if (submit()) {
            A.closeModal();
          }
        }
      });

      openModal({
        title: cfg.title || '输入内容',
        body: form,
        buttons: [
          {
            label: cfg.cancelLabel || '取消',
            kind: 'btn-ghost',
            onClick: ({ close }) => {
              finish(null);
              close();
            }
          },
          {
            label: cfg.confirmLabel || '确定',
            kind: 'btn-primary',
            onClick: ({ close }) => {
              if (submit()) {
                close();
              }
            }
          }
        ],
        onClose: () => finish(null)
      });
    });
  }

  /* ------------------------------ 目录选择器 ------------------------------ */

  /** 简单目录选择弹窗：单击进入子目录，「选择此目录」确认当前目录 */
  function openDirPicker(options) {
    const cfg = options || {};
    let current = normalizePath(cfg.startPath || F.cwd || '/');
    let loadSeq = 0;

    const body = create('div');
    const bar = create('div', 'picker-bar');
    const upButton = create('button', 'btn btn-ghost btn-sm', '上一级');
    upButton.type = 'button';
    const pathText = create('span', 'picker-path', current);
    bar.append(upButton, pathText);

    const list = create('div', 'picker-list');
    const status = create('div', 'picker-status', `当前目录：${current}`);
    body.append(bar, list, status);

    /** 加载指定目录下的子目录 */
    async function load(path) {
      current = normalizePath(path);
      pathText.textContent = current;
      status.textContent = `当前目录：${current}`;
      upButton.disabled = parentOf(current) === null;
      loadSeq += 1;
      const seq = loadSeq;
      clear(list);
      list.appendChild(create('div', 'picker-empty', '正在加载目录…'));
      try {
        const data = await api(`/api/files?path=${encodeURIComponent(current)}`);
        if (seq !== loadSeq) {
          return;
        }
        current = normalizePath(data.path || current);
        pathText.textContent = current;
        status.textContent = `当前目录：${current}`;
        upButton.disabled = parentOf(current) === null;
        const items = Array.isArray(data.items) ? data.items : [];
        const dirs = items.filter((item) => item.isDir);
        clear(list);
        if (dirs.length === 0) {
          list.appendChild(create('div', 'picker-empty', '该目录下没有子文件夹'));
          return;
        }
        for (const dir of dirs) {
          const button = create('button', 'picker-item');
          button.type = 'button';
          button.appendChild(icon('i-folder'));
          button.appendChild(create('span', null, dir.name));
          button.addEventListener('click', () => {
            load(joinPath(current, dir.name));
          });
          list.appendChild(button);
        }
      } catch (err) {
        if (seq !== loadSeq) {
          return;
        }
        clear(list);
        list.appendChild(create('div', 'picker-empty', (err && err.message) || '目录加载失败'));
      }
    }

    upButton.addEventListener('click', () => {
      const parent = parentOf(current);
      if (parent) {
        load(parent);
      }
    });

    openModal({
      title: cfg.title || '选择目标目录',
      body,
      wide: true,
      buttons: [
        { label: '取消', kind: 'btn-ghost' },
        {
          label: cfg.confirmLabel || '选择此目录',
          kind: 'btn-primary',
          onClick: ({ close }) => {
            const picked = current;
            close();
            if (typeof cfg.onPick === 'function') {
              cfg.onPick(picked);
            }
          }
        }
      ]
    });

    load(current);
  }

  /* ------------------------------ 基础文件操作 ------------------------------ */

  /** 新建文件夹 */
  function openMkdirDialog() {
    if (!hasPerm('FILE_WRITE')) {
      toast('没有新建文件夹的权限', 'warn');
      return;
    }
    promptText({
      title: '新建文件夹',
      label: '文件夹名称',
      placeholder: '例如：备份资料',
      hint: `将在 ${F.cwd} 下创建`,
      validate: (value) => (/[\\/]/.test(value) ? '名称不能包含路径分隔符' : null)
    }).then(async (name) => {
      if (!name) {
        return;
      }
      try {
        await api('/api/files/mkdir', { method: 'POST', body: { path: F.cwd, name } });
        toast('文件夹已创建', 'success');
        await reloadCurrent();
      } catch (err) {
        toast((err && err.message) || '新建文件夹失败', 'error');
      }
    });
  }

  /** 重命名 */
  function renameItem(item) {
    if (!hasPerm('FILE_WRITE')) {
      toast('没有重命名的权限', 'warn');
      return;
    }
    promptText({
      title: '重命名',
      label: '新名称',
      value: item.name,
      hint: `原路径：${item.path}`,
      validate: (value) => (/[\\/]/.test(value) ? '名称不能包含路径分隔符' : null)
    }).then(async (name) => {
      if (!name || name === item.name) {
        return;
      }
      try {
        await api('/api/files/rename', { method: 'POST', body: { path: item.path, name } });
        toast('已重命名', 'success');
        await reloadCurrent();
      } catch (err) {
        toast((err && err.message) || '重命名失败', 'error');
      }
    });
  }

  /** 删除单个条目（permanent 为真时彻底删除） */
  async function deleteItem(item, permanent) {
    if (!hasPerm('FILE_DELETE')) {
      toast('没有删除文件的权限', 'warn');
      return;
    }
    const confirmed = await confirmAction({
      title: permanent ? '彻底删除' : '删除文件',
      message: permanent
        ? `确定要彻底删除「${item.name}」吗？该操作无法撤销。`
        : `确定要把「${item.name}」移入回收站吗？`,
      confirmLabel: permanent ? '彻底删除' : '移入回收站',
      danger: Boolean(permanent)
    });
    if (!confirmed) {
      return;
    }
    try {
      await api('/api/files/delete', {
        method: 'POST',
        body: { path: item.path, permanent: Boolean(permanent) }
      });
      toast(permanent ? '已彻底删除' : '已移入回收站', 'success');
      F.selected.delete(itemKey(item));
      await reloadCurrent();
    } catch (err) {
      toast((err && err.message) || '删除失败', 'error');
    }
  }

  /** 收藏 / 取消收藏 */
  async function toggleFavorite(item) {
    const favorited = F.favorites.has(item.path);
    try {
      if (favorited) {
        await api(`/api/files/favorites?path=${encodeURIComponent(item.path)}`, { method: 'DELETE' });
        F.favorites.delete(item.path);
        toast('已取消收藏', 'success');
      } else {
        await api('/api/files/favorites', { method: 'POST', body: { path: item.path } });
        F.favorites.add(item.path);
        toast('已加入收藏', 'success');
      }
      if (F.mode === 'favorites') {
        await enterFavorites();
      } else {
        renderBody();
        updateBatchBar();
      }
    } catch (err) {
      toast((err && err.message) || '收藏操作失败', 'error');
    }
  }

  /** 分享：交给 shares.js 打开新建分享弹窗 */
  function shareItem(item) {
    const shares = window.FnosShares;
    if (!shares || typeof shares.openCreate !== 'function') {
      toast('分享模块尚未加载，请刷新页面后重试', 'error');
      return;
    }
    shares.openCreate(item.path);
  }

  /** 压缩为 zip */
  function openCompressDialog(paths, names) {
    if (!Array.isArray(paths) || paths.length === 0) {
      toast('请先选择要压缩的文件', 'warn');
      return;
    }
    const label = names && names[0] ? String(names[0]).replace(/\.[^.]+$/, '') : '打包文件';
    const suggested = `${label}${paths.length > 1 ? `_等${paths.length}项` : ''}.zip`;
    promptText({
      title: '压缩为 zip',
      label: '压缩包名称',
      value: suggested,
      hint: `将保存到 ${F.cwd}`,
      validate: (value) => (extOf(value) !== 'zip' ? '压缩包名称必须以 .zip 结尾' : null)
    }).then(async (name) => {
      if (!name) {
        return;
      }
      const target = joinPath(F.cwd, name);
      toast('正在压缩，请稍候…', 'info');
      try {
        const data = await api('/api/files/compress', {
          method: 'POST',
          body: { paths, target }
        });
        toast(`压缩完成，压缩包大小 ${formatBytes(data.bytes)}`, 'success');
        await reloadCurrent();
      } catch (err) {
        toast((err && err.message) || '压缩失败', 'error');
      }
    });
  }

  /** 解压 zip 到当前目录 */
  async function extractItem(item) {
    const dest = F.cwd;
    const confirmed = await confirmAction({
      title: '解压文件',
      message: `确定要把「${item.name}」解压到 ${dest} 吗？`,
      confirmLabel: '开始解压'
    });
    if (!confirmed) {
      return;
    }
    toast('正在解压，请稍候…', 'info');
    try {
      const data = await api('/api/files/extract', {
        method: 'POST',
        body: { path: item.path, dest }
      });
      toast(`解压完成，共释放 ${data.count} 个条目`, 'success');
      await reloadCurrent();
    } catch (err) {
      toast((err && err.message) || '解压失败', 'error');
    }
  }

  /** 工具栏「压缩」：对当前选中项生成 zip，保存到当前目录 */
  function compressSelected() {
    const items = selectedItems();
    if (items.length === 0) {
      toast('请先选择要压缩的文件', 'warn');
      return;
    }
    const paths = items.map((item) => item.path);
    const suggested = `${items[0].name}.zip`;
    promptText({
      title: '压缩为 zip',
      label: '压缩包名称',
      value: suggested,
      hint: `将保存到 ${F.cwd}`,
      validate: (value) => (/[\\/]/.test(value) ? '压缩包名称不能包含路径分隔符' : null)
    }).then(async (raw) => {
      const name = raw && raw.trim();
      if (!name) {
        return;
      }
      const fileName = /\.zip$/i.test(name) ? name : `${name}.zip`;
      const target = joinPath(F.cwd, fileName);
      toast('正在压缩，请稍候…', 'info');
      try {
        const data = await api('/api/files/compress', {
          method: 'POST',
          body: { paths, target }
        });
        toast(`压缩完成，压缩包大小 ${formatBytes(data.bytes)}`, 'success');
        await reloadCurrent();
      } catch (err) {
        toast((err && err.message) || '压缩失败', 'error');
      }
    });
  }

  /** 工具栏「解压」：解压选中的 zip 到其所在目录 */
  async function extractSelected() {
    const items = selectedItems();
    const item = items.length === 1 ? items[0] : null;
    if (!item || isDirItem(item) || extOf(item.name) !== 'zip') {
      toast('请选择一个 .zip 压缩包', 'warn');
      return;
    }
    toast('正在解压，请稍候…', 'info');
    try {
      const data = await api('/api/files/extract', {
        method: 'POST',
        body: { path: item.path }
      });
      toast(`解压完成，共 ${data.count} 项`, 'success');
      await reloadCurrent();
    } catch (err) {
      toast((err && err.message) || '解压失败', 'error');
    }
  }

  /** 复制 / 移动到指定目录 */
  function transferItems(paths, action) {
    if (!hasPerm('FILE_DELETE')) {
      toast('没有执行批量操作的权限', 'warn');
      return;
    }
    openDirPicker({
      title: action === 'move' ? '移动到…' : '复制到…',
      startPath: F.cwd,
      confirmLabel: action === 'move' ? '移动到这里' : '复制到这里',
      onPick: (target) => {
        applyBatch(action, paths, target);
      }
    });
  }

  /**
   * 统一的批量操作入口
   * @returns {Promise<boolean>} 是否成功执行
   */
  async function applyBatch(action, paths, target) {
    if (!Array.isArray(paths) || paths.length === 0) {
      toast('请先选择要操作的文件', 'warn');
      return false;
    }
    const dest = normalizePath(target || F.cwd);
    if (action !== 'delete' && paths.some((path) => normalizePath(path) === dest)) {
      toast('目标目录不能是所选文件本身', 'warn');
      return false;
    }
    try {
      const data = await api('/api/files/batch', {
        method: 'POST',
        body: { action, paths, target: dest }
      });
      const done = Array.isArray(data.done) ? data.done : [];
      const failed = Array.isArray(data.failed) ? data.failed : [];
      if (failed.length > 0) {
        const first = failed[0];
        toast(
          `已完成 ${done.length} 项，${failed.length} 项失败：${(first && first.error) || '未知原因'}`,
          'warn'
        );
      } else if (action === 'copy') {
        toast(`已复制 ${done.length} 项到 ${dest}`, 'success');
      } else if (action === 'move') {
        toast(`已移动 ${done.length} 项到 ${dest}`, 'success');
      } else {
        toast(`已删除 ${done.length} 项`, 'success');
      }
      F.selected.clear();
      await reloadCurrent();
      return failed.length === 0;
    } catch (err) {
      toast((err && err.message) || '批量操作失败', 'error');
      return false;
    }
  }

  /** 批量删除（进回收站） */
  async function batchDelete(items) {
    if (!Array.isArray(items) || items.length === 0) {
      return;
    }
    const confirmed = await confirmAction({
      title: '批量删除',
      message: `确定要把选中的 ${items.length} 项移入回收站吗？`,
      confirmLabel: '移入回收站',
      danger: true
    });
    if (!confirmed) {
      return;
    }
    await applyBatch('delete', items.map((item) => item.path), F.cwd);
  }

  /** 批量收藏 / 取消收藏 */
  async function batchFavorite(items, add) {
    if (!Array.isArray(items) || items.length === 0) {
      return;
    }
    let ok = 0;
    let failed = 0;
    for (const item of items) {
      try {
        if (add) {
          await api('/api/files/favorites', { method: 'POST', body: { path: item.path } });
          F.favorites.add(item.path);
        } else {
          await api(`/api/files/favorites?path=${encodeURIComponent(item.path)}`, { method: 'DELETE' });
          F.favorites.delete(item.path);
        }
        ok += 1;
      } catch (err) {
        failed += 1;
        if (err && err.status === 401) {
          return;
        }
      }
    }
    if (failed > 0) {
      toast(`已处理 ${ok} 项，${failed} 项失败`, 'warn');
    } else {
      toast(add ? `已收藏 ${ok} 项` : `已取消收藏 ${ok} 项`, 'success');
    }
    if (F.mode === 'favorites') {
      await enterFavorites();
      return;
    }
    renderBody();
    updateBatchBar();
  }

  /* ------------------------------ 回收站操作 ------------------------------ */

  /** 恢复回收站条目 */
  async function restoreItems(ids) {
    if (!Array.isArray(ids) || ids.length === 0) {
      return;
    }
    try {
      const data = await api('/api/files/trash/restore', { method: 'POST', body: { ids } });
      const restored = Array.isArray(data.restored) ? data.restored : [];
      const failed = Array.isArray(data.failed) ? data.failed : [];
      if (failed.length > 0) {
        toast(`已恢复 ${restored.length} 项，${failed.length} 项失败`, 'warn');
      } else {
        toast(`已恢复 ${restored.length} 项`, 'success');
      }
      F.selected.clear();
      await enterTrash();
    } catch (err) {
      toast((err && err.message) || '恢复失败', 'error');
    }
  }

  /** 彻底删除回收站条目 */
  async function purgeItems(ids) {
    if (!Array.isArray(ids) || ids.length === 0) {
      return;
    }
    const confirmed = await confirmAction({
      title: '彻底删除',
      message: `确定要彻底删除选中的 ${ids.length} 项吗？该操作无法撤销。`,
      confirmLabel: '彻底删除',
      danger: true
    });
    if (!confirmed) {
      return;
    }
    try {
      const data = await api('/api/files/trash/purge', { method: 'POST', body: { ids } });
      const purged = Array.isArray(data.purged) ? data.purged : [];
      const failed = Array.isArray(data.failed) ? data.failed : [];
      if (failed.length > 0) {
        toast(`已删除 ${purged.length} 项，${failed.length} 项失败`, 'warn');
      } else {
        toast(`已彻底删除 ${purged.length} 项`, 'success');
      }
      F.selected.clear();
      await enterTrash();
    } catch (err) {
      toast((err && err.message) || '彻底删除失败', 'error');
    }
  }

  /** 清空回收站（仅管理员） */
  async function clearTrash() {
    if (!isAdmin()) {
      toast('只有管理员可以清空回收站', 'warn');
      return;
    }
    const confirmed = await confirmAction({
      title: '清空回收站',
      message: '确定要清空回收站吗？所有条目将被彻底删除，无法恢复。',
      confirmLabel: '清空回收站',
      danger: true
    });
    if (!confirmed) {
      return;
    }
    try {
      const data = await api('/api/files/trash/purge', { method: 'POST', body: { all: true } });
      const purged = Array.isArray(data.purged) ? data.purged : [];
      toast(`已清空回收站，共删除 ${purged.length} 项`, 'success');
      F.selected.clear();
      await enterTrash();
    } catch (err) {
      toast((err && err.message) || '清空回收站失败', 'error');
    }
  }

  /* ------------------------------ 上传队列 ------------------------------ */

  /** 显示上传队列面板 */
  function showUploadPanel() {
    const panel = $('uploadPanel');
    if (panel) {
      panel.hidden = false;
    }
  }

  /** 同名文件冲突时的选择：overwrite / skip / null（取消） */
  function chooseConflict(names) {
    return new Promise((resolve) => {
      let settled = false;
      const finish = (value) => {
        if (!settled) {
          settled = true;
          resolve(value);
        }
      };
      const body = create('div');
      body.appendChild(create('p', 'modal-desc', `目标目录已存在 ${names.length} 个同名文件：`));
      const list = create('div', 'upload-conflict-list');
      const shown = names.slice(0, 8);
      for (const name of shown) {
        list.appendChild(create('div', 'field-hint', `· ${name}`));
      }
      if (names.length > shown.length) {
        list.appendChild(create('div', 'field-hint', `…… 另有 ${names.length - shown.length} 个文件`));
      }
      body.appendChild(list);
      body.appendChild(create('p', 'field-hint', '选择「覆盖」会先彻底删除同名文件再上传，操作不可撤销。'));
      openModal({
        title: '同名文件冲突',
        body,
        buttons: [
          {
            label: '取消',
            kind: 'btn-ghost',
            onClick: ({ close }) => {
              finish(null);
              close();
            }
          },
          {
            label: '跳过同名文件',
            kind: 'btn-ghost',
            onClick: ({ close }) => {
              finish('skip');
              close();
            }
          },
          {
            label: '覆盖',
            kind: 'btn-danger',
            onClick: ({ close }) => {
              finish('overwrite');
              close();
            }
          }
        ],
        onClose: () => finish(null)
      });
    });
  }

  /**
   * 添加上传任务
   * @param {FileList|File[]} fileList 浏览器选择的文件
   */
  async function addUploads(fileList) {
    if (!hasPerm('FILE_UPLOAD')) {
      toast('没有上传文件的权限', 'warn');
      return;
    }
    if (F.mode !== 'browse') {
      toast('请先切换到「我的文件」再上传', 'warn');
      return;
    }
    const files = Array.from(fileList || []).filter((file) => file && typeof file.name === 'string');
    if (files.length === 0) {
      return;
    }
    const dir = F.cwd;

    // 先检测同名文件，避免分块覆盖后残留旧文件尾部数据
    const conflicts = [];
    for (const file of files) {
      try {
        const data = await api(
          `/api/files/upload/status?path=${encodeURIComponent(dir)}&name=${encodeURIComponent(file.name)}`
        );
        if (data.exists) {
          conflicts.push(file.name);
        }
      } catch (err) {
        if (err && err.status === 401) {
          return;
        }
      }
    }

    let overwrite = false;
    let targets = files;
    if (conflicts.length > 0) {
      const choice = await chooseConflict(conflicts);
      if (!choice) {
        return;
      }
      if (choice === 'skip') {
        targets = files.filter((file) => conflicts.indexOf(file.name) < 0);
        if (targets.length === 0) {
          toast('已跳过全部同名文件', 'info');
          return;
        }
      } else {
        overwrite = true;
      }
    }

    for (const file of targets) {
      F.uploadSeq += 1;
      const task = {
        id: F.uploadSeq,
        name: file.name,
        dir,
        file,
        total: Number(file.size) || 0,
        loaded: 0,
        percent: 0,
        speed: 0,
        state: 'waiting',
        error: '',
        overwrite
      };
      F.uploads.push(task);
      createUploadRow(task);
      updateUploadState(task);
    }
    showUploadPanel();
    pumpUploads();
  }

  /** 创建上传队列中的一行 */
  function createUploadRow(task) {
    const list = $('uploadList');
    if (!list) {
      return;
    }
    const row = create('div', 'upload-row');
    const nameEl = create('div', 'upload-name', task.name);
    nameEl.title = task.name;
    const track = create('div', 'upload-track');
    const fill = create('div', 'upload-fill');
    track.appendChild(fill);
    const stateEl = create('div', 'upload-state', '等待中');
    row.append(nameEl, track, stateEl);
    list.appendChild(row);
    task.row = row;
    task.fill = fill;
    task.stateEl = stateEl;
  }

  /** 刷新一行的状态文字与进度条 */
  function updateUploadState(task) {
    const stateEl = task.stateEl;
    if (!stateEl) {
      return;
    }
    clear(stateEl);
    if (task.row) {
      task.row.classList.toggle('with-action', task.state === 'failed');
    }
    if (task.state === 'waiting') {
      stateEl.appendChild(create('span', null, '等待中'));
      return;
    }
    if (task.state === 'running') {
      stateEl.appendChild(create('span', null, `${task.percent}% · ${formatBytes(task.speed)}/s`));
      return;
    }
    if (task.state === 'done') {
      stateEl.appendChild(create('span', null, '已完成'));
      return;
    }
    stateEl.appendChild(create('span', null, '失败'));
    const retry = create('button', 'btn btn-ghost btn-sm', '重试');
    retry.type = 'button';
    retry.addEventListener('click', () => retryUpload(task));
    stateEl.appendChild(retry);
    if (task.error) {
      stateEl.title = task.error;
    }
  }

  /** 根据已上传字节数刷新进度条与速度 */
  function updateUploadProgress(task, startedAt) {
    const percent = task.total > 0 ? Math.min(100, Math.floor((task.loaded / task.total) * 100)) : 0;
    const elapsed = Math.max((Date.now() - startedAt) / 1000, 0.05);
    task.percent = percent;
    task.speed = task.loaded / elapsed;
    if (task.fill) {
      task.fill.style.width = `${percent}%`;
    }
    if (task.state === 'running') {
      updateUploadState(task);
    }
  }

  /**
   * 发送一个分块（XHR 才能拿到上传进度）
   * @returns {Promise<void>}
   */
  function sendChunk(task, blob, offset, startedAt) {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      const url = `/api/files/upload?path=${encodeURIComponent(task.dir)}&name=${encodeURIComponent(
        task.name
      )}&offset=${offset}`;
      xhr.open('POST', url, true);
      if (A.state.token) {
        xhr.setRequestHeader('Authorization', `Bearer ${A.state.token}`);
      }
      xhr.setRequestHeader('Content-Type', 'application/octet-stream');
      xhr.upload.addEventListener('progress', (event) => {
        if (event.lengthComputable) {
          task.loaded = offset + event.loaded;
          updateUploadProgress(task, startedAt);
        }
      });
      xhr.addEventListener('load', () => {
        if (xhr.status >= 200 && xhr.status < 300) {
          resolve();
          return;
        }
        let message = `上传失败（HTTP ${xhr.status}）`;
        try {
          const data = JSON.parse(xhr.responseText);
          if (data && data.error) {
            message = data.error;
          }
        } catch (parseErr) {
          /* 响应不是 JSON 时沿用默认提示 */
        }
        reject(new Error(message));
      });
      xhr.addEventListener('error', () => reject(new Error('网络错误，上传中断')));
      xhr.addEventListener('abort', () => reject(new Error('上传已取消')));
      xhr.send(blob);
    });
  }

  /** 执行单个上传任务，返回是否成功 */
  async function runUpload(task) {
    const startedAt = Date.now();
    try {
      if (task.overwrite) {
        // 覆盖上传前先彻底删除旧文件，避免分块写入后残留尾部数据
        await api('/api/files/delete', {
          method: 'POST',
          body: { path: joinPath(task.dir, task.name), permanent: true }
        });
      }
      if (task.total === 0) {
        await sendChunk(task, task.file, 0, startedAt);
      } else {
        let offset = 0;
        while (offset < task.total) {
          const end = Math.min(offset + CHUNK_SIZE, task.total);
          await sendChunk(task, task.file.slice(offset, end), offset, startedAt);
          offset = end;
          task.loaded = offset;
          updateUploadProgress(task, startedAt);
        }
      }
      await api('/api/files/upload/finish', {
        method: 'POST',
        body: { name: task.name, path: task.dir, size: task.total }
      });
      task.percent = 100;
      task.state = 'done';
      if (task.fill) {
        task.fill.style.width = '100%';
        task.fill.classList.add('done');
      }
      updateUploadState(task);
      return true;
    } catch (err) {
      task.state = 'failed';
      task.error = (err && err.message) || '上传失败';
      if (task.fill) {
        task.fill.classList.add('failed');
      }
      updateUploadState(task);
      toast(`「${task.name}」${task.error}`, 'error');
      return false;
    }
  }

  /** 串行消费上传队列 */
  async function pumpUploads() {
    if (F.uploadBusy) {
      return;
    }
    F.uploadBusy = true;
    let finished = 0;
    try {
      for (;;) {
        const task = F.uploads.find((item) => item.state === 'waiting');
        if (!task) {
          break;
        }
        task.state = 'running';
        task.loaded = 0;
        task.percent = 0;
        task.speed = 0;
        task.error = '';
        if (task.fill) {
          task.fill.classList.remove('failed', 'done');
          task.fill.style.width = '0%';
        }
        updateUploadState(task);
        const ok = await runUpload(task);
        if (ok) {
          finished += 1;
        }
      }
    } finally {
      F.uploadBusy = false;
    }
    if (finished > 0) {
      toast(`已上传 ${finished} 个文件`, 'success');
      await reloadCurrent();
    }
  }

  /** 重试失败的上传任务 */
  function retryUpload(task) {
    task.state = 'waiting';
    task.loaded = 0;
    task.percent = 0;
    task.speed = 0;
    task.error = '';
    if (task.fill) {
      task.fill.classList.remove('failed', 'done');
      task.fill.style.width = '0%';
    }
    updateUploadState(task);
    pumpUploads();
  }

  /* ------------------------------ 预览 / 下载 ------------------------------ */

  /** 预览弹窗使用的对象 URL（降级下载时释放） */
  let previewObjectUrl = '';

  function releasePreviewUrl() {
    if (previewObjectUrl) {
      URL.revokeObjectURL(previewObjectUrl);
      previewObjectUrl = '';
    }
  }

  function previewKindOf(name) {
    const ext = extOf(name);
    if (IMAGE_EXTS.has(ext)) {
      return 'image';
    }
    if (AUDIO_EXTS.has(ext)) {
      return 'audio';
    }
    if (VIDEO_EXTS.has(ext)) {
      return 'video';
    }
    if (ext === 'pdf') {
      return 'pdf';
    }
    if (TEXT_EXTS.has(ext)) {
      return 'text';
    }
    return 'none';
  }

  function previewUrlOf(path) {
    return `/api/files/preview?path=${encodeURIComponent(path)}`;
  }

  function downloadUrlOf(path) {
    return `/api/files/download?path=${encodeURIComponent(path)}`;
  }

  /** 带鉴权头的原始 fetch（用于文本/图片降级读取） */
  function authFetch(url) {
    const headers = {};
    if (A.state.token) {
      headers.Authorization = `Bearer ${A.state.token}`;
    }
    return fetch(url, { headers, credentials: 'same-origin', cache: 'no-store' });
  }

  /** 预览失败的中文提示 */
  function previewErrorMessage(status) {
    if (status === 401) {
      return '登录状态已失效，请重新登录后再试';
    }
    if (status === 403) {
      return '没有预览该文件的权限';
    }
    if (status === 404) {
      return '文件不存在或已被移动';
    }
    if (status === 413) {
      return '文件过大（超过 15MB），无法在线预览，请下载后查看';
    }
    return `预览失败（HTTP ${status}）`;
  }

  async function fetchPreviewBlob(path) {
    const response = await authFetch(previewUrlOf(path));
    if (!response.ok) {
      throw new Error(previewErrorMessage(response.status));
    }
    return response.blob();
  }

  async function fetchPreviewText(path) {
    const response = await authFetch(previewUrlOf(path));
    if (!response.ok) {
      throw new Error(previewErrorMessage(response.status));
    }
    return response.text();
  }

  function blobToDataUrl(blob) {
    return new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.addEventListener('load', () => resolve(String(reader.result || '')));
      reader.addEventListener('error', () => reject(new Error('读取文件内容失败')));
      reader.readAsDataURL(blob);
    });
  }

  /** 降级提示：说明无法在线预览并提供下载按钮 */
  function showFallback(stage, hint, item, message) {
    clear(stage);
    hint.textContent = '';
    const box = create('div', 'preview-fallback');
    box.appendChild(create('p', null, message));
    const button = create('button', 'btn btn-primary btn-sm', '下载文件');
    button.type = 'button';
    button.addEventListener('click', () => downloadPath(item));
    box.appendChild(button);
    stage.appendChild(box);
  }

  /** 按类型渲染预览内容 */
  async function renderPreview(stage, hint, item, kind) {
    releasePreviewUrl();
    clear(stage);
    hint.textContent = '';

    if (kind === 'image') {
      if (F.swReady) {
        const img = document.createElement('img');
        img.alt = item.name || '图片预览';
        img.src = previewUrlOf(item.path);
        img.addEventListener('error', () => {
          showFallback(stage, hint, item, '图片加载失败，可能是登录状态已失效或文件已被删除。');
        });
        stage.appendChild(img);
        return;
      }
      hint.textContent = '正在加载图片…';
      try {
        const blob = await fetchPreviewBlob(item.path);
        const dataUrl = await blobToDataUrl(blob);
        const img = document.createElement('img');
        img.alt = item.name || '图片预览';
        img.src = dataUrl;
        clear(stage);
        stage.appendChild(img);
        hint.textContent = '当前浏览器未启用媒体预览服务，已按原始数据加载。';
      } catch (err) {
        showFallback(stage, hint, item, (err && err.message) || '图片预览失败');
      }
      return;
    }

    if (kind === 'video' || kind === 'audio') {
      const label = kind === 'video' ? '视频' : '音频';
      if (!F.swReady) {
        showFallback(stage, hint, item, `当前浏览器未启用媒体预览服务，${label}请下载后播放。`);
        return;
      }
      const media = document.createElement(kind === 'video' ? 'video' : 'audio');
      media.controls = true;
      media.preload = 'metadata';
      media.src = previewUrlOf(item.path);
      media.addEventListener('error', () => {
        showFallback(stage, hint, item, `${label}无法在线播放，可能格式不被浏览器支持，请下载后查看。`);
      });
      stage.appendChild(media);
      return;
    }

    if (kind === 'pdf') {
      if (!F.swReady) {
        showFallback(stage, hint, item, '当前浏览器未启用 PDF 预览服务，请下载后查看。');
        return;
      }
      const frame = create('iframe', 'preview-frame');
      frame.title = item.name || 'PDF 预览';
      frame.src = previewUrlOf(item.path);
      stage.appendChild(frame);
      return;
    }

    if (kind === 'text') {
      hint.textContent = '正在加载文本内容…';
      try {
        const text = await fetchPreviewText(item.path);
        const box = create('pre', 'preview-text');
        const truncated = text.length > TEXT_PREVIEW_LIMIT;
        box.textContent = truncated ? text.slice(0, TEXT_PREVIEW_LIMIT) : text;
        clear(stage);
        stage.appendChild(box);
        hint.textContent = truncated
          ? `文件内容较长，仅显示前 ${TEXT_PREVIEW_LIMIT} 个字符，完整内容请下载查看。`
          : '';
      } catch (err) {
        showFallback(stage, hint, item, (err && err.message) || '文本预览失败');
      }
      return;
    }

    showFallback(stage, hint, item, '该文件类型暂不支持在线预览，请下载后查看。');
  }

  /** 打开预览弹窗 */
  function openPreview(item) {
    const kind = previewKindOf(item.name);
    const body = create('div', 'preview-body');
    const stage = create('div', 'preview-stage');
    const hint = create('div', 'preview-hint');
    body.append(stage, hint);
    openModal({
      title: `预览：${item.name || '未命名文件'}`,
      body,
      wide: true,
      buttons: [
        {
          label: '下载',
          kind: 'btn-primary',
          onClick: () => {
            downloadPath(item);
          }
        },
        { label: '关闭', kind: 'btn-ghost' }
      ],
      onClose: () => releasePreviewUrl()
    });
    renderPreview(stage, hint, item, kind).catch((err) => {
      showFallback(stage, hint, item, (err && err.message) || '预览失败');
    });
  }

  /**
   * 触发浏览器下载
   *
   * 注意：给 <a> 加 download 属性后，Chrome/Edge 会把该请求交给浏览器下载管理器处理，
   * 这条链路不经过 Service Worker，也就无法注入 Authorization 头，服务端会返回 401，
   * 表现为「点击下载后什么都没下载下来」。因此只有 blob: 地址（内容已在内存中）才用
   * download 属性指定文件名；直接访问接口时依赖服务端返回的 Content-Disposition。
   */
  function triggerDownload(url, name) {
    const link = document.createElement('a');
    link.href = url;
    if (name && String(url).slice(0, 5) === 'blob:') {
      link.download = name;
    }
    link.rel = 'noopener';
    document.body.appendChild(link);
    link.click();
    // 延后移除：同步移除节点会让个别浏览器取消刚触发的下载
    window.setTimeout(() => {
      if (link.parentNode) {
        link.parentNode.removeChild(link);
      }
    }, 1500);
  }

  /**
   * 通过隐藏 iframe 触发接口下载
   *
   * 相比 <a download>，iframe 导航请求会经过 Service Worker（可以注入 Authorization），
   * 文件名交给服务端 Content-Disposition；若服务端返回错误也不会顶掉当前页面。
   */
  function triggerDirectDownload(url) {
    const frame = document.createElement('iframe');
    frame.style.display = 'none';
    frame.setAttribute('aria-hidden', 'true');
    frame.src = url;
    document.body.appendChild(frame);
    window.setTimeout(() => {
      if (frame.parentNode) {
        frame.parentNode.removeChild(frame);
      }
    }, 60000);
  }

  /**
   * 确认 Service Worker 中缓存的令牌与当前登录令牌一致
   * （不一致时先同步一次，避免直连下载拿到 401）
   */
  async function ensureWorkerToken() {
    const token = (A.state && A.state.token) || '';
    if (!F.swReady || !token) {
      return false;
    }
    try {
      const cache = await window.caches.open('fnos-auth');
      const hit = await cache.match('/__fnos_token');
      const cached = hit ? (await hit.text()).trim() : '';
      if (cached === token) {
        return true;
      }
      syncTokenToWorker(token);
      await new Promise((resolve) => { window.setTimeout(resolve, 150); });
      const retry = await window.caches.open('fnos-auth');
      const hit2 = await retry.match('/__fnos_token');
      return Boolean(hit2) && (await hit2.text()).trim() === token;
    } catch (err) {
      console.warn('[文件] 校验下载令牌失败，改为内存下载：', err && err.message);
      return false;
    }
  }

  /** 批量下载：逐个触发，文件夹由服务端打包为 zip */
  function batchDownload(items) {
    if (!items || items.length === 0) {
      return;
    }
    const list = items.slice();
    if (list.length > 1) {
      toast(`正在依次下载 ${list.length} 项…`, 'info');
    }
    const next = (index) => {
      if (index >= list.length) {
        return;
      }
      downloadPath(list[index]);
      window.setTimeout(() => next(index + 1), 1200);
    };
    next(0);
  }

  /** 下载文件；目录会自动打包为 zip。Service Worker 可用时直连，否则先取回 Blob 再保存 */
  async function downloadPath(item) {
    if (!hasPerm('FILE_DOWNLOAD')) {
      toast('没有下载文件的权限', 'warn');
      return;
    }
    const folder = isDirItem(item);
    const url = folder
      ? `/api/files/download-folder?path=${encodeURIComponent(item.path)}`
      : downloadUrlOf(item.path);
    const fileName = folder ? `${item.name || '下载'}.zip` : item.name;
    if (folder) {
      toast('正在打包文件夹，请稍候…', 'info');
    }
    if (F.swReady) {
      // 直连下载：必须在确认 Service Worker 已拿到令牌后再触发，否则会收到 401
      if (await ensureWorkerToken()) {
        triggerDirectDownload(url);
        return;
      }
    }
    if (!folder) {
      toast('正在准备下载…', 'info');
    }
    try {
      const response = await authFetch(url);
      if (!response.ok) {
        throw new Error(previewErrorMessage(response.status));
      }
      const blob = await response.blob();
      previewObjectUrl = URL.createObjectURL(blob);
      triggerDownload(previewObjectUrl, fileName);
      window.setTimeout(() => releasePreviewUrl(), 60000);
    } catch (err) {
      toast((err && err.message) || '下载失败', 'error');
    }
  }

  /* ------------------------------ Service Worker 媒体桥 ------------------------------ */

  const SW_URL = '/sw.js';

  function supportsServiceWorker() {
    return Boolean(navigator && navigator.serviceWorker);
  }

  /** 把登录令牌发给 Service Worker，用于预览 / 下载请求注入鉴权头 */
  function syncTokenToWorker(token) {
    if (!supportsServiceWorker()) {
      return;
    }
    const payload = { type: 'fnos-token', token: typeof token === 'string' ? token : '' };
    if (navigator.serviceWorker.controller) {
      navigator.serviceWorker.controller.postMessage(payload);
    }
    navigator.serviceWorker.ready
      .then((registration) => {
        if (registration.active) {
          registration.active.postMessage(payload);
        }
      })
      .catch((err) => {
        console.warn('[文件] 同步预览令牌失败：', err && err.message);
      });
  }

  /** 注册媒体 Service Worker，并维护 F.swReady 状态 */
  async function initMediaBridge() {
    if (!supportsServiceWorker()) {
      F.swReady = false;
      return;
    }
    try {
      await navigator.serviceWorker.register(SW_URL, { scope: '/' });
      await navigator.serviceWorker.ready;
      F.swReady = Boolean(navigator.serviceWorker.controller);
      navigator.serviceWorker.addEventListener('controllerchange', () => {
        F.swReady = Boolean(navigator.serviceWorker.controller);
      });
      syncTokenToWorker(A.state.token);
    } catch (err) {
      console.warn('[文件] Service Worker 注册失败，媒体预览将自动降级：', err && err.message);
      F.swReady = false;
    }
  }

  // 暴露给 app.js：登录成功/退出登录时同步或清除令牌
  window.FnosMedia = {
    setToken(token) {
      syncTokenToWorker(token);
    },
    clear() {
      syncTokenToWorker('');
    }
  };

  /* ------------------------------ 事件绑定 ------------------------------ */

  function bindFileEvents() {
    if (F.bound) {
      return;
    }
    F.bound = true;

    const bind = (id, handler) => {
      const node = $(id);
      if (node) {
        node.addEventListener('click', handler);
      }
    };

    bind('filesRefresh', () => {
      reloadCurrent();
    });
    bind('filesMkdir', () => openMkdirDialog());
    bind('filesCompress', () => compressSelected());
    bind('filesExtract', () => extractSelected());
    bind('filesUploadBtn', () => $('filesFileInput').click());
    bind('filesTrashClear', () => clearTrash());
    bind('filesSearchBtn', () => runSearch());
    bind('filesSearchExit', () => exitSearch());
    bind('filesRetry', () => reloadCurrent());
    bind('uploadPanelClose', () => {
      $('uploadPanel').hidden = true;
    });

    const searchInput = $('filesSearchInput');
    if (searchInput) {
      searchInput.addEventListener('keydown', (event) => {
        if (event.key === 'Enter') {
          event.preventDefault();
          runSearch();
        }
      });
    }

    const fileInput = $('filesFileInput');
    if (fileInput) {
      fileInput.addEventListener('change', () => {
        // 先拷贝成数组再清空 input：Chrome 下清空 value 会让原 FileList 变空，
        // 直接传 FileList 会导致上传任务丢失（表现为「选择文件后没有任何反应」）。
        const files = Array.from(fileInput.files || []);
        fileInput.value = '';
        if (files.length > 0) {
          addUploads(files).catch((err) => {
            toast((err && err.message) || '添加上传任务失败', 'error');
          });
        }
      });
    }

    const sideItems = document.querySelectorAll('.files-side-item');
    for (const item of sideItems) {
      item.addEventListener('click', () => {
        switchMode(item.dataset.fmode);
      });
    }

    // 列表空白处右键菜单
    const blankArea = document.querySelector('#view-files .table-wrap');
    if (blankArea) {
      blankArea.addEventListener('contextmenu', (event) => {
        const target = event.target;
        if (!(target instanceof HTMLElement)) {
          return;
        }
        if (target.closest('tr[data-key]')) {
          return;
        }
        event.preventDefault();
        showBlankMenu(event.clientX, event.clientY);
      });
    }

    // 拖拽上传（仅浏览模式且具备上传权限时生效）
    const zone = $('view-files');
    const hint = $('filesDropHint');
    const showHint = (show) => {
      if (hint) {
        hint.hidden = !show;
      }
    };
    if (zone) {
      zone.addEventListener('dragenter', (event) => {
        if (F.mode !== 'browse' || !hasPerm('FILE_UPLOAD')) {
          return;
        }
        event.preventDefault();
        F.dragDepth += 1;
        showHint(true);
      });
      zone.addEventListener('dragover', (event) => {
        if (F.mode !== 'browse' || !hasPerm('FILE_UPLOAD')) {
          return;
        }
        event.preventDefault();
        if (event.dataTransfer) {
          event.dataTransfer.dropEffect = 'copy';
        }
      });
      zone.addEventListener('dragleave', () => {
        if (F.mode !== 'browse' || !hasPerm('FILE_UPLOAD')) {
          return;
        }
        F.dragDepth = Math.max(0, F.dragDepth - 1);
        if (F.dragDepth === 0) {
          showHint(false);
        }
      });
      zone.addEventListener('drop', (event) => {
        if (F.mode !== 'browse' || !hasPerm('FILE_UPLOAD')) {
          return;
        }
        event.preventDefault();
        F.dragDepth = 0;
        showHint(false);
        const files = event.dataTransfer ? event.dataTransfer.files : null;
        if (files && files.length > 0) {
          addUploads(files).catch((err) => {
            toast((err && err.message) || '添加上传任务失败', 'error');
          });
        }
      });
    }

    // 点击其它位置 / 滚动 / 缩放窗口时收起右键菜单
    document.addEventListener('click', (event) => {
      const menu = $('fileMenu');
      if (!menu || menu.hidden) {
        return;
      }
      const target = event.target;
      if (target instanceof Node && menu.contains(target)) {
        return;
      }
      hideMenu();
    });
    document.addEventListener('contextmenu', (event) => {
      if (event.defaultPrevented) {
        return;
      }
      hideMenu();
    });
    window.addEventListener('resize', hideMenu);
    window.addEventListener('scroll', hideMenu, true);
    document.addEventListener('keydown', (event) => {
      if (event.key === 'Escape') {
        hideMenu();
      }
    });
  }

  /* ------------------------------ 视图钩子 ------------------------------ */

  let mounted = false;

  /** 进入视图时调用 */
  async function mount() {
    bindFileEvents();
    initMediaBridge();
    if (!mounted) {
      mounted = true;
      await loadBrowse(F.cwd || '/');
      return;
    }
    await reloadCurrent();
  }

  /** 顶部刷新按钮调用 */
  function refresh() {
    return reloadCurrent();
  }

  A.registerView('files', { mount, refresh });

  // 脚本加载后立即注册 Service Worker，保证登录后第一次预览就能拿到鉴权头
  initMediaBridge();

  // 暴露给 shares.js：复用目录选择器（新建分享时选择路径）
  window.FnosFiles = {
    pickPath(options) {
      openDirPicker(options || {});
    },
    normalizePath,
    currentPath() {
      return F.cwd;
    }
  };
})();
