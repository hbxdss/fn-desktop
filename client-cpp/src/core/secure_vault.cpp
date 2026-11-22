#include "core/secure_vault.h"

#include <QByteArray>
#include <QMutex>
#include <QMutexLocker>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ncrypt.h>
#include <ncryptprotect.h>
#include <wincrypt.h>

#include <string>
#include <vector>
#include <algorithm>

namespace {

const QLatin1String kTagTpm("tpm1:");
const QLatin1String kTagCng("cng1:");
const QLatin1String kTagDpapi("dpapi1:");

QMutex &mutex()
{
    static QMutex m;
    return m;
}

QString &sessionPasswordStorage()
{
    static QString s;
    return s;
}

// 探测平台加密提供器（TPM）。结果进程内缓存。
int detectProvider()
{
    // 0 = 未知/不可用；1 = TPM；2 = 仅有 CNG 软件提供器（探测不到 TPM 时也认为可用，
    // 因为 NCryptCreateProtectionDescriptor(LOCAL=user) 在受支持系统上总可用）
    static int cached = -1;
    if (cached != -1) {
        return cached;
    }
    cached = 0;
    NCRYPT_PROV_HANDLE provider = 0;
    SECURITY_STATUS st = NCryptOpenStorageProvider(&provider, MS_PLATFORM_CRYPTO_PROVIDER, 0);
    if (st == ERROR_SUCCESS && provider) {
        cached = 1;
        NCryptFreeObject(provider);
        return cached;
    }
    // 退而求其次：CNG 软件提供器能否打开（Win10 自带）
    provider = 0;
    st = NCryptOpenStorageProvider(&provider, MS_KEY_STORAGE_PROVIDER, 0);
    if (st == ERROR_SUCCESS && provider) {
        cached = 2;
        NCryptFreeObject(provider);
    }
    return cached;
}

// 用 CNG 保护机密。useTpm=true 时指定 Platform Crypto Provider（TPM）。
bool cngProtect(bool useTpm, const QByteArray &plain, QByteArray *out)
{
    NCRYPT_DESCRIPTOR_HANDLE descriptor = nullptr;
    // "PROVIDERS=Microsoft Platform Crypto Provider" 让 CNG 把密钥托管在 TPM 中；
    // "LOCAL=user" 是当前用户绑定的软件密钥保护描述符。
    std::wstring rule = useTpm
                            ? std::wstring(L"PROVIDERS=") + MS_PLATFORM_CRYPTO_PROVIDER
                            : std::wstring(L"LOCAL=user");
    SECURITY_STATUS st = NCryptCreateProtectionDescriptor(rule.c_str(), 0, &descriptor);
    if (st != ERROR_SUCCESS || !descriptor) {
        return false;
    }

    BYTE *blob = nullptr;
    ULONG blobSize = 0;
    st = NCryptProtectSecret(descriptor, 0,
                             reinterpret_cast<const BYTE *>(plain.constData()),
                             ULONG(plain.size()), nullptr, nullptr, &blob, &blobSize);
    NCryptCloseProtectionDescriptor(descriptor);
    if (st != ERROR_SUCCESS || !blob || blobSize == 0) {
        return false;
    }
    *out = QByteArray(reinterpret_cast<const char*>(blob), int(blobSize));
    LocalFree(blob);
    return true;
}

bool cngUnprotect(const QByteArray &packed, QByteArray *out)
{
    BYTE *plain = nullptr;
    ULONG plainSize = 0;
    SECURITY_STATUS st = NCryptUnprotectSecret(
        nullptr, 0, reinterpret_cast<const BYTE *>(packed.constData()), ULONG(packed.size()),
        nullptr, nullptr, &plain, &plainSize);
    if (st != ERROR_SUCCESS || !plain || plainSize == 0) {
        return false;
    }
    *out = QByteArray(reinterpret_cast<const char*>(plain), int(plainSize));
    LocalFree(plain);
    return true;
}

bool dpapiProtect(const QByteArray &plain, QByteArray *out)
{
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()));
    in.cbData = DWORD(plain.size());
    DATA_BLOB result{};
    const BOOL ok = CryptProtectData(&in, L"fn-desktop-plus", nullptr, nullptr, nullptr,
                                     CRYPTPROTECT_UI_FORBIDDEN, &result);
    if (!ok || !result.pbData) {
        return false;
    }
    *out = QByteArray(reinterpret_cast<const char*>(result.pbData), int(result.cbData));
    LocalFree(result.pbData);
    return true;
}

