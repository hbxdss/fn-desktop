'use strict';

/**
 * 数据访问层：SQLite（better-sqlite3）+ 用户 / Token / 审计 / 系统设置。
 * 所有 SQL 均使用参数化查询，避免注入；Token 只保存 SHA-256 哈希。
 */

const Database = require('better-sqlite3');
const { DATA_DIR, DB_PATH, ensureDir } = require('./util');

ensureDir(DATA_DIR);

const db = new Database(DB_PATH);
db.pragma('journal_mode = WAL');
db.pragma('synchronous = NORMAL');
db.pragma('foreign_keys = ON');

db.exec(`
  CREATE TABLE IF NOT EXISTS users (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT    NOT NULL UNIQUE COLLATE NOCASE,
    password_hash TEXT    NOT NULL,
    role          TEXT    NOT NULL DEFAULT 'user',
    perms         INTEGER NOT NULL DEFAULT 0,
    disabled      INTEGER NOT NULL DEFAULT 0,
    created_at    INTEGER NOT NULL,
    updated_at    INTEGER NOT NULL
  );

  CREATE TABLE IF NOT EXISTS tokens (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL,
    token_hash TEXT    NOT NULL UNIQUE,
    created_at INTEGER NOT NULL,
    expires_at INTEGER NOT NULL,
    last_used  INTEGER,
    ip         TEXT,
    FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE CASCADE
  );

  CREATE TABLE IF NOT EXISTS audit_logs (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER,
    username   TEXT,
    action     TEXT    NOT NULL,
    detail     TEXT,
    ip         TEXT,
    created_at INTEGER NOT NULL
  );

  CREATE TABLE IF NOT EXISTS settings (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
  );

  CREATE TABLE IF NOT EXISTS shares (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    token         TEXT    NOT NULL UNIQUE,
    path          TEXT    NOT NULL,
    name          TEXT    NOT NULL,
    is_dir        INTEGER NOT NULL DEFAULT 0,
    owner_id      INTEGER NOT NULL,
    owner_name    TEXT    NOT NULL,
    password_hash TEXT,
    expires_at    INTEGER,
    max_downloads INTEGER NOT NULL DEFAULT 0,
    downloads     INTEGER NOT NULL DEFAULT 0,
    created_at    INTEGER NOT NULL
  );

  CREATE TABLE IF NOT EXISTS favorites (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id    INTEGER NOT NULL,
    path       TEXT    NOT NULL,
    name       TEXT    NOT NULL,
    is_dir     INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL,
    UNIQUE (user_id, path)
  );

  CREATE TABLE IF NOT EXISTS recent (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id   INTEGER NOT NULL,
    path      TEXT    NOT NULL,
    name      TEXT    NOT NULL,
    is_dir    INTEGER NOT NULL DEFAULT 0,
    opened_at INTEGER NOT NULL,
    UNIQUE (user_id, path)
  );

  CREATE TABLE IF NOT EXISTS trash (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    uuid          TEXT    NOT NULL UNIQUE,
    original_path TEXT    NOT NULL,
    name          TEXT    NOT NULL,
    is_dir        INTEGER NOT NULL DEFAULT 0,
    size          INTEGER NOT NULL DEFAULT 0,
    owner_id      INTEGER NOT NULL,
    owner_name    TEXT    NOT NULL,
    deleted_at    INTEGER NOT NULL
  );

  CREATE TABLE IF NOT EXISTS mounts (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    name       TEXT    NOT NULL UNIQUE,
    abs_path   TEXT    NOT NULL,
    readonly   INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL
  );

  CREATE INDEX IF NOT EXISTS idx_tokens_hash ON tokens (token_hash);
  CREATE INDEX IF NOT EXISTS idx_tokens_user ON tokens (user_id);
  CREATE INDEX IF NOT EXISTS idx_audit_created ON audit_logs (created_at DESC);
  CREATE INDEX IF NOT EXISTS idx_shares_token ON shares (token);
  CREATE INDEX IF NOT EXISTS idx_shares_owner ON shares (owner_id);
  CREATE INDEX IF NOT EXISTS idx_favorites_user ON favorites (user_id);
  CREATE INDEX IF NOT EXISTS idx_recent_user ON recent (user_id, opened_at DESC);
  CREATE INDEX IF NOT EXISTS idx_trash_owner ON trash (owner_id, deleted_at DESC);
`);

