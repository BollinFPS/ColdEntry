"""
COLD ENTRY - imports the SWAT operator (Mixamo skeleton) and its animation clips into /Game/Characters/Swat.
Source files: SourceArt/Characters/Swat/character.fbx, SourceArt/Characters/Swat/animations/*.fbx (-> Anims)
and SourceArt/Characters/Swat/shooter_pack/*.fbx (Mixamo Basic Shooter Pack -> Pack)
Run inside Unreal:
  UnrealEditor-Cmd.exe ColdEntry.uproject -run=pythonscript -script="<abs>/Tools/ue_import_swat.py" -unattended -nosplash
Writes Saved/Inspect/swat.json (bones, reference pose, clip lengths and hip travel) for wiring up the rig.
Idempotent: re-running replaces the assets.
"""
import json, os, re, traceback
import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SRC = os.path.join(ROOT, "SourceArt", "Characters", "Swat")
DST = "/Game/Characters/Swat"
OUT = os.path.join(ROOT, "Saved", "Inspect")
AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
report = {"mesh": None, "skeleton": None, "physics": None, "materials": [], "bones": [], "ref": {}, "anims": {}, "errors": []}


def log(*a):
    unreal.log("[CE-Swat] " + " ".join(str(x) for x in a))


def err(*a):
    report["errors"].append(" ".join(str(x) for x in a))
    log("ERROR", *a)


def vec(v):
    return [round(v.x, 2), round(v.y, 2), round(v.z, 2)]


def import_mesh():
    t = unreal.AssetImportTask()
    t.filename = os.path.join(SRC, "character.fbx")
    t.destination_path = DST
    t.destination_name = "SK_Swat"
    t.automated = True
    t.replace_existing = True
    t.save = True
    o = unreal.FbxImportUI()
    o.automated_import_should_detect_type = False
    o.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    o.import_mesh = True
    o.import_as_skeletal = True
    o.import_animations = False
    o.import_materials = True
    o.import_textures = True
    o.create_physics_asset = True
    t.options = o
    AT.import_asset_tasks([t])
    paths = list(t.imported_object_paths)
    log("mesh import ->", paths)
    mesh = skel = phys = None
    for p in paths:
        a = unreal.load_asset(p)
        if isinstance(a, unreal.SkeletalMesh):
            mesh = a
        elif isinstance(a, unreal.Skeleton):
            skel = a
        elif isinstance(a, unreal.PhysicsAsset):
            phys = a
    if mesh and not skel:
        skel = mesh.get_editor_property("skeleton")
    if mesh and not phys:
        phys = mesh.get_editor_property("physics_asset")
    return mesh, skel, phys


def import_anim(path, skel, folder):
    name = "A_" + re.sub(r"[^A-Za-z0-9]+", "_", os.path.splitext(os.path.basename(path))[0]).strip("_")
    t = unreal.AssetImportTask()
    t.filename = path
    t.destination_path = DST + "/" + folder
    t.destination_name = name
    t.automated = True
    t.replace_existing = True
    t.save = True
    o = unreal.FbxImportUI()
    o.automated_import_should_detect_type = False
    o.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
    o.import_mesh = False
    o.import_as_skeletal = False
    o.import_animations = True
    o.import_materials = False
    o.import_textures = False
    o.skeleton = skel
    t.options = o
    AT.import_asset_tasks([t])
    seq = None
    for p in list(t.imported_object_paths):
        a = unreal.load_asset(p)
        if isinstance(a, unreal.AnimSequence):
            seq = a
    if not seq:
        seq = unreal.load_asset(f"{DST}/{folder}/{name}")
    return name, seq if isinstance(seq, unreal.AnimSequence) else None


