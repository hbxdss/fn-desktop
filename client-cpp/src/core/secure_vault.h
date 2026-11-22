#pragma once

#include <QString>

/**
 * 本机敏感信息保管库。
 *
 * 加密优先级：
 * 1. TPM 2.0：CNG(NCrypt*) + Microsoft Platform Crypto Provider，
 *    密钥由 TPM 保护，密文带 "tpm1:" 前缀；
 * 2. DPAPI-NG（CNG LOCAL=user）：软件密钥但仍绑定当前 Windows 用户，密文带 "cng1:"；
 * 3. DPAPI（CryptProtectData）：经典用户绑定加密，密文带 "dpapi1:"。
 *
 * 密文统一为 base64 文本并带方案前缀，可安全写入 config.json。
 * 另外提供进程内会话密码（不落盘），用于本次登录期间的自动填充。
 */
namespace SecureVault {

/** 当前机器可用的保护方式名称（TPM 2.0 / DPAPI-NG / DPAPI）。 */
QString providerName();
/** 是否检测到可用的 TPM 平台加密提供器。 */
bool hasTpm();

/** 加密；空串返回空串。失败也返回空串。 */
QString protect(const QString &plain);
/** 解密；按前缀自动选择方案；失败返回空串。 */
QString unprotect(const QString &packed);

/** 保存本次登录的 NAS 密码（仅进程内存，不落盘）。 */
void setSessionPassword(const QString &password);
/** 取本次登录密码；没有时为空。 */
QString sessionPassword();
/** 退出登录时清除内存密码。 */
void clearSessionPassword();

} // namespace SecureVault
