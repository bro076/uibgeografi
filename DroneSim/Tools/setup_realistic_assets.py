"""Import the downloaded photoscanned assets and build DroneSim's realistic materials.

Run inside the Unreal editor, after Tools/fetch_assets.py has filled DroneSim/Downloads:

    Tools > Execute Python Script...  >  DroneSim/Tools/setup_realistic_assets.py

(Needs the "Python Editor Script Plugin", which is enabled by default.)

It creates, under /Game/DroneSim:
    M_DroneTerrain          layered ground material (grass, forest floor, heath, rock, snow, soil, hay, ice, lakes)
    M_DroneWater            fjord water with moving ripples
    Trees/SM_Conifer        fir, Nanite enabled
    Trees/SM_Broadleaf      deciduous tree, also used small for the orchards

DroneTerrain picks these up by path the next time you press Play. Until they exist it
falls back to flat colours and cone / sphere trees. Safe to run again: assets are replaced.

Each stage is independent: if one fails it is reported in the Output Log and the rest still run.
"""
import os

import unreal

ROOT = "/Game/DroneSim"
TEXTURE_PATH = ROOT + "/Textures"
TREE_PATH = ROOT + "/Trees"

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
DOWNLOADS = os.path.join(PROJECT_DIR, "Downloads")
SOURCE_ART = os.path.join(PROJECT_DIR, "Content", "SourceArt")

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def log(message):
    unreal.log("[DroneSim setup] " + message)


def warn(message):
    unreal.log_warning("[DroneSim setup] " + message)


# ------------------------------------------------------------------------------------------
# Importing
# ------------------------------------------------------------------------------------------

def import_file(filename, destination, options=None):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", filename)
    task.set_editor_property("destination_path", destination)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    if options is not None:
        task.set_editor_property("options", options)
    asset_tools.import_asset_tasks([task])


def import_texture(folder, file_name, is_normal=False, is_mask=False):
    """Imports Downloads/textures/<file_name> (or another folder) and returns the Texture2D, or None."""
    path = os.path.join(folder, file_name)
    asset_path = TEXTURE_PATH + "/" + os.path.splitext(file_name)[0]
    if not os.path.exists(path):
        warn("missing file " + path)
        return None
    import_file(path, TEXTURE_PATH)
    texture = unreal.load_asset(asset_path)
    if texture is None:
        warn("import failed for " + path)
        return None
    try:
        if is_normal:
            texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            texture.set_editor_property("srgb", False)
        elif is_mask:
            texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            texture.set_editor_property("srgb", False)
        eal.save_loaded_asset(texture)
    except Exception as error:  # texture still usable with default settings
        warn("could not adjust settings of %s: %s" % (file_name, error))
    return texture


def ground_texture(name, is_normal=False):
    return import_texture(os.path.join(DOWNLOADS, "textures"), name + ".jpg", is_normal=is_normal)


# ------------------------------------------------------------------------------------------
# Material graph helpers
# ------------------------------------------------------------------------------------------

class Graph:
    """Thin wrapper so the material graphs below read like expressions."""

    def __init__(self, name, folder=ROOT):
        path = folder + "/" + name
        if eal.does_asset_exist(path):
            eal.delete_asset(path)
        self.material = asset_tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
        self.path = path
        self.row = 0

    def node(self, cls, **properties):
        self.row += 1
        expression = mel.create_material_expression(self.material, cls, -1800 + (self.row % 12) * 140, (self.row // 12) * 160)
        for key, value in properties.items():
            expression.set_editor_property(key, value)
        return expression

    def link(self, source, target, target_input, source_output=""):
        if not mel.connect_material_expressions(source, source_output, target, target_input):
            warn("could not connect %s -> %s.%s" % (source.get_name(), target.get_name(), target_input))
        return target

    def output(self, source, material_property, source_output=""):
        if not mel.connect_material_property(source, source_output, material_property):
            warn("could not connect %s to %s" % (source.get_name(), material_property))

    # --- constants and inputs
    def scalar(self, value):
        return self.node(unreal.MaterialExpressionConstant, r=value)

    def colour(self, r, g, b):
        return self.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(r, g, b, 1.0))

    def channel(self, source, r=False, g=False, b=False, a=False, source_output=""):
        mask = self.node(unreal.MaterialExpressionComponentMask, r=r, g=g, b=b, a=a)
        return self.link(source, mask, "", source_output)

    def weight(self, uv_channel, component):
        coordinate = self.node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=uv_channel)
        return self.channel(coordinate, r=(component == 0), g=(component == 1))

    # --- maths
    def binary(self, cls, a, b):
        expression = self.node(cls)
        self.link(a, expression, "A")
        self.link(b, expression, "B")
        return expression

    def mul(self, a, b):
        return self.binary(unreal.MaterialExpressionMultiply, a, b)

    def add(self, a, b):
        return self.binary(unreal.MaterialExpressionAdd, a, b)

    def scale(self, a, factor):
        expression = self.node(unreal.MaterialExpressionMultiply, const_b=factor)
        return self.link(a, expression, "A")

    def lerp(self, a, b, alpha):
        expression = self.node(unreal.MaterialExpressionLinearInterpolate)
        self.link(a, expression, "A")
        self.link(b, expression, "B")
        self.link(alpha, expression, "Alpha")
        return expression

    def saturate(self, a):
        return self.link(a, self.node(unreal.MaterialExpressionSaturate), "")

    # --- textures
    def sample(self, texture, uv, is_normal=False):
        sampler_type = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if is_normal else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
        expression = self.node(unreal.MaterialExpressionTextureSample, texture=texture, sampler_type=sampler_type)
        try:
            # Shared sampler: the terrain uses more textures than the 16 dedicated sampler slots.
            expression.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        except Exception as error:
            warn("shared sampler not set: %s" % error)
        return self.link(uv, expression, "UVs")

    def finish(self):
        mel.layout_material_expressions(self.material)
        mel.recompile_material(self.material)
        eal.save_asset(self.path, only_if_is_dirty=False)
        log("created " + self.path)


