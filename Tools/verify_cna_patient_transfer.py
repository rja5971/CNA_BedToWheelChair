import unreal


def actor_by_label(label):
    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        if actor.get_actor_label() == label:
            return actor
    return None


def vector_text(value):
    return "({:.2f},{:.2f},{:.2f})".format(value.x, value.y, value.z)


for label in [
    "Ragdoll Patient",
    "Ragdoll Transfer Belt",
    "Ragdoll Wheelchair Target",
    "Ragdoll Transfer Manager",
    "Laying_Shaking_Head",
    "WheelChair",
]:
    actor = actor_by_label(label)
    if not actor:
        unreal.log_warning("CNA_RAGDOLL_VERIFY missing=" + label)
        continue
    unreal.log(
        "CNA_RAGDOLL_VERIFY actor={} class={} loc={} rot={}".format(
            label,
            actor.get_class().get_name(),
            vector_text(actor.get_actor_location()),
            actor.get_actor_rotation(),
        )
    )
    bounds_origin, bounds_extent = actor.get_actor_bounds(False)
    unreal.log(
        "CNA_RAGDOLL_VERIFY bounds={} origin={} extent={}".format(
            label, vector_text(bounds_origin), vector_text(bounds_extent)
        )
    )
    skeletal_components = actor.get_components_by_class(unreal.SkeletalMeshComponent)
    for component in skeletal_components:
        for bone in ["pelvis", "spine_01", "neck_01", "head", "foot_l", "foot_r"]:
            bone_index = component.get_bone_index(bone)
            if bone_index >= 0:
                unreal.log(
                    "CNA_RAGDOLL_VERIFY bone={} component={} {}={}".format(
                        label,
                        component.get_name(),
                        bone,
                        vector_text(component.get_socket_location(bone)),
                    )
                )

unreal.log("CNA_RAGDOLL_VERIFY COMPLETE")
