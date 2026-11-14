# 배포

ByteSieve는 설치형 exe(데스크톱 프로그램)와 포터블 ZIP으로 배포합니다. 이 문서는 결정 사항, 산출물, 릴리스 절차,
출시 전 확인 목록을 정리합니다.

## 결정 사항(1.0.0)

| 항목 | 결정 |
|---|---|
| 첫 버전 | **1.0.0** 정식 출시 |
| 코드 서명 | 첫 배포는 서명 없이 진행. 대신 `SHA256SUMS.txt`를 함께 올리고 SmartScreen 경고를 안내 |
| 배포 채널 | GitHub Releases(`gajigaji-dev/ByteSieve`) |
| 설치 프로그램 | Inno Setup 6, 사용자별 설치(관리자 권한 없음) |
| 포터블 | ZIP도 함께 제공 |
| 출시 전 버그 | `gui_trash` 실패 원인·진행 막대(로딩 바) 표시를 고친 뒤 출시 |
| 제거 시 설정 | 테마·언어 설정(`HKCU\Software\ByteSieve`)은 **남김** |
| 격리 파일 | 제거할 때 **절대 자동 삭제하지 않음**. 스캔 기록도 남김 |

## 산출물

`tools/package.ps1`이 만들고, `v*` 태그를 올리면 GitHub Actions(`.github/workflows/release.yml`)가 같은 스크립트로 만들어
릴리스 초안에 올립니다.

