"""Validate saved integration without saving or modifying the training map."""
import unreal

context = unreal.load_asset("/Game/VRTemplate/Input/IMC_UIInteract")
assert len(context.get_editor_property("mappings")) == 8
for suffix in ("Left", "Right"):
    action = unreal.load_asset("/Game/VRTemplate/Input/Actions/IA_UIInteract_" + suffix)
    triggers = action.get_editor_property("triggers")
    assert len(triggers) == 1 and isinstance(triggers[0], unreal.InputTriggerDown)
    assert abs(triggers[0].get_editor_property("actuation_threshold") - 0.5) < 0.0001
menu = unreal.load_asset("/Game/VRTemplate/Input/IMC_Menu")
assert all("IA_Menu_Interact" not in mapping.get_editor_property("action").get_name() for mapping in menu.get_editor_property("mappings"))
player = unreal.load_asset("/Game/Project/Media/MP_Display")
assert not player.get_editor_property("play_on_open")

world = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Project/Maps/CNA_Map_01")
assert world
displays = []
for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    if actor.get_class().get_name() == "BP_MediaDisplay_C":
        data = actor.get_editor_property("ContentData")
        source = data.get_editor_property("MediaSource") if data else None
        media_player = actor.get_editor_property("MediaPlayer")
        unreal.log("CNA_VERIFY_DISPLAY {} data={} source={} player={}".format(actor.get_actor_label(), data, source, media_player))
        assert data and source and media_player == player
        assert source.get_editor_property("file_path").startswith("./Movies/")
        component = actor.get_editor_property("TextWidget")
        assert component and component.get_editor_property("widget_class")
        displays.append(actor)
assert len(displays) == 2, "Expected the bathroom and bedroom media displays"
unreal.log("CNA_RELIABILITY_VERIFY PASS: input mappings, media references, and both placed displays")
