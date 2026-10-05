"""피날레 쏘아 올리기에 쓰는 머티리얼 두 개를 만든다(이미 있으면 다시 만든다).

    UnrealEditor-Cmd.exe FestaFiesta.uproject -run=pythonscript
        -script="Tools/Finale/create_launch_assets.py"

M_FinaleRiseTrail  꼬리. 알(HeadPosition)에서 멀어질수록 어두워진다. 메시 UV 에 기대지 않으려고
                   월드 거리로 그라데이션을 낸다 — 기본 원뿔의 UV 방향이 정해져 있지 않다.
M_FinaleRiseGlow   알과 발사 번쩍임의 둥근 빛. 빌보드에 쓴다. Phase 로 발마다 반짝임 박자를 어긋낸다.

둘 다 Unlit · Additive · TwoSided. 파라미터 이름은 C++ 의 FFF_FinaleLaunchParams 와 같아야 한다.
"""
import unreal

FOLDER = "/Game/FestaFiesta/Fireworks/Launch"
MEL = unreal.MaterialEditingLibrary


def fresh_material(name):
    path = "%s/%s" % (FOLDER, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    return material


def node(material, cls, x, y, **props):
    expression = MEL.create_material_expression(material, cls, x, y)
    for key, value in props.items():
        expression.set_editor_property(key, value)
    return expression


def link(a, a_out, b, b_in):
    assert MEL.connect_material_expressions(a, a_out, b, b_in), "연결 실패 %s.%s -> %s.%s" % (a, a_out, b, b_in)


def scalar(material, name, value, x, y):
    return node(material, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)


def vector(material, name, color, x, y):
    return node(material, unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=color)


def rgb(material, source, x, y):
    mask = node(material, unreal.MaterialExpressionComponentMask, x, y, r=True, g=True, b=True, a=False)
    link(source, "", mask, "")
    return mask


def build_trail():
    m = fresh_material("M_FinaleRiseTrail")
    head = rgb(m, vector(m, "HeadPosition", unreal.LinearColor(0, 0, 0, 0), -1400, -200), -1200, -200)
    world = node(m, unreal.MaterialExpressionWorldPosition, -1400, -400)
    distance = node(m, unreal.MaterialExpressionDistance, -1000, -300)
    link(world, "", distance, "A")
    link(head, "", distance, "B")

    length = scalar(m, "TrailLength", 3000.0, -1000, -100)
    ratio = node(m, unreal.MaterialExpressionDivide, -800, -250)
    link(distance, "", ratio, "A")
    link(length, "", ratio, "B")

    # 알 쪽 1, 꼬리 끝 0. 제곱해서 꼬리 끝이 빨리 사그라들게 한다.
    clamp = node(m, unreal.MaterialExpressionSaturate, -650, -250)
    link(ratio, "", clamp, "")
    near = node(m, unreal.MaterialExpressionOneMinus, -520, -250)
    link(clamp, "", near, "")
    falloff = node(m, unreal.MaterialExpressionPower, -380, -250, const_exponent=2.0)
    link(near, "", falloff, "Base")

    intensity = scalar(m, "Intensity", 40.0, -520, -50)
    fade = scalar(m, "Fade", 1.0, -520, 50)
    color = rgb(m, vector(m, "Color", unreal.LinearColor(1.0, 0.62, 0.3, 1.0), -700, 150), -520, 150)

    a = node(m, unreal.MaterialExpressionMultiply, -220, -200)
    link(falloff, "", a, "A")
    link(intensity, "", a, "B")
    b = node(m, unreal.MaterialExpressionMultiply, -80, -150)
    link(a, "", b, "A")
    link(fade, "", b, "B")
    c = node(m, unreal.MaterialExpressionMultiply, 60, -100)
    link(b, "", c, "A")
    link(color, "", c, "B")
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


def build_glow():
    m = fresh_material("M_FinaleRiseGlow")
    uv = node(m, unreal.MaterialExpressionTextureCoordinate, -1400, -300)
    center = node(m, unreal.MaterialExpressionConstant2Vector, -1400, -150, r=0.5, g=0.5)
    distance = node(m, unreal.MaterialExpressionDistance, -1200, -250)
    link(uv, "", distance, "A")
    link(center, "", distance, "B")

    # 가운데 1, 가장자리 0. 세제곱으로 가운데만 뜨겁게 한다.
    double = node(m, unreal.MaterialExpressionMultiply, -1050, -250, const_b=2.0)
    link(distance, "", double, "A")
    near = node(m, unreal.MaterialExpressionOneMinus, -920, -250)
    link(double, "", near, "")
    clamp = node(m, unreal.MaterialExpressionSaturate, -800, -250)
    link(near, "", clamp, "")
    core = node(m, unreal.MaterialExpressionPower, -680, -250, const_exponent=3.0)
    link(clamp, "", core, "Base")

    # 반짝임: 0.85 + 0.15 * sin(시간 * 6 + Phase). 발마다 Phase 가 달라 같이 깜빡이지 않는다.
    time = node(m, unreal.MaterialExpressionTime, -1400, 50)
    speed = node(m, unreal.MaterialExpressionMultiply, -1250, 50, const_b=6.0)
    link(time, "", speed, "A")
    phase = scalar(m, "Phase", 0.0, -1400, 150)
    shifted = node(m, unreal.MaterialExpressionAdd, -1100, 80)
    link(speed, "", shifted, "A")
    link(phase, "", shifted, "B")
    sine = node(m, unreal.MaterialExpressionSine, -960, 80)
    link(shifted, "", sine, "")
    swing = node(m, unreal.MaterialExpressionMultiply, -830, 80, const_b=0.15)
    link(sine, "", swing, "A")
    twinkle = node(m, unreal.MaterialExpressionAdd, -700, 80, const_b=0.85)
    link(swing, "", twinkle, "A")

    intensity = scalar(m, "Intensity", 40.0, -520, -50)
    fade = scalar(m, "Fade", 1.0, -520, 50)
    color = rgb(m, vector(m, "Color", unreal.LinearColor(1.0, 0.62, 0.3, 1.0), -700, 250), -520, 250)

    a = node(m, unreal.MaterialExpressionMultiply, -520, -200)
    link(core, "", a, "A")
    link(twinkle, "", a, "B")
    b = node(m, unreal.MaterialExpressionMultiply, -360, -150)
    link(a, "", b, "A")
    link(intensity, "", b, "B")
    c = node(m, unreal.MaterialExpressionMultiply, -220, -100)
    link(b, "", c, "A")
    link(fade, "", c, "B")
    d = node(m, unreal.MaterialExpressionMultiply, -80, -50)
    link(c, "", d, "A")
    link(color, "", d, "B")
    MEL.connect_material_property(d, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


trail = build_trail()
glow = build_glow()
for m in (trail, glow):
    names = [str(n) for n in MEL.get_scalar_parameter_names(m)] + [str(n) for n in MEL.get_vector_parameter_names(m)]
    unreal.log("[피날레] %s 파라미터: %s" % (m.get_name(), ", ".join(sorted(names))))
