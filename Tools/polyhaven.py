"""
COLD ENTRY - Poly Haven asset fetcher (CC0 photo-scanned textures, HDRIs, models).
Assets are public domain (CC0). Uses the public API at https://api.polyhaven.com  - Powered by Poly Haven.

Standard-library only, so it runs with the Python that ships inside Unreal:
  "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\ThirdParty\\Python3\\Win64\\python.exe" Tools\\polyhaven.py catalog
  ...                                                                                               Tools\\polyhaven.py fetch

catalog : writes SourceArt/PolyHaven/catalog_{textures,hdris,models}.json (id, name, categories, tags)
fetch   : downloads everything listed in Tools/photoreal_manifest.json into SourceArt/PolyHaven/<id>/
"""
import json, os, sys, time, urllib.request, urllib.error

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(ROOT, "SourceArt", "PolyHaven")
API = "https://api.polyhaven.com"
UA = {"User-Agent": "polyhaven-asset-fetch/1.0"}


def get_json(url):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.loads(r.read().decode("utf-8"))


def download(url, path):
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return "cached"
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".part"
    for attempt in range(3):
        try:
            req = urllib.request.Request(url, headers=UA)
            with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
                while True:
                    chunk = r.read(1 << 20)
                    if not chunk:
                        break
                    f.write(chunk)
            os.replace(tmp, path)
            return "ok"
        except Exception as e:  # retry transient failures
            print("   retry", attempt + 1, e)
            time.sleep(2)
    return "FAILED"


def catalog():
    os.makedirs(OUT, exist_ok=True)
    for t in ("textures", "hdris", "models"):
        data = get_json(f"{API}/assets?t={t}")
        slim = {k: {"name": v.get("name"), "categories": v.get("categories", []), "tags": v.get("tags", []),
                    "downloads": v.get("download_count", 0)} for k, v in data.items()}
        with open(os.path.join(OUT, f"catalog_{t}.json"), "w", encoding="utf-8") as f:
            json.dump(slim, f, indent=0)
        print(t, len(slim))


# Poly Haven map names -> our channel names (first match wins)
MAPS = {
    "BaseColor": ["Diffuse", "diff", "Color"],
    "Normal": ["nor_dx", "nor_gl"],
    "Roughness": ["Rough", "rough"],
    "AO": ["AO", "ao"],
    "Metal": ["Metal", "metal"],
}


def pick(files, names, res):
    for n in names:
        if n in files:
            tree = files[n]
            r = res if res in tree else sorted(tree.keys(), key=lambda s: int(s.rstrip("k")))[0]
            fmt = tree[r]
            for ext in ("jpg", "png", "exr"):
                if ext in fmt:
                    return n, fmt[ext]["url"], ext
    return None, None, None


def fetch():
    man_path = os.path.join(os.path.dirname(__file__), "photoreal_manifest.json")
    with open(man_path, encoding="utf-8") as f:
        man = json.load(f)
    res = man.get("resolution", "2k")
    result = {"textures": {}, "hdri": None, "models": {}}
    for surf, tid in man["textures"].items():
        if not tid:
            continue
        print(f"[texture] {surf} <- {tid}")
        try:
            files = get_json(f"{API}/files/{tid}")
        except Exception as e:
            print("    FAILED to look up", tid, e)
            continue
        entry = {"id": tid}
        for chan, names in MAPS.items():
            n, url, ext = pick(files, names, res)
            if not url:
                continue
            p = os.path.join(OUT, tid, f"{tid}_{chan}.{ext}")
            print("   ", chan, n, download(url, p))
            entry[chan] = p
        result["textures"][surf] = entry
    if man.get("hdri"):
        hid = man["hdri"]
        print(f"[hdri] {hid}")
        try:
            files = get_json(f"{API}/files/{hid}")
            tree = files["hdri"]
        except Exception as e:
            print("    FAILED", e)
            tree = None
    if man.get("hdri") and tree:
        r = man.get("hdri_resolution", "4k")
        hid = man["hdri"]
        r = r if r in tree else "2k"
        url = tree[r]["hdr"]["url"]
        p = os.path.join(OUT, hid, f"{hid}_{r}.hdr")
        print("   ", download(url, p))
        result["hdri"] = p
    # Models: the FBX alone, plus three small JPGs per material (base colour, DirectX normal, AO/rough/metal).
    # A model with several materials publishes its maps as "<part>_diff", "<part>_nor_dx", "<part>_arm".
    for key, spec in man.get("models", {}).items():
        if not spec:
            continue
        mid, mres = (spec, res) if isinstance(spec, str) else (spec["id"], spec.get("res", res))
        print(f"[model] {key} <- {mid} ({mres})")
        try:
            files = get_json(f"{API}/files/{mid}")
        except Exception as e:
            print("    FAILED to look up", mid, e)
            continue
        if "fbx" not in files:
            print("    no fbx, skipped")
            continue

        def at(tree, want):
            return tree[want] if want in tree else tree[sorted(tree.keys(), key=lambda s: int(s.rstrip("k")))[0]]

        base = os.path.join(OUT, mid)
        node = at(files["fbx"], mres)["fbx"]
        fbx = os.path.join(base, f"{mid}.fbx")
        print("    fbx", download(node["url"], fbx))
        groups = {}
        if "Diffuse" in files:
            groups[""] = {"BaseColor": "Diffuse", "Normal": "nor_dx", "ARM": "arm"}
        for k in files:
            if k.endswith("_diff"):
                g = k[:-5]
                groups[g] = {"BaseColor": k, "Normal": g + "_nor_dx", "ARM": g + "_arm"}
        mats = {}
        for g, maps in groups.items():
            entry = {}
            for chan, mapname in maps.items():
                if mapname not in files:
                    continue
                fmt = at(files[mapname], mres)
                ext = "jpg" if "jpg" in fmt else ("png" if "png" in fmt else None)
                if not ext:
                    continue
                p = os.path.join(base, f"{mid}{'_' + g if g else ''}_{chan}.{ext}")
                print("   ", g or "main", chan, download(fmt[ext]["url"], p))
                entry[chan] = p
            mats[g] = entry
        result["models"][key] = {"id": mid, "fbx": fbx, "materials": mats}
    with open(os.path.join(OUT, "fetched.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, indent=1)
    print("DONE ->", os.path.join(OUT, "fetched.json"))


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "fetch"
    {"catalog": catalog, "fetch": fetch}[cmd]()
