# 04-usb-device-hid — PC 에 키보드로 보인다

**상태: ⬜ 미착수** — 구현 상세는 착수할 때 채운다.

## 목표

Type-C 쪽 descriptor 에 HID 를 추가하고, **03단계에서 받은 리포트를 그대로 PC 로 흘린다.**
QMK 없이 "USB 연장선"이 되는 지점이다.

## 배경 / 근거

USB 양쪽(host / device)이 다 동작한다는 걸 확인하고 나서 키 처리로 넘어간다.
여기까지 되면 남은 건 순수 소프트웨어 문제고, 하드웨어·USB 리스크는 다 걷힌 상태가 된다.

02단계에서 CDC descriptor 를, 03단계에서 host 리포트를 물려받는다.

## 설계

→ [usb-stack.md](usb-stack.md#pc-쪽-복합-장치-구성)

```
IAD ─ CDC (comm + data)       : CLI · 로그        (02단계에서 이미 있음)
HID Keyboard                   : boot protocol 호환
HID Extra                      : consumer / system control, NKRO
HID Raw (usage page 0xFF60)    : VIA / Vial 통신 (06~07단계에서 쓴다)
Vendor (RESET)                 : picotool 호환용   (02단계에서 이미 있음)
```

`CFG_TUD_HID 3`. Windows 에서 CDC + HID 복합 장치를 제대로 잡으려면 **IAD** 가 필수다.

이번 단계에서 raw HID 는 **엔드포인트만 뚫어 두고 응답은 하지 않는다** (06단계에서 채운다).

### 패스스루

```
core1: tuh_hid_report_received_cb()  →  qbuffer
core0: qbuffer  →  tud_hid_report()
```

boot report 8바이트를 그대로 옮긴다. 키코드 변환도, 레이어도 없다.

## 구현 항목

- [ ] `tusb_config.h` — `CFG_TUD_HID 3`
- [ ] `usbd_desc.c` — HID descriptor 3개 + IAD 유지
- [ ] `hw/driver/usb/usbd_hid.c` — 리포트 디스크립터, `tud_hid_*` 콜백
- [ ] `ap/modules/link/` — host 리포트 → device 리포트 패스스루
- [ ] `tud_hid_set_report_cb` — LED (CapsLock 등) 수신, WS2812 로 표시
- [ ] CLI `usb info` — mounted / suspended / 리포트 카운트

## 완료 판정

1. Type-C 를 PC 에 꽂으면 **키보드 + 시리얼 포트**로 동시에 인식된다
2. USB-A 에 키보드를 꽂고 타이핑하면 **PC 에 그대로 입력된다**
3. CapsLock 을 누르면 호스트가 LED 리포트를 보내고 WS2812 색이 바뀐다
4. BIOS / 부트로더 화면에서도 동작한다 (boot protocol)
5. Windows · macOS · Linux 에서 각각 인식된다

## 열린 질문

| 항목 | 내용 |
|---|---|
| NKRO | boot protocol 로는 6키까지다. Extra 인터페이스에 NKRO 리포트를 둘지 05단계에서 판단 |
| 리포트 지연 | host→device 를 코어 간 큐로 넘기므로 지연이 생긴다. 실측해서 1ms 안에 드는지 확인 |
| suspend / resume | PC 가 자면 어떻게 할지. 08단계로 이월 가능 |
| USB-A 키보드가 없을 때 | HID 인터페이스는 그대로 노출한다. 리포트만 안 나간다 |
