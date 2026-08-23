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

# baram 자체 키보드 중 QMK 트리에 없는 것 (VIA 정의 + 행렬형 키맵).
# 없으면 그 프리셋만 건너뛴다 — 이 저장소 밖이라 없을 수 있다.
BARAM = pathlib.Path.home() / "hdd/git/baram-qmk-8k/src/ap/modules/qmk/keyboards/baram"


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
    """
    KC_xxx -> 이름.

    ★ 40% 처럼 레이어 키가 많은 배열은 그냥 두면 범례가 거의 다 빈다.
      범례는 "이 자리의 키를 누르라" 는 힌트일 뿐이고 좌표에 영향이 없으므로,
      감싸인 것 안쪽의 KC_ 를 꺼내 쓴다 — LT(1,KC_SPC) -> SPC.
      레이어 전환만 하는 것(MO/TG/TT/DF/OSL)은 FN 으로 적는다.
    """
    m = re.match(r"^\s*KC_([A-Z0-9_]+)\s*$", tok)
    if m:
        return KC.get(m.group(1))

    inner = re.findall(r"KC_([A-Z0-9_]+)", tok)
    for name in reversed(inner):                 # LT(1,KC_SPC) 는 뒤쪽이 실제 키다
        if name in KC:
            return KC[name]

    if re.search(r"\b(MO|TG|TT|DF|OSL|LM|LT)\s*\(", tok):
        return "FN"
    return None


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


def has_gap(layout):
    """
    한 행에서 앞 키의 오른쪽 끝과 다음 키의 왼쪽이 안 붙어 있으면 구멍이다.

    ★ 왜 보나 — 폭(w)이 빠진 정의가 흔하다.

      contra 의 LAYOUT_planck_mit 은 2U 스페이스를 **w 없이** 적어 두고 다음
      키를 한 칸 건너뛴 자리에 놓는다. 그대로 쓰면 마법사 화면에서 스페이스가
      1U 로 그려지고 그 옆에 빈 구멍이 생긴다. baram F1-40 / 45K 의 info.json 도
      같다 — 그쪽은 아예 매트릭스 표라 45키로 나온다.

      다만 TKL · 65% · 풀사이즈는 클러스터 사이가 실제로 떨어져 있다.
      그래서 이 검사는 **붙어 있어야 하는 배열(ortho)** 에만 건다.
    """
    rows = {}
    for k in layout:
        rows.setdefault(k["y"], []).append(k)

    for ks in rows.values():
        ks.sort(key=lambda a: a["x"])
        for a, b in zip(ks, ks[1:]):
            if round(b["x"] - (a["x"] + a.get("w", 1)), 3) > 0.001:
                return True
    return False


def matrix_keymap(path):
    """
    행렬형 키맵을 [행][열] 로 읽는다.

    baram 쪽 키보드는 LAYOUT(...) 이 아니라
        [0] = { {KC_A, ...}, {KC_B, ...} }
    로 적혀 있다. VIA 정의의 범례가 "행,열" 이므로 이 표로 이름을 찾는다.
    """
    s = re.sub(r"/\*.*?\*/", "", path.read_text(), flags=re.S)
    s = re.sub(r"//[^\n]*", "", s)

    m = re.search(r"\[\s*0\s*\]\s*=\s*\{", s)
    if not m:
        raise ValueError("[0] = { 를 못 찾았다: %s" % path)

    i, depth = m.end() - 1, 0
    for j in range(i, len(s)):
        if s[j] == "{":
            depth += 1
        elif s[j] == "}":
            depth -= 1
            if depth == 0:
                break

    rows = []
    for row in re.findall(r"\{([^{}]*)\}", s[i + 1:j]):
        out, d, cur = [], 0, ""
        for ch in row:
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
        rows.append(out)
    return rows


def build_via(label, via_json, keymap_c):
    """
    VIA 정의(KLE 그대로) + 행렬형 키맵으로 프리셋을 만든다.

    ★ VIA 정의의 범례는 **그 키보드의 매트릭스 좌표**다. 키 이름이 아니다.
      그대로 두면 마법사가 "0,0" 을 usage 0x00 으로 읽어 이미 배운 것처럼
      보인다. 키맵을 통해 이름으로 바꾼다.

    ★ 색(c) 같은 꾸밈은 버린다. 마법사는 x/y/w/h 만 본다.
    """
    d = json.loads(via_json.read_text())
    km = matrix_keymap(keymap_c)

    rows, n = [], 0
    for row in d["layouts"]["keymap"]:
        cur = []
        for it in row:
            if isinstance(it, dict):
                keep = {k: v for k, v in it.items() if k in ("x", "y", "w", "h")}
                if keep:
                    cur.append(keep)
                continue

            m = re.match(r"^(\d+),(\d+)$", str(it).strip())
            if not m:
                raise ValueError("%s : 범례가 좌표가 아니다 — %r" % (label, it))

            r, c = int(m.group(1)), int(m.group(2))
            if r >= len(km) or c >= len(km[r]):
                raise ValueError("%s : 키맵에 [%d][%d] 이 없다" % (label, r, c))

            cur.append(norm(km[r][c]) or "")
            n += 1
        rows.append(cur)

    return {"name": "%s (%d)" % (label, n), "keys": n, "layout": rows}


