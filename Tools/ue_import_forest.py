"""Import the prepared CC0 forest and build the reference-driven world materials.
Run using UnrealEditor-Cmd -run=pythonscript -script=<this file>.
"""
import json, math, traceback
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parents[1]
DST = '/Game/Forest'
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
manifest = json.loads((ROOT/'SourceArt/Forest/manifest.json').read_text())
prepared = json.loads((ROOT/'SourceArt/Forest/prepared.json').read_text())

def log(*parts):
    print('[CE-Forest]', *parts, flush=True)

def material(name, folder=DST+'/Materials'):
    path = folder+'/'+name
    mat = unreal.load_asset(path)
    if mat:
        MEL.delete_all_material_expressions(mat)
    else:
        mat = AT.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property('used_with_instanced_static_meshes', True)
    mat.set_editor_property('used_with_nanite', True)
    return mat

def import_file(path, folder, name, mesh=False):
    task = unreal.AssetImportTask()
    task.filename = str(path)
    task.destination_path = folder
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    if mesh:
        options = unreal.FbxImportUI()
        options.import_mesh = True
        options.import_materials = False
        options.import_textures = False
        options.import_as_skeletal = False
        options.static_mesh_import_data.combine_meshes = True
        options.static_mesh_import_data.generate_lightmap_u_vs = False
        options.static_mesh_import_data.auto_generate_collision = False
        task.options = options
    AT.import_asset_tasks([task])
    for path in task.imported_object_paths:
        obj = unreal.load_asset(path)
        if isinstance(obj, unreal.StaticMesh if mesh else unreal.Texture):
            return obj
    raise RuntimeError('Failed to import '+str(path))

def texture(path, name, channel):
    tex = unreal.load_asset(DST+'/Textures/'+name) if EAL.does_asset_exist(DST+'/Textures/'+name) else import_file(ROOT/path, DST+'/Textures', name)
    tex.set_editor_property('srgb', channel=='BaseColor')
    tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP if channel=='Normal'
                            else unreal.TextureCompressionSettings.TC_DEFAULT if channel=='BaseColor'
                            else unreal.TextureCompressionSettings.TC_MASKS)
    if channel=='Normal':
        tex.set_editor_property('flip_green_channel', False) # downloaded DirectX maps
    tex.set_editor_property('max_texture_size', 2048 if channel!='BaseColor' else 4096)
    EAL.save_loaded_asset(tex)
    return tex

