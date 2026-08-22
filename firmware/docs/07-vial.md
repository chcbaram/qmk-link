# 07-vial — Vial 트리를 추가한다

**상태: ⬜ 미착수** — 구현 상세는 착수할 때 채운다.

## 목표

`-DKEY_PROTOCOL=vial` 로 빌드하면 Vial 앱에서 인식·편집된다.

## 배경 / 근거

06단계가 되면 이 단계는 **트리를 갈아끼우는 작업**이다.
Vial 은 VIA 와 같은 raw HID 엔드포인트를 쓰고, 그 위 프로토콜만 다르다.

### 왜 독립 트리인가

vial-qmk 는 QMK 의 **포크**다. 갈라지는 지점이 프로토콜 레이어에 그치지 않는다 —
`dynamic_keymap.c`, `keycodes.h`, 인코더 / 탭댄스 / 콤보 / key override 의 flash 저장 구조,
그리고 **QMK 베이스 버전 자체가 upstream 과 다르다.**

한 트리에서 `#ifdef` 로 버티면 업스트림을 갱신할 때마다 깨진다.
그래서 `qmk/via/` 와 `qmk/vial/` 를 아예 따로 둔다.

소스 중복 비용은 있지만 **업스트림을 저장소에 넣지 않으므로 실제 중복은 `port/` 와
키보드 정의뿐**이고, 빌드 옵션으로 하나만 컴파일하니 바이너리 크기는 그대로다.

## 설계

### 업스트림

```json
"vial-qmk": { "url": "https://github.com/vial-kb/vial-qmk.git", "rev": "<vial 브랜치 커밋>" }
```

vial-qmk 는 **릴리스 태그가 없다.** `vial` 브랜치로 관리하며 주기적으로 QMK upstream 을 머지한다
(확인 시점: `dd43959a`, 2026-07-26).
`upstream.json` 에는 브랜치가 아니라 **커밋 SHA 를 박는다** — 브랜치를 그대로 두면 재현성이 없다.

SHA 로 받을 때는 `--branch` 를 못 쓴다:

```bash
git init <dir> && git -C <dir> remote add origin <url>
git -C <dir> fetch --depth 1 --filter=blob:none origin <sha>
git -C <dir> sparse-checkout set quantum
git -C <dir> checkout FETCH_HEAD
```

### 트리

```
src/ap/modules/qmk/vial/
├── CMakeLists.txt
├── config.h  keymap.c
├── vial.json
├── vial_generated_keyboard_definition.h
└── port/
```

`link/` 는 via 트리와 공유한다 (QMK API 를 안 만지는 순수 로직).

### 빌드 옵션

```bash
cmake -S . -B build -DKEY_PROTOCOL=vial
```

산출물 이름을 `qmk-link-vial.uf2` 로 구분한다.

### Vial 고유 요소

- `vial.json` → `vial_generated_keyboard_definition.h` 로 압축 변환 (vial-qmk 의 스크립트 사용)
- **키보드 UID** — `vial generate-keyboard-uid` 로 생성해 `config.h` 에 박는다
- unlock 조합 (`VIAL_UNLOCK_COMBO_ROWS/COLS`) — 가상 매트릭스 좌표로 지정해야 한다

## 구현 항목

- [ ] `upstream.json` 에 `vial-qmk` 추가 (커밋 SHA 고정)
- [ ] `fetch_upstream.py` 가 SHA fetch 경로를 지원하는지 확인
- [ ] `ap/modules/qmk/vial/` 트리
- [ ] `vial.json` + UID 생성 + 정의 헤더 변환
- [ ] `CMakeLists.txt` 의 `KEY_PROTOCOL` 분기
- [ ] 산출물 이름 분리

## 완료 판정

1. `-DKEY_PROTOCOL=vial` 빌드 통과
2. Vial 앱이 장치를 인식한다
3. 키맵 편집·저장이 되고 재부팅 후 유지된다
4. `-DKEY_PROTOCOL=via` 로 되돌려도 여전히 빌드·동작한다
5. 두 산출물이 이름으로 구분된다

## 열린 질문

| 항목 | 내용 |
|---|---|
| unlock 조합 | 가상 매트릭스라 물리 키 위치와 무관하다. 어떤 usage 조합을 쓸지 |
| vial-qmk 의 QMK 베이스 | 05단계에서 쓴 QMK 0.33.13 과 다르다. `port/` 를 그대로 못 쓸 수 있다 |
| 두 트리의 `port/` 중복 | 실제로 얼마나 다른지 보고, 같으면 공유를 검토한다 (지금은 중복을 감수) |
