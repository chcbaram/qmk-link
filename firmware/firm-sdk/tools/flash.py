#!/usr/bin/env python3
"""
RP2350 UF2 다운로드 (Windows / macOS / Linux 공통)

  python3 flash.py build/src/qmk-link.uf2       빌드 결과를 보드에 굽는다
  python3 flash.py --list                       감지된 BOOTSEL 볼륨과 시리얼 포트만 출력

동작 순서

  1. BOOTSEL 볼륨이 이미 보이면 4번으로 간다.
  2. 보이지 않으면 보드의 CDC 포트를 1200bps 로 열었다 닫는다 (1200bps touch).
     실행 중인 펌웨어가 이걸 받으면 스스로 BOOTSEL 로 재부팅한다.
     -> 2단계(USB CDC) 부터 동작한다. 1단계 펌웨어는 CDC 가 없으므로 이 경로를 못 탄다.
  3. 볼륨이 올라올 때까지 기다린다.
  4. uf2 를 복사한다. 복사가 끝나면 보드가 알아서 재부팅하며 새 펌웨어로 뜬다.

BOOTSEL 볼륨은 이름으로 찾지 않는다. 이름은 SDK/보드마다 다르지만
(RP2350, RPI-RP2 ...) INFO_UF2.TXT 는 항상 있다. 그 파일로 판별한다.

의존성

  pyserial 은 1200bps touch 에만 쓰이고, 없으면 그 단계만 건너뛴다.
  볼륨 탐지와 복사는 표준 라이브러리만 쓴다.

  pip3 install pyserial
"""

import argparse
import os
import shutil
import string
import sys
import time
from pathlib import Path


# 보드가 BOOTSEL 로 뜰 때의 USB VID/PID (RP2350 BOOTROM)
BOOTROM_VID = 0x2E8A
BOOTROM_PID_RP2350 = 0x000F

# 실행 중인 펌웨어의 CDC 포트 (2단계에서 확정한다)
FW_VID = 0x2E8A

# INFO_UF2.TXT 의 Board-ID 에 이 문자열이 들어가면 우리 대상으로 본다
BOARD_ID_HINTS = ("RP2350", "RP2040", "RPI-RP2")


# ---------------------------------------------------------------- 볼륨 탐지

def candidate_mounts():
    """OS 별 마운트 지점 후보를 훑는다."""
    if sys.platform == "darwin":
        yield from Path("/Volumes").glob("*")

    elif sys.platform.startswith("win"):
        for letter in string.ascii_uppercase:
            p = Path(f"{letter}:\\")
            if p.exists():
                yield p

    else:  # linux, bsd
        user = os.environ.get("USER", "")
        for base in ("/media", "/run/media"):
            yield from Path(base).glob("*/*")
            if user:
                yield from Path(base).glob(f"{user}/*")
        yield from Path("/mnt").glob("*")


def read_info_uf2(mount):
    """INFO_UF2.TXT 를 읽어 dict 로 돌려준다. 아니면 None."""
    info = mount / "INFO_UF2.TXT"
    try:
        if not info.is_file():
            return None
        text = info.read_text(errors="replace")
    except OSError:
        return None

    out = {}
    for line in text.splitlines():
        if ":" in line:
            k, _, v = line.partition(":")
            out[k.strip()] = v.strip()
    return out


def find_bootsel():
    """BOOTSEL 볼륨을 찾는다. (경로, info dict) 또는 (None, None)."""
    seen = set()
    for mount in candidate_mounts():
        if mount in seen:
            continue
        seen.add(mount)

        info = read_info_uf2(mount)
        if info is None:
            continue

        board_id = info.get("Board-ID", "")
        if any(h in board_id.upper() for h in BOARD_ID_HINTS):
            return mount, info

    return None, None


def wait_bootsel(timeout, quiet=False):
    """BOOTSEL 볼륨이 올라올 때까지 기다린다."""
    deadline = time.monotonic() + timeout
    dots = 0
    while time.monotonic() < deadline:
        mount, info = find_bootsel()
        if mount is not None:
            if dots and not quiet:
                print()
            return mount, info
        if not quiet:
            print("." if dots else "  볼륨 대기 중 .", end="", flush=True)
            dots += 1
        time.sleep(0.3)

    if dots and not quiet:
        print()
    return None, None


# ---------------------------------------------------------- 1200bps touch

def list_serial_ports():
    """(device, description, vid, pid) 목록. pyserial 이 없으면 빈 목록."""
    try:
        from serial.tools import list_ports
    except ImportError:
        return []

    out = []
    for p in list_ports.comports():
        name = (p.device or "")
        # macOS 는 /dev/tty.* 가 DCD 를 기다려 블록된다. cu.* 만 쓴다.
        if sys.platform == "darwin" and "/tty." in name:
            continue
        out.append((name, p.description or "", p.vid, p.pid))
    return out