# ------------------------------------------------------------------------------------------
# Terrain material
# ------------------------------------------------------------------------------------------
# DroneTerrain.cpp writes one blend weight per ground type into the mesh UV channels:
#   UV0 = (grass, forest floor)   UV1 = (heath, snow)   UV2 = (soil, hay)   UV3 = (blue ice, lake)
# Whatever is left over is bare rock. Textures are projected from above in world space, each at its
# real-world size close up and at a much larger size far away so the tiling never shows from altitude.

def build_terrain_material():
    textures = {}
    for name in ["sparse_grass", "forest_leaves_03", "aerial_grass_rock", "mossy_rock", "snow_02", "farm_soil", "withered_grass"]:
        textures[name] = (ground_texture(name + "_diff"), ground_texture(name + "_nor_dx", is_normal=True))
    meadow_far = ground_texture("rocky_terrain_02_diff")
    if any(diffuse is None or normal is None for diffuse, normal in textures.values()):
        warn("terrain material skipped: run Tools/fetch_assets.py first")
        return

    g = Graph("M_DroneTerrain")
    world = g.node(unreal.MaterialExpressionWorldPosition)
    top_xy = g.channel(world, r=True, g=True)

    def uv(size_m, source=None):
        return g.scale(top_xy if source is None else source, 1.0 / (size_m * 100.0))

    # 0 close to the camera, 1 from about 250 m away.
    depth = g.node(unreal.MaterialExpressionPixelDepth)
    far_amount = g.saturate(g.scale(g.link(depth, g.node(unreal.MaterialExpressionSubtract, const_b=2500.0), "A"), 1.0 / 22000.0))

    def layer(name, near_m, far_m, far_texture=None):
        diffuse, normal = textures[name]
        near = g.sample(diffuse, uv(near_m))
        far = g.sample(far_texture or diffuse, uv(far_m))
        colour = g.lerp(near, far, far_amount)
        return colour, g.sample(normal, uv(near_m), is_normal=True)

    # ---- Rock is the base. Its colour is projected from three sides so cliffs do not smear.
    rock_diffuse, rock_normal = textures["mossy_rock"]
    blend = g.link(g.link(g.node(unreal.MaterialExpressionVertexNormalWS), g.node(unreal.MaterialExpressionAbs), ""),
                   g.node(unreal.MaterialExpressionPower, const_exponent=4.0), "Base")
    blend_x, blend_y, blend_z = g.channel(blend, r=True), g.channel(blend, g=True), g.channel(blend, b=True)
    total = g.add(g.add(blend_x, blend_y), blend_z)

    def rock_at(size_m):
        top = g.sample(rock_diffuse, uv(size_m))
        side_x = g.sample(rock_diffuse, uv(size_m, g.channel(world, g=True, b=True)))   # faces looking along X
        side_y = g.sample(rock_diffuse, uv(size_m, g.channel(world, r=True, b=True)))   # faces looking along Y
        mixed = g.add(g.add(g.mul(top, blend_z), g.mul(side_x, blend_x)), g.mul(side_y, blend_y))
        return g.binary(unreal.MaterialExpressionDivide, mixed, total)

    colour = g.lerp(rock_at(4.0), rock_at(45.0), far_amount)
    normal = g.sample(rock_normal, uv(4.0), is_normal=True)
    roughness = g.scalar(0.85)

    # ---- Layers on top of the rock, in this order: (texture, near size m, far size m, far texture, UV channel, component, roughness)
    layers = [
        ("aerial_grass_rock", 15.0, 90.0, None, 1, 0, 0.90),      # heath
        ("forest_leaves_03", 2.3, 30.0, None, 0, 1, 0.90),        # forest floor
        ("sparse_grass", 2.0, 90.0, meadow_far, 0, 0, 0.85),      # pasture and lawns
        ("withered_grass", 2.0, 28.0, None, 2, 1, 0.90),          # hay meadow
        ("farm_soil", 2.0, 26.0, None, 2, 0, 0.92),               # ploughed soil
        ("snow_02", 2.0, 34.0, None, 1, 1, 0.55),                 # snow
    ]
    for name, near_m, far_m, far_texture, uv_channel, component, layer_roughness in layers:
        amount = g.weight(uv_channel, component)
        layer_colour, layer_normal = layer(name, near_m, far_m, far_texture)
        colour = g.lerp(colour, layer_colour, amount)
        normal = g.lerp(normal, layer_normal, amount)
        roughness = g.lerp(roughness, g.scalar(layer_roughness), amount)

    # ---- Blue glacier ice: tint the snow and make it glossy.
    ice = g.weight(3, 0)
    colour = g.lerp(colour, g.mul(colour, g.colour(0.42, 0.72, 0.95)), ice)
    roughness = g.lerp(roughness, g.scalar(0.22), ice)

    # ---- Meltwater lakes: dark, flat and mirror-like.
    lake = g.weight(3, 1)
    flat = g.colour(0.0, 0.0, 1.0)
    colour = g.lerp(colour, g.colour(0.015, 0.07, 0.085), lake)
    roughness = g.lerp(roughness, g.scalar(0.05), lake)
    normal = g.lerp(normal, flat, lake)

    # Fine surface detail fades out with distance, where it would only shimmer.
    normal = g.lerp(normal, flat, g.scale(far_amount, 0.75))

    g.output(colour, unreal.MaterialProperty.MP_BASE_COLOR)
    g.output(normal, unreal.MaterialProperty.MP_NORMAL)
    g.output(roughness, unreal.MaterialProperty.MP_ROUGHNESS)
    g.output(g.scalar(0.35), unreal.MaterialProperty.MP_SPECULAR)
    g.finish()


