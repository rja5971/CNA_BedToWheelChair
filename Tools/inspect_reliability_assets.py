"""Read-only asset inspection; run with UnrealEditor-Cmd -run=pythonscript."""
import os
import unreal

output = os.path.join(unreal.Paths.project_saved_dir(), "Reliability")
os.makedirs(output, exist_ok=True)
for path in (
    "/Game/VRTemplate/Blueprints/VRPawn",
    "/Game/VRTemplate/Blueprints/Menu",
    "/Game/VRTemplate/Blueprints/WidgetMenu",
    "/Game/Project/Blueprints/Actors/BP_MediaDisplay",
    "/Game/Project/Blueprints/Actors/BP_MediaEventHandler",
    "/Game/Project/Blueprints/UI/WBP_MediaText",
):
    asset = unreal.load_asset(path)
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = os.path.join(output, asset.get_name() + ".copy")
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    task.exporter = unreal.ObjectExporterT3D()
    unreal.log("CNA_EXPORT {} {}".format(path, unreal.Exporter.run_asset_export_task(task)))

for path in ("/Game/VRTemplate/Input/IMC_Menu", "/Game/VRTemplate/Input/IMC_Default"):
    context = unreal.load_asset(path)
    for mapping in context.get_editor_property("mappings"):
        action = mapping.get_editor_property("action")
        unreal.log("CNA_MAPPING {} {} {} triggers={}".format(path, mapping.get_editor_property("key"), action.get_path_name(), action.get_editor_property("triggers")))

for path in ("/Game/Movies/FMS_BasinVideo", "/Game/Project/Media/MP_Display"):
    asset = unreal.load_asset(path)
    unreal.log("CNA_MEDIA " + str(asset))
    if isinstance(asset, unreal.FileMediaSource):
        unreal.log("CNA_FILE " + asset.get_editor_property("file_path"))
    if isinstance(asset, unreal.MediaPlayer):
        unreal.log("CNA_AUTOPLAY " + str(asset.get_editor_property("play_on_open")))

for path in ("/Game/Project/Blueprints/Actors/BP_MediaDisplay", "/Game/Project/Blueprints/Actors/BP_MediaEventHandler"):
    bp = unreal.load_asset(path)
    cdo = unreal.get_default_object(bp.generated_class())
    for name in ("MediaPlayer", "ContentData", "MediaDisplayRef"):
        try:
            unreal.log("CNA_DEFAULT {} {}={}".format(path, name, cdo.get_editor_property(name)))
        except Exception:
            pass
