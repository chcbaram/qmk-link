#!/usr/bin/env python3
"""
web/presets.js 를 만든다.

★ 배열을 손으로 그리지 않는다.

  QMK / vial-qmk 의 레이아웃 좌표와, **그 레이아웃을 쓰는 키보드의 기본 키맵**을
  짝지어 뽑는다. 좌표는 정확하고 범례는 그 키보드가 실제로 쓰는 이름이다.
  손으로 그리면 1U 폭 하나만 틀려도 마법사에서 엉뚱한 자리를 누르게 된다.

  QMK 의 layout 배열 순서 = LAYOUT() 매크로의 인자 순서다. 그래서 키맵 토큰을
  순서대로 zip 하면 좌표 <-> 이름이 맞는다. (행 단위로 줄바꿈된 것과 무관하다 —
  HHKB Lite 2 는 4번째 줄이 12개인데 그 행의 자리는 13개다. 순서로 zip 해야 맞다)

사용법
    python3 web/tools/gen_presets.py > web/presets.js

vial-qmk 경로가 필요하다 (기본 ~/hdd/git/vial-qmk).
"""

import json
import pathlib
import re
import sys

QMK = pathlib.Path(sys.argv[1] if len(sys.argv) > 1
                   else pathlib.Path.home() / "hdd/git/vial-qmk")


# ── QMK 키코드 -> 마법사가 쓰는 이름 (gen_keymap.py 의 표와 같은 이름) ──
def _kc_map():
    m = {
        "ESC": "ESC", "GRV": "GRV", "MINS": "MINS", "EQL": "EQL", "BSPC": "BSPC",
        "TAB": "TAB", "LBRC": "LBRC", "RBRC": "RBRC", "BSLS": "BSLS", "CAPS": "CAPS",
        "SCLN": "SCLN", "QUOT": "QUOT", "ENT": "ENT", "LSFT": "LSFT", "COMM": "COMM",
        "DOT": "DOT", "SLSH": "SLSH", "RSFT": "RSFT", "LCTL": "LCTL", "LGUI": "LGUI",
        "LALT": "LALT", "SPC": "SPC", "RALT": "RALT", "RGUI": "RGUI", "RCTL": "RCTL",
        "APP": "APP", "DEL": "DEL", "INS": "INS", "HOME": "HOME", "END": "END",
        "PGUP": "PGUP", "PGDN": "PGDN", "UP": "UP", "DOWN": "DOWN", "LEFT": "LEFT",
        "RGHT": "RGHT", "PSCR": "PSCR", "SCRL": "SCRL", "PAUS": "PAUS", "NUM": "NUM",
        "PSLS": "PSLS", "PAST": "PAST", "PMNS": "PMNS", "PPLS": "PPLS", "PENT": "PENT",
        "PDOT": "PDOT", "NUBS": "NUBS", "NUHS": "NUHS", "MUTE": "MUTE",
        "VOLU": "VOLU", "VOLD": "VOLD",
    }
    for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789":
        m[c] = c
    for i in range(1, 25):
        m["F%d" % i] = "F%d" % i
    for i in range(0, 10):
        m["P%d" % i] = "P%d" % i
        m["KP_%d" % i] = "P%d" % i
    return m


KC = _kc_map()


def norm(tok):
    """KC_xxx -> 이름. MO(1) · QK_BOOT 같은 것은 이름이 없다 (빈 칸으로 둔다)."""
    m = re.match(r"^\s*KC_([A-Z0-9_]+)\s*$", tok)
    return KC.get(m.group(1)) if m else None


