"""피날레 등급별 발사 패턴을 다양하게 채운다. 별 모양 애셋도 만든다.

    UnrealEditor-Cmd.exe FestaFiesta.uproject -run=pythonscript
        -script="Tools/Finale/apply_volley_variety.py"

실제 불꽃놀이의 흐름을 따른다(장원재 2026-10-05 요청).
    대부분은 기본 불꽃(흩뿌림 · 동시 · 부챗살 · 일렬)
    → 가끔 모양 불꽃(알 하나가 터지며 하트 · 별 · 원을 그린다. 불티가 작다)
    → 후반에는 기본 불꽃 여러 발이 함께 올라가 하나씩 터지며 큰 하트 · 별을 그린다(도화선 차례).
진행률 칸(ShowProgressFrom)으로 순서를 낸다. S 는 3막이라 0.5 부터가 마무리 막이다.

발수 · 시간 · 높이 · 색은 손대지 않는다. 기존 하트 애셋은 그대로 쓴다.
"""
import math

import unreal

DA = "/Game/FestaFiesta/Fireworks/DA_FinaleShow"
HEART = "/Game/FestaFiesta/Fireworks/DA_FinaleShape_Heart"
STAR = "/Game/FestaFiesta/Fireworks/DA_FinaleShape_Star"
P = unreal.FF_FinaleVolleyPattern


def make_star():
    """다섯 꼭짓점 별. 정규화 좌표(긴 쪽 1, 가운데 원점)로 담는다(UFF_FinaleShapeData 주석)."""
    if unreal.EditorAssetLibrary.does_asset_exist(STAR):
        star = unreal.load_asset(STAR)
    else:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.FF_FinaleShapeData)
        star = tools.create_asset("DA_FinaleShape_Star", "/Game/FestaFiesta/Fireworks", unreal.FF_FinaleShapeData, factory)
    raw = []
    for i in range(10):
        radius = 1.0 if i % 2 == 0 else 0.42
        angle = math.radians(90.0 + 36.0 * i)
        raw.append((radius * math.cos(angle), radius * math.sin(angle)))
    xs = [p[0] for p in raw]
    ys = [p[1] for p in raw]
    scale = max(max(xs) - min(xs), max(ys) - min(ys))
    cx = (max(xs) + min(xs)) * 0.5
    cy = (max(ys) + min(ys)) * 0.5
    star.set_editor_property("points", [unreal.Vector2D((x - cx) / scale, (y - cy) / scale) for x, y in raw])
    star.set_editor_property("closed", True)
    star.set_editor_property("display_name", unreal.Text("별"))
    unreal.EditorAssetLibrary.save_loaded_asset(star)
    return star


def volley(pattern, count, delay=0.0, extent=6000.0, uniform=False, weight=1.0,
           progress=0.0, size=1.0, shape=None, stagger=False):
    v = unreal.FF_FinaleVolley()
    v.set_editor_property("pattern", pattern)
    v.set_editor_property("shell_count", count)
    v.set_editor_property("shell_delay_seconds", delay)
    v.set_editor_property("extent_cm", extent)
    v.set_editor_property("uniform_color", uniform)
    v.set_editor_property("weight", weight)
    v.set_editor_property("show_progress_from", progress)
    v.set_editor_property("size_multiplier", size)
    v.set_editor_property("stagger_by_fuse", stagger)
    if shape:
        v.set_editor_property("shape", shape)
    return v


def main():
    show = unreal.load_asset(DA)
    heart = unreal.load_asset(HEART)
    star = make_star()

    spec_s = show.get_editor_property("grade_s")
    spec_s.set_editor_property("volleys", [
        # 기본 불꽃 — 대부분
        volley(P.BURST, 6, weight=4.0),
        volley(P.SINGLE, 1, weight=2.0),
        volley(P.FAN, 9, delay=0.05, extent=12000.0, uniform=True, weight=1.4),
        volley(P.LINE, 7, delay=0.08, extent=14000.0, uniform=True, weight=1.0),
        # 모양 불꽃 — 가끔. 알 하나가 터지며 그린다(간격 0 → 알 하나).
        volley(P.RING, 16, extent=7000.0, uniform=True, weight=0.6, progress=0.08, size=0.5),
        volley(P.SHAPE, 16, extent=6000.0, uniform=True, weight=0.6, progress=0.08, size=0.45, shape=heart),
        volley(P.SHAPE, 16, extent=6500.0, uniform=True, weight=0.6, progress=0.08, size=0.45, shape=star),
        # 큰 모양 — 마무리 막. 여러 발이 함께 올라가 윤곽을 따라 하나씩 터진다.
        volley(P.SHAPE, 24, delay=0.09, extent=17000.0, uniform=True, weight=0.9, progress=0.5, shape=heart, stagger=True),
        volley(P.SHAPE, 20, delay=0.1, extent=18000.0, uniform=True, weight=0.9, progress=0.5, shape=star, stagger=True),
    ])
    # 기본 불꽃 종류를 섞는다. 모양 패턴은 한 차례 안에서 하나로 맞춰진다(FireVolley).
    effects = [unreal.load_asset("/Game/FestaFiesta/Fireworks/FireWorks_" + n) for n in ("S", "A", "B", "C")]
    spec_s.set_editor_property("burst_effects", effects)
    show.set_editor_property("grade_s", spec_s)

    spec_a = show.get_editor_property("grade_a")
    spec_a.set_editor_property("volleys", [
        volley(P.BURST, 5, weight=4.0),
        volley(P.SINGLE, 1, weight=2.0),
        volley(P.FAN, 7, delay=0.06, extent=9000.0, uniform=True, weight=1.2),
        volley(P.LINE, 5, delay=0.1, extent=10000.0, uniform=True, weight=0.8),
        volley(P.RING, 12, extent=6000.0, uniform=True, weight=0.5, progress=0.2, size=0.5),
        volley(P.SHAPE, 12, extent=5500.0, uniform=True, weight=0.5, progress=0.3, size=0.45, shape=heart),
    ])
    show.set_editor_property("grade_a", spec_a)

    spec_b = show.get_editor_property("grade_b")
    spec_b.set_editor_property("volleys", [
        volley(P.BURST, 4, weight=3.0),
        volley(P.SINGLE, 1, weight=2.0),
        volley(P.FAN, 5, delay=0.07, extent=7000.0, uniform=True, weight=0.8),
    ])
    show.set_editor_property("grade_b", spec_b)

    unreal.EditorAssetLibrary.save_loaded_asset(show)
    unreal.log("[피날레] 패턴 S %d · A %d · B %d 종 저장" % (
        len(spec_s.get_editor_property("volleys")), len(spec_a.get_editor_property("volleys")),
        len(spec_b.get_editor_property("volleys"))))


main()
