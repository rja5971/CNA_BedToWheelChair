"""Create the translucent floor circle used by the editor-configured lesson boundary."""
import unreal
path = '/Game/PatientSetup/Guidance/M_LessonWalkArea'
material = unreal.load_asset(path)
if material is None:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'M_LessonWalkArea', '/Game/PatientSetup/Guidance', unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', True)
    def node(cls, x, y):
        return unreal.MaterialEditingLibrary.create_material_expression(material, cls, x, y)
    def wire(a, output, b, pin):
        assert unreal.MaterialEditingLibrary.connect_material_expressions(a, output, b, pin), (output, pin)
    uv = node(unreal.MaterialExpressionTextureCoordinate, -800, 0)
    centre = node(unreal.MaterialExpressionSubtract, -600, 0)
    centre.set_editor_property('const_b', .5)
    wire(uv, '', centre, 'A')
    distance = node(unreal.MaterialExpressionLength, -400, 0)
    wire(centre, '', distance, '')
    # Outside radius .5 is transparent. A .015-wide edge outlines a lightly filled disk.
    outer = node(unreal.MaterialExpressionIf, -200, 0)
    outer.set_editor_property('const_b', .5)
    wire(distance, '', outer, 'A')
    zero = node(unreal.MaterialExpressionConstant, -400, 250)
    zero.set_editor_property('r', 0.)
    fill = node(unreal.MaterialExpressionIf, -200, 200)
    fill.set_editor_property('const_b', .485)
    wire(distance, '', fill, 'A')
    edge = node(unreal.MaterialExpressionConstant, -400, 350)
    edge.set_editor_property('r', .45)
    interior = node(unreal.MaterialExpressionConstant, -400, 450)
    interior.set_editor_property('r', .07)
    for pin, value in [('A > B', edge), ('A == B', edge), ('A < B', interior)]:
        wire(value, '', fill, pin)
    wire(zero, '', outer, 'A > B')
    wire(fill, '', outer, 'A == B'); wire(fill, '', outer, 'A < B')
    color = node(unreal.MaterialExpressionConstant3Vector, -200, -200)
    color.set_editor_property('constant', unreal.LinearColor(0., .45, .65, 1.))
    assert unreal.MaterialEditingLibrary.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert unreal.MaterialEditingLibrary.connect_material_property(outer, '', unreal.MaterialProperty.MP_OPACITY)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
unreal.log('LESSON_WALK_AREA ' + material.get_path_name())
