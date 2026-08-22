# 05-qmk — QMK 를 얹는다

**상태: ⬜ 미착수** — 구현 상세는 착수할 때 채운다.

## 목표

04단계의 패스스루 자리에 **QMK 키 처리**를 넣는다.
레이어 · 모디파이어 · 탭홀드가 동작한다.

## 배경 / 근거

여기까지 오면 하드웨어와 USB 리스크는 다 걷혀 있다. 남은 건 소프트웨어다.

**입력이 매트릭스가 아니라 HID report 라는 점**이 이 프로젝트의 핵심이자
일반 QMK 포팅과 다른 부분이다.

## 설계

### 업스트림을 저장소에 넣지 않는다

QMK 소스는 `firm-sdk/upstream/qmk_firmware` 에 받아 쓰고,
**우리 저장소에는 `port/` 레이어와 키보드 정의만 커밋한다.**

```bash
python3 firmware/firm-sdk/tools/fetch_upstream.py
```

`upstream.json` 이 리비전을 고정한다.

| | 리포지토리 | 값 |
|---|---|---|
| QMK | `github.com/qmk/qmk_firmware` | 릴리스 태그 `0.33.13` |

받는 방식:

```bash
git clone --depth 1 --filter=blob:none --sparse --branch 0.33.13 <url> <dir>
git -C <dir> sparse-checkout set quantum
```

전체 히스토리는 524MB 지만 `quantum/` 만 받으면 5MB 안팎이다.
hola-mini 가 QMK 에서 실제로 쓴 것도 `quantum/` 뿐이었다 (519 파일 / 3.5MB).

→ 근거는 [00-context.md](00-context.md#확정된-결정과-이유)

### 트리

```
src/ap/modules/
├── link/                HID report → 키 상태 비트맵 (QMK 무관, 공통)
└── qmk/
    └── via/
        ├── CMakeLists.txt    upstream 에서 QMK_SRC_FILES / QMK_INC_DIR 구성
        ├── config.h  keymap.c  info.json  version.h
        └── port/             platforms · protocol · matrix.c
```

`link/` 는 QMK API 를 만지지 않는 순수 로직이라 07단계의 vial 트리와 공유한다.
QMK API 를 만지는 `matrix.c` 는 각 트리의 `port/` 안에 둬서 의존을 한 방향으로만 유지한다.

### 가상 매트릭스

USB HID usage 코드(0x00~0xFF)를 그대로 좌표로 쓴다.

```
MATRIX_ROWS = 16
MATRIX_COLS = 16
row = usage >> 4,  col = usage & 0x0F
```

`link/` 가 256비트 비트맵을 채우고, `port/matrix.c` 의 `matrix_scan()` 이 그걸 읽는다.
keymap 도 이 좌표계로 쓴다.

### 포트 레이어

hola-mini 의 `src/ap/modules/qmk/port/` 를 가져온다
(`platforms/`, `protocol/`, `bootloader.c`, `eeprom.c`, `timer.c`, `wait.c`, `suspend.c`).

## 구현 항목

- [ ] `firm-sdk/upstream.json`
- [ ] `firm-sdk/tools/fetch_upstream.py` — shallow + partial + sparse, `--update`, `--check`
- [ ] CMake configure 단계에서 `fetch_upstream.py --check` 자동 호출
- [ ] **`0.33.13` 기준으로 hola-mini 포트 레이어 컴파일 가능 여부 확인** ← 가장 먼저
- [ ] `ap/modules/qmk/via/port/` — platforms · protocol · matrix.c
- [ ] `ap/modules/qmk/via/{config.h, keymap.c, info.json, version.h}`
- [ ] `ap/modules/link/` — HID report → 비트맵 (04단계 패스스루를 대체)
- [ ] `ap/modules/qmk/via/CMakeLists.txt`
- [ ] 루트 CMakeLists 의 `KEY_PROTOCOL` 옵션 연결

## 완료 판정

1. `fetch_upstream.py` 로 받은 `upstream/qmk_firmware` 크기가 10MB 미만
2. 빌드가 통과하고 FLASH 사용량이 2MB 안에 든다
3. USB-A 키보드로 타이핑하면 keymap 대로 PC 에 입력된다
4. 레이어 키가 동작한다
5. 탭홀드(`LT`, `MT`)가 동작한다
6. 모디파이어 조합이 정상

## 열린 질문

| 항목 | 내용 |
|---|---|
| **포트 ↔ 0.33.13 API 차이** | hola-mini 가 이식한 QMK 는 0.33.13 보다 한참 이전이다. `keycodes.h` 재편 · `keyboard.c` 스캔 흐름 · `eeconfig` 레이아웃 등에서 차이가 날 수 있다. **착수하면 컴파일부터 해 보고**, 차이가 크면 QMK 리비전을 hola-mini 쪽에 맞춰 내릴지 포트를 올릴지 정한다 |
| sparse 범위 | `quantum/` 만으로 헤더가 부족하면 `sparse-checkout set` 에 `platforms/`, `tmk_core/` 를 추가한다. `keyboards/` 만 빠지면 크기는 여전히 작다 |
| 매트릭스 크기 | 16×16 = 256키. consumer / system 키는 usage page 가 달라 별도 처리가 필요하다 |
| 여러 키보드 동시 연결 | 허브를 켜면 가능해진다. 비트맵을 OR 로 합칠지 결정 |