const statements = {
  countUsers: db.prepare('SELECT COUNT(*) AS total FROM users'),
  listUsers: db.prepare('SELECT * FROM users ORDER BY id ASC'),
  getUserById: db.prepare('SELECT * FROM users WHERE id = ?'),
  getUserByName: db.prepare('SELECT * FROM users WHERE username = ? COLLATE NOCASE'),
  insertUser: db.prepare(`
    INSERT INTO users (username, password_hash, role, perms, disabled, created_at, updated_at)
    VALUES (@username, @password_hash, @role, @perms, @disabled, @created_at, @updated_at)
  `),
  updateUser: db.prepare(`
    UPDATE users
       SET username = @username,
           role = @role,
           perms = @perms,
           disabled = @disabled,
           updated_at = @updated_at
     WHERE id = @id
  `),
  updatePassword: db.prepare('UPDATE users SET password_hash = ?, updated_at = ? WHERE id = ?'),
  deleteUser: db.prepare('DELETE FROM users WHERE id = ?'),
  insertToken: db.prepare(`
    INSERT INTO tokens (user_id, token_hash, created_at, expires_at, last_used, ip)
    VALUES (?, ?, ?, ?, ?, ?)
  `),
  getTokenByHash: db.prepare('SELECT * FROM tokens WHERE token_hash = ?'),
  touchToken: db.prepare('UPDATE tokens SET last_used = ? WHERE id = ?'),
  deleteTokenByHash: db.prepare('DELETE FROM tokens WHERE token_hash = ?'),
  deleteTokensByUser: db.prepare('DELETE FROM tokens WHERE user_id = ?'),
  deleteExpiredTokens: db.prepare('DELETE FROM tokens WHERE expires_at <= ?'),
  insertAudit: db.prepare(`
    INSERT INTO audit_logs (user_id, username, action, detail, ip, created_at)
    VALUES (@user_id, @username, @action, @detail, @ip, @created_at)
  `),
  listAudit: db.prepare('SELECT * FROM audit_logs ORDER BY id DESC LIMIT ? OFFSET ?'),
  countAudit: db.prepare('SELECT COUNT(*) AS total FROM audit_logs'),
  getSetting: db.prepare('SELECT value FROM settings WHERE key = ?'),
  setSetting: db.prepare(`
    INSERT INTO settings (key, value) VALUES (?, ?)
    ON CONFLICT(key) DO UPDATE SET value = excluded.value
  `),
  /* ------------------------------ 分享链接 ------------------------------ */
  insertShare: db.prepare(`
    INSERT INTO shares (token, path, name, is_dir, owner_id, owner_name,
                        password_hash, expires_at, max_downloads, downloads, created_at)
    VALUES (@token, @path, @name, @is_dir, @owner_id, @owner_name,
            @password_hash, @expires_at, @max_downloads, @downloads, @created_at)
  `),
  listSharesByOwner: db.prepare('SELECT * FROM shares WHERE owner_id = ? ORDER BY id DESC'),
  listSharesAll: db.prepare('SELECT * FROM shares ORDER BY id DESC'),
  getShareByToken: db.prepare('SELECT * FROM shares WHERE token = ?'),
  getShareById: db.prepare('SELECT * FROM shares WHERE id = ?'),
  deleteShare: db.prepare('DELETE FROM shares WHERE id = ?'),
  incShareDownloads: db.prepare('UPDATE shares SET downloads = downloads + 1 WHERE id = ?'),
  /* ------------------------------- 收藏夹 ------------------------------- */
  insertFavorite: db.prepare(`
    INSERT OR IGNORE INTO favorites (user_id, path, name, is_dir, created_at)
    VALUES (@user_id, @path, @name, @is_dir, @created_at)
  `),
  deleteFavorite: db.prepare('DELETE FROM favorites WHERE user_id = ? AND path = ?'),
  listFavorites: db.prepare('SELECT * FROM favorites WHERE user_id = ? ORDER BY id DESC'),
  getFavorite: db.prepare('SELECT * FROM favorites WHERE user_id = ? AND path = ?'),
  /* ------------------------------- 最近访问 ------------------------------ */
  upsertRecent: db.prepare(`
    INSERT INTO recent (user_id, path, name, is_dir, opened_at)
    VALUES (@user_id, @path, @name, @is_dir, @opened_at)
    ON CONFLICT(user_id, path) DO UPDATE SET
      opened_at = excluded.opened_at,
      name = excluded.name
  `),
  listRecent: db.prepare('SELECT * FROM recent WHERE user_id = ? ORDER BY opened_at DESC LIMIT ?'),
  clearRecent: db.prepare('DELETE FROM recent WHERE user_id = ?'),
  /* -------------------------------- 回收站 ------------------------------- */
  insertTrash: db.prepare(`
    INSERT INTO trash (uuid, original_path, name, is_dir, size, owner_id, owner_name, deleted_at)
    VALUES (@uuid, @original_path, @name, @is_dir, @size, @owner_id, @owner_name, @deleted_at)
  `),
  listTrash: db.prepare(`
    SELECT * FROM trash
     WHERE (? IS NULL OR owner_id = ?)
     ORDER BY deleted_at DESC, id DESC
  `),
  getTrashByUuid: db.prepare('SELECT * FROM trash WHERE uuid = ?'),
  getTrashById: db.prepare('SELECT * FROM trash WHERE id = ?'),
  deleteTrash: db.prepare('DELETE FROM trash WHERE uuid = ?'),
  clearTrashByOwner: db.prepare('DELETE FROM trash WHERE (? IS NULL OR owner_id = ?)'),
  /* ------------------------------ 网盘挂载点 ------------------------------ */
  listMounts: db.prepare('SELECT * FROM mounts ORDER BY id ASC'),
  getMountByName: db.prepare('SELECT * FROM mounts WHERE name = ?'),
  getMountById: db.prepare('SELECT * FROM mounts WHERE id = ?'),
  insertMount: db.prepare(`
    INSERT INTO mounts (name, abs_path, readonly, created_at)
    VALUES (@name, @abs_path, @readonly, @created_at)
  `),
  deleteMount: db.prepare('DELETE FROM mounts WHERE id = ?')
};

