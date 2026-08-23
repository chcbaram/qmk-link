# 08-finalize — 마감

**상태: ✅ 완료** (실기로 못 재는 것은 아래 "미검증" 에 남겼다)

## 목표

앞 단계에서 "나중에" 로 미뤄 둔 것들을 정리한다.

## 한 것

### 1. 미디어키 — 키보드 인터페이스로 오지 않는다

**볼륨 · 재생 · 뮤트는 Consumer 페이지(0x0C)를 쓰는 별도 인터페이스로 온다.**
그쪽 `bInterfaceProtocol` 은 `NONE` 이라 프로토콜만으로는 마우스와 구별되지 않는다.
**리포트 디스크립터를 파싱해야 안다.**

`tuh_hid_mount_cb` 에서 한 번 파싱해 표시해 둔다:

```c
n = tuh_hid_parse_report_descriptor(info, 4, desc_report, desc_len);
if (info[i].usage_page == HID_USAGE_PAGE_CONSUMER) {
    hid_info[instance].is_consumer = true;
    hid_info[instance].report_id   = info[i].report_id;   /* 0 이면 ID 바이트 없음 */
}
```

**★ QMK 를 거치지 않고 그대로 흘린다.** QMK 의 키맵은 매트릭스 좌표 기반인데
컨슈머 usage 는 거기 없다. 마우스와 같은 취급이다.

형식은 보통 usage 16비트 하나다 (`0` = 뗌). `report_id` 가 있으면 첫 바이트를 건너뛴다.
PC 로는 IF1(Extra) 에 리포트 ID 4(컨슈머)를 달아 내보낸다 — QMK 의 `report_extra_t` 와 같은 모양이다.

### 2. 키보드 여러 대 — 비트맵을 OR 로 합친다

`CFG_TUH_HUB` 가 켜져 있어 허브를 거쳐 두 대 이상 붙을 수 있다.

**전에는 버그였다.** `linkSetKeyboardReport()` 가 매트릭스를 통째로 덮어썼다.
각 리포트는 "그 키보드의 전체 상태" 라, 한쪽이 보낸 리포트가 **다른 쪽 키를 지웠다.**

```c
static uint16_t src_matrix[LINK_SOURCE_MAX][LINK_MATRIX_ROWS];   /* 키보드별 */
static uint16_t matrix[LINK_MATRIX_ROWS];                        /* OR 로 합친 결과 */
```

빠질 때도 **그 인스턴스만** 뗀다 (`linkClearInstance`). 두 대 중 한 대만 빼면
다른 대의 키까지 떼면 안 된다. `ap.c` 가 인스턴스별 연결 상태를 보고 부른다.

### 3. USB suspend — 소등

```c
if (tud_suspended() == true) return LED_ST_SUSPEND;   /* 최우선 */
```

서스펜드 중에는 키 반짝임도 하지 않는다. 자는 PC 옆에서 혼자 빛나지 않게,
그리고 서스펜드 전류를 줄이기 위해서다.

### 4. VID / PID 확정 — `0483:5305`

**네 곳이 일치해야 한다.** 하나라도 어긋나면 VIA/Vial 이 장치를 못 찾거나
`flash.py` 가 엉뚱한 보드를 리셋한다.

| 곳 | |
|---|---|
| `src/hw/hw_def.h` | `HW_USB_VID` / `HW_USB_PID` |
| `keyboards/qmk-link/layout-via.json` | 생성물 (`gen_keymap.py`) |
| `keyboards/qmk-link/layout-vial.json` | 생성물 |
| `firm-sdk/tools/flash.py` | `FW_VID` / `FW_PID` |

다른 baram 키보드와 겹치지 않는 값이다 (직전 최신이 wish-he `0x5304`).

★ **09단계에서 PID 가 고정이 아니게 됐다.** 꽂힌 키보드의 레이아웃 SLOT 에 따라
`0x5400`+SLOT 으로 바꿔 보고한다 (VIA 가 정의를 VID/PID 로 찾기 때문이다).
`0x5305` 는 담아 둔 것이 없을 때의 값이다. **PID 로 장치를 찾는 코드는 범위로
봐야 한다** — `flash.py` 의 `FW_PID_LIST` → [09-keyboard-profile.md](09-keyboard-profile.md)

### 5. 허브 — 이미 켜져 있다

`CFG_TUH_HUB 1`, `CFG_TUH_DEVICE_MAX 4`. 03단계에서 켰다 —
**HHKB Lite 2 가 허브 내장이라 끄면 열거가 끝나지 않았다.** 그때 실기로 확인했다.

### 6. 문서 · README

README 에 **`fetch_upstream.py` 단계가 빠져 있었다** — 그대로 따라 하면 빌드가 실패한다.
빌드 절차(via/vial 분리), 쓰는 법, LED 표, CLI 표를 채웠다.

## 완료 판정

| # | 항목 | 결과 |
|---|---|---|
| 1 | 미디어키 경로 | 구현. **미검증** (아래) |
| 2 | 키보드 여러 대 병합 | 구현. **미검증** (아래) |
| 3 | suspend 소등 | 구현. **미검증** (아래) |
| 4 | VID/PID 네 곳 일치 | ✅ 스크립트로 확인 |
| 5 | 허브 | ✅ 03단계에서 실기 확인 |
| 6 | 두 빌드 정상 | ✅ via 112,032 B / vial 130,200 B |
| 7 | 회귀 없음 | ✅ `drain == link`, `버림 0`, `보류 0` |

## 미검증 — 장비/환경이 없어서 못 잰 것

| 항목 | 무엇이 있어야 하나 |
|---|---|
| **미디어키** | **Consumer 인터페이스가 있는 키보드.** HHKB Lite 2 는 HID 인터페이스가 키보드 하나뿐이라(`usbh info` 로 확인) 볼륨키가 아예 없다 |
| **키보드 두 대 동시** | 허브 + 키보드 2대 |
| **suspend 소등** | PC 를 재우고 LED 확인 |
| 마우스 패스스루 | USB 마우스 |
| BIOS 화면 | 재부팅해서 BIOS 진입 |
| Windows / Linux | 해당 OS 실기. `flash.py` · `setup-windows.md` · `setup-linux.md` |

## 열린 질문

| 항목 | 내용 |
|---|---|
| 릴리스 | `qmk-link-via.uf2` / `qmk-link-vial.uf2` 를 GitHub Release 로 올릴지 |
| 미디어키 형식 | 비트맵으로 보내는 키보드도 있다. 실물을 만나면 그때 대응한다 |
| 레이어 표시 | LED 로 레이어를 보여줄지 — 색이 이미 여섯 가지라 더 넣으면 알아보기 어렵다 |
