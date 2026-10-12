"""Read-only UI/media audit: compile in memory, export graphs, inspect maps. No asset saves."""
import json, os, runpy
import unreal
root = unreal.Paths.project_dir()
out = os.path.join(root, 'Saved', 'UIAudit-2026-10-12')
os.makedirs(out, exist_ok=True)
report = {'assets': [], 'maps': [], 'verification': None}
paths = set()
for folder in ('/Game/Project/Blueprints/UI', '/Game/Project/Blueprints/Actors', '/Game/Project/Blueprints/Data', '/Game/Project/Media', '/Game/Movies'):
    paths.update(unreal.EditorAssetLibrary.list_assets(folder, recursive=True, include_folder=False))
paths.update('/Game/VRTemplate/Blueprints/' + name for name in ('VRPawn', 'Menu', 'WidgetMenu'))
for path in sorted(paths):
    if path.lower().endswith('.mp4'):
        continue
    asset = unreal.load_asset(path)
    entry = {'path': path, 'loaded': bool(asset)}
    if not asset:
        report['assets'].append(entry)
        continue
    entry['class'] = asset.get_class().get_name()
    if isinstance(asset, unreal.Blueprint):
        try:
            unreal.BlueprintEditorLibrary.compile_blueprint(asset)
            entry['compiled_in_memory'] = True
            try:
                entry['status'] = str(asset.get_editor_property('status'))
            except Exception:
                pass
        except Exception as exc:
            entry['compile_exception'] = str(exc)
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = os.path.join(out, asset.get_name() + '.copy')
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    task.exporter = unreal.ObjectExporterT3D()
    entry['exported'] = unreal.Exporter.run_asset_export_task(task)
    if isinstance(asset, unreal.FileMediaSource):
        entry['file_path'] = asset.get_editor_property('file_path')
    if isinstance(asset, unreal.DataTable):
        entry['rows'] = [str(row) for row in unreal.DataTableFunctionLibrary.get_data_table_row_names(asset)]
        try:
            text = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(asset)
            with open(os.path.join(out, asset.get_name() + '.json'), 'w', encoding='utf-8') as f:
                f.write(text)
        except Exception as exc:
            entry['table_export_exception'] = str(exc)
    report['assets'].append(entry)
for path in ('/Game/Project/Maps/TrainingModeSelectionMap', '/Game/Project/Maps/CNA_Map_01'):
    world = unreal.EditorLoadingAndSavingUtils.load_map(path)
    mapped = {'path': path, 'loaded': bool(world), 'actors': []}
    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        if not actor.get_class().get_name().startswith('BP_') and not actor.get_components_by_class(unreal.WidgetComponent):
            continue
        entry = {'name': actor.get_name(), 'label': actor.get_actor_label(), 'class': actor.get_class().get_name(), 'widgets': []}
        for comp in actor.get_components_by_class(unreal.WidgetComponent):
            entry['widgets'].append({'name': comp.get_name(), 'class': str(comp.get_editor_property('widget_class')), 'size': str(comp.get_editor_property('draw_size')), 'visible': comp.is_visible(), 'hidden_in_game': comp.get_editor_property('hidden_in_game'), 'collision': str(comp.get_collision_enabled()), 'visibility_response': str(comp.get_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY))})
        for prop in ('ContentData', 'MediaPlayer', 'bEnablePatientConversation', 'TrainingMode', 'GameMode', 'MediaDisplayRef'):
            try:
                entry[prop] = str(actor.get_editor_property(prop))
            except Exception:
                pass
        mapped['actors'].append(entry)
    report['maps'].append(mapped)
try:
    runpy.run_path(os.path.join(root, 'Tools', 'verify_menu_media_reliability.py'))
    report['verification'] = 'PASS'
except Exception as exc:
    report['verification'] = 'FAIL: ' + str(exc)
    unreal.log_error('UI_AUDIT verification failed: ' + str(exc))
with open(os.path.join(out, 'AssetsAndMaps.json'), 'w', encoding='utf-8') as f:
    json.dump(report, f, indent=2)
unreal.log('UI_AUDIT completed: {} assets, {} maps, existing verification={}'.format(len(report['assets']), len(report['maps']), report['verification']))
