# NT 4.0 네이티브 인스타그램 클라이언트 (밑바닥부터) — Claude Code 작업 프롬프트

아래 내용을 Claude Code에 그대로 붙여넣으세요.

---

## 프로젝트 목표
Windows NT 4.0에서 도는 **네이티브 인스타그램 클라이언트를 C/C++로 밑바닥부터** 만든다. 기성 브라우저·기성 GUI 프레임워크·OS 네이티브 TLS(SChannel)에 의존하지 않는다:
- **렌더링**: DIB 섹션 컴포지터 + 자체 알파 블렌딩 + 커스텀 컨트롤 (Win32 GDI 위에 직접).
- **네트워킹**: Winsock2 + IOCP 비동기 + 자체 HTTP 클라이언트 + WebSocket 프레이밍.
- **암호화**: **mbedTLS를 번들**해서 TLS 1.2 + SNI (NT4 네이티브 SChannel은 최신 cipher/TLS1.2 불가라 배제).

"Windows 95 Instagram"은 목업만 존재하고 실제 NT4 네이티브 실물은 전례가 없다. **실물 스크린샷을 뽑으면 사실상 세계최초.**

## 실행 환경
- 호스트: macOS (Apple Silicon, M2). 여기서 편집·크로스컴파일·테스트.
- 게스트: QEMU **에뮬레이션** NT 4.0 (가상화 아님 — x86를 통째 에뮬). crypto/디코드가 느리니 VM 내부 성능수치는 신뢰 금지.
- 크로스 툴체인: `brew install mingw-w64` → `i686-w64-mingw32`. Claude Code는 게스트 안에서 못 도니, **Mac에서 짜고 크로스컴파일 → exe만 VM으로 전송**하는 워크플로.

## NT4 타겟팅 규칙 (이거 안 지키면 VM에서 로드조차 안 됨)
- 컴파일: `-D_WIN32_WINNT=0x0400 -DWINVER=0x0400` → NT4 이후 API를 헤더 단계에서 차단.
- 링크: `-Wl,--major-subsystem-version=4,--minor-subsystem-version=0,--major-os-version=4,--minor-os-version=0`.
- **금지 API**(전부 NT4에 없음): `AlphaBlend`/`TransparentBlt`(msimg32) → 알파는 DIB 위에서 직접 구현. `UpdateLayeredWindow`/`WS_EX_LAYERED`. `GetGuiResources`. ComCtl32는 **v4.x만**(v6 비주얼스타일 없음). WebP/AVIF 디코드는 OS에 없음.
- 엔트로피는 NT4 **CryptoAPI `CryptGenRandom`** 사용.

## 아키텍처 — "순수 코어를 최대한 키운다"
NT4 특유의 것은 얇은 레이어에 격리하고, 로직 대부분을 **OS 독립 순수 C**로 만들어 **Mac에서 clang + ASan/UBSan으로 네이티브 유닛테스트**한다. VM은 최종 통합 검증에만 쓴다.

1. `core/` (OS 독립, Mac 네이티브 테스트 대상)
   - `http` HTTP/1.1 요청·응답 파서
   - `ws` WebSocket 핸드셰이크(SHA1+base64) + 프레이밍
   - `json` 순수 C JSON 파서(jsmn 류)
   - `model` 피드/포스트 도메인 모델
   - `raster` DIB 픽셀 버퍼에 대한 블리팅·자체 알파 블렌딩·다운스케일 (픽셀 로직만, GDI 무관)
2. `pal/` (Platform Abstraction Layer — 얇게 Win32 감싸기)
   - 창/DC/DIB섹션, 소켓(Winsock2+IOCP), 스레드, 시간, `CryptGenRandom`
3. `ui/` (NT4 GDI)
   - DIB 섹션 컴포지터 + 더블버퍼링, 커스텀 컨트롤(오너드로 피드 셀·스크롤 리스트), 창 클래스/메시지 루프
4. `net/` — `core`의 http/ws를 `pal` 소켓 + **mbedTLS**에 접합 (엔트로피=CryptGenRandom, net 콜백=Winsock).
5. `img/` — `stb_image`(JPEG) + `libwebp`(WebP) 디코드 → `core/raster` DIB. 다운스케일 후 디스크 캐시(에뮬레이션이라 재디코드 금지).
6. `source/` — pluggable 데이터 소스: `MockSource`(로컬 서버/픽스처) → `GraphApiSource`(공식) → `PrivateApiSource`(선택·밴위험 명시).

