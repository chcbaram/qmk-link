# 08-finalize — 마감

**상태: ⬜ 미착수** — 구현 상세는 착수할 때 채운다.

## 목표

앞 단계에서 "나중에" 로 미뤄 둔 것들을 정리한다.

## 항목

### WS2812 상태 인디케이터

지금은 그냥 점멸한다. 실제로 쓸모 있게 바꾼다.

| 상태 | 표시 |
|---|---|
| 부팅 / 초기화 | ? |
| USB-A 에 키보드 없음 | ? |
| 정상 동작 | ? |
| CapsLock / NumLock | ? |
| 레이어 | ? |
| USB suspend | 소등 |

### USB suspend / resume

- PC 가 자면 `tud_suspended()` → LED 소등, 전류 줄이기
- remote wakeup — USB-A 키보드를 누르면 PC 를 깨울지
- resume 후 host 쪽 재열거가 필요한지

### VID / PID 확정

개발 중에 쓴 임시값을 정리한다. `info.json` / `vial.json` 과 **반드시 일치**해야 한다.
VIA / Vial 은 VID/PID 로 정의 파일을 찾는다.

### 허브 대응

`CFG_TUH_HUB` 를 켤지 결정한다. 켜면 `CFG_TUH_DEVICE_MAX` 도 올려야 하고 메모리를 더 쓴다.
키보드 여러 개가 동시에 붙었을 때 비트맵을 어떻게 합칠지도 같이 정한다.

### 문서 정리

- [00-context.md](00-context.md) 의 "열린 질문" 을 비운다
- [roadmap.md](roadmap.md) 상태를 모두 완료로
- README 에 실제 사용법 (VIA/Vial 연결 방법, 지원 키보드) 추가

### 릴리스

- `qmk-link-via.uf2` / `qmk-link-vial.uf2` 를 GitHub Release 로
- [setup-windows.md](setup-windows.md) · [setup-linux.md](setup-linux.md) 를
  실제로 그 OS 에서 돌려 보고 검증 표시

## 완료 판정

- 열린 질문 표가 비어 있다
- 세 OS 에서 빌드 · 다운로드가 검증되었다
- VIA / Vial 양쪽 산출물이 릴리스되어 있다
