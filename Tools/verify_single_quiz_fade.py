"""Focused real-media completion regression: one quiz arrival fade while walking."""
import unreal, time, traceback
start=time.monotonic()
phase=0
ready=0.
bath_playing_at=0.
handle=None
quiz_baseline=None
def tick(delta):
 global phase,ready,bath_playing_at,quiz_baseline
 world=unreal.find_object(None,'/Game/Project/Maps/CNA_Map_01.CNA_Map_01')
 if not world:return
 try:
  assert time.monotonic()-start<50,'single-fade regression timeout'
  managers=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.TransferManagerActor)
  if not managers:return
  m=managers[0]
  assert m.get_training_stage()!=unreal.GuidedTrainingStage.ERROR,'arrival error'
  if time.monotonic()<ready:return
  pawn=unreal.GameplayStatics.get_player_pawn(world,0)
  camera=pawn.get_editor_property('Camera')
  quiz_cls=unreal.load_class(None,'/Game/Project/Blueprints/Actors/BP_QuizActor.BP_QuizActor_C')
  quizzes=unreal.GameplayStatics.get_all_actors_of_class(world,quiz_cls)
  if not quizzes:return
  quiz_actor=quizzes[0]
  panel=quiz_actor.get_editor_property('QuizPanel')
  if phase==0:
   if time.monotonic()-start<3 or not m.is_lesson_boundary_active() or not unreal.CNAReliabilityLibrary.is_training_movement_allowed(m):return
   wrong=quiz_actor.get_editor_property('PopupComponent')
   quiz_baseline=(quiz_actor.get_actor_scale3d(),panel.get_world_scale(),panel.get_editor_property('RelativeScale3D'),panel.get_draw_size(),wrong.get_world_scale(),wrong.get_editor_property('RelativeScale3D'),wrong.get_draw_size())
   unreal.log('QUIZ_SCALE_TEST authored quiz scale: '+str(quiz_baseline))
   # Use the real entry answer callback to reserve question 1.
   panel.get_user_widget_object().call_method('BndEvt__WBP_QuizPanel_Option2Button_K2Node_ComponentBoundEvent_1_OnButtonClickedEvent__DelegateSignature')
   phase=.5
   ready=time.monotonic()+2.
  elif phase==.5:
   if m.get_training_stage()!=unreal.GuidedTrainingStage.BATHROOM_VIDEO or not m.is_lesson_boundary_active():return
   if not unreal.CNAReliabilityLibrary.is_training_movement_allowed(m):return
   if bath_playing_at==0:
    bath_playing_at=time.monotonic()
    return
   if time.monotonic()-bath_playing_at<1.:return
   for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor):
    for c in a.get_components_by_class(unreal.TrainingMediaControllerComponent): c.call_method('MediaEnded')
   phase=.6
   ready=time.monotonic()+1.
  elif phase==.6:
   if m.get_training_stage()!=unreal.GuidedTrainingStage.BATHROOM_CONTINUE:return
   actors=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor)
   popups=[a for a in actors if a.get_class().get_name()=='BP_PopupActor_C']
   assert popups, 'Bathroom Continue popup must be retained'
   handler=next(a for a in actors if a.get_class().get_name()=='BP_MediaEventHandler_C')
   handler.call_method('OnContinueClicked_Event')
   unreal.log('SINGLE_FADE_TEST PASS: bathroom Continue popup and real room transition retained')
   phase=.7
   ready=time.monotonic()+2.
  elif phase==.7:
   if m.get_training_stage()!=unreal.GuidedTrainingStage.CONVERSATION:return
   unreal.SystemLibrary.execute_console_command(world,'cna.Guided.TestArrivalStage 6')
   phase=1
   ready=time.monotonic()+2.
  elif phase==1:
   if not m.is_lesson_boundary_active() or not unreal.CNAReliabilityLibrary.is_training_movement_allowed(m):return
   centre=next(a.get_editor_property('ScreenMesh').get_world_location() for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor) if a.get_name()=='BP_MediaDisplay_C_0')
   view=camera.get_world_location()
   dx,dy=centre.x-view.x,centre.y-view.y
   length=(dx*dx+dy*dy)**.5
   inward=unreal.Vector(dx/length,dy/length,0)
   pawn.add_actor_world_offset(inward*50,False,True)
   unreal.log('SINGLE_FADE_TEST walking at patient video end')
   phase=2
   ready=time.monotonic()+1.
  elif phase==2:
   if m.get_training_stage()==unreal.GuidedTrainingStage.TRANSFER_VIDEO:
    # Dispatch the controller's real end callback after playback has progressed.
    for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor):
     for c in a.get_components_by_class(unreal.TrainingMediaControllerComponent):
      c.call_method('MediaEnded')
    ready=time.monotonic()+.1
    return
   if m.get_training_stage()!=unreal.GuidedTrainingStage.PATIENT_QUIZ:return
   if not unreal.CNAReliabilityLibrary.is_training_movement_allowed(m):return
   assert panel.is_visible()
   wrong=quiz_actor.get_editor_property('PopupComponent')
   current=(quiz_actor.get_actor_scale3d(),panel.get_world_scale(),panel.get_editor_property('RelativeScale3D'),panel.get_draw_size(),wrong.get_world_scale(),wrong.get_editor_property('RelativeScale3D'),wrong.get_draw_size())
   assert all(a.equals(b) for a,b in zip(current,quiz_baseline)), 'Patient quiz or wrong-answer popup changed scale: '+str(current)
   unreal.log('QUIZ_SCALE_TEST PASS: real patient quiz handoff preserves actor, quiz and popup scales and draw sizes')
   assert panel.get_user_widget_object().get_editor_property('CurrentQuestionIndex')==1
   unreal.SystemLibrary.execute_console_command(world,'cna.Guided.TestMovement')
   # Simulate tracked walking over the circle after the fade; tracking stays free.
   centre=panel.get_world_location()
   view=camera.get_world_location()
   dx,dy=view.x-centre.x,view.y-centre.y
   length=(dx*dx+dy*dy)**.5
   outward=unreal.Vector(dx/length,dy/length,0)
   pawn.add_actor_world_offset(outward*20,False,True)
   for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor):
    for c in a.get_components_by_class(unreal.TrainingMediaControllerComponent):
     c.call_method('MediaEnded'); c.call_method('MediaEnded')
   unreal.log('SINGLE_FADE_TEST walking after quiz arrival and repeating end callbacks')
   phase=3
   ready=time.monotonic()+6.
  elif phase==3:
   assert m.get_training_stage()==unreal.GuidedTrainingStage.PATIENT_QUIZ
   assert unreal.CNAReliabilityLibrary.is_training_movement_allowed(m)
   assert panel.get_user_widget_object().get_editor_property('CurrentQuestionIndex')==1
   wrong=quiz_actor.get_editor_property('PopupComponent')
   current=(quiz_actor.get_actor_scale3d(),panel.get_world_scale(),panel.get_editor_property('RelativeScale3D'),panel.get_draw_size(),wrong.get_world_scale(),wrong.get_editor_property('RelativeScale3D'),wrong.get_draw_size())
   assert all(a.equals(b) for a,b in zip(current,quiz_baseline)), 'Repeated completion changed quiz UI scale'
   unreal.log('QUIZ_SCALE_TEST PASS: walking and duplicate video completions preserve every quiz UI scale')
   unreal.log('SINGLE_FADE_TEST PASS: real video end, one quiz advance, continued walking and duplicate completion callbacks')
   unreal.unregister_slate_post_tick_callback(handle)
   unreal.SystemLibrary.execute_console_command(world,'quit')
 except Exception:
  unreal.log_error('SINGLE_FADE_TEST FAIL '+traceback.format_exc())
  unreal.unregister_slate_post_tick_callback(handle)
  unreal.SystemLibrary.execute_console_command(world,'quit')
handle=unreal.register_slate_post_tick_callback(tick)
