"""Create the standalone guidance material in Unreal Editor; existing assets are untouched."""
import unreal

path = "/Game/PatientSetup/Guidance"
unreal.EditorAssetLibrary.make_directory(path)
material = unreal.load_asset(path + "/M_TransferGhost")
if material is None:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_TransferGhost", path, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("used_with_skeletal_mesh", True)
    tint = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -250, -100)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.0, 0.8, 1.0, 1.0))
    opacity = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -250, 100)
    opacity.set_editor_property("parameter_name", "Opacity")
    opacity.set_editor_property("default_value", 0.32)
    unreal.MaterialEditingLibrary.connect_material_property(tint, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
unreal.log("CNA_GUIDANCE_MATERIAL " + material.get_path_name())

# Both camera-sphere fades must cover nearby geometry as well as the room.
# Keep the same shader graph/material and independent patient MID.
from pathlib import Path
import shutil
fade_path = "/Game/Project/Custom_Materails/M_VRFade"
fade = unreal.load_asset(fade_path)
unreal.log("CNA_FADE_MATERIAL depth_disabled=" + str(fade.get_editor_property("disable_depth_test")) + " two_sided=" + str(fade.get_editor_property("two_sided")))
if not fade.get_editor_property("disable_depth_test"):
    root = Path(unreal.Paths.project_dir())
    source = root / "Content/Project/Custom_Materails/M_VRFade.uasset"
    backup = root / "Saved/Guidance/BeforeGuidance-M_VRFade.uasset"
    if not backup.exists():
        shutil.copy2(source, backup)
    fade.set_editor_property("disable_depth_test", True)
    unreal.MaterialEditingLibrary.recompile_material(fade)
    unreal.EditorAssetLibrary.save_loaded_asset(fade)
    unreal.log("CNA_FADE_MATERIAL foreground coverage enabled")
