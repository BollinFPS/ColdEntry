"""
COLD ENTRY - imports fetched Poly Haven assets into /Game/Photoreal and builds materials.
Run inside Unreal (Python plugin), e.g.:
  UnrealEditor-Cmd.exe ColdEntry.uproject -run=pythonscript -script="<abs path>/Tools/ue_import_photoreal.py" -unattended -nosplash
Reads SourceArt/PolyHaven/fetched.json (written by polyhaven.py fetch) and Tools/photoreal_manifest.json (tints/tiling).
Idempotent: re-running replaces assets.
"""
import json, os, traceback
import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SRC = os.path.join(ROOT, "SourceArt", "PolyHaven")
DST = "/Game/Photoreal"
AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


def log(*a):
    unreal.log("[CE-Photoreal] " + " ".join(str(x) for x in a))


def import_file(path, dest, name):
    t = unreal.AssetImportTask()
    t.filename = path
    t.destination_path = dest
    t.destination_name = name
    t.automated = True
    t.replace_existing = True
    t.save = False
    AT.import_asset_tasks([t])
    obj = unreal.load_asset(f"{dest}/{name}")
    if not obj:
        log("IMPORT FAILED", path)
    return obj


def setup_texture(tex, chan):
    if chan == "BaseColor":
        tex.set_editor_property("srgb", True)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    elif chan == "Normal":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("flip_green_channel", False)  # Poly Haven nor_dx = DirectX, same as UE
    else:
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    EAL.save_loaded_asset(tex)


