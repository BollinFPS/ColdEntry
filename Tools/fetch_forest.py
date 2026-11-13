"""Fetch CC0 Poly Haven forest source assets; standard-library Python only.
Raw downloads are excluded from Git. Run before prepare_forest.py and ue_import_forest.py.
"""
import concurrent.futures, hashlib, json, os, time, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "SourceArt" / "Forest"
API = "https://api.polyhaven.com"
MODELS = ["fir_tree_01", "pine_sapling_small", "fern_02", "grass_medium_01", "boulder_01", "dead_tree_trunk"]
SURFACES = ["forest_ground_04", "brown_mud_leaves_01", "gravel_floor", "concrete_wall_005", "peeling_painted_wall", "aerial_grass_rock"]
HEADERS = {"User-Agent": "ColdEntry-ForestArt/1.0 (Poly Haven CC0)"}

def api(asset):
    with urllib.request.urlopen(urllib.request.Request(f"{API}/files/{asset}", headers=HEADERS), timeout=60) as r:
        return json.load(r)

def pick(data, key, resolution="2k", alpha=False):
    if key not in data:
        return None
    variants = data[key]
    level = resolution if resolution in variants else sorted(variants, key=lambda r: int(r.rstrip("k")))[0]
    formats = variants[level]
    for ext in (["png", "jpg", "exr"] if alpha else ["jpg", "png", "exr"]):
        if ext in formats:
            return formats[ext], ext
    return None

def download(job):
    spec, path = job
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.stat().st_size == spec.get("size"):
        if not spec.get("md5") or hashlib.md5(path.read_bytes()).hexdigest() == spec["md5"]:
            return
    for attempt in range(3):
        try:
            with urllib.request.urlopen(urllib.request.Request(spec["url"], headers=HEADERS), timeout=120) as r, open(str(path) + ".part", "wb") as f:
                while chunk := r.read(1024 * 1024):
                    f.write(chunk)
            tmp = Path(str(path) + ".part")
            if spec.get("md5") and hashlib.md5(tmp.read_bytes()).hexdigest() != spec["md5"]:
                raise RuntimeError("Checksum mismatch: " + str(path))
            os.replace(tmp, path)
            print("Downloaded", path.name, round(path.stat().st_size / 1048576, 1), "MB", flush=True)
            return
        except Exception:
            if attempt == 2:
                raise
            time.sleep(2)

def main():
    jobs, manifest = [], {"license": "CC0", "provider": "Poly Haven", "models": {}, "surfaces": {}}
    def add(spec, asset, filename):
        path = OUT / "Downloaded" / asset / filename
        jobs.append((spec, path))
        return path.relative_to(ROOT).as_posix()
    for asset in MODELS + SURFACES:
        data = api(asset)
        entry = {"id": asset, "source": f"https://polyhaven.com/a/{asset}", "materials": {}}
        if asset in MODELS:
            spec = data["fbx"]["2k"]["fbx"]
            entry["fbx"] = add(spec, asset, asset + ".fbx")
            spec = data['blend']['2k']['blend']
            entry['blend'] = add(spec,asset,asset+'.blend')
        groups = [k[:-5] for k in data if k.endswith("_diff")]
        if "Diffuse" in data or "diff" in data:
            groups.append("")
        for group in groups:
            prefix = group + "_" if group else ""
            maps = {}
            choices = {"BaseColor": [prefix + "diff", "Diffuse"], "Normal": [prefix + "nor_dx"],
                       "ARM": [prefix + "arm"], "Alpha": [prefix + "alpha", "Alpha"],
                       "Roughness": [prefix + "rough", "Rough"], "AO": [prefix + "ao", "AO"]}
            for channel, keys in choices.items():
                for key in keys:
                    selected = pick(data, key, "4k" if asset in SURFACES else "2k", channel == "Alpha")
                    if selected:
                        spec, ext = selected
                        maps[channel] = add(spec, asset, f"{group or 'main'}_{channel}.{ext}")
                        break
            entry["materials"][group] = maps
        manifest["models" if asset in MODELS else "surfaces"][asset] = entry
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(download, jobs))
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print("Forest source manifest saved.", flush=True)

if __name__ == "__main__":
    main()
