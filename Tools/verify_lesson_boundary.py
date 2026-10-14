"""Rendered editor-game checks for live lesson settings, popup freedom and faded re-entry."""
import json, time, traceback
import unreal

start = time.monotonic()
phase = 0
ready_at = 0.
records = []

def record(message):
    records.append(message)
    unreal.log('LESSON_BOUNDARY ' + message)

def finish(world):
    with open(unreal.Paths.project_saved_dir() + 'GuidedTraining/LessonBoundaryRuntime.json', 'w') as stream:
        json.dump(records, stream, indent=2)
    unreal.unregister_slate_post_tick_callback(handle)
    unreal.SystemLibrary.execute_console_command(world, 'quit')

def tick(delta):
    global phase, ready_at
    world = unreal.find_object(None, '/Game/Project/Maps/CNA_Map_01.CNA_Map_01')
    if not world or time.monotonic() < ready_at: return
    try:
        assert time.monotonic() - start < 35, 'Timed out in phase ' + str(phase)
        managers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TransferManagerActor)
        if not managers: return
        manager = managers[0]
        cls = unreal.load_class(None, '/Game/Project/Blueprints/Actors/BP_QuizActor.BP_QuizActor_C')
        actors = unreal.GameplayStatics.get_all_actors_of_class(world, cls)
        if not actors: return
        actor = actors[0]
        panel = actor.get_editor_property('QuizPanel')
        widget = panel.get_user_widget_object()
        pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
        circles = [c for c in manager.get_components_by_class(unreal.StaticMeshComponent) if c.get_name() == 'AllowedWalkHologram']
        circle = circles[0] if circles else None
        if phase == 0:
            if time.monotonic() - start < 3 or not panel.is_visible() or not circle: return
            assert manager.is_lesson_boundary_active() and circle.is_visible()
            centre = panel.get_world_location()
            position = circle.get_world_location()
            assert abs(position.x - centre.x) < .1 and abs(position.y - centre.y) < .1
            assert circle.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION
            assert abs(circle.get_world_scale().x - 4.) < .01
            assert circle.get_material(0).get_name() == 'M_LessonWalkArea'
            view = pawn.get_editor_property('Camera').get_world_location()
            distance = ((view.x-centre.x)**2 + (view.y-centre.y)**2)**.5
            assert abs(distance-200.) < .1, 'Initial quiz arrival is not on the circumference'
            look = pawn.get_editor_property('Camera').get_forward_vector()
            assert (centre.x-view.x)*look.x + (centre.y-view.y)*look.y > 0
            unreal.SystemLibrary.execute_console_command(world, 'cna.Guided.TestMovement')
            record('PASS: actual floor hologram centred on quiz, two-metre radius, no collision')
            manager.set_editor_property('TrainingMovementRadiusMeters', 1.5)
            phase = 1
        elif phase == 1:
            if abs(circle.get_world_scale().x - 3.) >= .01: return
            assert abs(circle.get_world_scale().x - 3.) < .01
            record('PASS: live editor radius changes floor diameter')
            manager.set_editor_property('bShowAllowedWalkHologram', False)
            phase = 2
        elif phase == 2:
            if circle.is_visible(): return
            assert not circle.is_visible() and manager.is_lesson_boundary_active()
            record('PASS: editor hologram toggle hides visual and retains movement boundary')
            manager.set_editor_property('bShowAllowedWalkHologram', True)
            widget.call_method('BndEvt__WBP_QuizPanel_Option1Button_K2Node_ComponentBoundEvent_0_OnButtonClickedEvent__DelegateSignature')
            phase = 3
        elif phase == 3:
            if circle.is_visible(): return
            assert not circle.is_visible() and not manager.is_lesson_boundary_active()
            location = pawn.get_actor_location() + unreal.Vector(1000, 0, 0)
            assert unreal.CNAReliabilityLibrary.validate_training_teleport_location(manager, True, location)
            pawn.set_actor_location(location, False, True)
            popup = actor.get_editor_property('PopupComponent').get_user_widget_object()
            popup.call_method('BndEvt__WBP_WrongAnswerPopup_OKButton_K2Node_ComponentBoundEvent_0_OnButtonClickedEvent__DelegateSignature')
            assert not unreal.CNAReliabilityLibrary.is_training_movement_allowed(manager)
            record('PASS: wrong-answer popup has no radius; returning from outside starts protected fade')
            phase = 4
            ready_at = time.monotonic() + 2.
            return
        elif phase == 4:
            assert manager.get_training_stage() == unreal.GuidedTrainingStage.ENTRY_QUIZ, 'Lesson relocation failed'
            if not unreal.CNAReliabilityLibrary.is_training_movement_allowed(manager) or not panel.is_visible() or not circle.is_visible(): return
            assert panel.is_visible() and circle.is_visible() and manager.is_lesson_boundary_active()
            assert unreal.CNAReliabilityLibrary.is_training_movement_allowed(manager)
            camera = pawn.get_editor_property('Camera')
            centre = panel.get_world_location()
            view = camera.get_world_location()
            assert (view.x-centre.x)**2 + (view.y-centre.y)**2 <= 150**2 + .1
            assert abs(((view.x-centre.x)**2 + (view.y-centre.y)**2)**.5 - 150.) < .1
            record('PASS: faded lesson re-entry lands inside panel-centred editor radius')
            manager.set_editor_property('TrainingMovementRadiusMeters', 2.)
            # Supply eye height for the desktop camera, which has no tracked headset pose.
            camera.set_relative_location(unreal.Vector(0, 0, 160), False, False)
            camera.set_world_rotation(unreal.Rotator(pitch=-25, yaw=pawn.get_actor_rotation().yaw, roll=0), False, False)
            phase = 5
        elif phase == 5:
            unreal.SystemLibrary.execute_console_command(world, 'HighResShot 1 filename="' + unreal.Paths.project_saved_dir() + 'GuidedTraining/LessonWalkArea.png"')
            record('PASS: all lesson-boundary runtime checks')
            phase = 6
        elif phase == 6:
            finish(world)
            return
        ready_at = time.monotonic() + .3
    except Exception:
        record('FAIL: phase {} {}'.format(phase, traceback.format_exc()))
        finish(world)

handle = unreal.register_slate_post_tick_callback(tick)
