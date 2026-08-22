# setup-windows — Windows 개발환경 구축

> **미검증.** 01단계는 macOS 에서만 빌드를 확인했다.
> Windows 에서 처음 돌려 보고 다른 점이 있으면 이 문서를 고친다.

---

## 1. 툴체인

`winget` 이 가장 간단하다 (PowerShell 관리자 권한).

```powershell
winget install Arm.GnuArmEmbeddedToolchain
winget install Kitware.CMake
winget install Ninja-build.Ninja
winget install Git.Git
winget install Python.Python.3.12
```

설치 후 **새 PowerShell 창**을 열어 PATH 를 다시 읽는다.

```powershell
arm-none-eabi-gcc --version
cmake --version
python --version
```

`arm-none-eabi-gcc` 가 안 잡히면 설치 경로
(`C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\<버전>\bin`)를 PATH 에 넣는다.

### 대안 — Raspberry Pi 공식 설치 프로그램

[pico-setup-windows](https://github.com/raspberrypi/pico-setup-windows) 를 쓰면
툴체인 · cmake · ninja · VSCode 를 한 번에 깐다. 다만 SDK 도 같이 깔리는데,
**우리는 서브모듈 SDK 를 쓰므로 그 SDK 와 `PICO_SDK_PATH` 환경변수는 무시된다.**
(CMakeLists 가 서브모듈 경로를 직접 지정한다.)

## 2. 빌드 제너레이터

`tasks.json` 은 Windows 에서 `-G "MinGW Makefiles"` 를 쓴다.
`make` 가 없으면 Ninja 로 바꾸는 게 낫다.

```powershell
cmake -S . -B build -G Ninja
```

`.vscode/tasks.json` 의 `windows.command` 를 `-G Ninja` 로 고쳐 써도 된다.

## 3. 파이썬 패키지 (선택)

`pyserial` 은 `flash.py` 의 **1200bps touch** 에만 쓰인다.
없어도 Key1(BOOTSEL) 을 눌러 굽는 건 된다.

```powershell
pip install pyserial
```

## 4. picotool

**설치하지 않아도 된다.** SDK 가 `firmware\firm-sdk\.picotool\` 에 직접 받아 빌드한다.
그러려면 빌드 머신에 `git` 이 있어야 한다 (위에서 이미 깔았다).

## 5. 저장소

```powershell
git clone https://github.com/chcbaram/qmk-link.git
cd qmk-link

git submodule update --init
git -C firmware/firm-sdk/pico-sdk submodule update --init lib/tinyusb
```

**`--recursive` 를 쓰지 않는다.** 무선용 서브모듈까지 받아 338MB 가 된다.

### 긴 경로 문제

pico-sdk 와 tinyusb 는 경로가 깊다. Windows 기본 260자 제한에 걸릴 수 있다.

```powershell
git config --global core.longpaths true
```

레지스트리에서 `LongPathsEnabled` 를 1 로 켜 두는 것도 권장한다.
가능하면 저장소를 `C:\dev\qmk-link` 처럼 **짧은 경로**에 둔다.

## 6. 빌드

```powershell
cd firmware\qmk-link
cmake -S . -B build -G Ninja
cmake --build build -j16
```

`FLASH` 가 **2 MB** 로 나와야 한다.

## 7. 다운로드

Key1(BOOTSEL) 을 누른 채 Type-C 를 연결한다. **`RP2350` 이름의 드라이브**가 뜬다.

```powershell
python ..\firm-sdk\tools\flash.py build\src\qmk-link.uf2
```

`flash.py` 는 `A:` ~ `Z:` 를 훑으며 `INFO_UF2.TXT` 로 보드를 판별하므로
드라이브 문자가 무엇이든 상관없다.

확인:

```powershell
python ..\firm-sdk\tools\flash.py --list
```

## 8. VSCode

`firmware\qmk-link\prj\qmk-link.code-workspace` 를 연다.
`Ctrl+Shift+B` 가 `build-build` 다.

---

## 문제 해결

| 증상 | 원인 / 해결 |
|---|---|
| `'make' is not recognized` | `-G Ninja` 를 쓴다 (위 2번) |
| `Filename too long` | `core.longpaths true` + 저장소를 짧은 경로로 (위 5번) |
| FLASH 가 4MB 로 나온다 | 보드 헤더가 안 잡힌 것이다. `src/bsp/board/qmk_link.h` 경로 확인 |
| 드라이브는 보이는데 `flash.py` 가 못 찾는다 | 그 드라이브에 `INFO_UF2.TXT` 가 있는지 확인. 없으면 BOOTSEL 볼륨이 아니다 |
| 시리얼 포트(COMx)가 안 보인다 | 02단계 전에는 CDC 가 없으므로 정상이다. BOOTSEL 을 눌러 굽는다 |
| picotool 빌드가 실패한다 | `git` 이 PATH 에 있어야 한다. 실패해도 `flash.py` 로 굽는 데는 지장 없지만 uf2 생성이 막히므로 해결해야 한다 |
