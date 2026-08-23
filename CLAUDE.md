# qmk-link

RP2350-USB-A 보드로 만드는 **USB 키보드 변환기**.
USB-A 에 꽂은 일반 키보드를 QMK 로 처리해 PC 에는 VIA / Vial 키보드로 보이게 한다.

## 작업 시작 전

**[`firmware/docs/00-context.md`](firmware/docs/00-context.md) 를 먼저 읽는다.**

그 문서에 이 저장소를 봐서는 알 수 없는 것들이 있다 —
참조 프로젝트의 로컬 경로, 확정된 결정과 그 이유, 하지 않기로 한 것,
열린 질문, 그리고 지금 어느 단계인지.

문서 목록은 [`firmware/docs/README.md`](firmware/docs/README.md).
핀을 만지기 전에는 [`firmware/docs/hardware.md`](firmware/docs/hardware.md) 를 본다.

## 규칙

### 문서

- 문서 파일명은 **소문자 kebab-case**. `README.md`, `CLAUDE.md` 만 예외
- **단계를 끝내면** `firmware/docs/00-context.md` 의 "현재 위치" 와
  `firmware/docs/roadmap.md` 의 상태를 갱신한다
- 결정을 바꾸면 `00-context.md` 의 "확정된 결정과 이유" 에 **이유까지** 남긴다.
  같은 논의를 두 번 하지 않기 위해서다
- 단계 문서는 틀을 지킨다 — 목표 / 배경·근거 / 설계 / 구현 항목 / 완료 판정 / 열린 질문

### 코드

- **`src/common/` 아래 인터페이스나 구조를 바꿀 때는 반드시 먼저 확인받는다.**
  다른 baram 프로젝트와 공유하는 자산이다. 여기서 갈라지면 프로젝트마다
  같은 파일이 조금씩 달라져 유지가 안 된다.
  - 새 함수 추가도 마찬가지다. 다른 프로젝트에 이미 같은 이름의 헤더가 있으면
    그쪽 인터페이스를 먼저 확인하고 맞춘다
  - 이 보드에만 필요한 것은 `src/hw/driver/` 나 `src/ap/` 에서 해결한다
- **백그라운드 처리는 `delay()` 에 태운다.** `bsp.c` 의 `delay()` 가
  `cliLoopIdle()` 을 돌린다 (NU87-TinyDK 관례). 그래야 기존 코드를 고치지 않고도
  USB 처리 같은 게 계속 돈다. CLI 명령 안에서도 `delay()` 를 그대로 쓴다
- 계층은 `main → bsp → hw → ap`. `baram-hola-mini` 관례를 따른다
- 기능 on/off 는 프로젝트의 `src/hw/hw_def.h` 의 `_USE_HW_*` 로 한다.
  드라이버는 그 매크로로 자기 자신을 감싼다
- 핀 상수는 `src/bsp/board/qmk_link.h` 에만 둔다. 드라이버에 숫자를 박지 않는다
- **`via` 와 `vial` 두 트리가 있다.** 공용은 `keyboards/qmk-link/` 와
  `ap/modules/{link,qmk/qmk.c}` 다. 트리 고유 설정은 각 트리의 `config.h` 에 둔다
  - ★ `port/platforms/eeprom.c` 의 `EE_FLASH_BEGIN` 은 트리마다 다르다
    (`HW_FLASH_E2P_VIA_BEGIN` / `..._VIAL_BEGIN`). 복사해 오면 두 펌웨어가 섞인다
- **키보드 배열은 `keyboards/qmk-link/layout-kle.json` 하나만 손으로 고친다.**
  `tools/gen_keymap.py` 가 `layout-via.json` 을 만든다 (wish-he 관례).
  KLE 범례는 주소가 아니라 **키 이름**이다 — 주소를 손으로 적으면 반드시 어긋난다
- **플래시에 쓰는 코드는 `hw/driver/flash.c` 를 거친다.** 소거·기록 중 XIP 가 멈추는데
  core1 이 PIO USB 를 돌고 있다. `flash_safe_execute()` 로 core1 을 세워야 한다.
  `flash_range_erase()` 를 직접 부르면 안 된다
- **`ap.c` 의 static 함수는 `ap` 접두어를 붙이지 않는다.**
  `updateKeyboard()` · `isKeyDown()` 처럼 쓴다 (baram-kbd-tester 관례).
  외부로 나가는 `apInit()` / `apMain()` 만 접두어를 갖는다
- 주석과 문서는 한국어

### 빌드

```bash
cd firmware/qmk-link

# VIA (기본)
cmake -S . -B build
cmake --build build -j16
python3 ../firm-sdk/tools/flash.py build/src/qmk-link-via.uf2

# Vial
cmake -S . -B build-vial -DKEY_PROTOCOL=vial
cmake --build build-vial -j16
python3 ../firm-sdk/tools/flash.py build-vial/src/qmk-link-vial.uf2
```

산출물 이름에 트리가 붙는다. USB 제품 이름도 `QMK-LINK VIA` / `QMK-LINK VIAL` 로
갈리므로 지금 어느 펌웨어가 올라가 있는지 바로 보인다.

- `PICO_SDK_PATH` 환경변수를 쓰지 않는다. CMakeLists 가 서브모듈 경로를 직접 지정한다
- 서브모듈은 `--recursive` 로 받지 않는다 (무선용까지 받아 338MB 가 된다).
  → [`firmware/docs/00-context.md`](firmware/docs/00-context.md#클론-직후)

### 커밋

- **브랜치를 만들지 않는다. `main` 에 바로 커밋한다.** 1인 프로젝트다
- 단계 구분은 브랜치가 아니라 `step-NN-xxx` 태그로 한다
- 커밋 메시지에 Claude 서명을 넣지 않는다
