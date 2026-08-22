# firm-sdk — 외부 SDK 영역

이 폴더는 **외부에서 가져온 SDK 와 라이브러리, 그리고 그것을 다루는 도구**만 담는다.
우리가 쓴 코드는 여기 넣지 않는다. 프로젝트 소스는 `../qmk-link/src/` 에 있다.

```
firm-sdk/
├── pico-sdk/          git submodule, 2.3.0 고정
├── Pico-PIO-USB/      git submodule (3단계에서 추가)
├── upstream.json      QMK / Vial 리비전 고정 (5단계)
├── upstream/          .gitignore — fetch_upstream.py 가 받아온다 (5단계)
├── .picotool/         .gitignore — SDK 가 받아 빌드하는 picotool
└── tools/             OS 무관 파이썬 도구
    ├── flash.py           uf2 다운로드
    ├── list_ports.py      CDC 포트 열거 (2단계)
    └── fetch_upstream.py  QMK / Vial 받아오기 (5단계)
```

## pico-sdk

**2.3.0 태그에 고정**되어 있다. 부모 저장소가 커밋 SHA 를 기록하므로 클론하면 같은 버전이 나온다.

무선용 서브모듈(`btstack` 220MB, `mbedtls` 51MB, `cyw43-driver`, `lwip`)은
**이 보드에 무선이 없어서 받지 않는다.** `lib/tinyusb` 만 필요하다.

```bash
git submodule update --init                                       # pico-sdk
git -C pico-sdk submodule update --init lib/tinyusb               # tinyusb 만
```

`--recursive` 로 받으면 338MB 가 되고, 위처럼 받으면 49MB 다.

## picotool

**따로 설치하지 않아도 된다.** SDK 가 uf2 를 만들 때 필요로 하는데, PATH 에 없으면
`.picotool/` 에 직접 받아 빌드한다.

단 SDK 가 빌드하는 picotool 은 `-DPICOTOOL_NO_LIBUSB=1` 이라 **`picotool load` 는 못 한다.**
우리는 굽는 걸 `tools/flash.py` 로 하므로 상관없다.

## QMK / Vial (5단계부터)

`qmk_firmware` 는 전체 히스토리가 524MB, `vial-qmk` 는 265MB 다. 서브모듈로 두면
클론할 때마다 790MB 를 받게 되므로 **서브모듈로 두지 않는다.**

대신 `tools/fetch_upstream.py` 가 `upstream.json` 에 적힌 리비전으로
`--depth 1 --filter=blob:none --sparse` clone 해서 `upstream/` 에 둔다.
필요한 건 `quantum/` 뿐이라 (hola-mini 기준 519 파일 / 3.5MB) 리포당 5MB 안팎이다.
`upstream/` 은 `.gitignore` 라서 이 저장소 크기는 늘지 않는다.

자세한 근거는 `../docs/00-context.md` 와 `../docs/05-qmk.md` 를 본다.
