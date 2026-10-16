import os
import json
import base64
import hashlib
import platform
import uuid
from datetime import datetime
from pathlib import Path

from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.kdf.scrypt import Scrypt

from data_store import get_store


class SecureStore:
    VERIFY_TOKEN = b"FNOS_DESKTOP_VERIFY_V1"
    BACKUP_FORMAT = "fnos-desktop-backup"

    def __init__(self):
        self.store = get_store()
        self.mode = "auto"
        self.has_tpm = self._detect_tpm()
        self._password_state = None  # 密码状态探测结果缓存

    def _detect_tpm(self):
        try:
            import wmi
            c = wmi.WMI(namespace="root/cimv2/security/microsofttpm")
            for tpm in c.Win32_Tpm():
                if bool(tpm.IsActivated()) and str(tpm.SpecVersion or "").startswith("2.0"):
                    return True
        except Exception:
            pass
        return False

    def _get_blob(self):
        return self.store.get("secure_blob", {})

    def _set_blob(self, blob):
        self.store.set("secure_blob", blob)

    def get_mode(self):
        return self._get_blob().get("mode", "")

    def encryption_label(self):
        if self.get_mode():
            m = self.get_mode()
        elif self.has_tpm:
            m = "tpm"
        elif self.is_windows_password_empty():
            m = "fingerprint"
        else:
            m = "password"
        return {
            "tpm": "🔒 硬件加密 (TPM 2.0)",
            "password": "🔑 Windows 密码加密",
            "custom": "🔐 自定义密码加密",
            "pin": "📌 PIN 码加密",
            "fingerprint": "🧬 机器指纹加密",
        }.get(m, "未知")

    def effective_mode(self):
        if self.mode != "auto":
            return self.mode
        if self.has_tpm:
            return "tpm"
        # 只有明确确认账户设置了密码，才进入 Windows 密码加密；
        # 无密码或无法判断（unknown）都降级到机器指纹（无感），
        # 避免对无密码账户弹出无意义的解锁/验证框。
        state = self._probe_password_state()
        if state == "set":
            return "password"
        return "fingerprint"

    def _dpapi_encrypt(self, data):
        import win32crypt
        return win32crypt.CryptProtectData(data, None, None, None, None, 0)

    def _dpapi_decrypt(self, blob):
        import win32crypt
        return win32crypt.CryptUnprotectData(blob, None, None, None, None, 0)[1]

    @staticmethod
    def is_windows_password_empty(username=None):
        """真实探测当前 Windows 账户是否未设置密码。

        探测结果映射：
        - "empty"（确认无密码）→ True，程序走机器指纹加密，不弹密码框；
        - "set"（确认有密码）→ False，走 Windows 密码加密；
        - "unknown"（无法判断）→ True，自动降级到机器指纹，并由
          password_state_note() 在 UI 明确提示，不静默失效。
        """
        try:
            inst = SecureStore()
            return inst._probe_password_state() != "set"
        except Exception:
            return True

    def _probe_password_state(self):
        """探测 Windows 账户密码状态（带实例缓存）。

        返回: "empty"=确认无密码 / "set"=确认有密码 / "unknown"=无法判断
        探测顺序：
        1. WMI Win32_UserAccount.PasswordRequired（最准确，无需登录）；
        2. LogonUser 空密码交互式登录探测；
        3. 两者均不可用 → "unknown"。
        """
        if self._password_state is not None:
            return self._password_state
        username = SecureStore.current_username()
        state = "unknown"
        if username:
            try:
                import wmi
                c = wmi.WMI()
                accounts = c.Win32_UserAccount(Name=username)
                if accounts:
                    state = "empty" if not bool(accounts[0].PasswordRequired) else "set"
            except Exception:
                state = "unknown"
            if state == "unknown":
                try:
                    import win32security
                    h = win32security.LogonUser(
                        username, None, "",
                        win32security.LOGON32_LOGON_INTERACTIVE,
                        win32security.LOGON32_PROVIDER_DEFAULT)
                    win32security.CloseHandle(h)
                    state = "empty"
                except Exception:
                    state = "unknown"
        self._password_state = state
        return state

    def password_state_note(self):
        """当密码状态无法判断并发生自动降级时，返回给 UI 的说明文字。"""
        if self._probe_password_state() == "unknown":
            return ("未能确认 Windows 账户密码状态，已自动降级为机器指纹加密（无感解密）；"
                    "如需更安全的加密方式，可在「设置 / 密码迁移」中手动切换。")
        return ""

    @staticmethod
    def verify_windows_password(username, password):
        try:
            import win32security
            h = win32security.LogonUser(username, None, password,
                win32security.LOGON32_LOGON_INTERACTIVE,
                win32security.LOGON32_PROVIDER_DEFAULT)
            win32security.CloseHandle(h)
            return True
        except Exception:
            return False

    @staticmethod
    def current_username():
        return os.environ.get("USERNAME") or os.environ.get("USER") or ""

    def _machine_seed(self):
        parts = [platform.node(), str(uuid.getnode()),
                 os.environ.get("USERNAME") or os.environ.get("USER") or "",
                 platform.machine(), platform.processor()]
        return "|".join(parts).encode("utf-8", errors="ignore")

    def _derive_from_password(self, password, salt):
        return Scrypt(salt=salt, length=32, n=2**14, r=8, p=1).derive(password.encode("utf-8"))

    def _derive_from_machine(self, salt):
        seed = hashlib.sha256(self._machine_seed()).digest()
        return Scrypt(salt=salt, length=32, n=2**14, r=8, p=1).derive(seed)

    def save(self, data, password=""):
        plaintext = json.dumps(data, ensure_ascii=False).encode("utf-8")
        actual = self.effective_mode()
        if actual == "tpm":
            blob = {"mode": "tpm", "data": base64.b64encode(self._dpapi_encrypt(plaintext)).decode()}
        elif actual in ("password", "custom", "pin"):
            if not password:
                raise ValueError(f"{actual} 模式需要密码")
            salt = os.urandom(16)
            key = self._derive_from_password(password, salt)
            nonce = os.urandom(12)
            ct = AESGCM(key).encrypt(nonce, plaintext, None)
            vn = os.urandom(12)
            vb = vn + AESGCM(key).encrypt(vn, self.VERIFY_TOKEN, None)
            blob = {"mode": actual, "username": self.current_username(),
                    "salt": base64.b64encode(salt).decode(),
                    "nonce": base64.b64encode(nonce).decode(),
                    "data": base64.b64encode(ct).decode(),
                    "verify": base64.b64encode(vb).decode()}
        else:
            salt = os.urandom(16)
            key = self._derive_from_machine(salt)
            nonce = os.urandom(12)
            ct = AESGCM(key).encrypt(nonce, plaintext, None)
            blob = {"mode": "fingerprint",
                    "salt": base64.b64encode(salt).decode(),
                    "nonce": base64.b64encode(nonce).decode(),
                    "data": base64.b64encode(ct).decode()}
        self._set_blob(blob)

    def get_saved_username(self):
        return self._get_blob().get("username", "")

    def load(self, password=""):
        blob = self._get_blob()
        if not blob:
            return {}
        try:
            mode = blob.get("mode", "")
            if mode == "tpm":
                if not self.has_tpm:
                    return {}
                pt = self._dpapi_decrypt(base64.b64decode(blob["data"]))
            elif mode in ("password", "custom", "pin"):
                if not password:
                    return {}
                salt = base64.b64decode(blob["salt"])
                key = self._derive_from_password(password, salt)
                try:
                    vb = base64.b64decode(blob["verify"])
                    AESGCM(key).decrypt(vb[:12], vb[12:], None)
                except Exception:
                    return {}
                pt = AESGCM(key).decrypt(base64.b64decode(blob["nonce"]),
                                          base64.b64decode(blob["data"]), None)
            else:
                salt = base64.b64decode(blob["salt"])
                key = self._derive_from_machine(salt)
                pt = AESGCM(key).decrypt(base64.b64decode(blob["nonce"]),
                                          base64.b64decode(blob["data"]), None)
            return json.loads(pt)
        except Exception:
            return {}

    def clear(self):
        self.store.delete("secure_blob")

    def migrate(self, old_password, new_mode, new_password=""):
        if not self.get_mode():
            return False, "没有找到已保存的凭据"
        data = self.load(password=old_password)
        if not data:
            return False, "旧凭据解密失败，请检查旧密码"
        old_blob = self._get_blob()
        self.clear()
        self.mode = new_mode
        try:
            self.save(data, password=new_password)
            return True, "迁移成功"
        except Exception as e:
            self._set_blob(old_blob)
            return False, f"迁移失败: {e}"

    def export_backup(self, data, migration_password, output_path):
        salt = os.urandom(16)
        key = self._derive_from_password(migration_password, salt)
        nonce = os.urandom(12)
        ct = AESGCM(key).encrypt(nonce, json.dumps(data, ensure_ascii=False).encode("utf-8"), None)
        blob = {"format": self.BACKUP_FORMAT, "version": 1,
                "salt": base64.b64encode(salt).decode(),
                "nonce": base64.b64encode(nonce).decode(),
                "data": base64.b64encode(ct).decode(),
                "created_at": datetime.now().isoformat(timespec="seconds")}
        Path(output_path).write_text(json.dumps(blob), encoding="utf-8")

    def import_backup(self, backup_path, migration_password):
        try:
            blob = json.loads(Path(backup_path).read_text(encoding="utf-8"))
            if blob.get("format") != self.BACKUP_FORMAT:
                return None
            salt = base64.b64decode(blob["salt"])
            key = self._derive_from_password(migration_password, salt)
            pt = AESGCM(key).decrypt(base64.b64decode(blob["nonce"]),
                                      base64.b64decode(blob["data"]), None)
            return json.loads(pt)
        except Exception:
            return None




