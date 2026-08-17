import unreal


TERMS = ("patient", "belt", "wheel", "bed", "laying", "vrpawn", "player start")


def object_path(value):
    return value.get_path_name() if value else "None"


def describe_components(actor):
    for component in actor.get_components_by_class(unreal.ActorComponent):
        details = [component.get_name(), component.get_class().get_name()]
        if isinstance(component, unreal.SkeletalMeshComponent):
            details.append("skeletal_mesh=" + object_path(component.get_skeletal_mesh_asset()))
            details.append("physics_asset=" + object_path(component.get_editor_property("physics_asset_override")))
        elif isinstance(component, unreal.StaticMeshComponent):
            details.append("static_mesh=" + object_path(component.get_editor_property("static_mesh")))
        if isinstance(component, unreal.SceneComponent):
            details.append("relative_location=" + str(component.get_editor_property("relative_location")))
        unreal.log("CNA_INSPECT COMPONENT " + " | ".join(details))


unreal.log("CNA_INSPECT BEGIN")
world = unreal.EditorLevelLibrary.get_editor_world()
unreal.log("CNA_INSPECT WORLD " + object_path(world))

for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    label = actor.get_actor_label()
    class_name = actor.get_class().get_name()
    searchable = (label + " " + class_name).lower()
    if any(term in searchable for term in TERMS):
        unreal.log(
            "CNA_INSPECT ACTOR label={} | name={} | class={} | location={} | rotation={}".format(
                label,
                actor.get_name(),
                class_name,
                actor.get_actor_location(),
                actor.get_actor_rotation(),
            )
        )
        describe_components(actor)

for asset_path in (
    "/Game/VRTemplate/Blueprints/VRPawn.VRPawn",
    "/Game/Project/Blueprints/Actors/BP_PatientInteraction.BP_PatientInteraction",
    "/Game/Project/Blueprints/Actors/BP_Belt.BP_Belt",
):
    asset = unreal.load_asset(asset_path)
    unreal.log("CNA_INSPECT ASSET {} -> {}".format(asset_path, object_path(asset)))
    if isinstance(asset, unreal.Blueprint):
        generated_class = asset.generated_class()
        cdo = unreal.get_default_object(generated_class)
        unreal.log("CNA_INSPECT CDO " + object_path(cdo))
        describe_components(cdo)

unreal.log("CNA_INSPECT END")
