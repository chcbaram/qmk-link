#!/usr/bin/env python3
"""
키보드 레이아웃 정의를 보드에 담는다 / 꺼낸다 / 지운다.

★ 왜 파이썬인가

  Vial 은 정의를 **LZMA 로 압축된 상태**로 읽어간다. 브라우저에는 LZMA 인코더가
  없어서(CompressionStream 은 deflate/gzip 뿐이다) 웹 마법사가 직접 못 만든다.
  파이썬은 표준 라이브러리에 lzma 가 있다.

사용법
    python3 tools/kbd_upload.py list
    python3 tools/kbd_upload.py put 0 keyboards/qmk-link/layout-vial.json --name "HHKB Lite 2"
    python3 tools/kbd_upload.py get 0 out.json
    python3 tools/kbd_upload.py erase 0

의존 : pip install hidapi
"""

import argparse
import json
import lzma
import sys
import time

try:
    import hid
except ImportError:
    sys.exit("hidapi 가 필요하다 — pip install hidapi")


VID = 0x0483
USAGE_PAGE = 0xFF60
REPORT_LEN = 32

# ★ VID 만 보면 안 된다.
#
#   0x0483 은 baram 키보드들이 같이 쓴다. wish-he 도 usage page 0xFF60 짜리
#   raw HID 를 갖고 있어서, VID 만 걸러면 그쪽을 열고 "모르는 명령(0xFF)" 을
#   받는다. 실제로 그것 때문에 "칸이 비었다" 는 엉뚱한 답을 봤다.
#
#   0x5305 는 지금 PID, 0x5400~0x540F 는 저장된 레이아웃마다 바뀌는 PID 다.
PID_LIST = [0x5305] + list(range(0x5400, 0x5410))

CMD = 0xA0
INFO, PRESSED, SLOT_INFO, SLOT_READ, SLOT_BEGIN, SLOT_DATA, SLOT_COMMIT, SLOT_ERASE = range(8)

RC = {0: "OK", 1: "실패", 2: "범위 벗어남"}


def open_dev():
    seen = []
    for d in hid.enumerate(VID, 0):
        if d["usage_page"] != USAGE_PAGE:
            continue
        seen.append("%04X:%04X %s" % (d["vendor_id"], d["product_id"], d["product_string"]))
        if d["product_id"] not in PID_LIST:
            continue
        h = hid.device()
        h.open_path(d["path"])
        return h, d

    msg = "qmk-link 을 못 찾았다."
    if seen:
        msg += "\n같은 VID 의 raw HID 장치는 이렇게 보인다 (전부 우리 것이 아니다):"
        for x in seen:
            msg += "\n   " + x
    sys.exit(msg)


def cmd_raw(h, payload):
    """
    리포트 하나를 보내고 **그 명령의 응답**을 받는다.

    ★ 응답을 대조해야 한다.

      같은 raw HID 인터페이스를 웹 마법사 · VIA · Vial 이 나눠 쓴다.
      다른 쪽이 폴링 중이면 그쪽 응답이 우리 read 로 배달된다.
      실제로 그것 때문에 "칸이 비었다" 는 엉뚱한 답을 받았다.
      앞 두 바이트(프리픽스 · 서브명령)가 맞을 때까지 읽어 넘긴다.
    """
    buf = bytearray(REPORT_LEN)
    buf[: len(payload)] = payload
    h.write(b"\x00" + bytes(buf))

    want = (payload[0], payload[1])
    end = time.time() + 3.0
    while time.time() < end:
        r = bytes(h.read(REPORT_LEN, timeout_ms=500))
        if len(r) >= 2 and (r[0], r[1]) == want:
            return r
        # 남의 응답이다. 버리고 다시 읽는다.
    raise TimeoutError("응답 없음 (0x%02X 0x%02X). "
                       "웹 마법사 탭이 열려 있으면 [연결 끊기] 를 누른다." % want)


def cmd(h, sub, *args):
    payload = bytearray([CMD, sub])
    payload += bytes(args)
    return cmd_raw(h, payload)


