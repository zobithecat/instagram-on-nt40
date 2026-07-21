# NT 4.0 개발 환경 구축편 — QEMU + 툴체인 (M2 MacBook Air)

밑바닥 네이티브 클라이언트 프로젝트용 환경 셋업. Claude Code에 붙여넣어도 되고, 사람이 따라 해도 됨.

---

## 0. 큰 그림 (M2 = ARM이라 중요)
M2는 ARM이라 x86 하드웨어 가속이 **없다**. NT4(x86)는 QEMU **TCG(순수 에뮬레이션)** 로만 돈다 → 커뮤니티 가이드의 `-accel kvm`은 전부 **`-accel tcg`로 바꿔야** 함. 펜티엄 시절 체감속도지만 개발엔 충분. crypto/이미지 디코드가 느리니 **VM 내부 성능수치는 신뢰 금지**.

## 1. 툴체인 설치 (호스트 macOS)
```bash
# Xcode CLT (clang — 순수 코어 네이티브 테스트용)
xcode-select --install

# Homebrew 패키지
brew install qemu mingw-w64 cmake imagemagick wireshark
#   qemu        : VM
#   mingw-w64   : i686-w64-mingw32 크로스 컴파일러 (NT4 x86 PE 생성)
#   cmake       : mbedTLS/libwebp 크로스 빌드
#   imagemagick : screendump ppm→png 변환 (fallback용)
#   wireshark   : tshark (TLS/HTTP 와이어 디버깅)

# 파일 전송용 FTP 서버
pip3 install pyftpdlib

# 확인
i686-w64-mingw32-gcc --version
qemu-system-i386 --version
```

## 2. NT4 디스크 만들고 설치

### 디스크 생성 (4GB 넘기지 말 것)
```bash
qemu-img create -f qcow2 nt4.qcow2 2G
```
> ⚠️ NT4 setup은 **4GB 넘는 드라이브를 포맷 못 함**. 또 부팅 파티션은 옛 **7.8GB 실린더 한계** 안이어야 함 → 2GB로.

### 설치 실행
```bash
qemu-system-i386 \
  -M pc,hpet=off -cpu pentium3 -m 128 -accel tcg \
  -hda nt4.qcow2 \
  -cdrom nt4_workstation.iso \
  -boot d \
  -device VGA \
  -netdev user,id=lan -device pcnet,netdev=lan \
  -rtc base=localtime \
  -monitor stdio
```
- `-cpu pentium3` : **최신 CPU 모델이면 NT4가 부팅 중 BSOD** 남 → pentium3(또는 pentium)로 회피. (제일 흔한 함정.)
- `-M pc,hpet=off` : HPET 끔(NT4 호환).
- **부팅**: ISO가 부팅 가능하면 `-boot d`. 안 되면(정품 NT4 CD는 대개 부팅 불가) 설치 부트 플로피 3장을 `-fda disk1.img -boot a`로 넣고, 모니터에서 `change floppy0 disk2.img`로 교체하며 진행.
- 설치 중 **"display test"는 QEMU에서 버그로 멈춤** → 다이얼로그 그냥 수락하고 기본값 유지.
- HAL은 자동으로 "Standard PC" 선택됨(NT4는 ACPI 이전). 파티션은 NTFS로.

### 설치 후 필수 작업
1. **서비스팩 6a(SP6a)** 설치 — Winsock2, msvcrt 갱신, 보안픽스 다 들어옴. (TLS/네트워크 작업의 전제.)
2. **해상도**: 기본 VGA는 640×480×16색. **VBEMP 드라이버** 설치하면 고해상도/트루컬러. (또는 설치 시 `-vga cirrus`로 바꿔 NT4 내장 Cirrus 드라이버 사용도 가능.)
3. **네트워크**: `pcnet` = AMD PCnet. NT4에 내장 안 돼 있으면 **AMD PCnet NT4 드라이버**를 넣어줘야 함(널리 구할 수 있음). user-mode 네트워킹이라 DHCP로 `10.0.2.15` 받음, 게이트웨이(=호스트)는 `10.0.2.2`.
4. **디스크 속도**: UniATA 드라이버 넣으면 DMA로 빨라짐.
5. **시계 정확히 맞추기** — 나중에 mbedTLS 인증서 날짜검증이 실패하지 않으려면 VM 시간이 맞아야 함. `-rtc base=localtime` + 수동 확인.

