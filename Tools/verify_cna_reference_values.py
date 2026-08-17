import math
import unreal


FAILURES = []


def actor_by_label(label):
    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        if actor.get_actor_label() == label:
            return actor
    FAILURES.append("missing actor: " + label)
    return None


def component_named(actor, name):
    for component in actor.get_components_by_class(unreal.ActorComponent):
        if component.get_name() == name:
            return component
    FAILURES.append("{} missing component {}".format(actor.get_actor_label(), name))
    return None


def path_of(value):
    return value.get_path_name() if value else None


def check(label, actual, expected):
    if actual != expected:
        FAILURES.append("{} expected {!r}, got {!r}".format(label, expected, actual))


def check_float(label, actual, expected):
    if not math.isclose(float(actual), float(expected), rel_tol=0.0, abs_tol=0.001):
        FAILURES.append("{} expected {}, got {}".format(label, expected, actual))


def check_vector(label, actual, expected):
    for axis, expected_value in zip(("x", "y", "z"), expected):
        check_float("{}.{}".format(label, axis), getattr(actual, axis), expected_value)


patient = actor_by_label("Ragdoll Patient")
belt = actor_by_label("Ragdoll Transfer Belt")
wheelchair = actor_by_label("Ragdoll Wheelchair Target")
manager = actor_by_label("Ragdoll Transfer Manager")

if patient:
    patient_mesh = component_named(patient, "PatientMesh")
    seated = component_named(patient, "SeatedTransition")
    cooperation = component_named(patient, "CooperationRamp")
    check_vector("patient.scale", patient.get_actor_scale3d(), (1.0, 1.0, 1.0))
    check("patient.bypass_neck_support_for_belt", patient.get_editor_property("bypass_neck_support_for_belt"), True)
    check("patient.test_mode_start_limp", patient.get_editor_property("test_mode_start_limp"), False)
    check(
        "patient.bone_mapping",
        path_of(patient.get_editor_property("bone_mapping")),
        "/Game/PatientSetup/DA_BoneMapping_SKM_Manny_Simple.DA_BoneMapping_SKM_Manny_Simple",
    )
    expected_states = [
        "/Game/Data/States/DA_State_LyingDown.DA_State_LyingDown",
        "/Game/Data/States/DA_State_BeingSupported.DA_State_BeingSupported",
        "/Game/Data/States/DA_State_Seated.DA_State_Seated",
        "/Game/Data/States/DA_State_BeingTransferred.DA_State_BeingTransferred",
        "/Game/Data/States/DA_State_BeingLifted.DA_State_BeingLifted",
    ]
    check("patient.state_configs", [path_of(value) for value in patient.get_editor_property("state_configs")], expected_states)
    if patient_mesh:
        check(
            "patient.mesh",
            path_of(patient_mesh.get_editor_property("skeletal_mesh_asset")),
            "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple",
        )
        check(
            "patient.physics_asset",
            path_of(patient_mesh.get_editor_property("physics_asset_override")),
            "/Game/Characters/Mannequins/Rigs/PA_Mannequin.PA_Mannequin",
        )
        check("patient.collision_profile", str(patient_mesh.get_collision_profile_name()), "PhysicsActor")
    if seated:
        for prop, expected in {
            "sit_upright_angle_threshold": 25.0,
            "settle_velocity_threshold": 15.0,
            "max_settle_time": 3.0,
            "min_stable_time": 0.4,
        }.items():
            check_float("seated." + prop, seated.get_editor_property(prop), expected)
    if cooperation:
        for prop, expected in {
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
        }.items():
            check_float("cooperation." + prop, cooperation.get_editor_property(prop), expected)
        check("cooperation.cooperation_curve", cooperation.get_editor_property("cooperation_curve"), None)
if belt:
    check_vector("belt.scale", belt.get_actor_scale3d(), (0.4, 0.4, 0.05))
    check_vector("belt.handle_offset", belt.get_editor_property("handle_offset"), (40.0, 0.0, 0.0))
    check_float("belt.attach_radius", belt.get_editor_property("attach_radius"), 75.0)
    proximity = component_named(belt, "AttachProximity")
    if proximity:
        check_float("belt.attach_proximity_radius", proximity.get_unscaled_sphere_radius(), 30.0)

if wheelchair:
    check_vector("wheelchair.scale", wheelchair.get_actor_scale3d(), (0.6, 0.6, 0.5))
    check_float("wheelchair.acceptance_radius", wheelchair.get_editor_property("acceptance_radius_config"), 30.0)
    check_float("wheelchair.max_seating_velocity", wheelchair.get_editor_property("max_seating_velocity"), 50.0)
    seat_zone = component_named(wheelchair, "SeatZone")
    seat_target = component_named(wheelchair, "SeatTarget")
    if seat_zone:
        check_vector("wheelchair.seat_zone_location", seat_zone.get_relative_transform().translation, (0.0, 0.0, 50.0))
        check_vector("wheelchair.seat_zone_extent", seat_zone.get_unscaled_box_extent(), (30.0, 30.0, 20.0))
    if seat_target:
        check_vector("wheelchair.seat_target_location", seat_target.get_relative_transform().translation, (0.0, 0.0, 160.0))

if manager and patient and belt and wheelchair:
    check("manager.patient_ref", manager.get_editor_property("patient_ref"), patient)
    check("manager.belt_ref", manager.get_editor_property("belt_ref"), belt)
    check("manager.wheelchair_ref", manager.get_editor_property("wheelchair_ref"), wheelchair)
    check("manager.auto_start", manager.get_editor_property("auto_start"), True)
    check("manager.auto_configure_vr_hands", manager.get_editor_property("auto_configure_vr_hands"), True)

if FAILURES:
    for failure in FAILURES:
        unreal.log_error("CNA_REFERENCE_VERIFY FAIL " + failure)
    raise RuntimeError("CNA reference verification failed with {} mismatch(es)".format(len(FAILURES)))

unreal.log("CNA_REFERENCE_VERIFY PASS exact Ragdoll tuning values are saved in CNA_Map_01")