def keymap_tokens(path):
    """첫 LAYOUT(...) 의 인자를 순서대로 뽑는다. 중첩 괄호를 센다."""
    s = path.read_text()
    i = s.index("LAYOUT")
    i = s.index("(", i)
    depth = 0
    for j in range(i, len(s)):
        if s[j] == "(":
            depth += 1
        elif s[j] == ")":
            depth -= 1
            if depth == 0:
                break

    body = re.sub(r"/\*.*?\*/", "", s[i + 1:j], flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)

    out, d, cur = [], 0, ""
    for ch in body:
        if ch == "(":
            d += 1
        elif ch == ")":
            d -= 1
        if ch == "," and d == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return out


def to_kle(layout, names):
    """QMK 의 {x,y,w,h} 목록을 KLE 행 배열로. 델타로 적는다."""
    rows, cur = [], []
    prev_y, prev_x = None, 0

    for k, nm in zip(layout, names):
        y, x = k["y"], k["x"]
        w, h = k.get("w", 1), k.get("h", 1)

        if prev_y is None or y != prev_y:
            if cur:
                rows.append(cur)
            cur, prev_x = [], 0
            if prev_y is not None and round(y - prev_y, 3) != 1:
                cur.append({"y": round(y - prev_y - 1, 3)})
            prev_y = y

        dx = round(x - prev_x, 3)
        # QMK 의 분수 폭(0.916 등) 때문에 -0.003 같은 값이 나온다. 0 으로 본다.
        if abs(dx) > 0.01:
            cur.append({"x": dx})
        if w != 1:
            cur.append({"w": w})
        if h != 1:
            cur.append({"h": h})
        cur.append(nm or "")
        prev_x = x + w

    if cur:
        rows.append(cur)
    return rows


def build(label, layout_path, keymap_path, layout_key=None):
    d = json.loads((QMK / layout_path).read_text())
    key = layout_key or list(d["layouts"].keys())[0]
    L = d["layouts"][key]["layout"]

    names = [norm(t) for t in keymap_tokens(QMK / keymap_path)][:len(L)]
    names += [None] * (len(L) - len(names))

    return {"name": label, "keys": len(L), "layout": to_kle(L, names)}


ITEMS = [
    # (표시 이름, 좌표 json, 범례를 가져올 기본 키맵)
    ("HHKB Lite 2 (64)",
     "keyboards/hhkb_lite_2/keyboard.json",
     "keyboards/hhkb_lite_2/keymaps/default/keymap.c"),
    ("60% ANSI (61)",
     "layouts/default/60_ansi/info.json",
     "keyboards/iriskeyboards/keymaps/default/keymap.c"),
    ("60% HHKB (60)",
     "layouts/default/60_hhkb/info.json",
     "keyboards/panc60/keymaps/default/keymap.c"),
    ("65% ANSI (68)",
     "layouts/default/65_ansi/info.json",
     "keyboards/zj68/keymaps/default/keymap.c"),
    ("75% ANSI (84)",
     "layouts/default/75_ansi/info.json",
     "keyboards/jolofsor/denial75/keymaps/default/keymap.c"),
    ("TKL ANSI (87)",
     "layouts/default/tkl_ansi/info.json",
     "keyboards/poker87c/keymaps/default/keymap.c"),
    ("풀사이즈 ANSI (104)",
     "layouts/default/fullsize_ansi/info.json",
     "keyboards/gh80_3000/keymaps/default/keymap.c"),
]


def main():
    out = []
    for item in ITEMS:
        try:
            out.append(build(*item))
        except Exception as e:                       # noqa: BLE001
            print("// 실패 %s : %s" % (item[0], e), file=sys.stderr)

    print("// 생성물 — web/tools/gen_presets.py 가 만든다. 손으로 고치지 않는다.")
    print("//")
    print("// QMK 의 레이아웃 좌표 + 그 레이아웃을 쓰는 키보드의 기본 키맵에서 뽑았다.")
    print("// 손으로 그리면 1U 폭 하나만 틀려도 마법사가 엉뚱한 자리를 가리킨다.")
    print("export const PRESETS = " + json.dumps(out, ensure_ascii=False, indent=1) + ";")


if __name__ == "__main__":
    main()
