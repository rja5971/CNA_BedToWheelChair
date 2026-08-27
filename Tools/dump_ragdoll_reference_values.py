import json
import unreal


MAP_PATH = "/Game/VRTemplate/Maps/VRTemplateMap"


def value_text(value):
    if value is None:
        return None
    if isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, (list, tuple)):
        return [value_text(item) for item in value]
    if hasattr(value, "get_path_name"):
        return value.get_path_name()
    return str(value)


def read_properties(obj, names):
    values = {}
    for name in names:
        try:
            values[name] = value_text(obj.get_editor_property(name))
        except Exception as exc:
            values[name] = "<unavailable: {}>".format(exc)
    return values


unreal.EditorLevelLibrary.load_level(MAP_PATH)

actor_properties = {
    "PatientActor": [
        "spine_config",
        "state_configs",
        "test_mode_start_limp",
        "bypass_neck_support_for_belt",
        "bone_mapping",
        "grabbable_roles",
        "belt_attach_role",
        "neck_support_roles",
    ],
    "BeltActor": ["handle_offset", "attach_radius"],
    "WheelchairActor": ["acceptance_radius_config", "max_seating_velocity"],
    "TransferManagerActor": ["patient_ref", "wheelchair_ref", "belt_ref", "auto_start"],
}

component_properties = {
    "SeatedTransitionComponent": [
        "sit_upright_angle_threshold",
        "settle_velocity_threshold",
        "max_settle_time",
        "min_stable_time",
    ],
    "CooperationRampComponent": [
        "coop_start_angle",
        "max_coop_alpha",
        "arm_strength_multiplier",
        "arm_damping_multiplier",
        "relaxed_orientation_strength",
        "relaxed_angular_velocity_strength",
        "relaxed_position_strength",
        "relaxed_velocity_strength",
        "relaxed_max_angular_force",
        "relaxed_max_linear_force",
        "cooperating_orientation_strength",
        "cooperating_angular_velocity_strength",
        "cooperating_position_strength",
        "cooperating_velocity_strength",
        "cooperating_max_angular_force",
        "cooperating_max_linear_force",
        "cooperation_curve",
    ],
    "GrabComponent": [
        "grab_radius",
        "grab_linear_stiffness",
        "grab_linear_damping",
        "grab_angular_stiffness",
        "grab_angular_damping",
        "grab_interpolation_speed",
    ],
    "BeltComponent": ["left_handle_name", "right_handle_name"],
}

for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    class_name = actor.get_class().get_name()
    if class_name not in actor_properties:
        continue

    payload = {
        "class": class_name,
        "label": actor.get_actor_label(),
        "path": actor.get_path_name(),
        "location": value_text(actor.get_actor_location()),
        "rotation": value_text(actor.get_actor_rotation()),
        "scale": value_text(actor.get_actor_scale3d()),
        "properties": read_properties(actor, actor_properties[class_name]),
        "components": {},
    }

    for component in actor.get_components_by_class(unreal.ActorComponent):
        component_class = component.get_class().get_name()
        component_payload = {
            "class": component_class,
            "name": component.get_name(),
        }
        if component_class in component_properties:
            component_payload["properties"] = read_properties(
                component, component_properties[component_class]
            )
        if isinstance(component, unreal.SceneComponent):
            relative_transform = component.get_relative_transform()
            component_payload["relative_location"] = value_text(relative_transform.translation)
            component_payload["relative_rotation"] = value_text(relative_transform.rotation.rotator())
            component_payload["relative_scale"] = value_text(relative_transform.scale3d)
        if isinstance(component, unreal.StaticMeshComponent):
            component_payload["static_mesh"] = value_text(component.get_editor_property("static_mesh"))
            component_payload["collision_profile"] = str(component.get_collision_profile_name())
        if isinstance(component, unreal.SkeletalMeshComponent):
            component_payload["skeletal_mesh"] = value_text(component.get_editor_property("skeletal_mesh_asset"))
            component_payload["physics_asset"] = value_text(component.get_editor_property("physics_asset_override"))
            component_payload["collision_profile"] = str(component.get_collision_profile_name())
        if isinstance(component, unreal.BoxComponent):
            component_payload["box_extent"] = value_text(component.get_unscaled_box_extent())
        if isinstance(component, unreal.SphereComponent):
            component_payload["sphere_radius"] = component.get_unscaled_sphere_radius()
        payload["components"][component.get_name()] = component_payload

    unreal.log("RAGDOLL_VALUE " + json.dumps(payload, sort_keys=True))

unreal.log("RAGDOLL_VALUE COMPLETE")