def do_list(h):
    r = cmd(h, INFO)
    n = r[7]
    print("펌웨어 : %s   매트릭스 %dx%d   꽂힌 키보드 %d대"
          % ("VIAL" if r[3] else "VIA", r[5], r[6], n))
    for i in range(n):
        o = 8 + i * 4
        print("   %04X:%04X" % (r[o] | (r[o+1] << 8), r[o+2] | (r[o+3] << 8)))

    print("\n저장된 레이아웃")
    used = 0
    for slot in range(16):
        r = cmd(h, SLOT_INFO, slot)
        if r[2] != 0 or r[3] == 0:
            continue
        used += 1
        name = bytes(r[10:32]).split(b"\x00")[0].decode("utf-8", "replace")
        print("   [%2d] %04X:%04X  %5d B  PID 0x%04X  %s"
              % (slot, r[4] | (r[5] << 8), r[6] | (r[7] << 8),
                 r[8] | (r[9] << 8), 0x5400 + slot, name))
    if not used:
        print("   (없음)")


def do_put(h, slot, path, name, vid, pid):
    raw = open(path, "rb").read()

    # vial.json 이면 최소화 + LZMA — vial-qmk 의 util/vial_generate_definition.py 와 같다
    try:
        blob = lzma.compress(json.dumps(json.loads(raw), separators=(",", ":")).strip().encode())
    except json.JSONDecodeError:
        sys.exit("JSON 이 아니다: %s" % path)

    if vid is None or pid is None:
        r = cmd(h, INFO)
        if r[7] == 0:
            sys.exit("USB-A 에 키보드가 없다. --vid/--pid 로 직접 주거나 꽂아라")
        vid = vid if vid is not None else r[8] | (r[9] << 8)
        pid = pid if pid is not None else r[10] | (r[11] << 8)

    nm = (name or "").encode("utf-8")[:22]
    print("칸 %d  <-  %s" % (slot, path))
    print("  %d B  ->  LZMA %d B   %04X:%04X  \"%s\""
          % (len(raw), len(blob), vid, pid, nm.decode("utf-8", "replace")))

    payload = bytearray([CMD, SLOT_BEGIN, slot,
                         vid & 0xFF, vid >> 8, pid & 0xFF, pid >> 8,
                         len(blob) & 0xFF, len(blob) >> 8])
    payload += nm.ljust(23, b"\x00")
    r = cmd_raw(h, payload)
    if r[2] != 0:
        sys.exit("BEGIN 실패 — %s" % RC.get(r[2], r[2]))

    step = REPORT_LEN - 4
    for off in range(0, len(blob), step):
        chunk = blob[off:off + step]
        payload = bytearray([CMD, SLOT_DATA, off & 0xFF, off >> 8]) + chunk
        r = cmd_raw(h, payload)
        if r[2] != 0:
            sys.exit("DATA 실패 (오프셋 %d) — %s" % (off, RC.get(r[2], r[2])))

    r = cmd(h, SLOT_COMMIT, slot)
    print("  굽기 : %s" % RC.get(r[2], r[2]))


def do_get(h, slot, out):
    r = cmd(h, SLOT_INFO, slot)
    if r[2] != 0 or r[3] == 0:
        sys.exit("칸 %d 가 비었다" % slot)

    n = r[8] | (r[9] << 8)
    blob = b""
    while len(blob) < n:
        payload = bytearray([CMD, SLOT_READ, slot, len(blob) & 0xFF, len(blob) >> 8])
        rr = cmd_raw(h, payload)
        if rr[2] != 0:
            sys.exit("READ 실패")
        blob += rr[4:4 + rr[3]]
    blob = blob[:n]

    data = lzma.decompress(blob)
    open(out, "wb").write(data)
    print("칸 %d  ->  %s   (LZMA %d B -> %d B)" % (slot, out, n, len(data)))


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    p = sub.add_parser("put")
    p.add_argument("slot", type=int)
    p.add_argument("path")
    p.add_argument("--name", default="")
    p.add_argument("--vid", type=lambda x: int(x, 0), default=None)
    p.add_argument("--pid", type=lambda x: int(x, 0), default=None)
    p = sub.add_parser("get")
    p.add_argument("slot", type=int)
    p.add_argument("out")
    p = sub.add_parser("erase")
    p.add_argument("slot", type=int)
    a = ap.parse_args()

    h, _ = open_dev()
    try:
        if a.cmd == "list":
            do_list(h)
        elif a.cmd == "put":
            do_put(h, a.slot, a.path, a.name, a.vid, a.pid)
        elif a.cmd == "get":
            do_get(h, a.slot, a.out)
        elif a.cmd == "erase":
            print("칸 %d 지우기 : %s" % (a.slot, RC.get(cmd(h, SLOT_ERASE, a.slot)[2], "?")))
    finally:
        h.close()


if __name__ == "__main__":
    main()