function countUsers() {
  return statements.countUsers.get().total;
}

function listUsers() {
  return statements.listUsers.all();
}

function getUserById(id) {
  return statements.getUserById.get(Number(id));
}

function getUserByUsername(username) {
  return statements.getUserByName.get(String(username));
}

function createUser({ username, passwordHash, role, perms, disabled }) {
  const now = Date.now();
  const info = statements.insertUser.run({
    username,
    password_hash: passwordHash,
    role,
    perms,
    disabled: disabled ? 1 : 0,
    created_at: now,
    updated_at: now
  });
  return getUserById(info.lastInsertRowid);
}

function updateUser(id, { username, role, perms, disabled }) {
  statements.updateUser.run({
    id: Number(id),
    username,
    role,
    perms,
    disabled: disabled ? 1 : 0,
    updated_at: Date.now()
  });
  return getUserById(id);
}

function updateUserPassword(id, passwordHash) {
  statements.updatePassword.run(passwordHash, Date.now(), Number(id));
  return getUserById(id);
}

function deleteUser(id) {
  return statements.deleteUser.run(Number(id)).changes;
}

function createTokenRecord({ userId, tokenHash, expiresAt, ip }) {
  const now = Date.now();
  statements.insertToken.run(userId, tokenHash, now, expiresAt, now, ip || '');
}

function getTokenByHash(tokenHash) {
  return statements.getTokenByHash.get(tokenHash);
}

function touchToken(id) {
  statements.touchToken.run(Date.now(), id);
}

function deleteTokenByHash(tokenHash) {
  statements.deleteTokenByHash.run(tokenHash);
}

function deleteTokensByUser(userId) {
  statements.deleteTokensByUser.run(userId);
}

function deleteExpiredTokens() {
  statements.deleteExpiredTokens.run(Date.now());
}

function addAudit({ userId, username, action, detail, ip }) {
  statements.insertAudit.run({
    user_id: userId === undefined || userId === null ? null : Number(userId),
    username: username || '',
    action: String(action),
    detail: detail === undefined || detail === null ? '' : String(detail),
    ip: ip || '',
    created_at: Date.now()
  });
}

function listAudit(limit, offset) {
  const safeLimit = Math.min(Math.max(Number(limit) || 50, 1), 500);
  const safeOffset = Math.max(Number(offset) || 0, 0);
  return {
    total: statements.countAudit.get().total,
    items: statements.listAudit.all(safeLimit, safeOffset)
  };
}