def pick_port(explicit=None):
    """보드일 가능성이 가장 높은 포트를 고른다."""
    ports = list_serial_ports()
    if explicit:
        for dev, _, _, _ in ports:
            if dev == explicit:
                return explicit
        return explicit          # 목록에 없어도 사용자가 지정했으면 그대로 쓴다

    for dev, _, vid, _ in ports:
        if vid == FW_VID:
            return dev
    return None


def touch_1200(port):
    """포트를 1200bps 로 열었다 닫아 BOOTSEL 재부팅을 요청한다."""
    try:
        import serial
    except ImportError:
        return False, "pyserial 이 없다 (pip3 install pyserial)"

    try:
        s = serial.Serial(port, 1200)
        s.dtr = False            # DTR 을 내려야 펌웨어가 touch 로 인식한다
        s.close()
        return True, None
    except Exception as e:      # 포트가 사라지면서 나는 예외도 정상 동작이다
        return True, str(e)


# ------------------------------------------------------------------ 복사

def copy_uf2(uf2, mount):
    """uf2 를 볼륨에 쓴다. 보드는 다 받으면 스스로 재부팅한다."""
    dest = mount / uf2.name
    size = uf2.stat().st_size

    with open(uf2, "rb") as fs, open(dest, "wb") as fd:
        shutil.copyfileobj(fs, fd, length=1024 * 64)
        fd.flush()
        try:
            os.fsync(fd.fileno())
        except OSError:
            # 보드가 복사 도중 재부팅하며 연결을 끊으면 여기서 난다. 정상이다.
            pass

    return size


# ------------------------------------------------------------------ main

def cmd_list():
    mount, info = find_bootsel()
    print("BOOTSEL 볼륨")
    if mount:
        print(f"  {mount}")
        for k in ("Model", "Board-ID"):
            if k in info:
                print(f"    {k:10s}: {info[k]}")
    else:
        print("  없음")

    print()
    print("시리얼 포트")
    ports = list_serial_ports()
    if not ports:
        try:
            import serial  # noqa: F401
            print("  없음")
        except ImportError:
            print("  (pyserial 없음 — pip3 install pyserial)")
    for dev, desc, vid, pid in ports:
        mark = " *" if vid == FW_VID else "  "
        ids = f"{vid:04X}:{pid:04X}" if vid else "----:----"
        print(f" {mark} {dev:24s} {ids}  {desc}")
    return 0


def main():
    ap = argparse.ArgumentParser(
        description="RP2350 UF2 다운로드 (OS 무관)",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("uf2", nargs="?", help="구울 .uf2 파일")
    ap.add_argument("--port", help="1200bps touch 를 보낼 시리얼 포트 (기본: 자동)")
    ap.add_argument("--no-touch", action="store_true",
                    help="1200bps touch 를 건너뛴다 (BOOTSEL 을 직접 눌렀을 때)")
    ap.add_argument("--timeout", type=float, default=10.0,
                    help="BOOTSEL 볼륨 대기 시간(초). 기본 10")
    ap.add_argument("--list", action="store_true",
                    help="감지된 볼륨과 포트만 출력하고 끝낸다")
    args = ap.parse_args()

    if args.list:
        return cmd_list()

    if not args.uf2:
        ap.error("구울 .uf2 파일을 지정하거나 --list 를 쓴다")

    uf2 = Path(args.uf2)
    if not uf2.is_file():
        sys.exit(f"파일이 없다: {uf2}")
    if uf2.suffix.lower() != ".uf2":
        sys.exit(f".uf2 파일이 아니다: {uf2}")

    # 1. 이미 BOOTSEL 인가
    mount, info = find_bootsel()

    # 2. 아니면 1200bps touch 로 재부팅을 요청한다
    if mount is None and not args.no_touch:
        port = pick_port(args.port)
        if port:
            print(f"1200bps touch -> {port}")
            ok, err = touch_1200(port)
            if not ok:
                print(f"  건너뜀: {err}")
            time.sleep(0.5)
        else:
            print("보드의 CDC 포트를 찾지 못했다.")
            print("Key1(BOOTSEL) 을 누른 채 USB 를 다시 연결하면 된다.")

    # 3. 볼륨을 기다린다
    if mount is None:
        mount, info = wait_bootsel(args.timeout)

    if mount is None:
        sys.exit(
            f"BOOTSEL 볼륨을 찾지 못했다 ({args.timeout:.0f}초 대기).\n"
            "Key1(BOOTSEL) 을 누른 채 USB 를 연결한 뒤 다시 실행한다.")

    print(f"볼륨   : {mount}   ({info.get('Board-ID', '?')})")
    print(f"파일   : {uf2}")

    size = copy_uf2(uf2, mount)
    print(f"완료   : {size:,} bytes 복사, 보드 재부팅")
    return 0


if __name__ == "__main__":
    sys.exit(main())
