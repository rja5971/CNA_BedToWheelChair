import unreal, time, traceback
start=time.monotonic()
index=0
ready=0.
stages=[0,2,6,7]
def tick(delta):
 global index,ready
 world=unreal.find_object(None,'/Game/Project/Maps/CNA_Map_01.CNA_Map_01')
 if not world: return
 try:
  assert time.monotonic()-start<40, 'arrival timeout'
  managers=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.TransferManagerActor)
  if not managers: return
  manager=managers[0]
  assert manager.get_training_stage()!=unreal.GuidedTrainingStage.ERROR, 'arrival failed'
  if time.monotonic()<ready or time.monotonic()-start<3: return
  if not manager.is_lesson_boundary_active() or not unreal.CNAReliabilityLibrary.is_training_movement_allowed(manager): return
  pawn=unreal.GameplayStatics.get_player_pawn(world,0)
  camera=pawn.get_editor_property('Camera')
  unreal.log('ARRIVAL_LATER actor='+str(pawn.get_actor_rotation())+' camera='+str(camera.get_world_rotation())+' forward='+str(camera.get_forward_vector()))
  unreal.log('ARRIVAL_POSITION view='+str(camera.get_world_location())+' actor='+str(pawn.get_actor_location()))
  for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor):
   if a.get_class().get_name()=='BP_QuizActor_C': unreal.log('ARRIVAL_QUIZ center='+str(a.get_editor_property('QuizPanel').get_world_location()))
  unreal.log('FOCUSED_ARRIVAL_CHECK stage='+str(stages[index]))
  unreal.SystemLibrary.execute_console_command(world,'cna.Guided.TestMovement')
  index+=1
  if index==len(stages):
   unreal.log('FOCUSED_ARRIVAL_CHECK finished all four stages')
   unreal.unregister_slate_post_tick_callback(handle)
   unreal.SystemLibrary.execute_console_command(world,'quit')
   return
  unreal.SystemLibrary.execute_console_command(world,'cna.Guided.TestArrivalStage '+str(stages[index]))
  ready=time.monotonic()+2.5
 except Exception:
  unreal.log_error('FOCUSED_ARRIVAL_CHECK FAIL '+traceback.format_exc())
  unreal.unregister_slate_post_tick_callback(handle)
  unreal.SystemLibrary.execute_console_command(world,'quit')
handle=unreal.register_slate_post_tick_callback(tick)