> 로그인 화면의 Ctrl-Alt-Del은 게스트에 직접 못 보냄 → 모니터에서 `sendkey ctrl-alt-delete`.

## 3. 개발/디버그용 실행 커맨드 (설치 후 상시 사용)
디버그 훅(COM1 로그 + 스크립트용 모니터 + 네트워크)을 다 켠 버전:
```bash
qemu-system-i386 \
  -M pc,hpet=off -cpu pentium3 -m 256 -accel tcg \
  -hda nt4.qcow2 \
  -device VGA \
  -netdev user,id=lan,hostfwd=tcp::8080-:80 -device pcnet,netdev=lan \
  -serial file:debug.log \
  -monitor tcp:127.0.0.1:5555,server,nowait \
  -rtc base=localtime \
  -display cocoa
```
- `-serial file:debug.log` : 게스트 **COM1 → 호스트 파일**. 앱이 여기에 로그 쓰면 Claude가 `debug.log`를 실시간으로 읽어 진단.
- `-monitor tcp:...:5555` : 스크립트/Claude가 붙어서 `screendump`·`sendkey` 실행.
- `hostfwd=tcp::8080-:80` : 나중에 게스트에서 띄운 서버를 호스트 `localhost:8080`으로.

### 화면 캡처 (Claude가 UI를 눈으로 보는 법)
```bash
printf 'screendump /tmp/nt4.png -f png\n' | nc -w1 127.0.0.1 5555
#   최신 QEMU(7.1+)는 png 직접 지원. 안 되면:
printf 'screendump /tmp/nt4.ppm\n' | nc -w1 127.0.0.1 5555
magick /tmp/nt4.ppm /tmp/nt4.png
#   (nc가 안 끊기면 -w1 대신 -N 플래그 사용)
```
그 뒤 `/tmp/nt4.png`를 읽으면 실제 렌더 확인 가능. 로그인 자동화: `printf 'sendkey ctrl-alt-delete\n' | nc -w1 127.0.0.1 5555`.

### 와이어 디버깅 (TLS/HTTP 바이트 확인) — TLS 단계에서
실행 커맨드에 추가:
```bash
  -object filter-dump,id=d0,netdev=lan,file=capture.pcap
```
mbedTLS 키로그를 남기게 해서 `tshark`로 복호화하면 HTTP/WS 프레임까지 열람.

## 4. 파일 전송 루프 (매 빌드 exe를 VM으로)
호스트에서 FTP 서버:
```bash
python3 -m pyftpdlib -p 2121 -w -d ./dist   # ./dist에 빌드 산출물, -w 쓰기허용
```
게스트 NT4에서:
```
ftp
> open 10.0.2.2 2121
> binary
> get app.exe
```
> QEMU 내장 SMB(`-netdev user,smb=./dist`)도 있지만 NT4 SMB1/NTLM이 최신 samba와 자주 틀어짐 → **FTP가 더 안정적**.

## 5. 의존성 크로스 빌드 (mbedTLS / libwebp / stb_image)

### CMake 크로스 툴체인 파일 `nt4-toolchain.cmake`
```cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)
set(CMAKE_C_COMPILER   i686-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER i686-w64-mingw32-g++)
set(CMAKE_RC_COMPILER  i686-w64-mingw32-windres)
add_compile_definitions(_WIN32_WINNT=0x0400 WINVER=0x0400)
add_link_options(-Wl,--major-subsystem-version=4,--minor-subsystem-version=0,-Wl,--major-os-version=4,--minor-os-version=0)
```