def build_material():
    """Tintable uniform material: colour x team tint, normal map, roughness lowered where the specular map is bright."""
    MEL = unreal.MaterialEditingLibrary
    path = f"{DST}/M_CE_Swat"
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mat = AT.create_asset("M_CE_Swat", DST, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("used_with_skeletal_mesh", True)

    def node(cls, x, y):
        return MEL.create_material_expression(mat, cls, x, y)

    def tex(name, sampler, default, y):
        n = node(unreal.MaterialExpressionTextureSampleParameter2D, -900, y)
        n.set_editor_property("parameter_name", name)
        n.set_editor_property("sampler_type", sampler)
        n.set_editor_property("texture", unreal.load_asset(default))
        return n

    def scalar(name, val, y):
        n = node(unreal.MaterialExpressionScalarParameter, -900, y)
        n.set_editor_property("parameter_name", name); n.set_editor_property("default_value", val)
        return n

    def binop(cls, a, ao, b, bo, x, y):
        n = node(cls, x, y)
        MEL.connect_material_expressions(a, ao, n, "A"); MEL.connect_material_expressions(b, bo, n, "B")
        return n

    base = tex("BaseColorTex", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, "/Engine/EngineResources/DefaultTexture", -500)
    nrm = tex("NormalTex", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, "/Engine/EngineMaterials/DefaultNormal", -200)
    spec = tex("SpecTex", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, "/Engine/EngineResources/Black", 100)
    tint = node(unreal.MaterialExpressionVectorParameter, -900, -750)
    tint.set_editor_property("parameter_name", "Tint"); tint.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    MEL.connect_material_property(binop(unreal.MaterialExpressionMultiply, base, "RGB", tint, "", -450, -600), "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(nrm, "RGB", unreal.MaterialProperty.MP_NORMAL)
    gloss = binop(unreal.MaterialExpressionMultiply, spec, "R", scalar("SpecGloss", 0.4, 300), "", -450, 150)
    rough = binop(unreal.MaterialExpressionSubtract, scalar("Roughness", 0.85, 450), "", gloss, "", -250, 300)
    sat = node(unreal.MaterialExpressionSaturate, -100, 300)
    MEL.connect_material_expressions(rough, "", sat, "")
    MEL.connect_material_property(sat, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("material built")


def main():
    os.makedirs(OUT, exist_ok=True)
    mesh, skel, phys = import_mesh()
    if not mesh or not skel:
        err("skeletal mesh import failed")
        return
    for tname in ("Soldier_Body_normal", "Soldier_head_normal"):
        tex = unreal.load_asset(f"{DST}/{tname}")
        if tex:
            tex.set_editor_property("srgb", False)
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            EAL.save_loaded_asset(tex)
        else:
            err("normal map not found", tname)
    try:
        build_material()
    except Exception:
        err("material", traceback.format_exc())
    report["mesh"] = mesh.get_path_name()
    report["skeleton"] = skel.get_path_name()
    report["physics"] = phys.get_path_name() if phys else None
    try:
        for m in mesh.get_editor_property("materials"):
            mi = m.get_editor_property("material_interface")
            report["materials"].append({"slot": str(m.get_editor_property("material_slot_name")), "material": mi.get_path_name() if mi else None})
    except Exception:
        err("materials", traceback.format_exc())
    try:
        b = mesh.get_bounds()
        report["bounds"] = {"origin": vec(b.origin), "extent": vec(b.box_extent)}
    except Exception:
        err("bounds", traceback.format_exc())

    APE = unreal.AnimPoseExtensions
    key = ["Hips", "Spine", "Spine1", "Spine2", "Neck", "Head", "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand", "RightShoulder", "RightArm",
           "RightForeArm", "RightHand", "RightHandMiddle1", "RightHandIndex1", "RightHandThumb1", "LeftUpLeg", "LeftLeg", "LeftFoot", "LeftToeBase",
           "RightUpLeg", "RightLeg", "RightFoot", "RightToeBase"]
    names = []
    try:
        ref = APE.get_reference_pose(skel)
        names = [str(n) for n in APE.get_bone_names(ref)]
        report["bones"] = names
        for n in names:
            short = n.split(":")[-1]
            if short in key:
                tw = APE.get_bone_pose(ref, n, unreal.AnimPoseSpaces.WORLD)
                r = tw.rotation.rotator()
                report["ref"][n] = {"loc": vec(tw.translation), "rot_rpy": [round(r.roll, 1), round(r.pitch, 1), round(r.yaw, 1)]}
    except Exception:
        err("reference pose", traceback.format_exc())
    hips = next((n for n in names if n.split(":")[-1] == "Hips"), None)
    rhand = next((n for n in names if n.split(":")[-1] == "RightHand"), None)
    lhand = next((n for n in names if n.split(":")[-1] == "LeftHand"), None)

    def bone(short):
        return next((n for n in names if n.split(":")[-1] == short), None)

    lfoot, rfoot, head = bone("LeftFoot"), bone("RightFoot"), bone("Head")
    track = [b for b in (hips, rhand, lhand, lfoot, rfoot, head) if b]
    for sub_dir, folder in (("animations", "Anims"), ("shooter_pack", "Pack")):
        adir = os.path.join(SRC, sub_dir)
        if not os.path.isdir(adir):
            continue
        for fn in sorted(os.listdir(adir)):
            if not fn.lower().endswith(".fbx"):
                continue
            try:
                name, seq = import_anim(os.path.join(adir, fn), skel, folder)
                if not seq:
                    err("anim import failed", fn)
                    continue
                length = unreal.AnimationLibrary.get_sequence_length(seq)
                entry = {"asset": seq.get_path_name(), "length": round(length, 3), "source": fn}
                if hips:
                    opts = unreal.AnimPoseEvaluationOptions()
                    W = unreal.AnimPoseSpaces.WORLD

                    def at(t):
                        pose = APE.get_anim_pose_at_time(seq, min(max(0.0, t), length - 0.001), opts)
                        return {b.split(":")[-1]: APE.get_bone_pose(pose, b, W).translation for b in track}

                    p0, p1 = at(0.0), at(length)
                    h0, h1 = p0["Hips"], p1["Hips"]
                    entry["travel"] = [round(h1.x - h0.x, 1), round(h1.y - h0.y, 1)]      # +x = character's left, +y = forward
                    # how well the last frame matches the first once the travel is taken out (cm, worst tracked bone)
                    gap = 0.0
                    for k in p0:
                        d = unreal.Vector(p1[k].x - (h1.x - h0.x) - p0[k].x, p1[k].y - (h1.y - h0.y) - p0[k].y, p1[k].z - p0[k].z)
                        gap = max(gap, d.length())
                    entry["loop_gap"] = round(gap, 1)
                    rows = []
                    for f in (0.0, 0.25, 0.5, 0.75):
                        p = at(length * f)
                        h = p["Hips"]
                        row = {"t": round(length * f, 2), "hips_z": round(h.z, 1)}
                        for k in ("RightHand", "LeftHand", "LeftFoot", "RightFoot", "Head"):
                            if k in p:
                                row[k] = [round(p[k].x - h.x, 1), round(p[k].y - h.y, 1), round(p[k].z, 1)]   # x/y relative to hips, z absolute
                        rows.append(row)
                    entry["samples"] = rows
                report["anims"][folder + "/" + name] = entry
                log("anim", folder, name, entry["length"], entry.get("travel"), "gap", entry.get("loop_gap"))
            except Exception:
                err("anim", fn, traceback.format_exc())
    EAL.save_directory(DST, only_if_is_dirty=False, recursive=True)


try:
    main()
except Exception:
    err("fatal", traceback.format_exc())
with open(os.path.join(OUT, "swat.json"), "w") as f:
    json.dump(report, f, indent=1)
log("ALL DONE", len(report["anims"]), "anims,", len(report["errors"]), "errors")
