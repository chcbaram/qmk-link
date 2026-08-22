# setup-linux — Linux 개발환경 구축

> **미검증.** 01단계는 macOS 에서만 빌드를 확인했다.
> Linux 에서 처음 돌려 보고 다른 점이 있으면 이 문서를 고친다.

---

## 1. 툴체인

### Debian / Ubuntu

```bash
sudo apt update
sudo apt install -y gcc-arm-none-eabi libnewlib-arm-none-eabi \
                    libstdc++-arm-none-eabi-newlib \
                    cmake ninja-build build-essential git python3 python3-pip
```

배포판 패키지가 오래됐으면(gcc 10 이하) Arm 공식 배포판을 쓴다:
[Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
을 받아 풀고 `bin/` 을 PATH 에 넣는다.

### Fedora

```bash
sudo dnf install -y arm-none-eabi-gcc-cs arm-none-eabi-newlib \
                    cmake ninja-build git python3 python3-pip
```

### Arch

```bash
sudo pacman -S arm-none-eabi-gcc arm-none-eabi-newlib \
               cmake ninja git python python-pyserial
```

확인:

```bash
arm-none-eabi-gcc --version    # 10 이상, 14.x 권장
cmake --version                # 3.13 이상
```

## 2. 파이썬 패키지 (선택)

`pyserial` 은 `flash.py` 의 **1200bps touch** 에만 쓰인다.
없어도 Key1(BOOTSEL) 을 눌러 굽는 건 된다.

```bash
pip3 install pyserial
# 배포판이 막으면
sudo apt install python3-serial      # Debian/Ubuntu
```

## 3. picotool

**설치하지 않아도 된다.** SDK 가 `firmware/firm-sdk/.picotool/` 에 직접 받아 빌드한다.
그 빌드에는 호스트용 컴파일러가 필요하므로 `build-essential` 을 깔아 둔다.

## 4. 저장소

```bash
git clone https://github.com/chcbaram/qmk-link.git
cd qmk-link

git submodule update --init
git -C firmware/firm-sdk/pico-sdk submodule update --init lib/tinyusb
```

**`--recursive` 를 쓰지 않는다.** 무선용 서브모듈(btstack 220MB 등)까지 받아 338MB 가 된다.
위처럼 받으면 49MB.

## 5. 빌드

```bash
cd firmware/qmk-link
cmake -S . -B build
cmake --build build -j$(nproc)
```

`FLASH` 가 **2 MB** 로 나와야 한다.

## 6. 다운로드

Key1(BOOTSEL) 을 누른 채 Type-C 를 연결한다.

```bash
python3 ../firm-sdk/tools/flash.py build/src/qmk-link.uf2
```

### 자동 마운트가 안 되는 환경

`flash.py` 는 `/media/*/*`, `/run/media/*/*`, `/mnt/*` 를 훑는다.
데스크톱 환경이 없어 자동 마운트가 안 되면 직접 마운트한다.

```bash
lsblk                                    # RP2350 장치 확인 (보통 8MB 남짓)
sudo mkdir -p /mnt/rp2350
sudo mount /dev/sdX1 /mnt/rp2350
python3 ../firm-sdk/tools/flash.py build/src/qmk-link.uf2
sudo umount /mnt/rp2350                  # 보드가 먼저 재부팅하면 생략 가능
```

### udev 규칙 (권장)

BOOTSEL 장치와 CDC 포트를 sudo 없이 쓰려면:

```bash
sudo tee /etc/udev/rules.d/99-rp2350.rules >/dev/null <<'RULES'
# Raspberry Pi RP2 BOOTSEL / 실행 중 펌웨어
SUBSYSTEM=="usb", ATTRS{idVendor}=="2e8a", MODE="0666"
SUBSYSTEM=="tty", ATTRS{idVendor}=="2e8a", MODE="0666"
RULES
sudo udevadm control --reload-rules && sudo udevadm trigger
```

시리얼 포트 접근에는 그룹 추가도 필요할 수 있다 (재로그인 필요):

```bash
sudo usermod -aG dialout $USER     # Debian/Ubuntu
sudo usermod -aG uucp $USER        # Arch
```

확인:

```bash
python3 ../firm-sdk/tools/flash.py --list
```

## 7. VSCode

```bash
code firmware/qmk-link/prj/qmk-link.code-workspace
```

`Ctrl+Shift+B` 가 `build-build` 다.

---

## 문제 해결

| 증상 | 원인 / 해결 |
|---|---|
| `cannot find -lstdc++` / `-lm` | newlib 패키지가 빠졌다. `libnewlib-arm-none-eabi`, `libstdc++-arm-none-eabi-newlib` 설치 |
| FLASH 가 4MB 로 나온다 | 보드 헤더가 안 잡힌 것이다. `src/bsp/board/qmk_link.h` 경로 확인 |
| 볼륨을 못 찾는다 | 자동 마운트가 안 되는 환경이다. 위 6번 수동 마운트 |
| `Permission denied` (시리얼) | udev 규칙 + `dialout` 그룹 (위 6번) |
| 복사 도중 `Input/output error` | 보드가 uf2 를 다 받고 재부팅하며 나는 정상 동작이다. `flash.py` 는 이걸 무시한다 |
