# 06-via — VIA 로 키맵을 편집한다

**상태: ✅ 완료**

## 목표

VIA 웹앱에서 키맵과 옵션을 편집하고 저장한다. 재부팅해도 유지된다.

## 배경 / 근거

05단계에서 QMK 가 돌지만 키맵이 컴파일 시점에 박혀 있다.
편집 가능하게 만들려면 raw HID 프로토콜과 EEPROM 이 필요하다.
raw HID 엔드포인트(IF2)는 04단계에서 이미 뚫어 뒀다. 여기서 응답을 채운다.

## 설계

### 1. EEPROM 을 flash 로 — 이 단계의 핵심 리스크였다

RP2350 에는 EEPROM 이 없어 내장 플래시로 흉내낸다.
문제는 **소거·기록 동안 XIP 가 멈추는데 core1 이 PIO USB 를 돌고 있다**는 것이다.
core1 이 그 사이 플래시 코드를 인출하면 그대로 죽는다.

```c
flash_safe_execute_core_init();   // core1 쪽 — usbhCore1Main() 맨 앞
flash_safe_execute(func, param, timeout_ms);   // core0 쪽 — hw/driver/flash.c
```

`flash_safe_execute()` 가 core1 을 RAM 안 루프에 세웠다가 끝나면 풀어 준다.
core1 이 `flash_safe_execute_core_init()` 을 불러 두지 않으면 SDK 가 거절한다
(`PICO_ERROR_NOT_PERMITTED`).

#### ★ 정지를 없앤다 — 펌웨어를 통째로 RAM 에서 돌린다

```cmake
pico_set_binary_type(${PRJ_NAME} copy_to_ram)
target_compile_definitions(${PRJ_NAME} PRIVATE PICO_FLASH_ASSUME_CORE1_SAFE=1)
```

**소거 중 core1 이 도는 횟수 (`flash test` 실측)**

| | 소거 35ms 동안 `tuh_task()` |
|---|---:|
| XIP 실행 + `flash_safe_execute` lockout | **3 회** |
| RAM 실행 (`copy_to_ram`) | **37,456 회** |

RAM 에서 돌면 XIP 가 멈춰도 인출할 것이 없다. `PICO_FLASH_ASSUME_CORE1_SAFE=1`
로 lockout 을 끄면 core0 만 인터럽트를 막고 기다리고, **core1 의 PIO USB 는
한순간도 멈추지 않는다.**

비용은 RAM 뿐이다 — 111KB(펌웨어) + 데이터 = 152KB / 512KB (29%).

##### 여기까지 오는 데 두 번 헛짚었다

**① "41ms 정지는 견딜 만하다" — 틀렸다.**

core1 이 멈춘 동안 SOF 가 끊겨 키보드가 서스펜드에 빠지고 깨어나지 못한다.
그런데 TinyUSB 의 `hidh_xfer_cb` 는 전송 실패를 무시하고 길이 0 으로 콜백한다:

```c
(void) result;   // hid_host.c
tuh_hid_report_received_cb(daddr, idx, epbuf->epin, (uint16_t) xferred_bytes);  // 0
```

그래서 `rx` 는 계속 늘고 `mounted 1` · `drop 0` 이라 **겉보기엔 멀쩡한데 내용이 빈
리포트**다. 길이 0 이라 `link` 로도 안 가고 `isKeyDown()` 도 안 걸려 LED 도 안 깜빡인다.

| | drain | → link | 버림 |
|---|---:|---:|---:|
| 리셋 직후 | 11 | 11 | 0 |
| `flash test` ×3 뒤 | 85 | 15 | **70** |
| 그 뒤 계속 | 증가 | **15 고정** | 증가 |

**`mounted` / `drop` 은 이 고장을 못 잡는 지표다.** CLI `key info` 가 이걸 보라고 있다.

**② 뒤처리로 덮으려 한 두 시도 모두 실패했다.**

- `pio_usb_host_stop()` / `pio_usb_host_restart()` — **Pico-PIO-USB 0.7.2 에서 죽은
  코드다.** 플래그를 세우고 내려가기를 무한 대기하는데 내리는 곳이 소스에 없다.
  부르는 순간 그 코어가 멈춘다.

  ```c
  pio_usb_host.c:102   cancel_timer_flag = true;
  pio_usb_host.c:103   while (cancel_timer_flag) { continue; }
  ```

