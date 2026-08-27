import unreal


unreal.EditorLevelLibrary.load_level("/Game/VRTemplate/Maps/VRTemplateMap")
for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    class_name = actor.get_class().get_name()
    if class_name in ("PatientActor", "BeltActor", "WheelchairActor", "TransferManagerActor"):
        unreal.log(
            "RAGDOLL_REFERENCE {} label={} location={} rotation={} scale={}".format(
                class_name,
                actor.get_actor_label(),
                actor.get_actor_location(),
                actor.get_actor_rotation(),
                actor.get_actor_scale3d(),
            )
        )
