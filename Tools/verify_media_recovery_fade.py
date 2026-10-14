"""Recover from a real guided media failure after walking outside its circle."""
import unreal,time,traceback
start=time.monotonic()
phase=0
ready=0.
def tick(dt):
 global phase,ready
 world=unreal.find_object(None,'/Game/Project/Maps/CNA_Map_01.CNA_Map_01')
 if not world:return
 try:
  assert time.monotonic()-start<40,'recovery timeout'
  managers=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.TransferManagerActor)
  if not managers or time.monotonic()<ready:return
  m=managers[0]
  pawn=unreal.GameplayStatics.get_player_pawn(world,0)
  actors=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor)
  if phase==0:
   if time.monotonic()-start<3 or not unreal.CNAReliabilityLibrary.is_training_movement_allowed(m):return
   unreal.SystemLibrary.execute_console_command(world,'cna.Media.ForceOpenFailure 1')
   quiz=next(a for a in actors if a.get_class().get_name()=='BP_QuizActor_C')
   quiz.get_editor_property('QuizPanel').get_user_widget_object().call_method('BndEvt__WBP_QuizPanel_Option2Button_K2Node_ComponentBoundEvent_1_OnButtonClickedEvent__DelegateSignature')
   phase=1
  elif phase==1:
   if m.get_training_stage()!=unreal.GuidedTrainingStage.BATHROOM_VIDEO:return
   errors=[c.get_user_widget_object() for a in actors for c in a.get_components_by_class(unreal.WidgetComponent) if isinstance(c.get_user_widget_object(),unreal.TrainingMediaErrorWidget)]
   if not errors:return
   assert not m.is_lesson_boundary_active()
   pawn.add_actor_world_offset(unreal.Vector(1000,0,0),False,True)
   unreal.SystemLibrary.execute_console_command(world,'cna.Media.ForceOpenFailure 0')
   errors[0].call_method('RetryClicked');errors[0].call_method('RetryClicked')
   unreal.log('MEDIA_RECOVERY_FADE actual Retry callback after outside movement')
   phase=2
   ready=time.monotonic()+2.
  elif phase==2:
   if not m.is_lesson_boundary_active() or not unreal.CNAReliabilityLibrary.is_training_movement_allowed(m):return
   unreal.SystemLibrary.execute_console_command(world,'cna.Guided.TestMovement')
   phase=3
   ready=time.monotonic()+3.
  elif phase==3:
   assert m.get_training_stage()==unreal.GuidedTrainingStage.BATHROOM_VIDEO
   assert m.is_lesson_boundary_active()
   assert unreal.CNAReliabilityLibrary.is_training_movement_allowed(m)
   unreal.log('MEDIA_RECOVERY_FADE PASS: restored circle once without playback/fade loop')
   unreal.unregister_slate_post_tick_callback(handle)
   unreal.SystemLibrary.execute_console_command(world,'quit')
 except Exception:
  unreal.log_error('MEDIA_RECOVERY_FADE FAIL '+traceback.format_exc())
  unreal.unregister_slate_post_tick_callback(handle)
  unreal.SystemLibrary.execute_console_command(world,'quit')
handle=unreal.register_slate_post_tick_callback(tick)
