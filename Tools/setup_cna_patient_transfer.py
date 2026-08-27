import unreal


PATIENT_LABEL = "Ragdoll Patient"
BELT_LABEL = "Ragdoll Transfer Belt"
WHEELCHAIR_LABEL = "Ragdoll Wheelchair Target"
MANAGER_LABEL = "Ragdoll Transfer Manager"
SYSTEM_FOLDER = "Patient Transfer System"


def require_asset(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError("Required asset was not found: " + path)
    return asset


def require_class(path):
    loaded_class = unreal.load_class(None, path)
    if not loaded_class:
        raise RuntimeError("Required class was not found: " + path)
    return loaded_class


def actor_by_label(label):
    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        if actor.get_actor_label() == label:
            return actor
    return None


def actor_matching(label=None, class_name=None):
    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        if label and actor.get_actor_label() == label:
            return actor
        if class_name and actor.get_class().get_name() == class_name:
            return actor
    return None


def spawn_or_update(label, actor_class, location, rotation):
    actor = actor_by_label(label)
    if not actor:
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(actor_class, location, rotation)
    actor.set_actor_label(label)
    actor.set_actor_location(location, False, False)
    actor.set_actor_rotation(rotation, False)
    actor.set_folder_path(SYSTEM_FOLDER)
    return actor


def make_rotator(pitch=0.0, yaw=0.0, roll=0.0):
    rotation = unreal.Rotator()
    rotation.pitch = pitch
    rotation.yaw = yaw
    rotation.roll = roll
    return rotation


def component_named(actor, name):
    for component in actor.get_components_by_class(unreal.ActorComponent):
        if component.get_name() == name:
            return component
    raise RuntimeError("{} has no component named {}".format(actor.get_actor_label(), name))


def assign_skeletal_mesh(component, mesh):
    if hasattr(component, "set_skeletal_mesh_asset"):
        component.set_skeletal_mesh_asset(mesh)
    else:
        component.set_skeletal_mesh(mesh, True)


unreal.log("CNA_RAGDOLL_SETUP BEGIN")

patient_class = require_class("/Script/HandlingRagdolls.PatientActor")
belt_class = require_class("/Script/HandlingRagdolls.BeltActor")
wheelchair_class = require_class("/Script/HandlingRagdolls.WheelchairActor")
manager_class = require_class("/Script/HandlingRagdolls.TransferManagerActor")

patient_mesh_asset = require_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
patient_physics_asset = require_asset("/Game/Characters/Mannequins/Rigs/PA_Mannequin")
bone_mapping = require_asset("/Game/PatientSetup/DA_BoneMapping_SKM_Manny_Simple")
state_configs = [
    require_asset("/Game/Data/States/DA_State_LyingDown"),
    require_asset("/Game/Data/States/DA_State_BeingSupported"),
    require_asset("/Game/Data/States/DA_State_Seated"),
    require_asset("/Game/Data/States/DA_State_BeingTransferred"),
    require_asset("/Game/Data/States/DA_State_BeingLifted"),
]

# The CNA bed is rotated about 90 degrees relative to the Ragdoll reference map.
# Roll -90 lays the mannequin down; yaw 90 aligns it with this bed's long axis.
patient = spawn_or_update(
    PATIENT_LABEL,
    patient_class,
    # Align Manny's head with the original CNA patient's head on the pillow.
    unreal.Vector(-1003.844406, 1340.248123, 195.59),
    make_rotator(pitch=0.0, yaw=90.0, roll=-90.0),
)
patient_mesh = component_named(patient, "PatientMesh")
assign_skeletal_mesh(patient_mesh, patient_mesh_asset)
patient_mesh.set_physics_asset(patient_physics_asset, True)
patient_mesh.set_collision_profile_name("PhysicsActor")
patient.set_actor_scale3d(unreal.Vector(1.0, 1.0, 1.0))
patient.set_editor_property("bone_mapping", bone_mapping)
patient.set_editor_property("state_configs", state_configs)
patient.set_editor_property("test_mode_start_limp", False)
patient.set_editor_property("bypass_neck_support_for_belt", True)

# Exact component tuning from the Ragdoll reference map.
seated_transition = component_named(patient, "SeatedTransition")
seated_transition.set_editor_property("sit_upright_angle_threshold", 25.0)
seated_transition.set_editor_property("settle_velocity_threshold", 15.0)
seated_transition.set_editor_property("max_settle_time", 3.0)
seated_transition.set_editor_property("min_stable_time", 0.4)

cooperation_ramp = component_named(patient, "CooperationRamp")
cooperation_values = {
    "coop_start_angle": 70.0,
    "max_coop_alpha": 0.8,
    "arm_strength_multiplier": 0.3,
    "arm_damping_multiplier": 0.5,
    "relaxed_orientation_strength": 350.0,
    "relaxed_angular_velocity_strength": 200.0,
    "relaxed_position_strength": 0.0,
    "relaxed_velocity_strength": 0.0,
    "relaxed_max_angular_force": 350.0,
    "relaxed_max_linear_force": 0.0,
    "cooperating_orientation_strength": 1200.0,
    "cooperating_angular_velocity_strength": 120.0,
    "cooperating_position_strength": 200.0,
    "cooperating_velocity_strength": 50.0,
    "cooperating_max_angular_force": 1000.0,
    "cooperating_max_linear_force": 500.0,
    "cooperation_curve": None,
}
for property_name, property_value in cooperation_values.items():
    cooperation_ramp.set_editor_property(property_name, property_value)

belt = spawn_or_update(
    BELT_LABEL,
    belt_class,
    unreal.Vector(-824.067305, 1515.574917, 220.0),
    make_rotator(),
)
belt.set_actor_scale3d(unreal.Vector(0.4, 0.4, 0.05))
belt.set_editor_property("handle_offset", unreal.Vector(40.0, 0.0, 0.0))
belt.set_editor_property("attach_radius", 75.0)

# The CNA chair mesh already exists in the level. The native target stays
# invisible and supplies the seat target/acceptance logic at the same transform.
wheelchair = spawn_or_update(
    WHEELCHAIR_LABEL,
    wheelchair_class,
    unreal.Vector(-673.629977, 1346.574970, 105.0),
    make_rotator(yaw=178.028212),
)
wheelchair_mesh = component_named(wheelchair, "WheelchairMesh")
wheelchair_mesh.set_visibility(False, True)
wheelchair_mesh.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
wheelchair.set_actor_scale3d(unreal.Vector(0.6, 0.6, 0.5))
seat_zone = component_named(wheelchair, "SeatZone")
seat_zone.set_relative_location(unreal.Vector(0.0, 0.0, 50.0), False, True)
seat_zone.set_box_extent(unreal.Vector(30.0, 30.0, 20.0), False)
seat_target = component_named(wheelchair, "SeatTarget")
seat_target.set_relative_location(unreal.Vector(0.0, 0.0, 160.0), False, True)
wheelchair.set_editor_property("acceptance_radius_config", 30.0)
wheelchair.set_editor_property("max_seating_velocity", 50.0)
wheelchair.set_editor_property("start_with_brakes_locked", True)

manager = spawn_or_update(
    MANAGER_LABEL,
    manager_class,
    unreal.Vector(0.0, 0.0, 0.0),
    make_rotator(),
)
manager.set_editor_property("patient_ref", patient)
manager.set_editor_property("belt_ref", belt)
manager.set_editor_property("wheelchair_ref", wheelchair)
manager.set_editor_property("auto_start", True)
manager.set_editor_property("auto_configure_vr_hands", True)

# Avoid rendering two patients and two belts during play while preserving the
# original CNA actors and their Blueprint references for later integration.
legacy_patient = actor_matching(label="Laying_Shaking_Head")
if legacy_patient:
    legacy_patient.set_actor_hidden_in_game(True)
    legacy_patient.set_is_temporarily_hidden_in_editor(True)
legacy_belt = actor_matching(label="BP_Belt")
if legacy_belt:
    legacy_belt.set_actor_hidden_in_game(True)
    legacy_belt.set_is_temporarily_hidden_in_editor(True)

unreal.EditorLevelLibrary.save_current_level()
unreal.log(
    "CNA_RAGDOLL_SETUP COMPLETE patient={} belt={} wheelchair={} manager={}".format(
        patient.get_path_name(),
        belt.get_path_name(),
        wheelchair.get_path_name(),
        manager.get_path_name(),
    )
)
