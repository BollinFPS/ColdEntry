"""Blender background preprocessing of the CC0 forest models and terrain.
blender --background --factory-startup --python Tools/prepare_forest.py
"""
import bpy, bmesh, json, math, numpy as np
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "SourceArt/Forest/Prepared"
OUT.mkdir(parents=True, exist_ok=True)
manifest = json.loads((ROOT / "SourceArt/Forest/manifest.json").read_text())
prepared = []

def clear():
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    for mesh in list(bpy.data.meshes):
        if mesh.users == 0:
            bpy.data.meshes.remove(mesh)

def export_mesh(obj, name, asset, max_faces=100000, ground=True):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    obj.location = (0, 0, 0)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    # Geometry-node trees store UVMap as a 3D corner attribute. FBX silently
    # drops it unless converted to an actual two-component UV layer first.
    if not obj.data.uv_layers:
        attr=obj.data.attributes.get('UVMap')
        if attr and attr.domain=='CORNER' and attr.data_type=='FLOAT_VECTOR':
            uv=np.empty(len(attr.data)*3,dtype=np.float32)
            attr.data.foreach_get('vector',uv)
            obj.data.attributes.remove(attr)
            layer=obj.data.uv_layers.new(name='UVMap')
            layer.data.foreach_set('uv',uv.reshape(-1,3)[:,:2].ravel())
    if asset!='generated' and not obj.data.uv_layers:
        raise RuntimeError('No exportable texture UVs on '+obj.name)
    if ground:
        low = min(v.co.z for v in obj.data.vertices)
        for v in obj.data.vertices:
            v.co.z -= low
    if len(obj.data.polygons) > max_faces:
        # Preserve the material boundaries between bark, trunk scans, and foliage.
        dec = obj.modifiers.new('GameGeometry', 'DECIMATE')
        dec.ratio = max_faces / len(obj.data.polygons)
        dec.delimit = {'MATERIAL'}
        bpy.ops.object.modifier_apply(modifier=dec.name)
    obj.name = name
    path = OUT / (name + '.fbx')
    bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True, object_types={'MESH'},
        apply_unit_scale=True, axis_forward='-Y', axis_up='Z', mesh_smooth_type='FACE',
        use_mesh_modifiers=True, add_leaf_bones=False, bake_anim=False, path_mode='STRIP', use_custom_props=False)
    prepared.append({'name': name, 'asset': asset, 'fbx': path.relative_to(ROOT).as_posix(),
                     'faces': len(obj.data.polygons), 'dimensions_m': list(obj.dimensions),
                     'materials': [s.name for s in obj.material_slots], 'uv_channels':len(obj.data.uv_layers)})
    print('[CE-Forest] prepared', name, len(obj.data.polygons), list(obj.dimensions), flush=True)

for asset, source in manifest['models'].items():
    clear()
    selections={
        'fir_tree_01':[f'fir_tree_01_{c}_LOD1' for c in 'abc'],
        'pine_sapling_small':[f'pine_sapling_small_{c}' for c in 'abc'],
        'fern_02':[f'fern_02_{c}' for c in 'abc'],
        'grass_medium_01':[f'grass_medium_01_large_{c}_LOD1' for c in 'abc'],
        'boulder_01':[f'boulder_01_LOD{i}' for i in range(3)],
        'dead_tree_trunk':['dead_tree_trunk']}
    with bpy.data.libraries.load(str(ROOT/source['blend']),link=False) as (src,dst):
        dst.objects=selections[asset]
    for obj in dst.objects:
        if obj is None:raise RuntimeError('Source mesh missing for '+asset)
        bpy.context.scene.collection.objects.link(obj)
        obj.hide_set(False);obj.hide_viewport=False
    meshes = sorted([o for o in bpy.context.scene.objects if o.type == 'MESH'], key=lambda o: o.name)
    if asset == 'fir_tree_01':
        for index, obj in enumerate(meshes):
            # Artist-authored LOD1 keeps complete branch cards; blanket decimation
            # was destroying thin needles and making the forest look bare.
            export_mesh(obj, f'SM_Fir_{index + 1}', asset, 200000)
    else:
        # Each source can contain several variants; retain up to three useful silhouettes.
        for index, obj in enumerate(meshes[:3]):
            name = {'pine_sapling_small':'Sapling', 'fern_02':'Fern', 'grass_medium_01':'Grass',
                    'boulder_01':'Boulder', 'dead_tree_trunk':'Deadwood'}[asset]
            export_mesh(obj, f'SM_{name}_{index + 1}', asset, 150000 if name=='Sapling' else 45000)

clear()
# The center stays level for the existing tactical layout. Soft earth banks and hills rise
# beyond the compound, using the identical function sampled in CEMapEnvironment.cpp.
def terrain_z(x, y):
    dx, dy = max(abs(x - 20) - 53, 0), max(abs(y - 14) - 45, 0)
    distance = math.sqrt(dx * dx + dy * dy)
    fade = min(distance / 28, 1)
    fade = fade * fade * (3 - 2 * fade)
    return -0.12 + fade * (2.8 + 2.1 * math.sin(x * .031 + .7) * math.cos(y * .027)
                           + 1.1 * math.sin(x * .077 + y * .041))

verts, faces = [], []
n, spacing = 121, 4.0
for iy in range(n):
    for ix in range(n):
        x, y = 20 + (ix - (n - 1) / 2) * spacing, 14 + (iy - (n - 1) / 2) * spacing
        verts.append((x, y, terrain_z(x, y)))
for iy in range(n - 1):
    for ix in range(n - 1):
        a = iy * n + ix
        faces.append((a, a + 1, a + n + 1, a + n))
mesh = bpy.data.meshes.new('ForestTerrain')
mesh.from_pydata(verts, [], faces)
mesh.update()
obj = bpy.data.objects.new('ForestTerrain', mesh)
bpy.context.collection.objects.link(obj)
for polygon in mesh.polygons:
    polygon.use_smooth = True
export_mesh(obj, 'SM_ForestTerrain', 'generated', 100000, ground=False)

# A reusable four-metre chain-link panel with true wire geometry.
clear()
verts, faces = [], []
def wire(a, b, radius=.0045):
    a, b = Vector(a), Vector(b)
    axis = (b - a).normalized()
    side = axis.cross(Vector((0,1,0))).normalized()
    up = axis.cross(side).normalized()
    start = len(verts)
    for point in (a, b):
        for k in range(4):
            verts.append(tuple(point + radius * (side * math.cos(k*math.pi/2) + up * math.sin(k*math.pi/2))))
    for k in range(4):
        faces.append((start+k, start+(k+1)%4, start+4+(k+1)%4, start+4+k))
for direction in (-1, 1):
    for i in range(-40, 70):
        offset = i * .12
        points = []
        for x in (0,4):
            z = direction * x + offset
            if 0 <= z <= 2.4:
                points.append((x,0,z))
        for z in (0,2.4):
            x = (z-offset) / direction
            if 0 <= x <= 4:
                points.append((x,0,z))
        if len(points) >= 2 and (Vector(points[0])-Vector(points[1])).length > .001:
            wire(points[0],points[1])
mesh = bpy.data.meshes.new('SecurityFence')
mesh.from_pydata(verts,[],faces)
mesh.update()
obj = bpy.data.objects.new('SecurityFence',mesh)
bpy.context.collection.objects.link(obj)
export_mesh(obj,'SM_SecurityFence','generated',100000)
(ROOT/'SourceArt/Forest/prepared.json').write_text(json.dumps(prepared,indent=2))
print('[CE-Forest] preparation complete', flush=True)
