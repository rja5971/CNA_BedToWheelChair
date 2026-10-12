"""Editor Python helpers for inspecting live training UI. Never saves assets.

During PIE, load this module from the Output Log's Python console, then call
show('chat'), show('quiz0'), show('quiz1'), show('wrong'), show('lift'),
show('belt'), show('menu'), show('handwash'), or show('transfer').
Restart Play after previewing to test the uninterrupted training sequence.
"""
import unreal

_previews = []


def _world():
    # The subsystem selects the current PIE world, including its PIE package prefix.
    subsystem = (unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
                 if hasattr(unreal, 'UnrealEditorSubsystem') else None)
    if subsystem:
        world = subsystem.get_game_world()
    else:
        world = unreal.find_object(None, '/Game/Project/Maps/CNA_Map_01.CNA_Map_01')
    if not world or not unreal.GameplayStatics.get_player_controller(world, 0):
        raise RuntimeError('Start Play/VR Preview in CNA_Map_01 first.')
    return world


def _actors(world, name):
    cls = unreal.load_class(None, '/Game/Project/Blueprints/Actors/{0}.{0}_C'.format(name))
    if not cls:
        raise RuntimeError('Missing Blueprint: ' + name)
    return unreal.GameplayStatics.get_all_actors_of_class(world, cls)


def _one(world, name):
    actors = _actors(world, name)
    if len(actors) != 1:
        raise RuntimeError('Expected one {}, found {}'.format(name, len(actors)))
    return actors[0]


def _show_panel(world, panel):
    # Move only the live widget component so the learner can reach it immediately.
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    camera = pc.player_camera_manager
    yaw = camera.get_camera_rotation().yaw
    facing = unreal.Rotator(0, yaw, 0)
    position = camera.get_camera_location() + unreal.MathLibrary.get_forward_vector(facing) * 130.0
    panel.set_world_location_and_rotation(position, unreal.Rotator(0, yaw + 180.0, 0), False, False)
    panel.set_hidden_in_game(False)
    panel.set_visibility(True)
    panel.set_collision_enabled(unreal.CollisionEnabled.QUERY_ONLY)
    panel.set_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY, unreal.CollisionResponseType.ECR_BLOCK)
    widget = panel.get_user_widget_object()
    if widget:
        widget.set_is_enabled(True)
        widget.set_visibility(unreal.SlateVisibility.VISIBLE)
    return widget


def show(screen='chat'):
    world = _world()
    screen = screen.lower()
    if screen == 'chat':
        actor = _one(world, 'BP_PatientChat')
        actor.call_method('EnableChatPanel')
        _show_panel(world, actor.get_editor_property('ChatPanel'))
    elif screen in ('quiz0', 'quiz1', 'wrong'):
        actor = _one(world, 'BP_QuizActor')
        panel = actor.get_editor_property('QuizPanel')
        widget = panel.get_user_widget_object()
        if screen == 'wrong':
            widget.call_method('EnablePopUpComponent')
            _show_panel(world, actor.get_editor_property('PopupComponent'))
        else:
            if not unreal.CNAReliabilityLibrary.set_preview_quiz_question(widget, int(screen[-1])):
                raise RuntimeError('Could not select the preview quiz question.')
            actor.get_editor_property('PopupComponent').set_visibility(False)
            _show_panel(world, panel)
    elif screen == 'lift':
        actor = _one(world, 'BP_StepTrigger')
        actor.call_method('ActivateStep')
        _show_panel(world, actor.get_editor_property('Widget'))
        unreal.log_warning('UI_PREVIEW: Lift button retains the legacy patient callback; this is not a transfer fix.')
    elif screen == 'belt':
        # A visual/click preview only: the training map has no legacy BP_Belt owner.
        panel = unreal.CNAReliabilityLibrary.create_belt_ui_preview(world)
        if not panel:
            raise RuntimeError('Could not create the temporary belt widget.')
        actor = panel.get_owner()
        panel.set_world_scale3d(unreal.Vector(0.1, 0.1, 0.1))
        _previews.append(actor)
        _show_panel(world, panel)
        unreal.log_warning('UI_PREVIEW: Belt preview has no transfer delegate bound. Use it to inspect layout and clicks only.')
    elif screen == 'menu':
        pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
        pawn.call_method('ToggleMenu', args=(False,))
    elif screen in ('handwash', 'transfer'):
        content_name = 'DA_Basin_Content' if screen == 'handwash' else 'DA_BenToWheelchairVideo'
        matches = [a for a in _actors(world, 'BP_MediaDisplay')
                   if a.get_editor_property('ContentData').get_name() == content_name]
        if len(matches) != 1:
            raise RuntimeError('Expected one display for ' + content_name)
        unreal.CNAReliabilityLibrary.request_playback(matches[0])
        unreal.log('UI_PREVIEW: Video runs on its existing room display; completion uses the real Continue/quiz callback.')
    else:
        raise ValueError('Choose chat, quiz0, quiz1, wrong, lift, belt, menu, handwash, or transfer.')
    unreal.log('UI_PREVIEW shown: ' + screen)


def clear():
    """Remove belt preview actors. Restart Play to reset all other preview changes."""
    for actor in _previews:
        if unreal.SystemLibrary.is_valid(actor):
            actor.destroy_actor()
    _previews.clear()
