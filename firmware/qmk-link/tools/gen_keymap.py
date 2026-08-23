#!/usr/bin/env python3
"""
KLE 레이아웃 하나를 단일 진실 원본으로 삼아 VIA 정의 JSON 을 만든다.

    keyboards/<모델>/layout-kle.json   <- 손으로 편집하는 건 이것 하나
    keyboards/<모델>/menus.json        <- 커스텀 메뉴 (손으로 씀)
       └──▶ keyboards/<모델>/layout-via.json   VIA 앱에 넣는 정의 (생성물)

★ 이 보드의 매트릭스 좌표는 HID usage 다.

    row = usage >> 4,  col = usage & 0x0F

  물리 매트릭스가 없기 때문이다 — USB-A 에 꽂힌 키보드가 보낸 usage 를 그대로
  좌표로 쓴다 (src/ap/modules/link). 그래서 KLE 범례에는 주소가 아니라 **키 이름**
  을 적고, 이 스크립트가 이름 -> usage -> "row,col" 로 바꾼다.
  주소를 손으로 적으면 반드시 어긋난다.

사용법
    python3 tools/gen_keymap.py            # KLE + menus -> layout-via.json
    python3 tools/gen_keymap.py --show     # 매핑을 표로만 본다
    python3 tools/gen_keymap.py --check    # 생성물이 최신인지만 확인 (CI 용)

의존성 없음.
"""

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BOARDS = ROOT / "keyboards"
DEFAULT_BOARD = "qmk-link"

VID, PID = "0x0483", "0x5305"
ROWS, COLS = 16, 16


# ── HID Keyboard/Keypad usage 표 (HUT 1.12 §10) ──────────────────────────
#
# 이름은 QMK 의 KC_ 별칭에서 접두어만 뺀 것이다. KLE 범례에 이 이름을 적는다.
def _usage_table():
    u = {}
    for i, c in enumerate("ABCDEFGHIJKLMNOPQRSTUVWXYZ"):
        u[c] = 0x04 + i
    for i, c in enumerate("1234567890"):
        u[c] = 0x1E + i
    u.update(ENT=0x28, ESC=0x29, BSPC=0x2A, TAB=0x2B, SPC=0x2C,
             MINS=0x2D, EQL=0x2E, LBRC=0x2F, RBRC=0x30, BSLS=0x31,
             NUHS=0x32, SCLN=0x33, QUOT=0x34, GRV=0x35,
             COMM=0x36, DOT=0x37, SLSH=0x38, CAPS=0x39)
    for i in range(12):
        u["F%d" % (i + 1)] = 0x3A + i
    u.update(PSCR=0x46, SCRL=0x47, PAUS=0x48, INS=0x49, HOME=0x4A, PGUP=0x4B,
             DEL=0x4C, END=0x4D, PGDN=0x4E, RGHT=0x4F, LEFT=0x50, DOWN=0x51,
             UP=0x52, NUM=0x53, PSLS=0x54, PAST=0x55, PMNS=0x56, PPLS=0x57,
             PENT=0x58)
    for i in range(9):
        u["P%d" % (i + 1)] = 0x59 + i
    u.update(P0=0x62, PDOT=0x63, NUBS=0x64, APP=0x65, PWR=0x66, PEQL=0x67)
    for i in range(12):
        u["F%d" % (i + 13)] = 0x68 + i          # F13 ~ F24
    u.update(EXEC=0x74, HELP=0x75, MENU=0x76, SLCT=0x77, STOP=0x78, AGIN=0x79,
             UNDO=0x7A, CUT=0x7B, COPY=0x7C, PSTE=0x7D, FIND=0x7E,
             MUTE=0x7F, VOLU=0x80, VOLD=0x81, PCMM=0x85)
    for i in range(9):
        u["INT%d" % (i + 1)] = 0x87 + i         # INT1 ~ INT9
    for i in range(9):
        u["LNG%d" % (i + 1)] = 0x90 + i         # LNG1 ~ LNG9
    u.update(LCTL=0xE0, LSFT=0xE1, LALT=0xE2, LGUI=0xE3,
             RCTL=0xE4, RSFT=0xE5, RALT=0xE6, RGUI=0xE7)
    return u