| 파일 | 내용 |
|---|---|
| `ByteSieve-<버전>-setup-x64.exe` | 설치 프로그램(`installer/bytesieve.iss`) |
| `ByteSieve-<버전>-portable-x64.zip` | `ByteSieve-<버전>-portable-x64\` 폴더 하나에 `bytesieve-gui.exe`·`disk-scanner.exe`·`LICENSE.txt`·`README.txt`·`terms\`(이용약관 네 언어) |
| `SHA256SUMS.txt` | 위 두 파일의 SHA256(`<해시>  <파일 이름>`) |
| `RELEASE_NOTES.md` | `CHANGELOG.md`의 그 버전 구역(릴리스 본문으로 씀, 올리지는 않음) |

### exe 공통

- **버전**: `CMakeLists.txt`의 `project(ByteSieve VERSION x.y.z)` 한 곳에서만 정합니다. CMake가 `version.h`를 만들고,
  exe 버전 리소스(탐색기 속성), `disk-scanner --version`, GUI [설정] → 정보, 설치 프로그램 버전, 파일 이름이 모두 이 값을 따릅니다.
  태그와 이 값이 다르면 패키징이 실패합니다.
- **리소스**(`res/`): 앱 아이콘(`bytesieve.ico`, `tools/make_icon.py`로 생성), 버전 정보, 앱 매니페스트
  (공용 컨트롤 v6·시스템 DPI 인식·Windows 10/11·긴 경로·`asInvoker`). 매니페스트를 리소스로 넣으므로 링커는 `/MANIFEST:NO`.
- **C 런타임 정적 링크**(`/MT`): VC++ 재배포 패키지 없이 Windows 기본 DLL만으로 실행됩니다.
- **실행 중 표시**: GUI가 이름 있는 뮤텍스 `ByteSieve.Gui.Running`을 만들고, 설치·제거 프로그램이 이것으로 앱이 켜져 있는지
  확인해 닫으라고 안내합니다(여러 개 실행을 막지는 않음).

### 설치 프로그램(`installer/bytesieve.iss`)

| 항목 | 동작 |
|---|---|
| 설치 위치 | `%LOCALAPPDATA%\Programs\ByteSieve`(관리자 권한 없음, `PrivilegesRequired=lowest`) |
| 언어 | 시작할 때 고름(한국어·English·日本語·简体中文). 고른 언어를 `HKCU\Software\ByteSieve\Language`에 기록하되 **이미 값이 있으면 그대로**(앱 [설정]에서 고른 언어·이전 설치 유지) |
| 구성 요소 | ByteSieve(필수), 명령줄 도구 `disk-scanner`(기본 포함, 빼도 됨) |
| 바로 가기 | 시작 메뉴(항상), 바탕 화면(선택, 기본 꺼짐) |
| 업그레이드 | 같은 AppId로 덮어씀. 설정·기록 유지. 앱이 실행 중이면 닫으라고 안내 |
| 제거 | 설치한 프로그램 파일·바로 가기만 지움. 설정·스캔 기록·각 드라이브의 격리 폴더(`.bytesieve-trash`)는 남기고, 남겼다는 안내를 띄움(조용한 제거에서는 생략) |
| 지원 OS | Windows 10 이상, x64 |

`AppId`(`{76B7146F-FC3B-4E67-B142-1BA4B7B2CEBE}`)는 업그레이드·제거를 같은 앱으로 알아보는 값이므로 **절대 바꾸지 않습니다**.

### 설치 프로그램 자동 검증(`tools/test_installer.ps1`)

같은 스크립트를 검증용(`/DTestMode`)으로 컴파일해, 실제 설치본·설정과 겹치지 않는 곳(다른 AppId·이름, 설정 키
`HKCU\Software\ByteSieveTest\installer`, 임시 설치 폴더, 바로 가기 없음)에서 조용한 모드로 다음을 확인합니다.
릴리스 워크플로도 태그 빌드마다 실행합니다.

| 단계 | 확인 |
|---|---|
| 새로 설치(한국어) | 파일 4개·이용약관 4개(`terms\`)·제거 프로그램, 설치한 `LICENSE.txt`가 원문과 같음, 제거 목록 버전, 언어 기록 `ko`, 설치된 `disk-scanner --version` |
| 업그레이드(1.0.1, 영어 설치) | 제거 목록 버전 1.0.1, 사용자가 고른 언어(`ja`) 유지, 제거 프로그램 하나(같은 AppId) |
| 앱 실행 중 업그레이드 | 뮤텍스를 감지해 설치하지 않음(조용한 모드는 취소로 끝남), 버전 그대로 |
| 제거 | 제거 목록·프로그램 파일·제거 프로그램이 사라지고 언어 설정은 남음 |
| 실제 위치 | `%LOCALAPPDATA%\Programs\ByteSieve`·`HKCU\Software\ByteSieve`·시작 메뉴·바탕 화면·배포용 제거 항목이 검증 전과 같음 |

언어 선택 창·라이선스 화면·제거 뒤 안내 창·바로 가기처럼 화면을 봐야 하는 부분은 아래 확인 목록에서 사람이 봅니다.
중국어 간체 번역은 컴파일러에 있으면 그것을, 없으면(예전 Inno Setup) `installer/languages/ChineseSimplified.isl`을 씁니다.

### 포터블 ZIP

설치하지 않고 실행하는 판입니다. 데이터 위치는 설치본과 같습니다(설정은 `HKCU\Software\ByteSieve`, 기록은
`%LOCALAPPDATA%\ByteSieve\history`). exe 옆 폴더에 저장하는 "완전 포터블"은 아닙니다.

## 릴리스 절차

1. **버전·변경 기록**: `CMakeLists.txt`의 `project(... VERSION x.y.z)`를 올리고, `CHANGELOG.md`에 `## [x.y.z] - 날짜` 구역을 씁니다.
   이용약관을 바꾼 버전이면 `installer/terms/terms-*.txt` 네 파일의 `@EFFECTIVE_DATE@`(시행일·부칙)를 배포일로 바꿉니다
   (예: `2026년 12월 1일` / `December 1, 2026` / `2026年12月1日` / `2026年12月1日`). 남아 있으면 태그 빌드가 실패합니다(`tools/package.ps1`).
   README·`docs/*`의 기능 표·테스트 개수가 맞는지 확인합니다. README [설치]는 최신 릴리스 페이지와 `<버전>` 파일 이름만
   가리키므로 버전마다 고치지 않습니다(예전에는 버전별 다운로드 링크를 미리 바꿔 공개 전까지 404가 되었음).
