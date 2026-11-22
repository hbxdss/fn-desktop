'use strict';

/**
 * 自签证书生成器
 * 使用系统 OpenSSL 生成 RSA 2048 / SHA-256 / 10 年有效期的证书，包含本机 IP 与主机名的 SAN。
 * 用法：node gen_cert.js [--force]
 */

const crypto = require('crypto');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');

const { CERT_DIR, ensureDir } = require('./util');

const KEY_PATH = path.join(CERT_DIR, 'key.pem');
const CERT_PATH = path.join(CERT_DIR, 'cert.pem');
const CONFIG_PATH = path.join(CERT_DIR, 'openssl.cnf');

function findOpenssl() {
  if (process.env.OPENSSL_BIN && fs.existsSync(process.env.OPENSSL_BIN)) {
    return process.env.OPENSSL_BIN;
  }
  const candidates = [
    'openssl',
    'C:\\Program Files\\Git\\usr\\bin\\openssl.exe',
    'C:\\Program Files\\Git\\mingw64\\bin\\openssl.exe',
    'C:\\Program Files\\OpenSSL-Win64\\bin\\openssl.exe',
    'C:\\OpenSSL-Win64\\bin\\openssl.exe',
    'C:\\Program Files (x86)\\OpenSSL-Win32\\bin\\openssl.exe',
    '/usr/bin/openssl',
    '/usr/local/bin/openssl',
    '/opt/bin/openssl'
  ];
  for (const candidate of candidates) {
    if (candidate === 'openssl') {
      const check = spawnSync('openssl', ['version'], { encoding: 'utf8', windowsHide: true });
      if (!check.error && check.status === 0) {
        return 'openssl';
      }
      continue;
    }
    if (fs.existsSync(candidate)) {
      return candidate;
    }
  }
  return null;
}

function collectIpAddresses() {
  const result = new Set();
  const interfaces = os.networkInterfaces();
  for (const name of Object.keys(interfaces)) {
    const list = interfaces[name] || [];
    for (const item of list) {
      if (item && item.family === 'IPv4' && !item.internal) {
        result.add(item.address);
      }
    }
  }
  return Array.from(result);
}

function buildConfig() {
  const hostname = os.hostname();
  const ips = collectIpAddresses();
  const sanParts = ['DNS:localhost', `DNS:${hostname}`, 'IP:127.0.0.1'];
  for (const ip of ips) {
    sanParts.push(`IP:${ip}`);
  }
  return `[req]
default_bits       = 2048
prompt             = no
default_md         = sha256
distinguished_name = dn
x509_extensions    = v3_ext

[dn]
C  = CN
O  = fnos-desktop
CN = ${hostname}

[v3_ext]
basicConstraints       = critical, CA:TRUE
keyUsage               = critical, digitalSignature, keyEncipherment, keyCertSign
extendedKeyUsage       = serverAuth
subjectAltName         = ${sanParts.join(', ')}
subjectKeyIdentifier   = hash
`;
}

function certificateInfo() {
  if (!fs.existsSync(CERT_PATH) || !fs.existsSync(KEY_PATH)) {
    return null;
  }
  try {
    const pem = fs.readFileSync(CERT_PATH, 'utf8');
    const cert = new crypto.X509Certificate(pem);
    return {
      keyPath: KEY_PATH,
      certPath: CERT_PATH,
      key: fs.readFileSync(KEY_PATH, 'utf8'),
      cert: pem,
      fingerprint: cert.fingerprint256.replace(/:/g, '').toLowerCase(),
      fingerprintPretty: cert.fingerprint256,
      subject: cert.subject,
      validFrom: cert.validFrom,
      validTo: cert.validTo,
      subjectAltName: cert.subjectAltName || ''
    };
  } catch (err) {
    console.error('[证书] 现有证书无法解析：', err.message);
    return null;
  }
}

function generateCertificate(force) {
  ensureDir(CERT_DIR);
  if (!force) {
    const existing = certificateInfo();
    if (existing) {
      const expiresAt = new Date(existing.validTo).getTime();
      if (Number.isFinite(expiresAt) && expiresAt - Date.now() > 30 * 24 * 60 * 60 * 1000) {
        return existing;
      }
      console.warn('[证书] 现有证书即将过期，重新生成');
    }
  }

  const openssl = findOpenssl();
  if (!openssl) {
    throw new Error(
      '未找到 OpenSSL。请安装 Git for Windows（自带 openssl）或 OpenSSL，并通过环境变量 OPENSSL_BIN 指定路径后重试。'
    );
  }

  fs.writeFileSync(CONFIG_PATH, buildConfig(), { mode: 0o600 });
  const args = [
    'req',
    '-x509',
    '-newkey',
    'rsa:2048',
    '-sha256',
    '-days',
    '3650',
    '-nodes',
    '-keyout',
    KEY_PATH,
    '-out',
    CERT_PATH,
    '-config',
    CONFIG_PATH,
    '-extensions',
    'v3_ext'
  ];
  const result = spawnSync(openssl, args, { encoding: 'utf8', windowsHide: true });
  if (result.error) {
    throw new Error(`调用 OpenSSL 失败：${result.error.message}`);
  }
  if (result.status !== 0) {
    throw new Error(`OpenSSL 生成证书失败：${(result.stderr || '').trim() || '未知错误'}`);
  }
  try {
    fs.chmodSync(KEY_PATH, 0o600);
    fs.chmodSync(CONFIG_PATH, 0o600);
  } catch (chmodErr) {
    console.warn('[证书] 无法设置私钥文件权限：', chmodErr.message);
  }
  const info = certificateInfo();
  if (!info) {
    throw new Error('证书已生成但无法读取，请检查证书文件是否完整');
  }
  return info;
}

if (require.main === module) {
  try {
    const force = process.argv.includes('--force');
    const info = generateCertificate(force);
    console.log('证书已就绪：');
    console.log('  证书路径：', info.certPath);
    console.log('  私钥路径：', info.keyPath);
    console.log('  有效期至：', info.validTo);
    console.log('  SHA-256 指纹：', info.fingerprintPretty);
  } catch (err) {
    console.error('[证书] 生成失败：', err.message);
    process.exitCode = 1;
  }
}

module.exports = { generateCertificate, certificateInfo, CERT_PATH, KEY_PATH };