function getSetting(key) {
  const row = statements.getSetting.get(String(key));
  return row ? row.value : null;
}

function setSetting(key, value) {
  statements.setSetting.run(String(key), String(value));
}

/* ------------------------------ 分享链接 ------------------------------ */

/** 创建分享记录，返回新插入的行 */
function createShare({ token, path: sharePath, name, isDir, ownerId, ownerName, passwordHash, expiresAt, maxDownloads }) {
  if (typeof token !== 'string' || token.length === 0) {
    throw new Error('分享令牌不能为空');
  }
  const info = statements.insertShare.run({
    token,
    path: String(sharePath),
    name: String(name),
    is_dir: isDir ? 1 : 0,
    owner_id: Number(ownerId),
    owner_name: String(ownerName),
    password_hash: passwordHash || null,
    expires_at: expiresAt === undefined || expiresAt === null ? null : Number(expiresAt),
    max_downloads: Math.max(Number(maxDownloads) || 0, 0),
    downloads: 0,
    created_at: Date.now()
  });
  return statements.getShareById.get(info.lastInsertRowid);
}

/** 列出分享：ownerId 为 null 时返回全部（仅管理员使用） */
function listShares(ownerId) {
  if (ownerId === null || ownerId === undefined) {
    return statements.listSharesAll.all();
  }
  return statements.listSharesByOwner.all(Number(ownerId));
}

function getShareByToken(token) {
  if (typeof token !== 'string' || token.length === 0) {
    return null;
  }
  return statements.getShareByToken.get(token);
}

function getShareById(id) {
  return statements.getShareById.get(Number(id));
}

function deleteShare(id) {
  return statements.deleteShare.run(Number(id)).changes;
}

function incShareDownloads(id) {
  statements.incShareDownloads.run(Number(id));
}

/* ------------------------------- 收藏夹 ------------------------------- */

function addFavorite({ userId, path: favPath, name, isDir }) {
  statements.insertFavorite.run({
    user_id: Number(userId),
    path: String(favPath),
    name: String(name),
    is_dir: isDir ? 1 : 0,
    created_at: Date.now()
  });
  return statements.getFavorite.get(Number(userId), String(favPath));
}

function removeFavorite(userId, favPath) {
  return statements.deleteFavorite.run(Number(userId), String(favPath)).changes;
}

function listFavorites(userId) {
  return statements.listFavorites.all(Number(userId));
}

/* ------------------------------- 最近访问 ------------------------------ */

function upsertRecent({ userId, path: recentPath, name, isDir }) {
  statements.upsertRecent.run({
    user_id: Number(userId),
    path: String(recentPath),
    name: String(name),
    is_dir: isDir ? 1 : 0,
    opened_at: Date.now()
  });
}

function listRecent(userId, limit) {
  const safeLimit = Math.min(Math.max(Number(limit) || 50, 1), 200);
  return statements.listRecent.all(Number(userId), safeLimit);
}

function clearRecent(userId) {
  return statements.clearRecent.run(Number(userId)).changes;
}

/* -------------------------------- 回收站 ------------------------------- */

function addTrashRecord({ uuid, originalPath, name, isDir, size, ownerId, ownerName }) {
  if (typeof uuid !== 'string' || uuid.length === 0) {
    throw new Error('回收站记录 uuid 不能为空');
  }
  statements.insertTrash.run({
    uuid,
    original_path: String(originalPath),
    name: String(name),
    is_dir: isDir ? 1 : 0,
    size: Math.max(Number(size) || 0, 0),
    owner_id: Number(ownerId),
    owner_name: String(ownerName),
    deleted_at: Date.now()
  });
  return statements.getTrashByUuid.get(uuid);
}

/** 列出回收站；ownerId 为 null 时返回全部（仅管理员使用） */
function listTrash(ownerId) {
  if (ownerId === null || ownerId === undefined) {
    return statements.listTrash.all(null, null);
  }
  return statements.listTrash.all(Number(ownerId), Number(ownerId));
}

function getTrashByUuid(uuid) {
  if (typeof uuid !== 'string' || uuid.length === 0) {
    return null;
  }
  return statements.getTrashByUuid.get(uuid);
}

function getTrashById(id) {
  return statements.getTrashById.get(Number(id));
}

function deleteTrashRecord(uuid) {
  return statements.deleteTrash.run(String(uuid)).changes;
}

