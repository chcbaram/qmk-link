# setup-macos — macOS 개발환경 구축

**이 문서의 환경에서 01단계 빌드가 실제로 검증되었다.**

| | 검증된 버전 |
|---|---|
| OS | macOS (Apple Silicon) |
| 컴파일러 | arm-none-eabi-gcc 14.2.1 |
| 빌드 | cmake 4.4.2 / ninja 1.13.2 |
| 파이썬 | python3 3.14 |

---

## 1. Homebrew

없으면 설치한다.

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

## 2. 툴체인

```bash
brew install --cask gcc-arm-embedded     # arm-none-eabi-gcc
brew install cmake ninja git python3
```

확인:

```bash
arm-none-eabi-gcc --version    # 14.x 이상
cmake --version                # 3.13 이상
```

> `brew install arm-none-eabi-gcc` 로도 되지만, cask 쪽이 Arm 공식 배포판이라
> newlib / libstdc++ 조합이 더 안정적이다.

## 3. 파이썬 패키지 (선택)

`pyserial` 은 `flash.py` 의 **1200bps touch** 에만 쓰인다.
없어도 Key1(BOOTSEL) 을 눌러 굽는 건 된다.

```bash
pip3 install pyserial
```

`externally-managed-environment` 오류가 나면:

```bash
pip3 install --break-system-packages pyserial
# 또는
brew install pyserial
```

## 4. picotool

**설치하지 않아도 된다.** SDK 가 `firmware/firm-sdk/.picotool/` 에 직접 받아 빌드한다.
(그 picotool 은 `PICOTOOL_NO_LIBUSB=1` 이라 `load` 는 못 하지만, 우리는 `flash.py` 로 굽는다.)

굳이 쓰고 싶으면:

```bash
brew install picotool     # 2.3.0, SDK 요구 버전과 일치
```

## 5. 저장소

```bash
git clone https://github.com/chcbaram/qmk-link.git
cd qmk-link

git submodule update --init                                          # pico-sdk
git -C firmware/firm-sdk/pico-sdk submodule update --init lib/tinyusb
```

**`--recursive` 를 쓰지 않는다.** 무선용 서브모듈(btstack 220MB 등)까지 받아 338MB 가 된다.
위처럼 받으면 49MB.

## 6. 빌드

```bash
cd firmware/qmk-link
cmake -S . -B build
cmake --build build -j16
```

첫 빌드는 picotool 을 받아 빌드하느라 조금 걸린다.

성공하면:

```
Memory region         Used Size  Region Size  %age Used
           FLASH:        8472 B         2 MB      0.40%
             RAM:        4416 B       512 KB      0.84%
```

`FLASH` 가 **2 MB** 로 나와야 한다. 4MB 로 나오면 보드 헤더가 안 잡힌 것이다.

## 7. 다운로드

Key1(BOOTSEL) 을 누른 채 Type-C 를 연결한다. `RP2350` 볼륨이 뜬다.

```bash
python3 ../firm-sdk/tools/flash.py build/src/qmk-link.uf2
```

02단계부터는 BOOTSEL 을 누르지 않아도 된다 (1200bps touch).

확인:

```bash
python3 ../firm-sdk/tools/flash.py --list
```

## 8. VSCode

```bash
code firmware/qmk-link/prj/qmk-link.code-workspace
```

권장 확장은 워크스페이스가 알려준다 (`ms-vscode.cpptools`, `ms-vscode.cmake-tools`, `marus25.cortex-debug`).
`⌘⇧B` 가 `build-build` 다.

---

## 문제 해결

| 증상 | 원인 / 해결 |
|---|---|
| `Unable to find definition of board 'qmk_link'` | `PICO_BOARD_HEADER_DIRS` 가 `pico_sdk_init()` 뒤에 설정된 경우다. 루트 `CMakeLists.txt` 순서 확인 |
| FLASH 가 4MB 로 나온다 | 보드 헤더 대신 `pico2` 가 잡힌 것이다. `src/bsp/board/qmk_link.h` 경로 확인 |
| `arm-none-eabi-gcc: command not found` | `brew install --cask gcc-arm-embedded` 후 새 터미널을 연다 |
| 볼륨을 못 찾는다 | Finder 에 `RP2350` 이 보이는지 먼저 확인. `flash.py --list` 로 감지 여부 확인 |
| 시리얼 포트가 안 보인다 | macOS 는 `/dev/cu.*` 를 쓴다. `/dev/tty.*` 는 DCD 를 기다려 블록된다 — `flash.py` 는 이미 `cu.*` 만 본다 |
