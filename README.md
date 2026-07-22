# instagram-on-nt40

**Windows NT 4.0용 네이티브 인스타그램 클라이언트** — C로 밑바닥부터. 브라우저도,
GUI 프레임워크도, OS 네이티브 TLS도 안 씁니다. macOS(Apple Silicon)에서 크로스컴파일해
QEMU x86 에뮬레이션 위에서 돌립니다.

![NT4에서 렌더링되는 피드](docs/screenshots/nt4-instagram-photos.png)

- **렌더링**: 손수 짠 알파 블렌딩 + 오너드로우 컨트롤을 얹은 32bpp DIB-섹션 컴포지터.
  Win32 GDI 바로 위에서 동작.
- **이미지**: 밑바닥부터 구현한 **QOI** 디코더 (JPEG 라이브러리 불필요, freestanding 친화적).
- **네트워킹**(예정): Winsock2 + 비동기 + 직접 만든 HTTP/WebSocket 클라이언트.
- **암호화**(예정): **mbedTLS** 번들로 TLS 1.2 + SNI (NT4의 SChannel은 최신 암호/TLS 1.2
  불가), 엔트로피는 NT4 `CryptGenRandom`에서.

전체 아키텍처는 [native.md](native.md), QEMU/툴체인 환경은 [setup.md](setup.md) 참고.

## 디렉토리 구조

| 디렉토리 | 역할 | 테스트 |
|-----|------|--------|
| `core/` | OS 독립 순수 C: `raster`(DIB 픽셀·알파·다운스케일), `font`(5x7 비트맵 폰트); 이후 `http` `ws` `json` `model` | Mac clang + ASan/UBSan |
| `img/`  | `qoi` — 밑바닥부터 만든 QOI 이미지 코덱 (디코드+인코드) | ASan 라운드트립 |
| `pal/`  | 얇은 Win32 래퍼: 창, DIB-섹션 더블버퍼, COM1 로그, freestanding CRT shim, 파일 로더 | NT4 통합 |
| `ui/`   | NT4 GDI: 회색 베벨 피드 컴포지터 + 커스텀 컨트롤 | NT4 통합 |
| `tools/` | VM 실행/캡처/전송 스크립트, 호스트 렌더 프리뷰, `mkqoi` 신 생성기 | — |

전략: **순수 코어를 크게** 유지하고 NT4 전용 코드는 얇게. 대부분의 로직을 Mac에서 풀스피드로
검증하고, VM은 최종 통합용으로만 씁니다.

## 빌드

`brew install qemu mingw-w64 cmake imagemagick wireshark` 와 Xcode CLT 필요.

```bash
make test      # core/ + 테스트를 clang + ASan/UBSan로 빌드·실행
make nt4       # NT4 앱을 크로스컴파일 -> dist/app.exe (i686-w64-mingw32)
make preview   # 피드를 호스트에서 오프스크린 렌더 -> build/feed.png
```

`make preview`는 NT4 앱이 만드는 것과 *완전히 동일한* 프레임버퍼를 렌더합니다 (피드가 순수
`core/raster`라서). VM 부팅 없이 룩을 반복 개선할 수 있어요.

## NT4에서 실행 (QEMU)

작동이 확인된 QEMU 설정 (자세한 내용은 [docs/vm-notes.md](docs/vm-notes.md)):

```bash
make nt4
# app.exe + assets/photos/*.qoi 를 ISO로 묶어 CD로 게스트에 전달
hdiutil makehybrid -iso -joliet -o vm/app.iso vm/xfer
./tools/run-vm.sh                    # -cpu 486 -device cirrus-vga, COM1->vm/debug.log, monitor :5555
# NT4에서:  Start -> Run -> D:\app.exe
./tools/screendump.sh vm/nt4.png     # 실제 게스트 렌더 캡처
tail -f vm/debug.log                 # 앱의 COM1 디버그 로그
```

> NT4 설치는 수동 1회 작업입니다 (NT4 ISO 필요) — [setup.md](setup.md) §2 참고.

## NT4 타게팅 규칙 (강제)

`-D_WIN32_WINNT=0x0400 -DWINVER=0x0400`로 API 표면을 제한하고, 링커 플래그로 PE의
서브시스템/OS 버전을 4.0으로 찍습니다. `AlphaBlend`/`UpdateLayeredWindow`/비주얼 스타일
안 씀 — 알파는 `core/raster`에서 직접. 빌드 검증:

```bash
make nt4 && python3 - <<'PY'
import struct; d=open('dist/app.exe','rb').read(); o=struct.unpack_from('<I',d,0x3c)[0]+24
print("subsystem", struct.unpack_from('<H',d,o+68)[0], "(2=GUI)",
      "os", struct.unpack_from('<HH',d,o+40), "subsysver", struct.unpack_from('<HH',d,o+48))
PY
# exe가 kernel32/user32/gdi32 만 임포트하고 CMOV가 0인지도 확인
```

## 진행 상황

- [x] **마일스톤 0** — 리포, 2타겟 빌드, `core/raster` + ASan 테스트, 호스트 프리뷰
- [x] **마일스톤 1 — 실물 NT4에서 실행**: DIB 컴포지터 + 더블버퍼, freestanding(무 CRT)
      `app.exe`가 QEMU에서 로드되어 창을 띄움
- [x] **마일스톤 2 — 회색 베벨 피드**를 NT4에서 트루컬러로 렌더 (Cirrus @ 1280x1024)
      — 부드러운 그라디언트, 액션바, 네비바
- [x] **마일스톤 2.1 — 진짜 텍스트**: `core/font`의 5x7 비트맵 폰트 (Instagram 워드마크,
      유저네임, 위치, 좋아요, 캡션, 댓글) — NT4에서 검증
- [x] **마일스톤 3 — 이미지 디코드**: `img/qoi`에 밑바닥부터 만든 **QOI** 코덱 (JPEG
      라이브러리 불필요, freestanding 친화적). CD에서 `.qoi` 사진을 로드(`pal_read_file`)해
      디코드 + area-다운스케일 후 피드에 표시. 샘플 신은 `tools/mkqoi`로 제작. NT4에서 검증
- [ ] **마일스톤 3.1** — 스크롤 가능한 피드; 동일 API 뒤에 진짜 JPEG 디코더
- [ ] **마일스톤 4** — `core/http`+`json`+`ws`를 `pal` 소켓 위에 얹어 로컬 목 서버와 통신
- [ ] **마일스톤 5** — mbedTLS 번들, 셀프사인 HTTPS 목에 TLS 1.2 + SNI
- [ ] **마일스톤 6** — `GraphApiSource`(공식 API), 페이지네이션, 포스트 상세

### 해결한 NT4 함정들 (자세히는 `docs/vm-notes.md`)

- **설치 중 BSOD 0x1E** (탈착식 미디어 클래스 드라이버) → `-cpu 486 -nodefaults`
- **"unable to load DLL"** → Homebrew mingw가 UCRT(NT4에 없음)에 링크함. **freestanding**
  으로 빌드 (`-nostdlib` + `pal/nt4_crt.c`가 자체 엔트리/힙/mem*, 로그는 `wvsprintfA`) →
  exe가 kernel32/user32/gdi32만 임포트. CMOV도 전부 제거됨
- **그라디언트 밴딩** → 기본이 640×480×16색. Cirrus 드라이버 설치해 트루컬러로

## 라이선스 / 주의

밑바닥부터 작성한 코드입니다. NT4 설치 미디어(ISO)와 CD 키는 저작권 대상이라 이 리포에
포함되지 않습니다 (`.gitignore`로 제외).