# ------------------------------------------------------------------------------------------
# Water material
# ------------------------------------------------------------------------------------------

def build_water_material():
    ripple = import_texture(SOURCE_ART, "water_normal.png", is_normal=True)
    g = Graph("M_DroneWater")
    g.output(g.colour(0.006, 0.028, 0.036), unreal.MaterialProperty.MP_BASE_COLOR)
    g.output(g.scalar(0.06), unreal.MaterialProperty.MP_ROUGHNESS)
    g.output(g.scalar(0.6), unreal.MaterialProperty.MP_SPECULAR)

    if ripple is not None:
        world_xy = g.channel(g.node(unreal.MaterialExpressionWorldPosition), r=True, g=True)

        def waves(size_m, speed_x, speed_y):
            panner = g.node(unreal.MaterialExpressionPanner, speed_x=speed_x, speed_y=speed_y)
            g.link(g.scale(world_xy, 1.0 / (size_m * 100.0)), panner, "Coordinate")
            return g.sample(ripple, panner, is_normal=True)

        # Two sizes drifting in different directions never visibly repeat.
        combined = g.add(waves(14.0, 0.035, 0.012), waves(47.0, -0.008, 0.015))
        flattened = g.mul(combined, g.colour(0.5, 0.5, 1.0))   # keep ripples gentle: a fjord, not open sea
        g.output(g.link(flattened, g.node(unreal.MaterialExpressionNormalize), "VectorInput"), unreal.MaterialProperty.MP_NORMAL)
    g.finish()


# ------------------------------------------------------------------------------------------
# Trees
# ------------------------------------------------------------------------------------------

def tree_texture(name, is_normal=False, is_mask=False):
    return import_texture(os.path.join(DOWNLOADS, "textures"), name + ".jpg", is_normal=is_normal, is_mask=is_mask)


