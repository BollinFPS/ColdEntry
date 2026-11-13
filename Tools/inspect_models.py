"""
COLD ENTRY - inspects the imported template character/weapon assets and exports weapon geometry.
Run inside Unreal:  UnrealEditor-Cmd.exe ColdEntry.uproject -run=pythonscript -script="<abs>/Tools/inspect_models.py"
Writes Saved/Inspect/model_inspect.json and Saved/Inspect/<mesh>.obj (or .fbx) so the first-person
weapon placement can be measured instead of guessed.
"""
import json, os, traceback
import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(ROOT, "Saved", "Inspect")
os.makedirs(OUT, exist_ok=True)
report = {"assets": {}, "errors": []}


def vec(v):
    return [round(v.x, 3), round(v.y, 3), round(v.z, 3)]


def bounds(b):
    return {"origin": vec(b.origin), "extent": vec(b.box_extent), "radius": round(b.sphere_radius, 3)}


def export(obj, name):
    for ext, ascii_fbx in (("obj", False), ("fbx", True)):
        path = os.path.join(OUT, f"{name}.{ext}")
        t = unreal.AssetExportTask()
        t.object = obj
        t.filename = path
        t.automated = True
        t.prompt = False
        t.replace_identical = True
        if ext == "fbx":
            o = unreal.FbxExportOption()
            o.ascii = True
            t.options = o
        try:
            ok = unreal.Exporter.run_asset_export_task(t)
        except Exception as e:
            ok = False
            report["errors"].append(f"export {name}.{ext}: {e}")
        if ok and os.path.exists(path):
            return path
    return None


def inspect(path):
    entry = {"path": path}
    try:
        a = unreal.load_asset(path)
        if not a:
            entry["loaded"] = False
            return entry
        entry["loaded"] = True
        entry["class"] = a.get_class().get_name()
        if isinstance(a, unreal.StaticMesh):
            entry["bounds"] = bounds(a.get_bounds())
            entry["materials"] = [str(m.material_slot_name) for m in a.get_editor_property("static_materials")]
            entry["export"] = export(a, a.get_name())
        elif isinstance(a, unreal.SkeletalMesh):
            entry["bounds"] = bounds(a.get_bounds())
            entry["materials"] = [str(m.material_slot_name) + ":" + (m.material_interface.get_path_name() if m.material_interface else "None")
                                  for m in a.get_editor_property("materials")]
            try:
                socks = []
                for i in range(a.num_sockets()):
                    so = a.get_socket_by_index(i)
                    socks.append({"name": str(so.socket_name), "bone": str(so.bone_name),
                                  "loc": vec(so.relative_location),
                                  "rot": [round(so.relative_rotation.roll, 2), round(so.relative_rotation.pitch, 2), round(so.relative_rotation.yaw, 2)]})
                entry["sockets"] = socks
            except Exception as e:
                entry["sockets_err"] = str(e)
            try:
                entry["physics_asset"] = a.get_editor_property("physics_asset").get_path_name()
            except Exception:
                pass
        elif isinstance(a, unreal.MaterialInstanceConstant) or isinstance(a, unreal.Material):
            MEL = unreal.MaterialEditingLibrary
            src = a if isinstance(a, unreal.Material) else a
            try:
                entry["vector_params"] = [str(n) for n in MEL.get_vector_parameter_names(src)]
                entry["scalar_params"] = [str(n) for n in MEL.get_scalar_parameter_names(src)]
                entry["texture_params"] = [str(n) for n in MEL.get_texture_parameter_names(src)]
            except Exception as e:
                entry["param_err"] = str(e)
        elif isinstance(a, unreal.AnimBlueprint):
            gc = unreal.load_object(None, path + "." + a.get_name() + "_C")
            entry["generated_class"] = gc.get_path_name() if gc else None
    except Exception:
        entry["error"] = traceback.format_exc()
    return entry


ASSETS = [
    "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple",
    "/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple",
    "/Game/Characters/Mannequins/Materials/M_Mannequin",
    "/Game/Characters/Mannequins/Materials/Manny/MI_Manny_01_New",
    "/Game/Characters/Mannequins/Materials/Manny/MI_Manny_02_New",
    "/Game/Characters/Mannequins/Rigs/PA_Mannequin",
    "/Game/Variant_Shooter/Anims/ABP_TP_Rifle",
    "/Game/Variant_Shooter/Anims/ABP_TP_Pistol",
    "/Game/Weapons/Rifle/Meshes/SM_Rifle",
    "/Game/Weapons/Rifle/Meshes/SKM_Rifle",
    "/Game/Weapons/Pistol/Meshes/SM_Pistol",
    "/Game/Weapons/Pistol/Meshes/SKM_Pistol",
]
for p in ASSETS:
    report["assets"][p] = inspect(p)

with open(os.path.join(OUT, "model_inspect.json"), "w") as f:
    json.dump(report, f, indent=2)
print("[CE-Inspect] wrote", os.path.join(OUT, "model_inspect.json"))