2. **로컬 패키징**(선택): `powershell -ExecutionPolicy Bypass -File tools\package.ps1`
   (Release 빌드 → Release 테스트 → `dist\`에 설치 프로그램·ZIP·SHA256). Inno Setup이 없으면 `-NoInstaller`.
   이어서 `tools\test_installer.ps1`로 설치·업그레이드·제거를 검증합니다(위 표).
   UI가 바뀌었으면 `tools\screenshots.ps1`로 `docs/screenshots`를 다시 찍습니다([gui.md](gui.md#스크린샷)).
3. **병합**: 릴리스 브랜치를 `main`에 병합합니다(CI 통과 확인).
4. **태그**: `main`에서 `git tag v<버전>`(예: `git tag v1.0.0`) → `git push origin v1.0.0`.
   Actions가 패키지를 만들어 **릴리스 초안**에 올립니다.
5. **출시 전 확인**: 아래 목록을 초안의 파일로 확인합니다.
6. **공개**: 초안에서 본문(=`CHANGELOG.md` 구역)을 확인하고 [Publish release].

문제가 있으면 초안을 지우고 태그를 지운 뒤(`git push origin :refs/tags/v1.0.0`) 고쳐서 다시 태그합니다. 이미 공개한 버전은
고치지 않고 다음 버전(예: 1.0.1)으로 냅니다.

## 출시 전 확인 목록(사람이 직접)

깨끗한 가상 머신 두 대(**Windows 10 22H2**, **Windows 11 24H2**)에서, **관리자가 아닌 계정**으로 합니다.
정리 기능은 테스트용 폴더에서만 시험합니다(실제 사용자 파일로 하지 않음).

- [ ] `SHA256SUMS.txt`와 내려받은 파일의 `Get-FileHash` 결과가 같다
- [ ] 설치 프로그램 실행 → SmartScreen 경고에서 [추가 정보] → [실행]으로 진행된다(관리자 권한 요청 없음)
- [ ] 언어 선택 창에 네 언어가 있고, 고른 언어로 마법사가 나온다. 설치 뒤 앱이 그 언어로 시작한다
- [ ] 라이선스 화면에 설치 언어의 이용약관이 깨지지 않고 나오고(시행일이 배포일), 본문의 동의 단추 이름이 화면 단추와 같으며,
      끝까지 내리면 [부록]에 PolyForm Noncommercial 1.0.0 원문이 `Required Notice:` 줄부터 그대로 있다
- [ ] 설치 폴더의 `terms\`에 약관 네 언어가 있고 `README.txt`가 그 위치를 안내한다
- [ ] 시작 메뉴 바로 가기로 실행된다. 탐색기 속성 → 자세히에 출시 버전(예: 1.0.1), [설정] → 정보에 같은 버전
- [ ] 125%·150% 배율에서 글자·사이드바가 잘리지 않는다. 다크 모드 전환이 된다
- [ ] 폴더 스캔·드라이브·테스트 폴더 정리(휴지통으로 이동)·격리 폴더 복원이 된다
- [ ] `disk-scanner --version`이 출시 버전을 출력한다(설치 폴더에서)
- [ ] 앱을 켠 채로 다시 설치하면 닫으라고 안내한다. 업그레이드 뒤 테마·언어 설정이 그대로다
      (이전 공개 버전을 먼저 설치한 뒤 새 설치 프로그램으로 업그레이드)
- [ ] [설정]에서 언어를 바꾼 뒤 재설치해도 설정한 언어가 유지된다
- [ ] 제거하면 프로그램 파일·바로 가기가 사라지고, 남긴 데이터 안내가 나온다.
      `HKCU\Software\ByteSieve`, `%LOCALAPPDATA%\ByteSieve\history`, 테스트 드라이브의 `.bytesieve-trash`가 남아 있다
- [ ] 포터블 ZIP을 풀어 실행된다(VC++ 재배포 패키지가 없는 깨끗한 PC에서)

## 사용자 안내

- **SmartScreen**: 서명이 없는 새 프로그램이라 "Windows의 PC 보호" 창이 뜰 수 있습니다. 내려받은 파일의 SHA256을 확인한 뒤
  [추가 정보] → [실행]을 누릅니다. README·릴리스 노트에 같은 안내를 둡니다.
- **SHA256 확인**: PowerShell에서 `Get-FileHash .\ByteSieve-1.0.0-setup-x64.exe -Algorithm SHA256` 결과를
  `SHA256SUMS.txt`와 비교합니다.
- **백신 오탐**: 파일을 옮기고 프로세스를 끝내고 시작 프로그램을 바꾸는 기능이 있어 휴리스틱 검사에 걸릴 수 있습니다.
  오탐이 보고되면 Microsoft Defender 오탐 제출(https://www.microsoft.com/wdsi/filesubmission)로 신고합니다.

## 나중에 검토할 것

- 코드 서명(Azure Trusted Signing 또는 OV 인증서): SmartScreen 경고 제거
- winget 등록(GitHub Releases의 설치 프로그램 URL·SHA256으로 매니페스트 작성)
- 설치 프로그램에서 `disk-scanner`를 사용자 PATH에 추가하는 선택
- 자동 업데이트 확인(지금은 GitHub Releases에서 직접 내려받기)