def build_tree_material(name, prefix, has_alpha):
    """Bark (opaque) or foliage (masked, two-sided) material from <prefix>_diff / _nor_dx / _alpha."""
    diffuse = tree_texture(prefix + "_diff")
    normal = tree_texture(prefix + "_nor_dx", is_normal=True)
    alpha = tree_texture(prefix + "_alpha", is_mask=True) if has_alpha else None
    if diffuse is None:
        return None

    g = Graph(name, TREE_PATH)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    g.output(g.sample(diffuse, uv), unreal.MaterialProperty.MP_BASE_COLOR, "RGB")
    if normal is not None:
        g.output(g.sample(normal, uv, is_normal=True), unreal.MaterialProperty.MP_NORMAL, "RGB")
    g.output(g.scalar(0.75 if has_alpha else 0.9), unreal.MaterialProperty.MP_ROUGHNESS)
    if alpha is not None:
        g.material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        g.material.set_editor_property("two_sided", True)
        mask = g.sample(alpha, uv)
        mask.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        g.output(mask, unreal.MaterialProperty.MP_OPACITY_MASK, "R")
    g.finish()
    return g.material


def import_tree(fbx_name, asset_name, slot_materials):
    """Imports Downloads/models/<fbx_name>.fbx as /Game/DroneSim/Trees/<asset_name>.

    slot_materials: list of (text the material slot name must contain, material); first match wins,
    so list the more specific names first.
    """
    path = os.path.join(DOWNLOADS, "models", fbx_name + ".fbx")
    if not os.path.exists(path):
        warn("missing file " + path)
        return
    staging = TREE_PATH + "/Import_" + fbx_name
    target = TREE_PATH + "/" + asset_name

    options = None
    try:
        options = unreal.FbxImportUI()
        options.set_editor_property("import_mesh", True)
        options.set_editor_property("import_as_skeletal", False)
        options.set_editor_property("import_materials", False)
        options.set_editor_property("import_textures", False)
        options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
        mesh_data = options.get_editor_property("static_mesh_import_data")
        mesh_data.set_editor_property("combine_meshes", True)   # the fir file holds three saplings: use them as a clump
        mesh_data.set_editor_property("auto_generate_collision", False)
    except Exception as error:
        warn("FBX options not applied (%s); importing with defaults" % error)
        options = None

    import_file(path, staging, options)

    meshes = []
    for asset_path in eal.list_assets(staging, recursive=True):
        asset = unreal.load_asset(asset_path.split(".")[0])
        if isinstance(asset, unreal.StaticMesh):
            meshes.append(asset)
    if not meshes:
        warn("no static mesh came out of " + path)
        return
    mesh = meshes[0]

    slots = mesh.get_editor_property("static_materials")
    for index, slot in enumerate(slots):
        slot_name = str(slot.get_editor_property("material_slot_name")).lower()
        for needle, material in slot_materials:
            if material is not None and needle in slot_name:
                mesh.set_material(index, material)
                break
        else:
            warn("%s: no material matched slot '%s'" % (asset_name, slot_name))

    try:
        nanite = mesh.get_editor_property("nanite_settings")
        nanite.set_editor_property("enabled", True)
        mesh.set_editor_property("nanite_settings", nanite)
    except Exception as error:
        warn("could not enable Nanite on %s (%s). Enable it by hand in the mesh editor: "
             "these trees have over a million triangles each." % (asset_name, error))

    eal.save_loaded_asset(mesh)
    if eal.does_asset_exist(target):
        eal.delete_asset(target)
    if eal.rename_asset(mesh.get_path_name().split(".")[0], target):
        log("created " + target)
    else:
        warn("could not rename %s to %s" % (mesh.get_path_name(), target))


def build_trees():
    fir_needles = build_tree_material("M_FirNeedles", "fir_sapling_twigs", True)
    fir_bark = build_tree_material("M_FirBark", "fir_sapling_branches", False)
    import_tree("fir_sapling", "SM_Conifer", [("twig", fir_needles), ("branch", fir_bark)])

    leaves = build_tree_material("M_BroadleafLeaves", "island_tree_02_leaves", True)
    twigs = build_tree_material("M_BroadleafBranches", "island_tree_02_branches", False)
    trunk = build_tree_material("M_BroadleafTrunk", "island_tree_02", False)
    import_tree("island_tree_02", "SM_Broadleaf", [("leaves", leaves), ("branch", twigs), ("island_tree_02", trunk)])


# ------------------------------------------------------------------------------------------

def main():
    if not os.path.isdir(DOWNLOADS):
        warn("Folder not found: %s. Run 'python Tools/fetch_assets.py' first." % DOWNLOADS)
    for stage in (build_terrain_material, build_water_material, build_trees):
        try:
            stage()
        except Exception as error:
            warn("%s failed: %s" % (stage.__name__, error))
            import traceback
            unreal.log_warning(traceback.format_exc())
    log("finished. Press Play to see the result.")


main()
