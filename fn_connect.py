import concurrent.futures
import urllib3
import requests

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

DOMAINS = ["5ddd.com", "fnos.net"]


def _probe(fnid, domain, timeout=6):
    url = f"https://{fnid}.{domain}"
    try:
        r = requests.get(url, timeout=timeout, verify=False, allow_redirects=True,
                         headers={"User-Agent": "Mozilla/5.0 fnos-desktop"})
        return {"domain": domain, "url": url, "status": r.status_code, "ok": True}
    except requests.exceptions.SSLError:
        return {"domain": domain, "url": url, "status": "ssl", "ok": True}
    except Exception:
        return {"domain": domain, "url": url, "ok": False}


def search_fnos(fnid, timeout=6):
    fnid = fnid.strip().lower()
    if not fnid:
        return []
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        futures = [pool.submit(_probe, fnid, d, timeout) for d in DOMAINS]
        for f in concurrent.futures.as_completed(futures):
            r = f.result()
            if r.get("ok"):
                results.append(r)
    return results



