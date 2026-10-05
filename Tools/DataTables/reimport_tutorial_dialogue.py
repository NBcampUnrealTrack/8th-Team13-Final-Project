"""Content/FestaFiesta/Data/Tutorial/DT_Dialogue_Tutorial.csv 를 DT_Dialogue_Tutorial 에 다시 넣는다.

CSV 만 고치고 이 스크립트(또는 에디터의 Reimport)를 안 돌리면 게임에 새 대사가 안 나온다.

    UnrealEditor-Cmd.exe FestaFiesta.uproject -run=pythonscript
        -script="Tools/DataTables/reimport_tutorial_dialogue.py"
"""
import csv
import io
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[2]
CSV = ROOT / "Content/FestaFiesta/Data/Tutorial/DT_Dialogue_Tutorial.csv"
DT = "/Game/FestaFiesta/Data/Tutorial/DT_Dialogue_Tutorial"


def main():
    table = unreal.load_asset(DT)
    assert isinstance(table, unreal.DataTable), DT

    text = CSV.read_text(encoding="utf-8-sig")
    if not unreal.DataTableFunctionLibrary.fill_data_table_from_csv_string(table, text):
        raise SystemExit("CSV 파싱 실패 — 열 구성이 FFF_DialogueRow 와 맞는지 확인할 것")

    unreal.EditorAssetLibrary.save_loaded_asset(table)

    # 기대값은 CSV 를 따로 읽어 만든다. 표가 CSV 의 행을 하나도 빠뜨리지 않고, 대사가 같은지 본다.
    rows = list(csv.DictReader(io.StringIO(text)))
    names = [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)]
    missing = [row["Name"] for row in rows if row["Name"] not in names]
    assert not missing, "표에 없는 행: %s" % missing

    column = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(table, "Text")
    by_name = dict(zip(names, column))
    changed = [row["Name"] for row in rows if by_name.get(row["Name"], "") != row["Text"]]
    assert not changed, "CSV 와 다른 대사: %s" % changed

    unreal.log("[DT] 튜토리얼 대사 %d 행 반영 완료" % len(names))


main()
