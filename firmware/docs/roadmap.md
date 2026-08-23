# roadmap

각 단계는 **동작하는 펌웨어**로 끝난다. 끝나면 `step-NN-xxx` 태그를 남겨 회귀 확인이 가능하게 한다.

| 단계 | 상태 | 목표 | 완료 판정 |
|---|---|---|---|
| [01 LED](01-led.md) | ✅ **완료** | 프로젝트 골격 · firm-sdk · 빌드/다운로드 경로 · 120MHz 클럭 | WS2812 500ms 점멸, uf2 다운로드 성공 |
| [02 CLI/CDC](02-cli-cdc.md) | ✅ **완료** | USB device CDC + cli/log/swtimer + 1200bps touch 리부트 | CDC 콘솔에서 CLI 동작, `flash.py` 가 BOOTSEL 을 안 눌러도 굽는다 |
| [03 USB HOST](03-usb-host.md) | ✅ **완료** | Pico-PIO-USB + TinyUSB host(RHPort1), core1 전용 태스크 | USB-A 에 키보드 꽂으면 CLI 에 HID report 덤프 |
| [04 HID DEVICE](04-usb-device-hid.md) | ✅ **완료** | Type-C 복합 descriptor (HID kbd/extra/raw + CDC + vendor) | host→device 패스스루로 PC 에서 타이핑됨 |
| [05 QMK](05-qmk.md) | ✅ **완료** | `fetch_upstream.py` + `qmk/via/port/` + `link/` + keymap | 레이어 · 모디파이어 · 탭홀드 동작 |
| [06 VIA](06-via.md) | ✅ **완료** | raw HID VIA 프로토콜 + dynamic keymap + flash EEPROM | VIA 웹앱에서 키맵 편집 · 저장 · 재부팅 후 유지 |
| [07 VIAL](07-vial.md) | ✅ **완료** | `qmk/vial/` 트리 추가, `-DKEY_PROTOCOL=vial` + vial.json | Vial 앱 인식 · 편집 |
| [08 마감](08-finalize.md) | ✅ **완료** | WS2812 상태 인디케이터 · USB suspend/resume · VID/PID · 허브 | |

### 마감(08) 이후에 할 것

| 항목 | 왜 | 무엇을 해야 하나 |
|---|---|---|
| **[09 키보드 프로파일](09-keyboard-profile.md)** | 꽂은 키보드를 알아보고 그 키보드의 레이아웃·키맵을 쓴다. **키맵 쪽이 기능이고 체감이 크다** — 지금은 모든 키보드가 키맵 한 벌을 공유한다 | 1) 학습 마법사 웹페이지(펌웨어 변경 0) 2) 온디바이스 저장 + Vial 정의 서빙 3) 키맵 프로파일.<br>★ Vial 은 정의를 장치에서 읽어가지만 **VIA 는 그 통로가 없다** |
|---|---|---|
| **EEPROM 을 wear leveling 으로** | 지금은 저장 1회 = 섹터 소거 1회다. **수명은 문제가 아니다** (섹터당 10만 회 / 하루 10번 저장이면 27년). 진짜 구멍은 **소거 → 기록 사이 45ms** 다. 그때 전원이 끊기면 섹터가 빈 채로 남아 키맵이 기본값으로 돌아간다. QMK 의 wear leveling 은 append 로그라 이 창이 없다 | `upstream.json` 의 sparse 에 `drivers` 추가 → `quantum/wear_leveling/wear_leveling.c` 와 `drivers/eeprom/eeprom_wear_leveling.c` 는 그대로 쓰고, backing store 7함수(`backing_store_init/unlock/erase/write/write_bulk/read/lock`)를 우리 `hw/driver/flash.c` 위에 새로 쓴다. `port/platforms/eeprom.c` 는 걷어낸다. **via·vial 두 트리가 같은 backing store 를 쓰므로 07단계 뒤에 한 번에 한다.**<br>★ `wear_leveling_rp2040_flash.c` 는 못 가져온다 — RP2040 의 SSI 레지스터를 직접 두드린다 |

---

## 왜 이 순서인가

**아래에서 위로 쌓는다. 각 단계는 바로 앞 단계가 동작한다는 것만 전제한다.**

```
01 LED        빌드하고 굽는 경로부터 뚫는다. 이게 안 되면 아무것도 디버깅할 수 없다.
              120MHz 클럭도 여기서 확정한다 — 나중에 바꾸면 이미 초기화된 PIO/USB 가 어긋난다.
   ↓
02 CLI/CDC    로그와 CLI 를 먼저 만든다. 이 보드는 디버그 UART 핀이 없어서
              CDC 가 유일한 관측 수단이다. 03 단계부터는 이게 없으면 눈을 감고 짜는 셈이다.
   ↓
03 USB HOST   가장 위험한 부분(PIO USB · 타이밍 · 하드웨어 풀업)을 QMK 를 얹기 전에 검증한다.
              여기서 실패하면 프로젝트 전제가 무너지므로 최대한 앞에 둔다.
   ↓
04 HID DEVICE 호스트에서 받은 걸 그대로 PC 로 흘린다. QMK 없이 "USB 연장선"이 되는 지점.
              USB 양쪽이 다 동작한다는 걸 확인하고 나서 키 처리로 넘어간다.
   ↓
05 QMK        비로소 키 처리. 입력은 매트릭스가 아니라 04 의 HID report 다.
   ↓
06 VIA / 07 VIAL   편집 프로토콜. 06 이 되면 07 은 트리만 갈아끼우는 작업이다.
   ↓
08 마감
```

---

## 단계 문서의 틀

```
# NN_XXX — 한 줄 목표
## 목표          이 단계에서 무엇이 되게 하는가
## 배경 / 근거   왜 이 순서인가, 이전 단계에서 무엇을 물려받는가
## 설계          구조 · 핀 · API · CMake 옵션
## 구현 항목     파일 단위 체크리스트
## 완료 판정     구체적인 확인 절차
## 열린 질문     이 단계에서 결정하지 못한 것 (다음 단계로 이월)
```

02 이후 문서는 **지금 시점에서 정해진 목표 · 설계 방향 · 완료 판정 · 열린 질문까지만** 적혀 있다.
구현 상세는 각 단계에 착수할 때 채운다.

---

## 관련 문서

- [00-context.md](00-context.md) — 이어서 작업하기 위한 정보 (**여기부터 읽는다**)
- [hardware.md](hardware.md) — 핀 배정과 근거
- [usb-stack.md](usb-stack.md) — 02~04단계의 설계 근거
- 개발환경 구축: [macOS](setup-macos.md) · [Windows](setup-windows.md) · [Linux](setup-linux.md)