def find_pairs(names, ortho):
    """
    커뮤니티 레이아웃 이름마다 (좌표, 키맵) 짝을 찾는다.

    조건 셋을 다 만족해야 한다 — 매크로 이름이 같고, 기본 키맵이 그 매크로를
    부르고, 토큰 수가 자리 수와 같다. 하나라도 어긋나면 범례가 밀린다.
    ortho 에 든 이름은 구멍까지 없어야 한다 (has_gap 주석 참고).

    ★ 조건을 통과한 것이 여럿이면 **범례가 가장 많이 채워지는 것**을 고른다.
      범례는 좌표에 영향이 없는 힌트일 뿐이지만, 마법사가 "이 자리의 키를
      누르라" 고 가리킬 때 이름이 있는 편이 훨씬 낫다. 이름이 없으면 빈 칸이다.
      같은 점수면 경로 순으로 — 돌릴 때마다 결과가 바뀌면 안 된다.
    """
    best = {}

    for kb in (QMK / "keyboards").rglob("*.json"):
        if kb.name not in ("keyboard.json", "info.json"):
            continue
        try:
            d = json.loads(kb.read_text())
        except Exception:                            # noqa: BLE001
            continue

        for name in (d.get("community_layouts") or []):
            if name not in names:
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
            if name in ortho and has_gap(lay["layout"]):
                continue                             # 폭이 빠진 정의다. 다음 것을 본다

            blank = sum(1 for t in toks if norm(t) is None)
            key   = (blank, str(kb))
            if name not in best or key < best[name][0]:
                best[name] = (key, (kb.relative_to(QMK), macro, km.relative_to(QMK)))

    return {k: v[1] for k, v in best.items()}


# (표시 이름, 커뮤니티 레이아웃 이름)
COMMUNITY = [
    ("40% ortho MIT", "planck_mit"),     # 12/12/12 + 2U 스페이스
    ("60% ANSI",      "60_ansi"),
    ("60% HHKB",      "60_hhkb"),
    ("65% ANSI",      "65_ansi"),
    ("75% ANSI",      "75_ansi"),
    ("TKL ANSI",      "tkl_ansi"),
    ("풀사이즈 ANSI", "fullsize_ansi"),
]

# 붙어 있어야 하는 배열 — 구멍이 있으면 그 정의를 버린다 (has_gap 주석 참고)
ORTHO = {"planck_mit"}

# 커뮤니티 레이아웃이 아닌 것 — 키보드 자기 정의를 그대로 쓴다
#
# ★ baram F1-40 은 vial-qmk 안에 정식으로 들어가 있다. 그쪽 정의는 폭까지
#   제대로 적혀 있어서 그대로 쓴다 (구멍 없음을 확인했다).
#
OWN = [
    ("HHKB Lite 2",
     "keyboards/hhkb_lite_2/keyboard.json", "LAYOUT",
     "keyboards/hhkb_lite_2/keymaps/default/keymap.c"),
    ("F1-40 722",
     "keyboards/baram/geon/f1_40/staggered/keyboard.json", "LAYOUT_all",
     "keyboards/baram/geon/f1_40/staggered/keymaps/default/keymap.c"),
]


# VIA 정의를 그대로 쓰는 것 — QMK 트리 밖의 baram 키보드다.
#
# ★ keyboard.json 이 아니라 json/*.JSON (VIA 정의) 을 쓴다.
#
#   그쪽 keyboard.json 은 폭(w)이 빠진 매트릭스 표라 아랫줄에 구멍이 뚫린다.
#   VIA 정의에는 폭이 제대로 적혀 있다 — 실제로 VIA 가 그걸로 그리기 때문이다.
#   범례가 좌표라서 행렬형 키맵으로 이름을 찾는다 (build_via 주석 참고).
VIA_OWN = [
    ("BARAM 45K", BARAM / "45k/json/BARAM-45K-HS-VIA..JSON", BARAM / "45k/keymap.c"),
]


def main():
    out = []

    for label, path, macro, km in OWN:
        out.append(build(label, path, macro, km))

    for label, via, km in VIA_OWN:
        if not via.exists() or not km.exists():
            print("// 건너뜀 (경로 없음) : %s" % label, file=sys.stderr)
            continue
        out.append(build_via(label, via, km))
        print("//   %-14s <- %s" % (label, via), file=sys.stderr)

    pairs = find_pairs({n for _, n in COMMUNITY}, ORTHO)
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
