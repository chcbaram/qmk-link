# 06-via — VIA 로 키맵을 편집한다

**상태: ⬜ 미착수** — 구현 상세는 착수할 때 채운다.

## 목표

VIA 웹앱에서 키맵을 편집하고 저장한다. 재부팅해도 유지된다.

## 배경 / 근거

05단계에서 QMK 가 돌아가지만 키맵이 컴파일 시점에 박혀 있다.
편집 가능하게 만들려면 raw HID 프로토콜과 EEPROM 이 필요하다.

04단계에서 raw HID 엔드포인트는 이미 뚫려 있다. 여기서 응답을 채운다.

## 설계

### raw HID

usage page `0xFF60`, usage `0x61`. IN/OUT 각 32바이트.
QMK 의 `via.c` 가 프로토콜을 처리하므로 우리가 할 일은
`raw_hid_receive()` / `raw_hid_send()` 를 TinyUSB 에 연결하는 것뿐이다.

### EEPROM 에뮬레이션

RP2350 에는 EEPROM 이 없다. flash 마지막 섹터를 쓴다.

- `hw/driver/eeprom.c` — flash 기반. hola-mini 판을 가져온다
- **XIP 주의**: flash 쓰기 중에는 코드 실행이 멈춘다.
  듀얼코어라 `flash_safe_execute()` 또는 `multicore_lockout` 이 필요하다.
  core1 이 `tuh_task()` 를 돌고 있으므로 **이 부분이 이 단계의 핵심 리스크다.**

### QMK 옵션

```
VIA_ENABLE
RAW_ENABLE
DYNAMIC_KEYMAP_ENABLE
```

### bootloader_jump

VIA 의 "bootloader" 버튼 → `reset_usb_boot(0, 0)`.
02단계에서 만든 것과 같은 함수다. → [usb-stack.md](usb-stack.md#펌웨어-업데이트)

### VIA 정의 파일

`info.json` 을 VIA 형식으로 만든다. 16×16 가상 매트릭스를 어떻게 보여줄지가 문제다 —
256키를 그대로 그리면 쓸 수 없으므로, **실제로 쓰는 키만 추린 레이아웃**을 정의한다.

## 구현 항목

- [ ] `hw/driver/eeprom.c` — flash 기반, 듀얼코어 안전
- [ ] `ap/modules/qmk/via/port/via_hid.c` — raw HID ↔ TinyUSB
- [ ] `via.c` / `dynamic_keymap.c` 를 빌드에 포함
- [ ] `info.json` — VIA 레이아웃 정의
- [ ] `bootloader_jump()` 연결
- [ ] CLI `eeprom` 명령 (덤프 / 지우기)

## 완료 판정

1. VIA 웹앱이 장치를 인식한다
2. 키맵을 바꾸면 즉시 반영된다
3. **재부팅 후에도 유지된다**
4. VIA 의 bootloader 버튼으로 BOOTSEL 진입
5. flash 쓰기 중에 core1 의 USB host 가 죽지 않는다

## 열린 질문

| 항목 | 내용 |
|---|---|
| **flash 쓰기 vs core1** | XIP 정지 중 core1 의 `tuh_task()` 가 어떻게 되는지가 최대 리스크다. `flash_safe_execute()` 로 core1 을 잠그거나, 키 입력이 없는 타이밍에만 쓰거나 |
| 레이아웃 | 256키를 다 노출할 수 없다. 어떤 부분집합을 보여줄지 |
| VID / PID | VIA 는 VID/PID 로 정의 파일을 찾는다. `info.json` 과 반드시 일치해야 한다 |
| EEPROM 크기 | dynamic keymap 이 레이어 수 × 256키 × 2바이트다. 레이어를 몇 개로 할지에 따라 flash 사용량이 결정된다 |
