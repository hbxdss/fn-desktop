from datetime import datetime
from data_store import get_store


class TrustStore:
    def __init__(self):
        self.store = get_store()

    def _get_all(self):
        return self.store.get("trusted_certs", {})

    def _save_all(self, data):
        self.store.set("trusted_certs", data)

    def get(self, host):
        return self._get_all().get(host)

    def trust(self, host, fingerprint, subject=""):
        data = self._get_all()
        data[host] = {"fingerprint": fingerprint, "subject": subject,
                      "trusted_at": datetime.now().isoformat(timespec="seconds")}
        self._save_all(data)

    def is_trusted(self, host, fingerprint):
        rec = self._get_all().get(host)
        if not rec:
            return None
        return rec.get("fingerprint") == fingerprint



