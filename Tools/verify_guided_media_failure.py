"""Rendered CNA_Map_01 test of recovery in the actual guided bathroom stage."""
import time, traceback
import unreal
start = time.monotonic()
phase = 0

def tick(dt):
    global phase
    world = unreal.find_object(None, '/Game/Project/Maps/CNA_Map_01.CNA_Map_01')
    if not world: return
    try:
        assert time.monotonic() - start < 25, 'Could not reach guided media recovery'
        managers = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TransferManagerActor)
        if not managers: return
        manager = managers[0]
        if phase == 0 and time.monotonic() - start > 2:
            actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.load_class(None, '/Game/Project/Blueprints/Actors/BP_QuizActor.BP_QuizActor_C'))
            quiz = actors[0].get_editor_property('QuizPanel').get_user_widget_object()
            quiz.call_method('BndEvt__WBP_QuizPanel_Option2Button_K2Node_ComponentBoundEvent_1_OnButtonClickedEvent__DelegateSignature')
            phase = 1
        elif phase == 1 and manager.get_training_stage() == unreal.GuidedTrainingStage.BATHROOM_VIDEO:
            unreal.unregister_slate_post_tick_callback(handle)
            unreal.SystemLibrary.execute_console_command(world, 'cna.Media.SmokeOpenFailure')
            phase = 2
    except Exception:
        unreal.log_error('GUIDED_MEDIA FAIL: ' + traceback.format_exc())
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.SystemLibrary.execute_console_command(world, 'quit')

handle = unreal.register_slate_post_tick_callback(tick)
