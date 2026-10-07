import json
from pathlib import Path


class DataStore:
    def __init__(self):
        self.path = Path.home() / ".fnos-browser" / "data.json"
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._data = self._load()

    def _load(self):
        if self.path.exists():
            try:
                return json.loads(self.path.read_text(encoding="utf-8"))
            except Exception:
                pass
        return {}

    def _save(self):
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self._data, ensure_ascii=False, indent=2), encoding="utf-8")
        tmp.replace(self.path)

    def get(self, key, default=None):
        return self._data.get(key, default)

    def set(self, key, value):
        self._data[key] = value
        self._save()

    def delete(self, key):
        if key in self._data:
            del self._data[key]
            self._save()

    def all(self):
        return dict(self._data)


# 模块级单例，直接 import 就能用
_instance = None


def get_store():
    global _instance
    if _instance is None:
        _instance = DataStore()
    return _instance