/** 清空回收站；ownerId 为 null 时清空全部（仅管理员使用） */
function clearTrash(ownerId) {
  if (ownerId === null || ownerId === undefined) {
    return statements.clearTrashByOwner.run(null, null).changes;
  }
  return statements.clearTrashByOwner.run(Number(ownerId), Number(ownerId)).changes;
}

/* ------------------------------ 网盘挂载点 ------------------------------ */

function listMounts() {
  return statements.listMounts.all();
}

function getMountByName(name) {
  if (typeof name !== 'string' || name.length === 0) {
    return null;
  }
  return statements.getMountByName.get(name);
}

function getMountById(id) {
  return statements.getMountById.get(Number(id));
}

/** 新增挂载点，返回新插入的行 */
function insertMount({ name, abs_path: absPath, readonly, created_at: createdAt }) {
  const info = statements.insertMount.run({
    name: String(name),
    abs_path: String(absPath),
    readonly: readonly ? 1 : 0,
    created_at: createdAt === undefined || createdAt === null ? Date.now() : Number(createdAt)
  });
  return statements.getMountById.get(info.lastInsertRowid);
}

function deleteMount(id) {
  return statements.deleteMount.run(Number(id)).changes;
}

/** 将数据库行转换为可安全返回给前端的对象（绝不包含 password_hash） */
function serializeUser(row) {
  if (!row) {
    return null;
  }
  return {
    id: row.id,
    username: row.username,
    role: row.role,
    perms: row.perms,
    disabled: Boolean(row.disabled),
    createdAt: row.created_at,
    updatedAt: row.updated_at
  };
}

/** 分享记录转前端对象：绝不返回 password_hash，只返回 hasPassword 布尔 */
function serializeShare(row) {
  if (!row) {
    return null;
  }
  return {
    id: row.id,
    token: row.token,
    path: row.path,
    name: row.name,
    isDir: Boolean(row.is_dir),
    ownerId: row.owner_id,
    ownerName: row.owner_name,
    hasPassword: Boolean(row.password_hash),
    expiresAt: row.expires_at === null ? null : row.expires_at,
    maxDownloads: row.max_downloads,
    downloads: row.downloads,
    createdAt: row.created_at
  };
}

/** 收藏记录转前端对象 */
function serializeFavorite(row) {
  if (!row) {
    return null;
  }
  return {
    id: row.id,
    path: row.path,
    name: row.name,
    isDir: Boolean(row.is_dir),
    createdAt: row.created_at
  };
}

/** 最近访问记录转前端对象 */
function serializeRecent(row) {
  if (!row) {
    return null;
  }
  return {
    id: row.id,
    path: row.path,
    name: row.name,
    isDir: Boolean(row.is_dir),
    openedAt: row.opened_at
  };
}

/** 回收站记录转前端对象 */
function serializeTrash(row) {
  if (!row) {
    return null;
  }
  return {
    id: row.id,
    uuid: row.uuid,
    path: row.original_path,
    name: row.name,
    isDir: Boolean(row.is_dir),
    size: row.size,
    ownerId: row.owner_id,
    ownerName: row.owner_name,
    deletedAt: row.deleted_at
  };
}

module.exports = {
  db,
  countUsers,
  listUsers,
  getUserById,
  getUserByUsername,
  createUser,
  updateUser,
  updateUserPassword,
  deleteUser,
  createTokenRecord,
  getTokenByHash,
  touchToken,
  deleteTokenByHash,
  deleteTokensByUser,
  deleteExpiredTokens,
  addAudit,
  listAudit,
  getSetting,
  setSetting,
  serializeUser,
  /* 分享 */
  createShare,
  listShares,
  getShareByToken,
  getShareById,
  deleteShare,
  incShareDownloads,
  serializeShare,
  /* 收藏 */
  addFavorite,
  removeFavorite,
  listFavorites,
  serializeFavorite,
  /* 最近访问 */
  upsertRecent,
  listRecent,
  clearRecent,
  serializeRecent,
  /* 回收站 */
  addTrashRecord,
  listTrash,
  getTrashByUuid,
  getTrashById,
  deleteTrashRecord,
  clearTrash,
  serializeTrash,
  /* 网盘挂载点 */
  listMounts,
  getMountByName,
  getMountById,
  insertMount,
  deleteMount
};
