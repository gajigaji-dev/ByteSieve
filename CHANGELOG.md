# 변경 기록

형식은 [Keep a Changelog](https://keepachangelog.com/ko/1.1.0/)를 따르고, 버전은 [유의적 버전](https://semver.org/lang/ko/)을 씁니다.
릴리스할 때 `tools/package.ps1`이 해당 버전 구역을 GitHub 릴리스 노트로 씁니다(절차: [docs/release.md](docs/release.md)).

## [Unreleased]

## [1.0.1] - 2026-11-14

GUI의 모든 페이지를 한국어·영어·일본어·중국어 간체로 표시하고, 표시 언어를 바꾸면 다시 시작하지 않고 바로 적용하는 패치 릴리스입니다.
설치 프로그램에 이용약관을 추가했습니다. 1.0.0을 설치했다면 새 설치 프로그램을 실행해 그대로 업그레이드할 수 있습니다(설정 유지).

### 내려받기

| 파일 | 설명 |
|---|---|
| `ByteSieve-1.0.1-setup-x64.exe` | 설치 프로그램(관리자 권한 없이 사용자별 설치, 한국어·English·日本語·简体中文) |
| `ByteSieve-1.0.1-portable-x64.zip` | 설치 없이 실행(압축을 풀고 `bytesieve-gui.exe`) |
| `SHA256SUMS.txt` | 위 두 파일의 SHA256. `Get-FileHash <파일> -Algorithm SHA256`으로 확인 |

코드 서명이 없어 처음 실행할 때 Windows SmartScreen이 "Windows의 PC 보호" 경고를 띄울 수 있습니다.
SHA256을 확인한 뒤 [추가 정보] → [실행]을 누르세요.

### 변경

- 설치 프로그램의 라이선스 화면에 조항 형식의 이용약관을 추가했습니다(한국어·English·日本語·简体中文, 설치 언어로 표시).
  개인정보를 수집·전송하지 않는다는 점, 데이터 저장 위치, 정리 기능의 안전 장치와 이용자 책임, 불법 이용 시 행위자의 책임 등을
  안내하고, PolyForm Noncommercial 1.0.0 원문은 그 아래에 수정 없이 그대로 보여 줍니다. 라이선스 정책은 바뀌지 않았습니다.
  일본어·중국어 약관은 검토가 필요한 번역 초안입니다.
- 이용약관을 설치 폴더와 포터블 ZIP의 `terms\`에도 넣어 설치한 뒤 다시 볼 수 있게 했습니다(`README.txt`에 안내).
- [설정]에서 표시 언어를 바꾸면 다시 시작하지 않고 사이드바·페이지 제목과 모든 페이지에 바로 적용합니다(글꼴 포함).
  상태 줄·결과 칸은 마지막에 보인 내용(스캔·정리·복원 결과, 드라이브 조회·모니터 측정 결과 등)을 새 언어로 다시 씁니다.

### 수정

- 폴더 스캔 페이지에서 버튼은 고른 언어로 나오는데 상태 줄(진행·결과·안내 메시지), 분석 줄(디스크 사용량·직전 스캔 대비 변화),
  트리·순위 목록의 덧붙임 문구(중복 제외·변화 열), 트리맵(안내·풍선 도움말·우클릭 메뉴), 확인·오류 창과 저장 대화 상자가
  한국어로 남던 문제를 고쳤습니다. 이 문구들을 번역 표(`ui_lang`)로 옮겨 영어·일본어·중국어 간체로 보입니다.
- 격리 폴더 페이지의 단추·안내·열 머리글·항목 상태, 결과 열(복원·영구 삭제 결과와 이유), 상태 줄, 같은 이름 확인 창과
  영구 삭제 확인·결과 창이 고른 언어로 나오지 않던 문제를 고쳤습니다. 언어를 바꾸면 결과 열·상태 줄도 새 언어로 다시 씁니다.
  결과 열의 Windows 오류 설명과 확인 창의 [예]/[아니요] 단추도 앱 언어로 보입니다(그 언어가 Windows에 없으면 기본 언어).
  긴 문구에서 단추가 잘리지 않게 단추 폭을 글자 폭에 맞춥니다(한국어 화면은 그대로).
- 프로젝트 정리 페이지의 단추·필터·열 머리글, 목록 칸(유형·비고·복구 가능성·마지막 활동·정리 결과), 안내·요약·선택 줄,
  상태 줄(탐지·Git 분석·휴지통 이동 진행과 결과), 휴지통 이동 확인 창·실패 요약 창이 고른 언어로 나오지 않던 문제를 고쳤습니다.
  언어를 바꾸면 결과도 새 언어로 다시 씁니다. core 상태 문구(이동 결과·보호 이유·복구 가능성)는 CLI용 한국어를 그대로 두고
  GUI에서 번역 표로 연결합니다.
- [시스템 정리]·[개발 캐시] 페이지의 단추·안내·열 머리글, 대상 이름·분류·상태·제외·증감·위치 근거 칸, 선택 줄, 고른 대상의
  상세 설명, 상태 줄(분석·정리 진행과 결과), 정리 확인 창·휴지통 비우기 확인 창·결과 창이 고른 언어로 나오지 않던 문제를
  고쳤습니다. 언어를 바꾸면 결과와 상세 설명도 새 언어로 다시 씁니다. core의 대상 이름·설명·상태 문구(CLI용 한국어)는
  그대로 두고 GUI에서 번역 표로 연결합니다.
- [드라이브]·[모니터]·[Booster] 페이지의 단추·이름표·열 머리글, 드라이브 종류·암호화(BitLocker)·사용률 칸, 메모리·CPU
  문구와 그래프 제목·범례, 시작 프로그램 상태·위치·결과 칸, 프로세스 종류·상태(보호 이유·끝내기 결과) 칸, 요약·정보 줄,
  프로세스 끝내기 확인 창이 고른 언어로 나오지 않던 문제를 고쳤습니다. 이제 모든 페이지가 번역되어 있습니다. 언어를 바꾸면
  마지막 조회·측정 결과와 바꾸기·끝내기 결과를 다시 측정하지 않고 새 언어로 다시 씁니다. core 상태 문구(BitLocker 상태·
  시작 프로그램 위치·보호 이유·끝내기 결과)는 CLI용 한국어를 그대로 두고 GUI에서 번역 표로 연결합니다.

### 알려진 제한

- 코드 서명이 없음(SmartScreen 경고)
- 다크 모드에서 확인 창·오른쪽 클릭 메뉴·진행 막대는 밝게 표시됨
- HTML 보고서와 CLI 출력은 아직 한국어만

### English summary

Patch release: every GUI page is now translated into English, Japanese and Simplified Chinese, and changing the display
language in Settings applies immediately without a restart (status lines and results are rewritten in the new language).
The installer now shows plain-language Terms of Use in the setup language above the unchanged PolyForm Noncommercial 1.0.0
license text. Upgrading from 1.0.0 keeps your settings. Binaries are unsigned; verify them with `SHA256SUMS.txt`.

## [1.0.0] - 2026-11-07

첫 정식 출시입니다. Windows 10/11(64비트)용 설치 프로그램과 포터블 ZIP을 제공합니다.

### 내려받기

| 파일 | 설명 |
|---|---|
| `ByteSieve-1.0.0-setup-x64.exe` | 설치 프로그램(관리자 권한 없이 사용자별 설치, 한국어·English·日本語·简体中文) |
| `ByteSieve-1.0.0-portable-x64.zip` | 설치 없이 실행(압축을 풀고 `bytesieve-gui.exe`) |
| `SHA256SUMS.txt` | 위 두 파일의 SHA256. `Get-FileHash <파일> -Algorithm SHA256`으로 확인 |

코드 서명이 없어 처음 실행할 때 Windows SmartScreen이 "Windows의 PC 보호" 경고를 띄울 수 있습니다.
SHA256을 확인한 뒤 [추가 정보] → [실행]을 누르세요.

### 주요 기능

- **디스크 분석**: 폴더 스캔(병렬 미리 읽기), 용량·디스크 사용 순위, 트리맵, 정밀 스캔(하드 링크 중복 보정),
  직전 스캔 대비 변화 추적, CSV·HTML 보고서, 드라이브별 사용률과 BitLocker 상태
- **프로젝트 정리**: `node_modules`·빌드 결과물·캐시·로그 탐지, Git 기반 프로젝트 분석(휴면 판정, 미커밋·미푸시 경고,
  lockfile 기반 복구 가능성), 실제 확보 용량(할당 크기·하드 링크 반영)
- **시스템 정리·개발 캐시**: 임시 파일·오류 보고서·휴지통·브라우저 캐시, 공용 개발 캐시 15종(npm·pip·Gradle·Cargo·NuGet 등),
  WSL·Docker 가상 디스크 분석과 증감 기록
- **안전한 정리**: 휴지통으로 이동(휴지통이 없는 드라이브는 같은 드라이브의 격리 폴더), 정리 직전 다시 확인,
  보호 경로, 영구 삭제는 직접 고르고 확인한 항목만. 격리 폴더 항목은 복원하거나 두 번 확인 뒤 영구 삭제
- **모니터·부스터**: RAM·CPU 실시간 그래프, 시작 프로그램 켜기/끄기, 프로세스 끝내기(시스템·자신 보호)
- **CLI `disk-scanner`**: 스캔·`--track`·JSON Lines, `find`·`clean project|system`(기본 dry-run)·`devenv`, `--version`
- **화면**: 목적별 사이드바, 다크 모드(시스템 설정 따르기), [설정](테마·표시 언어·스캔 기록·버전 정보),
  한국어·영어·일본어·중국어 간체(사이드바·설정·폴더 스캔 페이지까지 번역)

### 이번 출시에서 고친 것

- 진행 막대: 실패·취소하면 막대가 0으로 돌아가 사라진 것처럼 보이던 문제. 이제 완료는 가득, 취소는 멈춘 자리에서 노란색,
  실패(일부 실패 포함)는 빨간색으로 남고, 전체량을 모르는 단계(분석 시작·Git 분석·휴지통 비우기)는 움직이는 막대로 표시
- `gui_trash` 테스트: 빌드 폴더에서 휴지통을 쓸 수 없는 환경(이동식 드라이브, 휴지통 꺼짐·용량 부족)에서 실패하고
  테스트 파일을 드라이브의 격리 폴더에 남기던 문제. 이제 이유를 알리고 테스트 파일을 복원한 뒤 건너뜀

### 알려진 제한

- 코드 서명이 없음(SmartScreen 경고)
- 다크 모드에서 확인 창·오른쪽 클릭 메뉴·진행 막대는 밝게 표시됨
- 표시 언어를 바꾸면 다시 시작해야 적용됨. 상태 줄·확인 창 등 일부 문구는 아직 한국어만

### English summary

First stable release of ByteSieve, a Windows 10/11 disk usage analyzer and safe cleaner: folder scan with treemap and
change tracking, project cleanup with Git analysis, system/dev-cache cleanup, quarantine for drives without a Recycle Bin,
monitor and booster pages, the `disk-scanner` CLI, dark mode and a settings page (Korean, English, Japanese, Simplified Chinese).
Binaries are unsigned; verify them with `SHA256SUMS.txt`.
