# qmk-link 문서

RP2350-USB-A 보드로 만드는 **USB 키보드 변환기** 펌웨어 문서다.

```
[일반 USB 키보드] --USB-A(J1)--> [RP2350] --Type-C--> [PC]
                    PIO USB host            HID + CDC
```

---

## 어디부터 읽나

**처음이거나, 이어서 작업하러 왔다면 → [00-context.md](00-context.md)**

그 문서 하나에 참조 프로젝트 경로 · 빌드 환경 · 확정된 결정과 이유 ·
하지 않기로 한 것 · 열린 질문 · 현재 위치가 다 있다.

---

## 목록

### 항상 참고

| 문서 | 내용 |
|---|---|
| [00-context.md](00-context.md) | **이어서 작업하기 위한 정보.** 현재 위치, 참조 경로, 확정된 결정 |
| [roadmap.md](roadmap.md) | 전체 단계와 진행 상태, 왜 이 순서인지 |
| [hardware.md](hardware.md) | 핀 배정과 회로도 검증 근거. **코드에서 핀 만지기 전에 필수** |
| [usb-stack.md](usb-stack.md) | USB 이중 역할 · 클럭 · descriptor · 펌웨어 업데이트. 02~04단계 근거 |

### 개발환경 구축

| 문서 | 상태 |
|---|---|
| [setup-macos.md](setup-macos.md) | ✅ 검증됨 |
| [setup-windows.md](setup-windows.md) | ⬜ 미검증 |
| [setup-linux.md](setup-linux.md) | ⬜ 미검증 |

### 단계별

| 문서 | 상태 | 한 줄 |
|---|---|---|
| [01-led.md](01-led.md) | ✅ | 빌드하고 굽는 경로를 뚫는다 |
| [02-cli-cdc.md](02-cli-cdc.md) | ⬜ | 관측 수단(CDC)을 만든다 |
| [03-usb-host.md](03-usb-host.md) | ⬜ | USB-A 에 꽂힌 키보드를 읽는다 |
| [04-usb-device-hid.md](04-usb-device-hid.md) | ⬜ | PC 에 키보드로 보인다 |
| [05-qmk.md](05-qmk.md) | ⬜ | QMK 를 얹는다 |
| [06-via.md](06-via.md) | ⬜ | VIA 로 키맵을 편집한다 |
| [07-vial.md](07-vial.md) | ⬜ | Vial 트리를 추가한다 |
| [08-finalize.md](08-finalize.md) | ⬜ | 마감 |

---

## 문서 규칙

- 파일명은 **소문자 kebab-case**. `README.md` 만 예외
- 단계 문서는 같은 틀을 쓴다 — 목표 / 배경·근거 / 설계 / 구현 항목 / 완료 판정 / 열린 질문
- **단계를 끝내면** [00-context.md](00-context.md) 의 "현재 위치" 와
  [roadmap.md](roadmap.md) 의 상태를 반드시 갱신한다
- 결정을 바꾸면 [00-context.md](00-context.md) 의 "확정된 결정과 이유" 에 **이유까지** 남긴다.
  같은 논의를 두 번 하지 않기 위해서다