# ---------------------------------------------------------------------------------------------
# Master material: world-aligned box projection (all level geometry is axis-aligned boxes)
# ---------------------------------------------------------------------------------------------
def build_master():
    path = f"{DST}/Materials/M_CE_Surface"
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mat = AT.create_asset("M_CE_Surface", f"{DST}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    mat.set_editor_property("tangent_space_normal", False)  # we output a world-space normal

    X = [0]
    def node(cls, x=None, y=0):
        X[0] += 1
        return MEL.create_material_expression(mat, cls, -1600 + (x if x is not None else (X[0] % 8) * 200), y + (X[0] // 8) * 160)
    def con(a, ao, b, bi):
        ok = MEL.connect_material_expressions(a, ao, b, bi)
        if not ok:
            log("connect failed", a.get_name(), ao, "->", b.get_name(), bi)
    def scalar(name, val):
        n = node(unreal.MaterialExpressionScalarParameter)
        n.set_editor_property("parameter_name", name); n.set_editor_property("default_value", val)
        return n
    def vec(name, col):
        n = node(unreal.MaterialExpressionVectorParameter)
        n.set_editor_property("parameter_name", name); n.set_editor_property("default_value", col)
        return n
    def const3(x, y, z):
        n = node(unreal.MaterialExpressionConstant3Vector)
        n.set_editor_property("constant", unreal.LinearColor(x, y, z, 0))
        return n
    def mask(src, r=False, g=False, b=False, a=False, out=""):
        n = node(unreal.MaterialExpressionComponentMask)
        n.set_editor_property("r", r); n.set_editor_property("g", g); n.set_editor_property("b", b); n.set_editor_property("a", a)
        con(src, out, n, "")
        return n
    def binop(cls, a, b, ao="", bo=""):
        n = node(cls)
        con(a, ao, n, "A"); con(b, bo, n, "B")
        return n
    def mul(a, b, ao="", bo=""): return binop(unreal.MaterialExpressionMultiply, a, b, ao, bo)
    def add(a, b, ao="", bo=""): return binop(unreal.MaterialExpressionAdd, a, b, ao, bo)
    def lerp(a, b, alpha):
        n = node(unreal.MaterialExpressionLinearInterpolate)
        con(a, "", n, "A"); con(b, "", n, "B"); con(alpha, "", n, "Alpha")
        return n
    def unary(cls, a):
        n = node(cls); con(a, "", n, ""); return n
    def const(v):
        n = node(unreal.MaterialExpressionConstant); n.set_editor_property("r", v); return n
    def tex_param(name, sampler, default):
        n = node(unreal.MaterialExpressionTextureSampleParameter2D)
        n.set_editor_property("parameter_name", name)
        n.set_editor_property("sampler_type", sampler)
        n.set_editor_property("texture", unreal.load_asset(default))
        return n

    # --- projection weights & UVs ---
    wp = node(unreal.MaterialExpressionWorldPosition)
    vn = node(unreal.MaterialExpressionVertexNormalWS)
    tile = scalar("TileSize", 200.0)
    wps = binop(unreal.MaterialExpressionDivide, wp, tile)
    flip = node(unreal.MaterialExpressionConstant2Vector)
    flip.set_editor_property("r", 1.0); flip.set_editor_property("g", -1.0)
    uvx = mul(mask(wps, g=True, b=True), flip)   # faces facing X: (y, -z)
    uvy = mul(mask(wps, r=True, b=True), flip)   # faces facing Y: (x, -z)
    uvz = mask(wps, r=True, g=True)              # floors/ceilings: (x, y)
    w = unary(unreal.MaterialExpressionRound, unary(unreal.MaterialExpressionAbs, vn))
    wx, wy, wz = mask(w, r=True), mask(w, g=True), mask(w, b=True)
    uv = add(add(mul(uvx, wx), mul(uvy, wy)), mul(uvz, wz))

    base = tex_param("BaseColorTex", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, "/Engine/EngineResources/DefaultTexture")
    rough = tex_param("RoughnessTex", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, "/Engine/EngineResources/WhiteSquareTexture")
    ao = tex_param("AOTex", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, "/Engine/EngineResources/WhiteSquareTexture")
    nrm = tex_param("NormalTex", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, "/Engine/EngineMaterials/DefaultNormal")
    for t in (base, rough, ao, nrm):
        con(uv, "", t, "UVs")

    # --- grime band at the base of walls (each storey is 3.5 m) + large-scale variation ---
    z = mask(wp, b=True)
    band = unary(unreal.MaterialExpressionFrac, binop(unreal.MaterialExpressionDivide, z, const(350.0)))
    grime = unary(unreal.MaterialExpressionSaturate, unary(unreal.MaterialExpressionOneMinus, mul(band, const(5.0))))
    grime = mul(mul(grime, unary(unreal.MaterialExpressionOneMinus, wz)), scalar("GrimeStrength", 0.5))
    noise = node(unreal.MaterialExpressionNoise)
    noise.set_editor_property("scale", 0.0025); noise.set_editor_property("levels", 3)
    noise.set_editor_property("output_min", 0.0); noise.set_editor_property("output_max", 1.0)
    macro = lerp(const(1.0), const(0.82), mul(noise, scalar("MacroVariation", 0.6)))

    tint = mask(vec("Tint", unreal.LinearColor(1, 1, 1, 1)), r=True, g=True, b=True)
    col = mul(mul(mask(base, r=True, g=True, b=True, out="RGB"), tint), macro)
    col = mul(col, lerp(const(1.0), const(0.55), grime))
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)

    rr = add(mul(mask(rough, r=True, out="R"), scalar("RoughnessMult", 1.0)), mul(grime, const(0.12)))
    MEL.connect_material_property(unary(unreal.MaterialExpressionSaturate, rr), "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(scalar("Metallic", 0.0), "", unreal.MaterialProperty.MP_METALLIC)
    aoo = lerp(const(1.0), mask(ao, r=True, out="R"), scalar("AOStrength", 1.0))
    MEL.connect_material_property(aoo, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)

    # --- world-space normal from tangent-space map using the projection basis ---
    nstr = scalar("NormalStrength", 1.0)
    nr, ng, nb = mask(nrm, r=True, out="R"), mask(nrm, g=True, out="G"), mask(nrm, b=True, out="B")
    nr = mul(nr, nstr); ng = mul(ng, nstr)
    tan = add(add(mul(const3(0, 1, 0), wx), mul(const3(1, 0, 0), wy)), mul(const3(1, 0, 0), wz))
    bit = add(add(mul(const3(0, 0, -1), wx), mul(const3(0, 0, -1), wy)), mul(const3(0, -1, 0), wz))
    wn = add(add(mul(tan, nr), mul(bit, ng)), mul(vn, nb))
    MEL.connect_material_property(unary(unreal.MaterialExpressionNormalize, wn), "", unreal.MaterialProperty.MP_NORMAL)

    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("master material built")
    return mat


def make_instance(master, name, texset, p):
    path = f"{DST}/Materials/MI_{name}"
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mi = AT.create_asset(f"MI_{name}", f"{DST}/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, master)
    for chan, pname in (("BaseColor", "BaseColorTex"), ("Normal", "NormalTex"), ("Roughness", "RoughnessTex"), ("AO", "AOTex")):
        if chan in texset:
            MEL.set_material_instance_texture_parameter_value(mi, pname, texset[chan])
    t = p.get("tint", [1, 1, 1])
    MEL.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(t[0], t[1], t[2], 1))
    for key, pname in (("tile", "TileSize"), ("rough", "RoughnessMult"), ("metal", "Metallic"), ("grime", "GrimeStrength"),
                       ("normal", "NormalStrength"), ("macro", "MacroVariation"), ("ao", "AOStrength")):
        if key in p:
            MEL.set_material_instance_scalar_parameter_value(mi, pname, float(p[key]))
    EAL.save_loaded_asset(mi)
    log("instance", name)


def main():
    with open(os.path.join(SRC, "fetched.json"), encoding="utf-8") as f:
        fetched = json.load(f)
    with open(os.path.join(os.path.dirname(__file__), "photoreal_manifest.json"), encoding="utf-8") as f:
        man = json.load(f)

    # textures
    texsets = {}
    for surf, entry in fetched["textures"].items():
        tid = entry["id"]
        if tid in texsets:
            continue
        ts = {}
        for chan in ("BaseColor", "Normal", "Roughness", "AO"):
            if chan in entry and os.path.exists(entry[chan]):
                tex = import_file(entry[chan], f"{DST}/Textures/{tid}", f"T_{tid}_{chan}")
                if tex:
                    setup_texture(tex, chan)
                    ts[chan] = tex
        texsets[tid] = ts
        log("textures", tid, list(ts.keys()))

    master = build_master()
    for surf, p in man["surfaces"].items():
        tid = man["textures"].get(p.get("tex", surf))
        if not tid or tid not in texsets:
            log("skip surface (no texture)", surf)
            continue
        make_instance(master, surf, texsets[tid], p)

    # HDRI -> TextureCube for the sky light
    if fetched.get("hdri") and os.path.exists(fetched["hdri"]):
        cube = import_file(fetched["hdri"], f"{DST}/HDRI", "HDRI_Sky")
        if cube:
            EAL.save_loaded_asset(cube)
            log("hdri imported", type(cube).__name__)

    # props
    for key, m in fetched.get("models", {}).items():
        if not os.path.exists(m["fbx"]):
            continue
        t = unreal.AssetImportTask()
        t.filename = m["fbx"]
        t.destination_path = f"{DST}/Props/{key}"
        t.automated = True
        t.replace_existing = True
        t.save = True
        opts = unreal.FbxImportUI()
        opts.import_mesh = True
        opts.import_materials = True
        opts.import_textures = True
        opts.import_as_skeletal = False
        opts.static_mesh_import_data.combine_meshes = True
        opts.static_mesh_import_data.generate_lightmap_u_vs = False
        opts.static_mesh_import_data.auto_generate_collision = True
        t.options = opts
        AT.import_asset_tasks([t])
        paths = list(t.imported_object_paths)
        meshes = [pp for pp in paths if isinstance(unreal.load_asset(pp), unreal.StaticMesh)]
        if meshes:
            target = f"{DST}/Props/SM_{key}"
            if EAL.does_asset_exist(target):
                EAL.delete_asset(target)
            src = meshes[0].split(".")[0]
            if EAL.rename_asset(src, target):
                sm = unreal.load_asset(target)
                sm.set_editor_property("light_map_resolution", 64)
                EAL.save_loaded_asset(sm)
        log("prop", key, paths)

    EAL.save_directory(DST, only_if_is_dirty=False, recursive=True)
    log("ALL DONE")


try:
    main()
except Exception:
    log("ERROR\n" + traceback.format_exc())