### mbedTLS (TLS 1.2, 정적)
```bash
cmake -B build-nt4 -DCMAKE_TOOLCHAIN_FILE=../nt4-toolchain.cmake \
  -DENABLE_TESTING=Off -DENABLE_PROGRAMS=Off \
  -DUSE_SHARED_MBEDTLS_LIBRARY=Off -DUSE_STATIC_MBEDTLS_LIBRARY=On
cmake --build build-nt4
```
> mbedTLS의 Windows 엔트로피는 `CryptAcquireContext`/`CryptGenRandom`(CryptoAPI) 사용 → **NT4 호환**. 좋음.

### libwebp (정적)
```bash
cmake -B build-nt4 -DCMAKE_TOOLCHAIN_FILE=../nt4-toolchain.cmake \
  -DWEBP_BUILD_ANIM_UTILS=Off -DWEBP_BUILD_CWEBP=Off -DWEBP_BUILD_DWEBP=Off \
  -DBUILD_SHARED_LIBS=Off
cmake --build build-nt4
```

### stb_image
헤더 온리 — 그냥 `#include "stb_image.h"` (JPEG/PNG 디코드). 빌드 불필요.

## 6. 앱 컴파일/링크 플래그 (GUI 서브시스템, NT4 타겟)
```bash
i686-w64-mingw32-gcc src/*.c -o dist/app.exe \
  -D_WIN32_WINNT=0x0400 -DWINVER=0x0400 \
  -mwindows \
  -Wl,--major-subsystem-version=4,--minor-subsystem-version=0 \
  -Wl,--major-os-version=4,--minor-os-version=0 \
  -static -static-libgcc \
  -Igdeps/include -Ldeps/lib \
  -lmbedtls -lmbedx509 -lmbedcrypto -lwebp \
  -lgdi32 -lcomctl32 -lws2_32 -ladvapi32 -lole32 -loleaut32
```
- `-mwindows` : GUI 서브시스템(콘솔창 없음).
- `-static -static-libgcc` : libgcc DLL 의존 제거(NT4엔 없음). CRT는 NT4의 msvcrt.dll 사용.
- `advapi32` : CryptGenRandom. `ws2_32` : Winsock2.

### 네이티브 테스트 빌드 (순수 코어, Mac clang + ASan)
```bash
clang -fsanitize=address,undefined -g -O1 \
  core/*.c tests/*.c -o build/coretest && ./build/coretest
```
`core/`(http·ws·json·raster)와 `img/`는 OS 독립이라 여기서 풀스피드로 검증 → NT4엔 통합만.

## 7. 자주 터지는 문제 → 원인
- **부팅 중 BSOD/행** → CPU 모델. `-cpu pentium3`(또는 pentium)로.
- **`-accel kvm` 에러** → M2엔 x86 KVM 없음. `-accel tcg`.
- **설치가 파티션 포맷 실패/부팅 안 됨** → 디스크가 4GB 초과 or 부팅파티션 7.8GB 밖. 2GB 디스크로.
- **640×480 16색에서 안 올라감** → VBEMP 드라이버(또는 `-vga cirrus`).
- **네트워크 안 잡힘** → pcnet AMD NT4 드라이버 미설치. 드라이버 넣기.
- **TLS 인증서 검증 실패(나중 단계)** → VM 시계가 틀림. 날짜/시간 맞추기.
- **display test에서 멈춤** → QEMU 버그. 수락하고 기본값.
- **exe가 NT4에서 "not a valid application"** → 서브시스템 버전 플래그 누락 or post-NT4 API 사용. `_WIN32_WINNT=0x0400` + 링커 플래그 확인.

---

### 추천 순서
1. §1 툴체인 → 2. §2로 NT4 설치 + SP6a + 드라이버 → 3. §6의 `-mwindows` 빈 창 하나 크로스컴파일해서 §4 FTP로 넣고 §3으로 띄워 `screendump` 확인 (= 마일스톤1 그린라이트). 여기까지 되면 나머진 코드 문제만 남음.