## 빌드 시스템 (타겟 2개)
- **native-test**: clang, macOS. `core/` + `img/` 순수 로직 pytest/CTest + **ASan/UBSan**.
- **nt4**: `i686-w64-mingw32` 크로스, 위 NT4 플래그. mbedTLS·libwebp·stb_image도 같은 툴체인으로 크로스 빌드.

## 디버깅 인프라 (초반에 깔아둘 것 — Claude를 루프에 넣는 핵심)
- **COM1 로그**: 앱이 디버그 로그를 COM1에 씀 → QEMU `-serial file:/…/debug.log` → Mac 파일 실시간 갱신 → Claude가 읽고 진단.
- **화면 확인**: QEMU 모니터 `screendump`으로 게스트 프레임버퍼 캡처 → PNG 변환 → Claude가 실제 렌더 확인·스킨 튜닝. (오프스크린 DIB를 BMP로 덤프하는 경로도.)
- **와이어 디버깅**: QEMU `-object filter-dump`로 pcap + mbedTLS 키로그 → tshark로 복호화해 HTTP/WS/TLS 프레임 확인.
- **크래시**: NT4 Dr.Watson fault address + MinGW `.map`/`i686-w64-mingw32-addr2line`로 주소→소스라인.
- **GDI 누수**: `CreateCompatibleDC/SelectObject/DeleteObject`를 회계 래퍼로 감싸 핸들 카운트를 COM1 로그로.
- **전송**: 매 빌드 exe를 VM으로 넣는 스크립트(호스트 FTP 서버 `pyftpdlib` + NT4 `ftp.exe`, 또는 QEMU smb). 반복 루프 자동화.

## 데이터 소스 전략
- 어려운 건 Instagram의 **적대성**(비공식 API 서명/디바이스 스푸핑/밴)이지 TLS가 아님. 그러니:
- **초기엔 `MockSource` + 로컬 목 서버로 전체 파이프라인(TLS→JSON→디코드→렌더)을 완주.** 목 서버를 self-signed HTTPS로 띄워 **네이티브 TLS 경로까지 실제로 태운다.**
- 그다음 `GraphApiSource`(공식, 본인/비즈니스 계정, 밴 위험 없음). Basic Display API는 2024년말 폐기됨 유의.
- `PrivateApiSource`는 격리된 선택 플러그인, 경고 명시.

## 마일스톤 (스크린샷을 앞당기되 토대는 지킴)
1. **빌드 2타겟 + PAL 스켈레톤 + 빈 창** 이 NT4(QEMU)에서 열림 → `screendump`으로 확인. COM1 로그·전송 스크립트·addr2line 셋업.
2. **DIB 컴포지터 + 더블버퍼 + 커스텀 피드 셀 컨트롤**(자체 알파). 하드코딩 목 데이터 + 번들 JPEG 하나로 **회색 베벨 피드 렌더 → 머니 스크린샷.**
3. `img/` 디코드(JPEG/WebP→DIB, 다운스케일, 캐시) + 스크롤 리스트 컨트롤. (`core`/`img`는 Mac ASan 테스트.)
4. `core/http`+`json`+`ws` 순수 로직 완성(Mac 네이티브 테스트) → `pal` 소켓+IOCP에 접합 → **로컬 목 HTTP 서버**에서 피드가 네트워크로 흐름.
5. **mbedTLS 번들** → TLS1.2+SNI, 엔트로피=CryptGenRandom → self-signed HTTPS 목 서버로 검증(pcap+키로그 디버깅).
6. `GraphApiSource` 접합(설정으로 Mock↔실데이터). 포스트 상세·페이지네이션. (`PrivateApiSource` 선택.)

## 작업 방식 요청
- 리포 초기화, 작업 목록으로 진행 관리, 파괴적 작업 전 확인.
- **순수 코어에 테스트 우선**(ASan 포함). NT4는 통합 검증용.
- README: macOS 크로스 빌드법, QEMU 실행/`screendump`/`-serial`/pcap 커맨드, VM 전송법.
- 완벽보다 **마일스톤2의 "회색 베벨 피드 스크린샷"을 최단거리로**. 막히면 가정 명시 후 Mock으로 우회.

**먼저 리포 구조 + 빌드 2타겟(clang/MinGW) 셋업 + 마일스톤1 계획을 제안하고 시작해줘.**