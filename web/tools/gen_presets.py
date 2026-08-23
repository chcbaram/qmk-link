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

★ 좌표와 키맵은 **같은 매크로**에서 와야 한다.

  처음에 layouts/default/<이름>/info.json 의 좌표에다 "그 레이아웃을 쓴다고
  적힌 키보드"의 기본 키맵을 붙였는데, 그 키보드의 LAYOUT 은 다른 매크로였다.
  키가 하나 더 많아서 **범례가 두 번째 행부터 한 칸씩 밀렸다** — 1.5U Tab 자리가
  비고 TAB 이 옆칸으로 갔다. 눈으로 봐야 알아채는 종류의 오류다.

  그래서 지금은
    · keyboard.json 에 LAYOUT_<이름> 이 있고
    · 그 키보드의 기본 키맵이 **그 매크로를 그대로 부르며**
    · 토큰 수 == 자리 수
  인 것만 고른다. 셋 다 맞아야 짝이 성립한다.

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


def keymap_tokens(path, macro="LAYOUT"):
    """<macro>(...) 의 인자를 순서대로 뽑는다. 중첩 괄호를 센다."""
    s = path.read_text()
    m = re.search(re.escape(macro) + r"\s*\(", s)
    if not m:
        raise ValueError("%s 를 못 찾았다: %s" % (macro, path))
    i = m.end() - 1
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


def build(label, layout_path, macro, keymap_path):
    d = json.loads((QMK / layout_path).read_text())
    L = d["layouts"][macro]["layout"]
    toks = keymap_tokens(QMK / keymap_path, macro)

    # ★ 여기서 안 맞으면 범례가 밀린다. 조용히 넘어가지 않는다.
    if len(toks) != len(L):
        raise ValueError("%s : 자리 %d 개인데 키맵 토큰이 %d 개다"
                         % (label, len(L), len(toks)))

    return {"name": "%s (%d)" % (label, len(L)), "keys": len(L),
            "layout": to_kle(L, [norm(t) for t in toks])}


def find_pairs(names):
    """
    커뮤니티 레이아웃 이름마다 (좌표, 키맵) 짝을 찾는다.

    조건 셋을 다 만족해야 한다 — 매크로 이름이 같고, 기본 키맵이 그 매크로를
    부르고, 토큰 수가 자리 수와 같다. 하나라도 어긋나면 범례가 밀린다.
    """
    found = {}

    for kb in (QMK / "keyboards").rglob("*.json"):
        if kb.name not in ("keyboard.json", "info.json"):
            continue
        try:
            d = json.loads(kb.read_text())
        except Exception:                            # noqa: BLE001
            continue

        for name in (d.get("community_layouts") or []):
            if name not in names or name in found:
                continue

            macro = "LAYOUT_" + name
            lay = (d.get("layouts") or {}).get(macro)
            km = kb.parent / "keymaps" / "default" / "keymap.c"
            if not lay or not km.exists():
                continue

            try:
                toks = keymap_tokens(km, macro)
            except Exception:                        # noqa: BLE001
                continue
            if len(toks) != len(lay["layout"]):      # ★ 이 검사가 밀림을 막는다
                continue

            found[name] = (kb.relative_to(QMK), macro, km.relative_to(QMK))

    return found


# (표시 이름, 커뮤니티 레이아웃 이름)
COMMUNITY = [
    ("60% ANSI",      "60_ansi"),
    ("60% HHKB",      "60_hhkb"),
    ("65% ANSI",      "65_ansi"),
    ("75% ANSI",      "75_ansi"),
    ("TKL ANSI",      "tkl_ansi"),
    ("풀사이즈 ANSI", "fullsize_ansi"),
]

# 커뮤니티 레이아웃이 아닌 것 — 키보드 자기 정의를 그대로 쓴다
OWN = [
    ("HHKB Lite 2",
     "keyboards/hhkb_lite_2/keyboard.json", "LAYOUT",
     "keyboards/hhkb_lite_2/keymaps/default/keymap.c"),
]


def main():
    out = []

    for label, path, macro, km in OWN:
        out.append(build(label, path, macro, km))

    pairs = find_pairs({n for _, n in COMMUNITY})
    for label, name in COMMUNITY:
        if name not in pairs:
            print("// 짝을 못 찾음 : %s" % name, file=sys.stderr)
            continue
        kb, macro, km = pairs[name]
        out.append(build(label, kb, macro, km))
        print("//   %-14s <- %s  (%s)" % (name, kb, km), file=sys.stderr)

    print("// 생성물 — web/tools/gen_presets.py 가 만든다. 손으로 고치지 않는다.")
    print("//")
    print("// QMK 의 레이아웃 좌표 + 그 레이아웃을 쓰는 키보드의 기본 키맵에서 뽑았다.")
    print("// 손으로 그리면 1U 폭 하나만 틀려도 마법사가 엉뚱한 자리를 가리킨다.")
    print("export const PRESETS = " + json.dumps(out, ensure_ascii=False, indent=1) + ";")


if __name__ == "__main__":
    main()
