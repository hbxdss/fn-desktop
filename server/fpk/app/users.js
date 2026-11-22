'use strict';

/**
 * 用户管理路由：/api/users
 * 所有写操作均做权限校验、角色约束与审计记录。
 */

const db = require('./db');
const auth = require('./auth');
const {
  PERMS,
  ROLES,
  ROLE_LABELS,
  ALL_PERMISSIONS,
  DEFAULT_USER_PERMS,
  DEFAULT_ADMIN_PERMS,
  sendJson,
  sendError,
  readJson,
  isValidUsername,
  passwordPolicyMessage,
  clientIp
} = require('./util');

const PERMISSION_GROUPS = [
  {
    key: 'user',
    label: '用户管理',
    items: [
      { key: 'USER_CREATE', label: '创建用户' },
      { key: 'USER_DELETE', label: '删除用户' },
      { key: 'USER_EDIT', label: '编辑用户' },
      { key: 'USER_RESET_PWD', label: '重置密码' }
    ]
  },
  {
    key: 'system',
    label: '系统管理',
    items: [
      { key: 'SYSTEM_SETTINGS', label: '修改系统设置' },
      { key: 'SYSTEM_AUDIT_VIEW', label: '查看审计日志' },
      { key: 'SYSTEM_SHUTDOWN', label: '系统关机' }
    ]
  },
  {
    key: 'file',
    label: '文件管理',
    items: [
      { key: 'FILE_READ', label: '浏览文件' },
      { key: 'FILE_WRITE', label: '写入与重命名' },
      { key: 'FILE_DELETE', label: '删除文件' },
      { key: 'FILE_UPLOAD', label: '上传文件' },
      { key: 'FILE_DOWNLOAD', label: '下载文件' },
      { key: 'FILE_SHARE', label: '分享文件' }
    ]
  },
  {
    key: 'docker',
    label: 'Docker 管理',
    items: [
      { key: 'DOCKER_VIEW', label: '查看容器' },
      { key: 'DOCKER_START', label: '启动容器' },
      { key: 'DOCKER_STOP', label: '停止容器' },
      { key: 'DOCKER_DELETE', label: '删除容器' },
      { key: 'DOCKER_CREATE', label: '创建容器' },
      { key: 'DOCKER_EXEC', label: '容器内执行命令' }
    ]
  },
  {
    key: 'storage',
    label: '存储管理',
    items: [
      { key: 'STORAGE_VIEW', label: '查看存储' },
      { key: 'STORAGE_MANAGE', label: '管理存储' }
    ]
  }
];

function permissionCatalog() {
  return {
    bits: 24,
    all: ALL_PERMISSIONS,
    groups: PERMISSION_GROUPS.map((group) => ({
      key: group.key,
      label: group.label,
      items: group.items.map((item) => ({
        key: item.key,
        label: item.label,
        bit: PERMS[item.key],
        value: PERMS[item.key]
      }))
    })),
    roles: [
      { key: ROLES.SUPER, label: ROLE_LABELS.super },
      { key: ROLES.ADMIN, label: ROLE_LABELS.admin },
      { key: ROLES.USER, label: ROLE_LABELS.user }
    ],
    defaults: {
      admin: DEFAULT_ADMIN_PERMS,
      user: DEFAULT_USER_PERMS
    }
  };
}

function audit(ctx, action, detail) {
  db.addAudit({
    userId: ctx.user.id,
    username: ctx.user.username,
    action,
    detail,
    ip: clientIp(ctx.req)
  });
}

function defaultPermsForRole(role) {
  if (role === ROLES.ADMIN) {
    return DEFAULT_ADMIN_PERMS;
  }
  return DEFAULT_USER_PERMS;
}

/** 非超管只能授予自己拥有的权限位，防止越权提权 */
function clampPermissions(requester, requested) {
  if (requester.role === ROLES.SUPER) {
    return requested & ALL_PERMISSIONS;
  }
  return requested & requester.perms & ALL_PERMISSIONS;
}

function handleList(ctx) {
  const canView =
    auth.hasPermission(ctx.user, PERMS.USER_EDIT) ||
    auth.hasPermission(ctx.user, PERMS.USER_CREATE) ||
    auth.hasPermission(ctx.user, PERMS.USER_DELETE) ||
    auth.hasPermission(ctx.user, PERMS.USER_RESET_PWD);
  if (!canView) {
    sendError(ctx.res, 403, '没有权限查看用户列表', 'FORBIDDEN');
    return;
  }
  const items = db.listUsers().map((row) => {
    const user = db.serializeUser(row);
    user.roleLabel = ROLE_LABELS[user.role] || user.role;
    user.isSuper = user.role === ROLES.SUPER;
    return user;
  });
  sendJson(ctx.res, 200, { ok: true, items, total: items.length });
}