bool dpapiUnprotect(const QByteArray &packed, QByteArray *out)
{
    DATA_BLOB in{};
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(packed.constData()));
    in.cbData = DWORD(packed.size());
    DATA_BLOB result{};
    LPWSTR name = nullptr;
    const BOOL ok = CryptUnprotectData(&in, &name, nullptr, nullptr, nullptr,
                                       CRYPTPROTECT_UI_FORBIDDEN, &result);
    if (name) {
        LocalFree(name);
    }
    if (!ok || !result.pbData) {
        return false;
    }
    *out = QByteArray(reinterpret_cast<const char*>(result.pbData), int(result.cbData));
    LocalFree(result.pbData);
    return true;
}

} // namespace

namespace SecureVault {

QString providerName()
{
    switch (detectProvider()) {
    case 1:
        return QStringLiteral("TPM 2.0");
    case 2:
        return QStringLiteral("DPAPI-NG");
    default:
        return QStringLiteral("DPAPI（兼容回退）");
    }
}

bool hasTpm()
{
    return detectProvider() == 1;
}

QString protect(const QString &plain)
{
    if (plain.isEmpty()) {
        return QString();
    }
    QMutexLocker locker(&mutex());
    const QByteArray raw = plain.toUtf8();
    QByteArray blob;

    if (detectProvider() == 1 && cngProtect(true, raw, &blob)) {
        return kTagTpm + QString::fromLatin1(blob.toBase64());
    }
    if (cngProtect(false, raw, &blob)) {
        return kTagCng + QString::fromLatin1(blob.toBase64());
    }
    if (dpapiProtect(raw, &blob)) {
        return kTagDpapi + QString::fromLatin1(blob.toBase64());
    }
    return QString();
}

QString unprotect(const QString &packed)
{
    if (packed.isEmpty()) {
        return QString();
    }
    QMutexLocker locker(&mutex());

    QLatin1String tag;
    if (packed.startsWith(kTagTpm)) {
        tag = kTagTpm;
    } else if (packed.startsWith(kTagCng)) {
        tag = kTagCng;
    } else if (packed.startsWith(kTagDpapi)) {
        tag = kTagDpapi;
    } else {
        return QString();
    }

    const QByteArray raw = QByteArray::fromBase64(packed.mid(tag.size()).toLatin1());
    QByteArray out;
    bool ok = false;
    if (tag == kTagTpm || tag == kTagCng) {
        ok = cngUnprotect(raw, &out);
        if (!ok && tag == kTagTpm) {
            // 极个别情况下提供器降级，CNG 描述符本身不兼容时仍尝试 DPAPI 无意义，直接失败。
            return QString();
        }
    } else {
        ok = dpapiUnprotect(raw, &out);
    }
    return ok ? QString::fromUtf8(out) : QString();
}

void setSessionPassword(const QString &password)
{
    QMutexLocker locker(&mutex());
    sessionPasswordStorage() = password;
}

QString sessionPassword()
{
    QMutexLocker locker(&mutex());
    return sessionPasswordStorage();
}

void clearSessionPassword()
{
    QMutexLocker locker(&mutex());
    QString &p = sessionPasswordStorage();
    std::fill(p.begin(), p.end(), QChar(0));
    p.clear();
}

} // namespace SecureVault

