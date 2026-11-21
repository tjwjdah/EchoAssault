"""Execute in Unreal Editor via -run=pythonscript -script=... after building."""
import unreal as u
from pathlib import Path

root = Path(u.Paths.convert_relative_path_to_full(u.Paths.project_dir()))
assets = u.AssetToolsHelpers.get_asset_tools()
lib = u.EditorAssetLibrary
lib.make_directory("/Game/Echo")
for name in ("Scout", "Hunter", "Lancer", "Crystal"):
    task = u.AssetImportTask()
    task.filename = str(root / "ArtSource" / (name + ".obj"))
    task.destination_path = "/Game/Echo"
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    opts = u.FbxImportUI()
    opts.set_editor_property("automated_import_should_detect_type", False)
    opts.set_editor_property("mesh_type_to_import", u.FBXImportType.FBXIT_STATIC_MESH)
    opts.import_materials = False
    opts.import_textures = False
    opts.static_mesh_import_data.combine_meshes = True
    opts.static_mesh_import_data.auto_generate_collision = False
    task.options = opts
    task.factory = u.FbxFactory()
    assets.import_asset_tasks([task])
    mesh = lib.load_asset("/Game/Echo/" + name)
    if not isinstance(mesh, u.StaticMesh):
        raise RuntimeError(f"Import failed for {name}: {task.imported_object_paths}")
    extent = mesh.get_bounds().box_extent
    if min(extent.x, extent.y, extent.z) <= 0:
        raise RuntimeError(f"Invalid mesh bounds for {name}: {extent}")
    u.log(f"ECHO_MESH {name} half extent {extent}")

mat_path = "/Game/Echo/Hull"
created_material = not lib.does_asset_exist(mat_path)
if created_material:
    mat = assets.create_asset("Hull", "/Game/Echo", u.Material, u.MaterialFactoryNew())
    color = u.MaterialEditingLibrary.create_material_expression(mat, u.MaterialExpressionVectorParameter)
    color.set_editor_property("parameter_name", "Tint")
    color.set_editor_property("default_value", u.LinearColor(0.2, 0.8, 1, 1))
    u.MaterialEditingLibrary.connect_material_property(color, "", u.MaterialProperty.MP_BASE_COLOR)
else:
    mat = lib.load_asset(mat_path)
if not isinstance(mat, u.Material):
    raise RuntimeError(f"Invalid material at {mat_path}")
if created_material or not mat.get_editor_property("used_with_instanced_static_meshes"):
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    u.MaterialEditingLibrary.recompile_material(mat)
    if not lib.save_loaded_asset(mat):
        raise RuntimeError(f"Material save failed: {mat_path}")
map_path = "/Game/Echo/Arena"
if not lib.does_asset_exist(map_path):
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    if not levels.new_level(map_path): raise RuntimeError("Map creation failed")
    actors = u.get_editor_subsystem(u.EditorActorSubsystem)
    actors.spawn_actor_from_class(u.PlayerStart, u.Vector(0,0,45))
    light = actors.spawn_actor_from_class(u.DirectionalLight, u.Vector(0,0,500), u.Rotator(0,-55,-32))
    light.light_component.set_editor_property("intensity", 5.0)
    sky = actors.spawn_actor_from_class(u.SkyLight, u.Vector(0,0,400))
    sky.light_component.set_editor_property("intensity", 0.7)
    if not levels.save_current_level(): raise RuntimeError("Map save failed")
u.log("ECHO_IMPORT_OK")