async function handleCreate(ctx) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.USER_CREATE, '创建用户')) {
    return;
  }
  const body = await readJson(ctx.req, 64 * 1024);
  const username = typeof body.username === 'string' ? body.username.trim() : '';
  if (!isValidUsername(username)) {
    sendError(ctx.res, 400, '用户名需为 3~32 位字母、数字、下划线、点或短横线', 'INVALID_USERNAME');
    return;
  }
  const passwordError = passwordPolicyMessage(body.password);
  if (passwordError) {
    sendError(ctx.res, 400, passwordError, 'WEAK_PASSWORD');
    return;
  }
  const role = body.role === ROLES.ADMIN ? ROLES.ADMIN : ROLES.USER;
  if (body.role === ROLES.SUPER) {
    sendError(ctx.res, 400, '超级管理员唯一，不能再创建', 'INVALID_ROLE');
    return;
  }
  if (db.getUserByUsername(username)) {
    sendError(ctx.res, 409, '用户名已存在', 'USERNAME_TAKEN');
    return;
  }
  const requestedPerms = Number.isInteger(body.perms) ? body.perms : defaultPermsForRole(role);
  if (requestedPerms < 0 || requestedPerms > ALL_PERMISSIONS) {
    sendError(ctx.res, 400, '权限位数值超出 24 位范围', 'INVALID_PERMS');
    return;
  }
  const perms = clampPermissions(ctx.user, requestedPerms);
  const passwordHash = await auth.hashPassword(body.password);
  const created = db.createUser({
    username,
    passwordHash,
    role,
    perms,
    disabled: false
  });
  audit(ctx, 'USER_CREATE', `创建用户 ${username}（${ROLE_LABELS[role]}），权限位 ${perms}`);
  sendJson(ctx.res, 201, { ok: true, user: db.serializeUser(created) });
}

async function handleUpdate(ctx, id) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.USER_EDIT, '编辑用户')) {
    return;
  }
  const targetRow = db.getUserById(id);
  if (!targetRow) {
    sendError(ctx.res, 404, '用户不存在', 'NOT_FOUND');
    return;
  }
  if (targetRow.role === ROLES.SUPER && ctx.user.role !== ROLES.SUPER) {
    sendError(ctx.res, 403, '不能修改超级管理员账号', 'FORBIDDEN');
    return;
  }
  const isSelf = targetRow.id === ctx.user.id;
  const body = await readJson(ctx.req, 64 * 1024);

  let username = targetRow.username;
  if (body.username !== undefined) {
    const next = typeof body.username === 'string' ? body.username.trim() : '';
    if (!isValidUsername(next)) {
      sendError(ctx.res, 400, '用户名需为 3~32 位字母、数字、下划线、点或短横线', 'INVALID_USERNAME');
      return;
    }
    const existing = db.getUserByUsername(next);
    if (existing && existing.id !== targetRow.id) {
      sendError(ctx.res, 409, '用户名已存在', 'USERNAME_TAKEN');
      return;
    }
    username = next;
  }

  let role = targetRow.role;
  if (body.role !== undefined) {
    if (body.role !== ROLES.ADMIN && body.role !== ROLES.USER) {
      sendError(ctx.res, 400, '角色只能是管理员或普通用户（超级管理员不可变更）', 'INVALID_ROLE');
      return;
    }
    if (targetRow.role === ROLES.SUPER) {
      sendError(ctx.res, 400, '超级管理员不可降级', 'SUPER_PROTECTED');
      return;
    }
    if (isSelf && ctx.user.role !== ROLES.SUPER) {
      sendError(ctx.res, 403, '不能修改自己的角色', 'FORBIDDEN');
      return;
    }
    role = body.role;
  }

  let perms = targetRow.perms;
  if (body.perms !== undefined) {
    if (targetRow.role === ROLES.SUPER) {
      sendError(ctx.res, 400, '超级管理员固定拥有全部权限', 'SUPER_PROTECTED');
      return;
    }
    if (isSelf && ctx.user.role !== ROLES.SUPER) {
      sendError(ctx.res, 403, '不能修改自己的权限位', 'FORBIDDEN');
      return;
    }
    const requested = Number(body.perms);
    if (!Number.isInteger(requested) || requested < 0 || requested > ALL_PERMISSIONS) {
      sendError(ctx.res, 400, '权限位数值超出 24 位范围', 'INVALID_PERMS');
      return;
    }
    perms = clampPermissions(ctx.user, requested);
    if (perms !== requested) {
      sendError(ctx.res, 403, '不能授予超出自身权限范围的权限位', 'FORBIDDEN');
      return;
    }
  }

  let disabled = Boolean(targetRow.disabled);
  if (body.disabled !== undefined) {
    if (targetRow.role === ROLES.SUPER) {
      sendError(ctx.res, 400, '超级管理员不可被禁用', 'SUPER_PROTECTED');
      return;
    }
    if (isSelf) {
      sendError(ctx.res, 400, '不能禁用当前登录的账号', 'SELF_PROTECTED');
      return;
    }
    disabled = Boolean(body.disabled);
  }

  const updated = db.updateUser(targetRow.id, { username, role, perms, disabled });
  if (disabled) {
    db.deleteTokensByUser(targetRow.id);
  }
  audit(
    ctx,
    'USER_EDIT',
    `修改用户 ${targetRow.username} -> ${username}，角色 ${ROLE_LABELS[role]}，权限位 ${perms}，禁用 ${disabled ? '是' : '否'}`
  );
  sendJson(ctx.res, 200, { ok: true, user: db.serializeUser(updated) });
}

