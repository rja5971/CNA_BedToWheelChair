"""Run in rendered standalone editor game: py Tools/verify_guided_journey.py.
Uses the saved mode selection and actual widget callbacks. Synthetic patient
transfer assertions are delegated to cna.Guided.SmokeTransfer after both videos.
"""
import json, time, traceback
import unreal

started = time.monotonic()
phase = 0
records = []
next_tick = 0.
movement_stages_checked = set()

def log(message):
    records.append(message)
    unreal.log('GUIDED_JOURNEY ' + message)

def world_for(name):
    return unreal.find_object(None, '/Game/Project/Maps/' + name + '.' + name)

def actor(world, name):
    cls = unreal.load_class(None, '/Game/Project/Blueprints/Actors/' + name + '.' + name + '_C')
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, cls)
    return actors[0] if actors else None

def click(widget, name):
    widget.call_method(name)

def save():
    with open(unreal.Paths.project_saved_dir() + 'GuidedTraining/Journey.json', 'w') as stream:
        json.dump(records, stream, indent=2)

def tick(delta):
    global phase, next_tick
    if time.monotonic() < next_tick: return
    next_tick = time.monotonic() + .1
    world = None
    try:
        assert time.monotonic() - started < 650, 'Journey timed out at phase ' + str(phase)
        if phase == 0:
            world = world_for('TrainingModeSelectionMap')
            if not world or time.monotonic() - started < 3: return
            selection = actor(world, 'BP_TraningModuleSelection')
            if not selection: return
            panels = selection.get_components_by_class(unreal.WidgetComponent)
            widget = next((p.get_user_widget_object() for p in panels if p.get_user_widget_object()), None)
            if not widget: return
            click(widget, 'BndEvt__WBP_TraininhModeSelection_ButtonHouseRedidance_K2Node_ComponentBoundEvent_0_OnButtonClickedEvent__DelegateSignature')
            log('PASS: real residence-selection callback invoked')
            phase = 1
            return
        world = world_for('CNA_Map_01')
        if not world: return
        managers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TransferManagerActor)
        if not managers: return
        manager = managers[0]
        quiz_actor = actor(world, 'BP_QuizActor')
        quiz_panel = quiz_actor.get_editor_property('QuizPanel')
        quiz = quiz_panel.get_user_widget_object()
        if not quiz: return
        stage = manager.get_training_stage()
        stages = unreal.GuidedTrainingStage
        if stage in (stages.BATHROOM_VIDEO, stages.TRANSFER_VIDEO, stages.PATIENT_QUIZ) and stage not in movement_stages_checked and manager.is_lesson_boundary_active() and unreal.CNAReliabilityLibrary.is_training_movement_allowed(manager):
            if stage == stages.PATIENT_QUIZ:
                pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
                view = pawn.get_editor_property('Camera').get_world_location()
                centre = quiz_panel.get_world_location()
                front = quiz_panel.get_forward_vector()
                assert (view.x-centre.x)*front.x + (view.y-centre.y)*front.y > 0, 'Patient quiz arrival is behind the panel'
                look = pawn.get_editor_property('Camera').get_forward_vector()
                assert (centre.x-view.x)*look.x + (centre.y-view.y)*look.y > 0, 'Patient quiz arrival faces away from the panel'
                log('PASS: patient quiz arrival is in front of the panel and faces the quiz')
            assert unreal.CNAReliabilityLibrary.is_training_movement_allowed(manager)
            unreal.SystemLibrary.execute_console_command(world, 'cna.Guided.TestMovement')
            movement_stages_checked.add(stage)
        wrong = quiz_actor.get_editor_property('PopupComponent')
        chat_actor = actor(world, 'BP_PatientChat')
        chat_panel = chat_actor.get_editor_property('ChatPanel')
        chat = chat_panel.get_user_widget_object()
        q1 = 'BndEvt__WBP_QuizPanel_Option1Button_K2Node_ComponentBoundEvent_0_OnButtonClickedEvent__DelegateSignature'
        q2 = 'BndEvt__WBP_QuizPanel_Option2Button_K2Node_ComponentBoundEvent_1_OnButtonClickedEvent__DelegateSignature'
        ok = 'BndEvt__WBP_WrongAnswerPopup_OKButton_K2Node_ComponentBoundEvent_0_OnButtonClickedEvent__DelegateSignature'
        if phase == 1:
            if stage != stages.ENTRY_QUIZ: return
            if not quiz_panel.is_visible() or not unreal.CNAReliabilityLibrary.is_training_movement_allowed(manager): return
            assert manager.get_editor_property('bGuidedTraining')
            assert not chat_actor.get_editor_property('bShowConversationForUITesting')
            assert not chat_panel.is_visible() and chat_panel.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION
            assert unreal.CNAReliabilityLibrary.is_training_movement_allowed(manager)
            pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
            before = pawn.get_actor_location()
            unreal.SystemLibrary.execute_console_command(world, 'cna.Guided.TestMovement')
            assert pawn.get_actor_location().equals(before)
            click(quiz, q1)
            assert quiz.get_editor_property('CurrentQuestionIndex') == 0 and wrong.is_visible()
            assert not manager.is_lesson_boundary_active(), 'Wrong-answer popup must suspend the lesson boundary'
            click(quiz, q2)
            assert quiz.get_editor_property('CurrentQuestionIndex') == 0
            click(wrong.get_user_widget_object(), ok)
            assert not wrong.is_visible() and quiz_panel.is_visible()
            assert manager.is_lesson_boundary_active()
            click(quiz, q2); click(quiz, q2)
            assert quiz.get_editor_property('CurrentQuestionIndex') == 1
            assert manager.get_training_stage() == stages.BATHROOM_TRANSITION
            log('PASS: map travel, delivery flags, wrong answer/retry, bounded quiz teleport checks and duplicate quiz submission')
            phase = 2
        elif phase == 2 and stage == stages.BATHROOM_CONTINUE:
            handler = actor(world, 'BP_MediaEventHandler')
            handler.call_method('OnContinueClicked_Event'); handler.call_method('OnContinueClicked_Event')
            assert manager.get_training_stage() == stages.ROOM_TRANSITION
            log('PASS: complete bathroom video and one-shot Continue callback')
            phase = 3
        elif phase == 3 and stage == stages.CONVERSATION:
            assert not manager.is_lesson_boundary_active()
            assert chat_panel.is_visible() and not quiz_panel.is_visible()
            next_button = 'BndEvt__WBP_PatientChat_Button_422_K2Node_ComponentBoundEvent_3_OnButtonClickedEvent__DelegateSignature'
            response = 'BndEvt__WBP_PatientChat_PatientInteractButton_K2Node_ComponentBoundEvent_0_OnButtonClickedEvent__DelegateSignature'
            click(chat, next_button)
            assert manager.get_training_stage() == stages.CONVERSATION
            click(chat, response); click(chat, response)
            assert chat.get_editor_property('PatientResponseText').get_visibility() == unreal.SlateVisibility.VISIBLE
            assert manager.get_training_stage() == stages.CONVERSATION
            click(chat, next_button); click(chat, next_button)
            assert manager.get_training_stage() == stages.TRANSFER_VIDEO
            assert not quiz_panel.is_visible() and not chat_panel.is_visible()
            log('PASS: room transition, conversation prerequisite, response without premature video and one-shot Next')
            phase = 4
        elif phase == 4 and stage == stages.PATIENT_QUIZ:
            if not quiz_panel.is_visible() or not unreal.CNAReliabilityLibrary.is_training_movement_allowed(manager): return
            assert quiz_panel.is_visible() and quiz.get_editor_property('CurrentQuestionIndex') == 1
            assert 'head/neck' in str(quiz.get_editor_property('TextBlock_OP2').get_text())
            click(quiz, q1)
            assert wrong.is_visible() and quiz.get_editor_property('CurrentQuestionIndex') == 1
            click(wrong.get_user_widget_object(), ok)
            click(quiz, q2); click(quiz, q2)
            assert manager.get_training_stage() == stages.TRANSFER and quiz.get_editor_property('CurrentQuestionIndex') == 2
            log('PASS: complete transfer video, corrected quiz, wrong-answer retry and native transfer handoff')
            save()
            unreal.unregister_slate_post_tick_callback(handle)
            unreal.SystemLibrary.execute_console_command(world, 'cna.Guided.SmokeTransfer')
            phase = 5
    except Exception:
        log('FAIL: phase {} {}'.format(phase, traceback.format_exc()))
        save()
        unreal.unregister_slate_post_tick_callback(handle)
        if world: unreal.SystemLibrary.execute_console_command(world, 'quit')

handle = unreal.register_slate_post_tick_callback(tick)