USAGE = _usage_table()
NAME_OF = {v: k for k, v in USAGE.items()}


def addr_of(name):
    """키 이름 -> VIA 범례 "row,col" """
    if name not in USAGE:
        raise KeyError(name)
    u = USAGE[name]
    return "%d,%d" % (u >> 4, u & 0x0F)


def board_dir(name):
    d = BOARDS / name
    if not d.is_dir():
        sys.exit("키보드 폴더가 없다: %s" % d)
    return d


def load(path):
    try:
        return json.loads(path.read_text())
    except FileNotFoundError:
        sys.exit("파일이 없다: %s" % path)
    except json.JSONDecodeError as e:
        sys.exit("%s : JSON 오류 %s" % (path.name, e))


def build(kle_doc, menus_doc, name):
    """KLE 범례(키 이름)를 주소로 바꾼 VIA 정의를 만든다."""
    out_rows = []
    seen = {}
    unknown = []

    for r, row in enumerate(kle_doc["layout"]):
        out = []
        for item in row:
            if isinstance(item, dict):
                # KLE 속성(x/y/w/h ...)은 그대로 통과시킨다
                out.append(item)
                continue

            key = str(item)
            try:
                addr = addr_of(key)
            except KeyError:
                unknown.append((r, key))
                continue

            if addr in seen:
                sys.exit("좌표 중복: %s 와 %s 가 둘 다 %s (usage 0x%02X)"
                         % (seen[addr], key, addr, USAGE[key]))
            seen[addr] = key
            out.append(addr)
        out_rows.append(out)

    if unknown:
        for r, k in unknown:
            print("  %d행 : 모르는 키 이름 '%s'" % (r, k), file=sys.stderr)
        sys.exit("layout-kle.json 의 키 이름을 고쳐라 (usage 표는 이 스크립트 안에 있다)")

    via = {
        "name": name,
        "vendorId": VID,
        "productId": PID,
        "matrix": {"rows": ROWS, "cols": COLS},
        "layouts": {"keymap": out_rows},
    }
    if menus_doc and menus_doc.get("menus"):
        via["menus"] = menus_doc["menus"]

    return via, seen


def show(seen):
    print("%-6s %-6s %s" % ("이름", "usage", "좌표"))
    for addr, name in sorted(seen.items(), key=lambda kv: USAGE[kv[1]]):
        print("%-6s 0x%02X   %s" % (name, USAGE[name], addr))
    print("\n총 %d 키 / %d 칸 (%d x %d)" % (len(seen), ROWS * COLS, ROWS, COLS))

    used = {USAGE[n] for n in seen.values()}
    miss = [NAME_OF[u] for u in sorted(NAME_OF) if u not in used]
    if miss:
        print("\nKLE 에 없는 usage (동작은 하지만 VIA 에서 못 고친다):")
        print("  " + " ".join(miss))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--board", default=DEFAULT_BOARD)
    ap.add_argument("--show", action="store_true", help="매핑만 출력하고 쓰지 않는다")
    ap.add_argument("--check", action="store_true", help="생성물이 최신인지 확인만")
    args = ap.parse_args()

    d = board_dir(args.board)
    kle = load(d / "layout-kle.json")
    menus_path = d / "menus.json"
    menus = load(menus_path) if menus_path.exists() else None

    via, seen = build(kle, menus, args.board.upper())
    text = json.dumps(via, indent=2, ensure_ascii=False) + "\n"
    out = d / "layout-via.json"

    if args.show:
        show(seen)
        return

    if args.check:
        if not out.exists() or out.read_text() != text:
            sys.exit("layout-via.json 이 낡았다 — tools/gen_keymap.py 를 다시 돌려라")
        print("layout-via.json 최신")
        return

    out.write_text(text)
    print("%s  (%d 키)" % (out.relative_to(ROOT), len(seen)))
    if menus:
        print("  커스텀 메뉴 %d 개 포함" % len(menus["menus"]))


if __name__ == "__main__":
    main()