function handleDelete(ctx, id) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.USER_DELETE, '删除用户')) {
    return;
  }
  const targetRow = db.getUserById(id);
  if (!targetRow) {
    sendError(ctx.res, 404, '用户不存在', 'NOT_FOUND');
    return;
  }
  if (targetRow.role === ROLES.SUPER) {
    sendError(ctx.res, 400, '超级管理员不可删除', 'SUPER_PROTECTED');
    return;
  }
  if (targetRow.id === ctx.user.id) {
    sendError(ctx.res, 400, '不能删除当前登录的账号', 'SELF_PROTECTED');
    return;
  }
  db.deleteUser(targetRow.id);
  audit(ctx, 'USER_DELETE', `删除用户 ${targetRow.username}`);
  sendJson(ctx.res, 200, { ok: true });
}

async function handleResetPassword(ctx, id) {
  if (!auth.requirePermission(ctx.res, ctx.user, PERMS.USER_RESET_PWD, '重置用户密码')) {
    return;
  }
  const targetRow = db.getUserById(id);
  if (!targetRow) {
    sendError(ctx.res, 404, '用户不存在', 'NOT_FOUND');
    return;
  }
  if (targetRow.role === ROLES.SUPER && ctx.user.role !== ROLES.SUPER) {
    sendError(ctx.res, 403, '不能重置超级管理员的密码', 'FORBIDDEN');
    return;
  }
  const body = await readJson(ctx.req, 64 * 1024);
  const passwordError = passwordPolicyMessage(body.password);
  if (passwordError) {
    sendError(ctx.res, 400, passwordError, 'WEAK_PASSWORD');
    return;
  }
  const passwordHash = await auth.hashPassword(body.password);
  db.updateUserPassword(targetRow.id, passwordHash);
  db.deleteTokensByUser(targetRow.id);
  audit(ctx, 'USER_RESET_PWD', `重置用户 ${targetRow.username} 的密码`);
  sendJson(ctx.res, 200, { ok: true });
}

async function handle(ctx) {
  const { pathname, method } = ctx;

  if (pathname === '/api/users/permissions' && method === 'GET') {
    sendJson(ctx.res, 200, Object.assign({ ok: true }, permissionCatalog()));
    return true;
  }

  if (pathname === '/api/users' && method === 'GET') {
    handleList(ctx);
    return true;
  }

  if (pathname === '/api/users' && method === 'POST') {
    await handleCreate(ctx);
    return true;
  }

  const match = pathname.match(/^\/api\/users\/(\d+)(\/reset-password)?$/);
  if (!match) {
    return false;
  }
  const id = Number(match[1]);
  const isReset = Boolean(match[2]);

  if (isReset && method === 'POST') {
    await handleResetPassword(ctx, id);
    return true;
  }
  if (!isReset && method === 'PUT') {
    await handleUpdate(ctx, id);
    return true;
  }
  if (!isReset && method === 'DELETE') {
    handleDelete(ctx, id);
    return true;
  }
  return false;
}

module.exports = { handle, permissionCatalog };
