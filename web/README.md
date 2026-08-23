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
2. keyboard-layout-editor.com 에서 배열을 그리고 **Raw data** 를 붙여넣는다
3. **배열 읽기** → **마법사 시작**
4. 노란색으로 강조된 자리의 키를 누른다. 자동으로 다음 자리로 넘어간다
5. 못 누르는 키는 자리를 클릭하고 이름으로 직접 넣는다
6. 다 되면 내려받는다

## 파일

| | |
|---|---|
| `usage-table.js` | **생성물.** `firmware/qmk-link/tools/gen_keymap.py` 의 표에서 뽑는다 |
| `app.js` | WebHID · KLE 파싱 · 마법사 · 내보내기 |

`usage-table.js` 를 손으로 고치지 않는다. 두 벌을 관리하면 반드시 어긋난다.