- `hcd_event_device_remove()` → `hcd_event_device_attach()` 로 재열거 —
  **떼기는 되는데 다시 붙지 못한다.** `connect st 1` · `speed full` 인데
  `mounted 0` 으로 굳고 자체 회복도 안 된다.

정지를 만들어 놓고 되살리려 하지 말고 애초에 만들지 않는 것이 답이었다.

##### QMK 의 RP2040 EEPROM 은 참고가 안 된다 (이 문제에 한해서)

`platforms/chibios/drivers/wear_leveling/wear_leveling_rp2040_flash.c` 는
`save_and_disable_interrupts()` + RAM 함수만 쓴다. **멀티코어 처리가 아예 없다** —
QMK 는 core1 에서 PIO USB 호스트를 돌리지 않기 때문이다.
우리 정지 문제는 그 구성에서는 생기지 않는다.

다만 **wear leveling 자체는 별개로 가치가 있다** → [열린 질문](#열린-질문)

**실측** (W25Q16 / clk_sys 120MHz, `flash test` CLI):**실측** (W25Q16 / clk_sys 120MHz, `flash test` CLI):

| | 시간 | 그 사이 core1 `tuh_task()` |
|---|---:|---:|
| 소거 4KB | 약 30 ms | 3~4 회 |
| 기록 4KB | 약 10 ms | 0 회 |
| 합계 | **약 41 ms** | — |

평소 `tuh_task()` 는 초당 150만 회쯤 돈다. 즉 **USB 호스트가 통째로 멈추는 시간**이다.
위 표는 `copy_to_ram` 이전, XIP 로 실행하며 lockout 을 걸었을 때의 값이다.
지금은 core1 이 멈추지 않으므로 이 시간은 **core0 만의 정지**다 (USB device 쪽은
NAK 로 버티고 호스트가 재시도한다).

그래도 줄일 수 있는 만큼 줄였다:

- **0xFF 페이지는 쓰지 않는다.** 소거 직후 섹터는 전부 0xFF 다. 값이 든 페이지만 쓰면
  대부분 비어 있는 EEPROM 에서 기록이 거의 0 이 된다 (실측 41ms → 28ms)
- **연속된 페이지는 한 번에 묶는다.** `flashWrite()` 한 번이 lockout 한 번이다.
  4KB 를 페이지마다 부르면 16회가 되어 49ms, 묶으면 1회로 42ms
- **섹터 사이를 띄운다.** 4섹터를 붙여 쓰면 164ms 연속 정지가 된다.
  `EE_FLUSH_MS`(200ms) 간격으로 하나씩 쓴다

#### RAM 섀도 + 지연 플러시

QMK 는 EEPROM 을 바이트 단위로 아무 때나 쓴다. NOR 플래시는 그렇게 못 쓴다.

| | |
|---|---|
| 읽기 | RAM 섀도에서 바로. 플래시를 건드리지 않는다 |
| 쓰기 | RAM 섀도를 고치고 그 섹터를 dirty 로 표시만 |
| 플러시 | 조용해진 뒤(`EE_FLUSH_MS`)에 dirty 섹터를 하나씩 소거+기록 |

VIA 로 키맵을 바꾸면 바이트 쓰기가 수백 번 연달아 온다. 매번 쓰면 같은 섹터를
수백 번 지우게 되고(수명) 그동안 USB 가 멈춘다. 모았다 쓰면 소거가 섹터당 1회다.

`eeprom_task()` 를 `qmkUpdate()` 가 부른다. **QMK 가 꺼져 있어도 부른다** —
껐을 때 남은 dirty 섹터가 그대로 날아가면 안 된다.

#### VIA 와 Vial 을 다른 자리에 둔다

```
0x1F0000  VIA  EEPROM  16KB (4섹터)
0x1F4000  Vial EEPROM  16KB (4섹터)
0x1F8000  (예약)       32KB
0x200000  끝
```

같은 보드에 두 펌웨어를 번갈아 구울 수 있다. 한 영역을 공유하면 트리를 바꿔 구운
순간 상대가 남긴 바이트를 자기 레이아웃으로 읽는다. eeconfig 매직이 우연히 맞으면
초기화도 안 되고 엉뚱한 키맵이 나온다. 아예 떼어 놓는다.

크기는 16KB 다. 동적 키맵만 8레이어 × 16 × 16 × 2B = 4096B 이고
나머지를 eeconfig · VIA user data · 매크로 버퍼가 나눠 쓴다.

#### flash 드라이버

`common/hw/include/flash.h` 는 **wish-he 것을 그대로** 가져왔다 (오프셋 기준 주소).
구현 `hw/driver/flash.c` 만 RP2350 용으로 새로 썼다.

`rp2040_fw` 계열에서 가져온 것:

- **영역 가드 (`flash_tbl`)** — 주소를 잘못 넘기면 자기 펌웨어를 지운다.
  다만 그쪽 `flashInSector()` 는 **겹치기만 하면** 통과시킨다(0x000000 부터 2MB 도 통과).
  여기서는 요청 구간이 표 안에 **완전히 들어갈 때만** 통과시킨다
- **페이지 단위 read-modify-write** — 정렬되지 않은 부분 기록 지원
- **디바이스 ID (`flash_do_cmd` 0x90)** — `flash info` 에서 확인 (실측 `EF 14` = W25Q16)

멀티코어는 참고할 게 없었다. RP2040 프로젝트 중 `flash_safe_execute` / `multicore_lockout`
을 쓰는 곳이 하나도 없다 — 전부 `__disable_irq()` 단독이고, core1 이 도는 상황이 없었다.

**주소 규약이 다르다.** rp2040 계열 `flash.c` 는 XIP 절대주소(0x10000000~)를 받고
여기는 오프셋이다. 그쪽 코드를 베껴 올 때 주소를 그대로 넘기면 안 된다.

### 2. raw HID — TinyUSB 로

wish-he 판은 CherryUSB 용이라 새로 썼다.

```
받기    tud_hid_set_report_cb -> 큐 -> qmkUpdate() 에서 raw_hid_receive()
보내기  host_driver_t 의 send_raw_hid -> usbdHidSendRaw()
```

**받는 쪽이 큐를 타는 이유**: OUT 리포트는 `tud_task()` 안의 콜백으로 들어온다.
거기서 바로 VIA 를 처리하면 응답을 보내려고 IN 엔드포인트를 기다리는데,
그 대기가 다시 `tud_task()` 를 부른다. 큐에 넣고 메인 루프에서 꺼낸다.

`usbdHidSendRaw()` 안에서 **`delay()` 를 쓰면 안 된다.**
`bsp.c` 의 `delay()` 는 `cliLoopIdle()` 을 돌리고 그 안에 `qmkUpdate()` 가 있다.
VIA 처리 중에 다시 VIA 처리로 들어간다. `tud_task()` 만 직접 돌린다.

**★ `RAW_ENABLE` 은 `host_driver_t` 에 6번째 멤버(`send_raw_hid`)를 만든다.**
`port/driver_usb.c` 의 초기화도 같은 `#ifdef` 로 묶여 있다. 한쪽만 켜면 구조체가 어긋난다.

### 3. 키보드 정의 폴더 (wish-he 관례)

```
firmware/qmk-link/
├── keyboards/qmk-link/
│   ├── config.h            via · vial 공용
│   ├── keymap.c            공용 — usage 패스스루
│   ├── layout-kle.json     ★ 손으로 편집하는 건 이것 하나
│   ├── menus.json          손으로 씀 — VIA 커스텀 메뉴
│   └── layout-via.json     생성물
└── tools/gen_keymap.py     KLE + menus -> layout-via.json
```

**KLE 범례는 주소가 아니라 키 이름**(`ESC`, `A`, `LSFT`, `P7` …)이다.
`gen_keymap.py` 가 이름 → HID usage → `"row,col"` 로 바꾼다.
주소를 손으로 적으면 반드시 어긋난다.

```bash
python3 tools/gen_keymap.py          # layout-via.json 생성
python3 tools/gen_keymap.py --show   # 매핑 표 + KLE 에 빠진 usage
python3 tools/gen_keymap.py --check  # 생성물이 최신인지만 확인
```

생성기가 막아 주는 것: 모르는 키 이름, 좌표 중복.

### 4. 배열 — 왜 풀사이즈인가

**이 배열은 물리 PCB 가 아니다. 그냥 그림이다.**
매트릭스 좌표가 HID usage 라서(`row = usage>>4`, `col = usage&0xF`) 그림이
키가 동작하는지를 결정하지 않는다.

여기서 세 가지가 따라온다:

1. **풀사이즈로 그린다.** TKL / 75% / 65% / 60% 는 전부 풀사이즈 usage 의
   부분집합이라 그대로 덮인다
2. **레이아웃 옵션이 필요 없다.** ISO Enter 도 ANSI Enter 도 똑같이 `0x28` 을 보낸다.
   wish-he 의 `labels.json`(백스페이스 2U/스플릿 같은 것)에 해당하는 게 없다
3. **그림에 없는 키도 동작한다.** 키맵 256칸이 전부 패스스루라, 안 그린 키는
   "못 쓰는" 게 아니라 "VIA 에서 못 고치는" 것뿐이다

그래서 배열이 다를 때의 답은 **ANSI 에 없는 usage 를 별도 서랍으로 붙이는 것**이다.
현재 3줄이 붙어 있다:

| 서랍 | 내용 |
|---|---|
| F13 ~ F24 | `0x68`~`0x73` |
| ISO / JIS / 한글 | `NUBS` `NUHS` `PEQL` `PCMM` `INT1`~`INT6` `LNG1`~`LNG5` |
| 편집 / 미디어 | `MUTE` `VOLU` `VOLD` `PWR` `EXEC` `HELP` `MENU` `SLCT` `STOP` `AGIN` `UNDO` `CUT` `COPY` `PSTE` `FIND` |

합쳐 146키. 빠진 것은 `INT7`~`INT9` / `LNG6`~`LNG9` 뿐이고 실물이 거의 없다.
모르는 usage 가 오면 **CLI `qmk matrix`** 로 확인하고 `layout-kle.json` 에 한 줄 더한다.

### 5. 빌드 옵션을 VIA 로 뺀다 — custom menu

QMK 는 NKRO · 탭홀드를 보통 컴파일 타임 매크로로 정한다. 바꾸려면 다시 구워야 한다.
이 보드는 **꽂는 키보드가 매번 달라서** 그게 특히 불편하다.
Vial 의 QMK settings 와 같은 효과를 VIA 에서 얻는다 — **웹앱을 포크하지 않는다.**

```
호스트 -> 장치   [0] id_custom_get/set_value  [1] 채널  [2] 값 ID  [3..] 값
```

| 채널 | 값 ID | 항목 | QMK 훅 |
|---:|---:|---|---|
| 14 | 1 | Tapping Term (50~500) | `get_tapping_term()` + `get_quick_tap_term()` |
| 14 | 2 | Hold On Other Key Press | `get_hold_on_other_key_press()` |
| 14 | 3 | Permissive Hold | `get_permissive_hold()` |
| 14 | 4 | Retro Tapping | `get_retro_tapping()` |
| 15 | 1 | NKRO | `keymap_config.nkro` (eeconfig) |
| 15 | 2 | Passthrough | `ap.c` — QMK 를 건너뛴다 |

**★ `*_PER_KEY` 매크로가 없으면 QMK 가 `get_*()` 를 아예 부르지 않는다.**
`action_tapping.c` / `action.c` 가 컴파일 상수로 굳혀 버려서, 메뉴를 만들어 놓고도
슬라이더가 아무 일도 하지 않는 상태가 된다. `config.h` 에서 켠다:

```c
#define TAPPING_TERM_PER_KEY
#define QUICK_TAP_TERM_PER_KEY
#define HOLD_ON_OTHER_KEY_PRESS_PER_KEY
#define PERMISSIVE_HOLD_PER_KEY
#define RETRO_TAPPING_PER_KEY
```

**★ 퀵탭텀도 같이 따라가게 한다.** QMK 는 `QUICK_TAP_TERM` 을 안 주면
`TAPPING_TERM` 으로 잡는데 그건 컴파일 때 200 으로 굳는다. 탭텀을 150 으로 내려도
퀵탭텀은 200 으로 남아 원래 지키려던 `퀵탭텀 <= 탭텀` 이 깨진다 (wish-he 에서 겪은 것).

**★ 탭텀은 두 바이트, 큰 자리가 먼저다.** 앱은 슬라이더 최댓값이 255 를 넘으면
자동으로 2바이트로 보낸다.

**★ 계층이 세 겹이어야 한다 — 메뉴 > 그룹 > 컨트롤.**

`menus[i].content[]` 에 컨트롤을 바로 넣으면 VIA 가 정의를 **통째로 거부한다.**
스키마가 그 자리를 그룹(`{label, content:[객체...]}`)으로만 받기 때문이다.
처음에 두 겹으로 썼다가 앱에서 이렇게 나왔다:

```
/menus/0/content/0: must NOT have additional properties      <- type · options
/menus/0/content/0/content/0: must be object                 <- content 가 [id, ch, val]
```

wish-he 도 `QMK > Key Handling > 컨트롤` 3단이다. VIA 에 넣어 봐야 알 수 있는
종류의 실수라 `tools/gen_keymap.py` 가 계층을 검사한다 (타입 · content 모양 ·
예약 채널까지).

**★ 채널 ID 는 14 부터.** 1~5 는 VIA 가 조명용으로 예약해 뒀다.

**★ 라벨은 영어로 쓴다.** 웹앱이 이 문자열을 그대로 i18n 키로 쓴다.
한글을 박으면 어느 언어로 보든 한글만 나온다 (wish-he 에서 겪은 것).

값은 `EECONFIG_USER_DATA` 영역(64B)에 `version` 을 앞세운 구조체로 둔다.
구조가 바뀌면 `LINK_CFG_VERSION` 을 올려 옛 EEPROM 을 기본값으로 되돌린다.

#### 디바운스 메뉴는 두지 않는다

우리 `matrix.c` 는 `debounce()` 를 아예 부르지 않는다 (원본 키보드가 이미 했다).
그 메뉴를 두면 아무것도 하지 않는 스위치가 된다. wish-he 와 같은 이유다.

### 6. Test Matrix — `VIA_INSECURE` 를 켠다

**★ 이게 없으면 upstream `via.c` 가 매트릭스 조회에 무조건 0 을 넣는다.**

```c
case id_switch_matrix_state: {
#if defined(VIA_INSECURE)
    matrix_row_t value = matrix_get_row(row + offset);
#elif defined(SECURE_ENABLE)
    ... secure_is_unlocked() 일 때만 ...
#else
    matrix_row_t value = 0;      // <- 기본값. 기능이 있는 척하고 영영 0 이다
#endif
```

**이 프로젝트에서는 특히 쓸모가 크다.** 꽂은 키보드가 어떤 HID usage 를 보내는지
VIA 에서 바로 보인다 — 좌표가 곧 usage 라 눌린 키가 배열에서 그대로 반짝인다.
이게 없으면 CLI `qmk matrix` 로만 알 수 있어서 시리얼을 안 붙이는 사용자는 방법이 없다.

대가는 raw HID 를 여는 앱이 눌린 키를 읽을 수 있다는 것이다. QMK 가 기본으로 막아
둔 이유가 그것이다. 이 보드는 그 위험보다 "어떤 usage 가 오는지 알아야 한다" 는
쪽이 크다고 보고 켰다.

응답 형식 (직접 두드려 볼 때 헷갈린다):

```
요청   [0] 0x02(id_get_keyboard_value)  [1] 0x03  [2] offset
응답   [0] 0x02  [1] 0x03  [2] offset(에코)  [3..] 행마다 2바이트, 큰 자리 먼저
```

`MATRIX_COLS` 가 16 이라 한 요청에 `28/2 = 14` 행씩 온다 → offset 0 과 14 로 두 번.

### 7. bootloader_jump

**★ `id_bootloader_jump` 는 upstream `via.c` 에 구현이 없다.**
`via.h` 에 enum(0x0B) 만 있고 switch 에 case 가 없다 — 키보드 쪽에서 처리하라는 뜻이다.
`via_command_kb()` 가 `raw_hid_receive()` 맨 앞에서 불리고, `true` 를 주면
"응답까지 내가 다 했다" 는 의미다.

응답을 먼저 보내고 20ms 뒤에 넘어간다. 안 그러면 앱이 타임아웃으로 오해한다.

`VIA_EEPROM_ALLOW_RESET` 도 켰다 — 앱의 "Reset EEPROM" 이 살아난다.

### 8. QMK 자동 시작

**05단계까지는 `qmk start` 로만 켰다.** 이식 중에 `qmkInit()` 안에서 죽으면
USB 가 통째로 안 올라와 BOOTSEL 로만 되살릴 수 있어서였다.
VIA 까지 실기에서 확인됐으므로 `apInit()` 에서 자동으로 올린다.
되살릴 길은 남아 있다 — Key2(Reset) 더블클릭이면 BOOTSEL 이다.

## 구현 항목

- [x] `common/hw/include/flash.h` (wish-he 인터페이스 그대로) + `hw/driver/flash.c`
- [x] `usbhCore1Main()` 에 `flash_safe_execute_core_init()`
- [x] `port/platforms/eeprom.c` — RAM 섀도 + 지연 플러시
- [x] `via.c` / `dynamic_keymap.c` / `raw_hid.c` / `nvm_*.c` 를 빌드에 포함
- [x] `usbd_hid.c` — raw HID 수신 큐 + 송신
- [x] `port/driver_usb.c` — `send_raw_hid`
- [x] `keyboards/qmk-link/` + `tools/gen_keymap.py`
- [x] `port/via_port.c` — 커스텀 메뉴 + `via_command_kb()`
- [x] `bootloader_jump()` 연결
- [x] CLI — `flash info/read/erase/test`, `qmk eeprom [flush|erase]`
- [x] QMK 자동 시작

## 완료 판정

| # | 항목 | 결과 |
|---|---|---|
| 1 | VIA 프로토콜 응답 | ✅ version 13, layer count 8 |
| 2 | 키맵 읽기 / 쓰기 | ✅ `[0][0][4]` 0x0004 → 0x001D |
| 3 | **재부팅 후 유지** | ✅ 0x001D 남음, `flush cnt 0` (다시 쓰지 않음) |
| 4 | 커스텀 메뉴 | ✅ 6개 항목 get/set, 재부팅 후 유지, 모르는 채널은 `id_unhandled` |
| 5 | bootloader 버튼 | ✅ 응답 0x0B 후 BOOTSEL 진입 |
| 6 | flash 쓰기 중 키보드 생존 | ✅ 소거 중 core1 `tuh_task` 37,456회. `flash test` 4회 + VIA 쓰기 4회에도 `connected 1` · `버림 0` · `drain == link` |

## 열린 질문

| 항목 | 내용 |
|---|---|
| VIA 웹앱 실물 확인 | 메뉴 계층 오류는 앱에서 잡아 고쳤다. 배열 그림이 제대로 나오는지는 아직 확인 중 |
| 미디어키 | 원본 키보드가 consumer 페이지로 보내는 키는 아직 안 받는다. `updateKeyboard()` 가 `HID_ITF_PROTOCOL_KEYBOARD` 만 본다 → 08단계 |
| 매크로 버퍼 | `DYNAMIC_KEYMAP_MACRO_COUNT` 기본값을 그대로 쓴다. 16KB 안에서 남는 만큼이 버퍼다 |
| **wear leveling** | 지금은 키 하나 바꿀 때마다 섹터를 지운다(소거 1회/저장). QMK 의 `quantum/wear_leveling/` 은 append 로그라 소거가 훨씬 드물다. 정지 문제는 `copy_to_ram` 으로 해결됐으니 급하지 않지만 **수명 면에서는 그쪽이 맞다.** 백엔드(`wear_leveling_rp2040_flash.c`)는 RP2040 전용 레지스터를 쓰므로 우리 `flash.c` 위에 새로 얹어야 한다 |
| 첫 부팅 정지 | 빈 EEPROM 에서 동적 키맵을 처음 채울 때 4섹터를 쓴다. 200ms 간격이라 총 1초쯤 |