class Graph:
    def __init__(self, mat):
        self.mat, self.count = mat, 0
    def node(self, cls, **props):
        self.count += 1
        n = MEL.create_material_expression(self.mat, getattr(unreal,'MaterialExpression'+cls),
                                           -1800+(self.count%8)*190, (self.count//8)*180)
        for key,val in props.items():
            n.set_editor_property(key,val)
        return n
    def con(self, a, b, port='', out=''):
        if not MEL.connect_material_expressions(a,out,b,port):
            raise RuntimeError('Connection failed: '+a.get_name()+' -> '+b.get_name()+':'+port)
    def c(self, x): return self.node('Constant',r=float(x))
    def vec(self, r,g,b): return self.node('Constant3Vector',constant=unreal.LinearColor(r,g,b,1))
    def op(self, kind,a,b):
        n=self.node(kind);self.con(a,n,'A');self.con(b,n,'B');return n
    def mul(self,a,b): return self.op('Multiply',a,b)
    def add(self,a,b): return self.op('Add',a,b)
    def sub(self,a,b): return self.op('Subtract',a,b)
    def div(self,a,b): return self.op('Divide',a,b)
    def one(self,a):
        n=self.node('OneMinus');self.con(a,n);return n
    def sat(self,a):
        n=self.node('Saturate');self.con(a,n);return n
    def abs(self,a):
        n=self.node('Abs');self.con(a,n);return n
    def mask(self,a,channels='rgb'):
        n=self.node('ComponentMask',r='r' in channels,g='g' in channels,b='b' in channels,a='a' in channels)
        self.con(a,n);return n
    def lerp(self,a,b,t):
        n=self.node('LinearInterpolate');self.con(a,n,'A');self.con(b,n,'B');self.con(t,n,'Alpha');return n
    def scalar(self,name,default): return self.node('ScalarParameter',parameter_name=name,default_value=float(default))
    def tex(self,tex,uv=None,normal=False):
        sampler = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if normal else (
            unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if tex.get_editor_property('srgb') else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        n=self.node('TextureSample',texture=tex,sampler_type=sampler)
        if uv:self.con(uv,n,'UVs')
        return n
    def out(self,n,prop,out=''):
        if not MEL.connect_material_property(n,out,getattr(unreal.MaterialProperty,'MP_'+prop)):
            raise RuntimeError('Output connection failed: '+prop)
    def noise(self,position,scale=.001,levels=2):
        n=self.node('Noise',scale=scale,levels=levels,output_min=0.0,output_max=1.0)
        self.con(position,n);return n
    def finish(self):
        MEL.recompile_material(self.mat)
        EAL.save_loaded_asset(self.mat)

def import_maps():
    maps={}
    for asset,source in {**manifest['models'],**manifest['surfaces']}.items():
        maps[asset]={}
        for group,channels in source['materials'].items():
            maps[asset][group]={}
            for channel,path in channels.items():
                # ARM already contains AO/roughness; avoid duplicate textures on meshes.
                if asset in manifest['models'] and channel in ('AO','Roughness') and 'ARM' in channels:continue
                maps[asset][group][channel]=texture(path,f'T_{asset}_{group or "main"}_{channel}',channel)
        log('textures',asset)
    return maps

def model_material(asset,group,maps):
    name=f'M_{asset}_{group or "main"}'
    mat=material(name)
    foliage='Alpha' in maps or group=='twig' or asset.startswith(('fern','grass'))
    mat.set_editor_property('two_sided',foliage)
    if foliage:
        mat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED)
        mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
        mat.set_editor_property('opacity_mask_clip_value',0.25)
    g=Graph(mat)
    uv=None
    projections=None
    if asset=='fir_tree_01' and not foliage:
        # Upper trunks use tiled bark; world projection avoids stretched UV
        # seams where the source generator joins trunk and branch meshes.
        wp=g.div(g.node('WorldPosition'),g.c(110));vn=g.abs(g.node('VertexNormalWS'))
        xy=g.mask(wp,'rg');xz=g.mask(wp,'rb');yz=g.mask(wp,'gb')
        weights=g.mul(vn,vn);weights=g.mul(weights,weights);weights=g.mul(weights,weights)
        total=g.op('DotProduct',weights,g.vec(1,1,1))
        weights=g.div(weights,total)
        projections=[(yz,g.mask(weights,'r')),(xz,g.mask(weights,'g')),(xy,g.mask(weights,'b'))]
        mat.set_editor_property('tangent_space_normal',False)
    def sample(texture,normal=False):
        if not projections:return g.tex(texture,uv,normal=normal)
        # Blend sampled colors, not UVs: interpolating the coordinates creates
        # long pinched strips around curved trunks even with valid source UVs.
        samples=[g.tex(texture,coords,normal=normal) for coords,weight in projections]
        if normal:
            signs=g.node('Sign');g.con(g.node('VertexNormalWS'),signs)
            a,b,c=samples
            samples=[g.add(g.add(g.mul(g.vec(1,0,0),g.mul(g.mask(a,'b'),g.mask(signs,'r'))),g.mul(g.vec(0,1,0),g.mask(a,'r'))),g.mul(g.vec(0,0,1),g.mask(a,'g'))),
                g.add(g.add(g.mul(g.vec(1,0,0),g.mask(b,'r')),g.mul(g.vec(0,1,0),g.mul(g.mask(b,'b'),g.mask(signs,'g')))),g.mul(g.vec(0,0,1),g.mask(b,'g'))),
                g.add(g.add(g.mul(g.vec(1,0,0),g.mask(c,'r')),g.mul(g.vec(0,1,0),g.mask(c,'g'))),g.mul(g.vec(0,0,1),g.mul(g.mask(c,'b'),g.mask(signs,'b'))))]
        result=g.add(g.add(g.mul(samples[0],projections[0][1]),g.mul(samples[1],projections[1][1])),g.mul(samples[2],projections[2][1]))
        if normal:
            normalized=g.node('Normalize');g.con(result,normalized);return normalized
        return result
    base=g.mask(sample(maps['BaseColor']))
    # Slightly quieter greens than the source scan; preserve the photographed bark.
    if foliage:base=g.mul(base,g.vec(.65,.76,.60))
    g.out(base,'BASE_COLOR')
    if 'Normal' in maps:g.out(sample(maps['Normal'],normal=True),'NORMAL')
    if 'ARM' in maps:
        arm=sample(maps['ARM'])
        g.out(g.mask(arm,'r'),'AMBIENT_OCCLUSION')
        g.out(g.op('Max',g.mask(arm,'g'),g.c(.6)),'ROUGHNESS')
    else:g.out(g.c(.86),'ROUGHNESS')
    if 'Alpha' in maps:g.out(g.tex(maps['Alpha']),'OPACITY_MASK','R')
    if foliage:
        g.out(g.mul(base,g.c(.22)),'SUBSURFACE_COLOR')
    g.finish()
    return mat

def ground_material(name,forest,mud,asphalt=None,grass=None):
    mat=material(name)
    g=Graph(mat)
    wp=g.node('WorldPosition')
    uv=g.mask(g.div(wp,g.c(180)),'rg')
    patch=g.noise(wp,.0009,2)
    forest_col=g.mask(g.tex(forest['BaseColor'],uv))
    mud_col=g.mask(g.tex(mud['BaseColor'],g.mul(uv,g.c(.69))))
    dirt=g.lerp(forest_col,mud_col,g.sat(g.mul(patch,g.c(.75))))
    if grass:
        grass_col=g.mask(g.tex(grass['BaseColor'],g.mul(uv,g.c(.7))))
        dirt=g.lerp(dirt,grass_col,g.sat(g.mul(g.sub(g.noise(wp,.003,2),g.c(.28)),g.c(2.3))))
    # Damp humus and moss should remain darker than the pale gravel in the scan.
    dirt=g.mul(dirt,g.vec(.48,.53,.42))
    detail=g.tex(forest['Normal'],uv,True)
    if asphalt:
        road_uv=g.mask(g.div(wp,g.c(320)),'rg')
        road=g.mask(g.tex(asphalt['BaseColor'],road_uv))
        # Uneven forest-soil shoulders enter the yard from its edges.
        x=g.div(g.abs(g.sub(g.mask(wp,'r'),g.c(2000))),g.c(5000))
        y=g.div(g.abs(g.sub(g.mask(wp,'g'),g.c(1400))),g.c(4200))
        edge=g.sat(g.div(g.add(g.sub(g.op('Max',x,y),g.c(.55)),g.mul(patch,g.c(.15))),g.c(.34)))
        col=g.lerp(road,dirt,edge)
        road_normal=g.tex(asphalt['Normal'],road_uv,True)
        detail=g.lerp(g.mask(road_normal),g.mask(detail),edge)
        # Wide irregular pools have genuinely flat normals and low roughness;
        # the remaining asphalt stays granular rather than uniformly polished.
        pool_noise=g.add(g.noise(wp,.0018,3),g.mul(g.noise(wp,.025,2),g.c(.08)))
        puddle=g.mul(g.sat(g.mul(g.sub(pool_noise,g.c(.54)),g.c(13))),g.one(edge))
        col=g.mul(col,g.lerp(g.c(.69),g.c(.31),puddle))
        rough=g.lerp(g.c(.7),g.c(.075),puddle)
        normal=g.lerp(detail,g.vec(0,0,1),g.mul(puddle,g.c(.975)))
        g.out(normal,'NORMAL')
    else:
        col=g.mul(dirt,g.lerp(g.c(.8),g.c(1.05),patch))
        rough=g.c(.86)
        g.out(detail,'NORMAL','RGB')
    g.out(col,'BASE_COLOR');g.out(rough,'ROUGHNESS');g.out(g.c(.5),'SPECULAR')
    g.finish()
    return mat

def facade_material(maps):
    # Existing world-projected scans remain responsible for the structural material;
    # refine their scale/roughness instead of destroying the existing shared master.
    tweaks={'Concrete':{'TileSize':180.,'NormalStrength':1.3,'GrimeStrength':.85,'MacroVariation':.65,'AntiTile':.35},
            'Block':{'TileSize':180.,'NormalStrength':1.15,'GrimeStrength':.6,'AntiTile':.25},
            'Floor':{'TileSize':240.,'NormalStrength':1.2,'RoughnessMult':.82,'AntiTile':.4},
            'Container':{'TileSize':240.,'NormalStrength':1.15},
            'ContainerB':{'TileSize':240.,'NormalStrength':1.15}}
    for name,params in tweaks.items():
        mi=unreal.load_asset('/Game/Photoreal/Materials/MI_'+name)
        if mi:
            for key,value in params.items():MEL.set_material_instance_scalar_parameter_value(mi,key,value)
            if name=='Concrete':MEL.set_material_instance_vector_parameter_value(mi,'Tint',unreal.LinearColor(.76,.79,.75,1))
            if name=='Container':MEL.set_material_instance_vector_parameter_value(mi,'Tint',unreal.LinearColor(.52,.48,.32,1))
            if name=='ContainerB':MEL.set_material_instance_vector_parameter_value(mi,'Tint',unreal.LinearColor(.59,.32,.24,1))
            EAL.save_loaded_asset(mi)
    mat=material('M_WallStreaks')
    mat.set_editor_property('material_domain',unreal.MaterialDomain.MD_DEFERRED_DECAL)
    mat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
    g=Graph(mat)
    uv=g.node('TextureCoordinate')
    u,v=g.mask(uv,'r'),g.mask(uv,'g')
    # Long irregular vertical rain marks, with faded borders in decal UV space.
    xyz=g.node('AppendVector');g.con(uv,xyz,'A');g.con(g.c(0),xyz,'B')
    stretched=g.mul(xyz,g.vec(34,1.6,0))
    noise=g.noise(stretched,1.,2)
    stripes=g.sat(g.mul(g.sub(noise,g.c(.37)),g.c(3)))
    edge=g.mul(g.mul(g.sat(g.mul(u,g.c(9))),g.sat(g.mul(g.one(u),g.c(9)))),
               g.mul(g.sat(g.mul(v,g.c(6))),g.sat(g.mul(g.one(v),g.c(3)))))
    g.out(g.vec(.085,.096,.073),'BASE_COLOR')
    g.out(g.mul(g.mul(stripes,edge),g.c(.37)),'OPACITY')
    g.out(g.c(.95),'ROUGHNESS')
    g.finish()
    # Dedicated weathered facade: axis-projected scanned plaster, chipped paint,
    # moisture at the footing and long runoff marks below the parapet.
    mat=material('M_WeatheredFacade')
    mat.set_editor_property('tangent_space_normal',False)
    g=Graph(mat);wp=g.node('WorldPosition');vn=g.node('VertexNormalWS')
    av=g.abs(vn);wx=g.mask(av,'r');wy=g.mask(av,'g');wz=g.mask(av,'b')
    coords=g.div(wp,g.c(330))
    flip=g.node('Constant2Vector',r=1.,g=-1.)
    uv=g.add(g.add(g.mul(g.mul(g.mask(coords,'gb'),flip),wx),g.mul(g.mul(g.mask(coords,'rb'),flip),wy)),g.mul(g.mask(coords,'rg'),wz))
    concrete=maps['concrete_wall_005'][''];paint=maps['peeling_painted_wall']['']
    c0=g.mask(g.tex(concrete['BaseColor'],uv));c1=g.mask(g.tex(paint['BaseColor'],g.mul(uv,g.c(.72))))
    # Retain the scan's cracks and chips while replacing the original pink paint.
    c0=g.op('DotProduct',c0,g.vec(.2126,.7152,.0722))
    c1=g.op('DotProduct',c1,g.vec(.2126,.7152,.0722))
    c0=g.mul(c0,g.vec(1.15,1.18,1.16));c1=g.mul(c1,g.vec(1.55,1.59,1.56))
    wear=g.sat(g.mul(g.sub(g.noise(wp,.005,3),g.c(.31)),g.c(2.8)))
    col=g.lerp(c0,c1,wear)
    height=g.mask(wp,'b')
    foot=g.mul(g.sat(g.one(g.div(height,g.c(125)))),g.sat(g.mul(g.noise(wp,.012,2),g.c(1.6))))
    drip_pos=g.mul(wp,g.vec(.025,.025,.0025))
    drip=g.sat(g.mul(g.sub(g.noise(drip_pos,1.,3),g.c(.4)),g.c(3.8)))
    drip=g.mul(drip,g.sat(g.div(g.sub(height,g.c(450)),g.c(240))))
    col=g.mul(col,g.vec(.91,.94,.9))
    col=g.lerp(col,g.mul(col,g.vec(.3,.36,.28)),g.mul(foot,g.c(.74)))
    col=g.mul(col,g.lerp(g.c(1.),g.c(.42),g.mul(drip,g.c(.5))))
    n=g.tex(concrete['Normal'],uv,True)
    nr=g.mask(n,'r');ng=g.mask(n,'g');nb=g.mask(n,'b')
    tangent=g.add(g.add(g.mul(g.vec(0,1,0),wx),g.mul(g.vec(1,0,0),wy)),g.mul(g.vec(1,0,0),wz))
    bitangent=g.add(g.mul(g.vec(0,0,-1),g.add(wx,wy)),g.mul(g.vec(0,-1,0),wz))
    world_n=g.add(g.add(g.mul(tangent,nr),g.mul(bitangent,ng)),g.mul(vn,nb))
    normalized=g.node('Normalize');g.con(world_n,normalized)
    g.out(col,'BASE_COLOR');g.out(normalized,'NORMAL');g.out(g.c(.88),'ROUGHNESS');g.out(g.c(.25),'SPECULAR');g.finish()

def main():
    maps=import_maps()
    materials={}
    for asset in manifest['models']:
        materials[asset]={group:model_material(asset,group,
            maps[asset]['bark'] if asset=='fir_tree_01' and group!='twig' else channels)
            for group,channels in maps[asset].items()}
    forest=ground_material('M_ForestGround',maps['forest_ground_04'][''],maps['brown_mud_leaves_01'][''],grass=maps['aerial_grass_rock'][''])
    asphalt={channel:unreal.load_asset('/Game/Photoreal/Textures/asphalt_02/T_asphalt_02_'+channel) for channel in ('BaseColor','Normal')}
    if not all(asphalt.values()):raise RuntimeError('Existing asphalt maps missing')
    ground_material('M_WetYard',maps['forest_ground_04'][''],maps['brown_mud_leaves_01'][''],asphalt,grass=maps['aerial_grass_rock'][''])
    ground_material('M_Gravel',maps['gravel_floor'][''],maps['brown_mud_leaves_01'][''])
    facade_material(maps)
    if '-cematerialsonly' in unreal.SystemLibrary.get_command_line().lower():
        log('MATERIAL UPDATE COMPLETE');return
    report=[]
    for entry in prepared:
        sm=import_file(ROOT/entry['fbx'],DST+'/Meshes',entry['name'],True)
        asset=entry['asset']
        if asset=='generated':
            sm.set_material(0,forest if 'Terrain' in entry['name'] else unreal.load_asset('/Game/Photoreal/Materials/MI_Steel'))
        else:
            groups=materials[asset]
            for i,slot in enumerate(sm.get_editor_property('static_materials')):
                label=str(slot.material_slot_name).lower()
                found=sorted([k for k in groups if k and k in label],key=len,reverse=True)
                group=found[0] if found else '' if '' in groups else 'bark' if 'bark' in groups else next(iter(groups))
                sm.set_material(i,groups[group])
        # Nanite handles the opaque high-detail terrain, rocks and bark/foliage trees.
        if not any(k in entry['name'] for k in ('Fern','Grass','Fence')):
            settings=sm.get_editor_property('nanite_settings')
            settings.set_editor_property('enabled',True)
            if 'Fir' in entry['name'] or 'Sapling' in entry['name']:
                try:settings.set_editor_property('preserve_area',True)
                except Exception:pass
            # Editor subsystems are unavailable in the Python commandlet. Property
            # notification performs the same mesh rebuild without a live editor.
            sm.set_editor_property('nanite_settings',settings)
        EAL.save_loaded_asset(sm)
        bounds=sm.get_bounding_box()
        report.append({'mesh':entry['name'],'bounds_min':str(bounds.min),'bounds_max':str(bounds.max),'slots':len(sm.get_editor_property('static_materials'))})
        log('mesh',entry['name'],'bounds',bounds.min,bounds.max)
    # Production branding assets; clean background and logo can be composed independently.
    for filename,name in [('ColdEntry-Logo-Transparent.png','T_ColdEntryLogo'),('ColdEntry-Menu-Background.png','T_ColdEntryMenu')]:
        tex=import_file(ROOT/'SourceArt/Branding'/filename,'/Game/UI/Branding',name)
        tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property('lod_group',unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property('srgb',True)
        EAL.save_loaded_asset(tex)
    EAL.save_directory(DST,only_if_is_dirty=False,recursive=True)
    (ROOT/'Saved').mkdir(exist_ok=True)
    (ROOT/'Saved/forest-import-report.json').write_text(json.dumps(report,indent=2))
    log('IMPORT COMPLETE',len(report),'meshes')

if __name__=='__main__':
    try:main()
    except Exception:
        log('FAILED',traceback.format_exc())
        raise
