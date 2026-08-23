# qmk-link 배열 마법사

KLE 로 그린 배열의 각 자리에 **그 키가 실제로 보내는 HID usage** 를 채워
`layout-via.json` / `vial.json` / `layout-kle.json` 을 만든다.

## 왜 브라우저 키 이벤트를 안 쓰나

OS 가 F13~F24 를 앱에 안 넘기고 Fn 조합·미디어키도 삼킨다.
대신 **보드에서 직접 읽는다** — WebHID 로 raw HID(`0xFF60`) 를 열고
qmk-link 고유 명령(`0xA0`)으로 눌린 usage 를 받는다.

## 띄우는 법

**WebHID 는 보안 컨텍스트에서만 된다.** `file://` 로 열면 동작하지 않는다.

```bash
cd web
python3 -m http.server 8000
```

→ <http://localhost:8000> (Chrome / Edge. Safari · Firefox 는 WebHID 미지원)

## 쓰는 법

1. **보드 연결** — USB-A 쪽에도 배우려는 키보드를 꽂아 둔다
2. 배열을 넣는다. 셋 중 아무거나 —
   - **프리셋** 에서 고른다 (HHKB Lite 2 · 60% · 65% · 75% · TKL · 풀사이즈)
   - **KLE json 파일 열기** — KLE raw · 우리 `layout-kle.json` · VIA/Vial 정의를 다 받는다
   - keyboard-layout-editor.com 의 **Raw data** 를 붙여넣는다
3. **배열 읽기** → **마법사 시작**
4. 노란색으로 강조된 자리의 키를 누른다. 자동으로 다음 자리로 넘어간다
5. 못 누르는 키는 자리를 클릭하고 이름으로 직접 넣는다
6. 다 되면 내려받는다

## 파일

| | |
|---|---|
| `usage-table.js` | **생성물.** `firmware/qmk-link/tools/gen_keymap.py` 의 표에서 뽑는다 |
| `presets.js` | **생성물.** `tools/gen_presets.py` 가 만든다 |
| `app.js` | WebHID · KLE 파싱 · 마법사 · 내보내기 |

**생성물을 손으로 고치지 않는다.** 두 벌을 관리하면 반드시 어긋난다.

### 프리셋은 어디서 오나

QMK / vial-qmk 의 **레이아웃 좌표** 와, 그 레이아웃을 쓰는 **키보드의 기본 키맵**을
짝지어 뽑는다. 좌표는 정확하고 범례는 그 키보드가 실제로 쓰는 이름이다.

```bash
python3 tools/gen_presets.py > presets.js      # vial-qmk 경로가 필요하다
```

손으로 그리면 1U 폭 하나만 틀려도 마법사가 엉뚱한 자리를 가리킨다.

★ QMK 의 layout 배열 순서 = `LAYOUT()` 매크로의 인자 순서라 **순서대로 zip** 하면
맞는다. 행 단위 줄바꿈과는 무관하다 — HHKB Lite 2 는 키맵 4번째 줄이 12개인데
그 행의 자리는 13개다 (오른쪽 Shift 옆 `Fn`). 행으로 맞추려 하면 어긋난다.
