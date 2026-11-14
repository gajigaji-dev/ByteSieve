# 아키텍처와 설계

## 기술 스택

지금은 Windows 전용입니다. Windows 종속 코드의 위치와 core 의존성, 다른 플랫폼 확장 방향은 [platform.md](platform.md)에 정리했습니다.

- **언어·빌드**: C11, CMake 3.21+, MSVC(`/W4 /utf-8`), 유니코드(`UNICODE`) 빌드
- **Windows API**: 폴더 열거 `GetFileInformationByHandleEx(FileFullDirectoryInfo)`(미지원 파일 시스템은 `FindFirstFileExW`),
  긴 경로 `\\?\`, 휴지통 이동 `IFileOperation`, 격리 폴더 이동·복원 `MoveFileExW`(같은 볼륨, 덮어쓰기 없음), 드라이브 `GetLogicalDrives`·`GetDiskFreeSpaceExW`,
  암호화 상태 셸 속성(`SHCreateItemFromParsingName`·`IShellItem2::GetProperty`, `System.Volume.BitLockerProtection`),
  설치 경로 보호 `RegGetValueW`(프로그램 제거 정보), 실제 확보 용량 `GetFileInformationByName`(Windows 11 24H2+, 없으면
  `GetFileInformationByHandleEx(FileStandardInfo)`), 메모리·CPU `GlobalMemoryStatusEx`·`GetSystemTimes`, GUI는 Win32 + Common Controls v6(창 외곽은 DWM `DwmSetWindowAttribute`)
- **구조**: UI와 무관한 정적 라이브러리 위에 CLI·GUI를 얹었습니다. 무거운 작업(스캔·탐지·휴지통 이동·격리 폴더 영구 삭제·드라이브 조회·측정)은
  모두 작업 스레드에서 실행하고, GUI 스레드는 `PostMessage`로 결과만 받아 화면을 갱신합니다.

```
                    disk-scanner (CLI)                bytesieve-gui (GUI)
                     │  cli · jsonl · cli_clean         │  사이드바(nav_model) → 폴더 스캔(+트리맵) │ 드라이브 │ 프로젝트 정리 │ 시스템 정리 │ 개발 캐시 │ 격리 폴더 │ 모니터 │ 부스터 │ 설정
                     │        │                         │                         │             │
                     │        └──────── clean_plan (정리 계획: 대상·선택 정책·요약·실행) ┘─────────────┘
                     │                    │   ↑ cleaner_build_plan / clean_plan_from_project
                     │                    │   cleaner (시스템 정크 분석)
                     │                                  │
   scan_history ── scan_model ── scan_job        cleanup_job ── cleanup_trash ── dev_cache   drive   sysmon   treemap
   (변화 추적)     (폴더 트리)   (스캔 스레드)   (탐지 스레드)   (휴지통 이동)  (공용 개발 캐시)(드라이브· (RAM·CPU) (트리맵 배치)
                                                                                             암호화)
                                                                        │
                                                                    quarantine
                                                                    (격리 폴더)
                        │                             │
                        └────────── scanner ──────────┘   cleanup_scan ── cleanup_rules
                         (DFS 열거·병렬 미리 열거·      (탐지 엔진)     (이름 규칙·개발 환경·보호 경로)
                          할당 크기 분석·오류 분류)          │
                                                          reclaim (실제 확보 용량: 할당 크기·하드 링크)
                                     │
                                    path (경로 결합·\\?\ 변환)
```

그림에는 주요 모듈만 두었습니다. 내보내기(`scan_export`)·용량 표시(`size_format`) 등은 아래 표를 참고하세요.

| 라이브러리(CMake 대상) | 역할 |
|---|---|
| `bytesieve_path` | 경로 결합·정규화, `\\?\` 확장 경로 변환 |
| `bytesieve_scanner` | 폴더 열거(DFS), 집계·디스크 할당 크기 분석, 오류 분류, 취소·제외 훅, 병렬 미리 열거 |
| `bytesieve_model` | 폴더 트리 모델, 정렬, 용량·디스크 사용 순위, 진행률 |
| `bytesieve_job` | 스캔 작업 스레드, 진행률 사본, 취소 |
| `bytesieve_history` | 폴더별 용량 스냅숏 저장·읽기·비교 |
| `bytesieve_export` | 스캔 결과 CSV·HTML 보고서 내보내기(임시 파일 → 바꿔치기, 64 KiB 버퍼로 나눠 쓰기) |
| `bytesieve_cleanup_rules` / `bytesieve_cleanup` / `bytesieve_cleanup_job` | 정리 후보 규칙(이름·표시 파일)과 시스템·개발 도구 설치 보호 경로(환경 변수·프로그램 제거 정보 레지스트리), 탐지 엔진, 탐지 스레드 |
| `bytesieve_cleanup_trash` | 삭제 보호 판정, 직전 확인, 휴지통 이동(쓸 수 없으면 격리 폴더로). 항목마다 탐지 경로를 따로 받는 작업도 지원 |
| `bytesieve_reclaim` | 실제 확보 가능 용량: 파일마다 할당 크기·링크 수·파일 ID 조회(파일 내용은 읽지 않음), MFT 상주 파일 0, (항목, 파일 ID) 표로 항목 밖과 나누는 하드 링크 판정 |
| `bytesieve_dev_cache` | 공용 개발 캐시(npm·pip·Gradle·Cargo·NuGet) 위치 탐지(환경 변수·설정 파일·기본 위치, 캐시 구조 확인), 용량·실제 확보 용량 세기, 작업 스레드 |
| `bytesieve_quarantine` | 드라이브별 격리 폴더: 같은 볼륨 안 이동, 기록(원자적 쓰기), 목록·불일치 판정, 복원, 영구 삭제 |
| `bytesieve_drive` / `bytesieve_sysmon` / `bytesieve_format` | 드라이브 조회(용량, BitLocker 상태: 셸 속성 `System.Volume.BitLockerProtection`), RAM·CPU 측정, 용량 표시 문자열 |
| `bytesieve_op_result` | 끝난 작업의 결과 판정(`op_result`): 처리·실패·건너뜀 수와 취소 여부로 완료·일부 완료·완료하지 못함·취소됨·처리할 항목 없음을 정함(순수 계산, GUI 완료 결과 영역이 사용) |
| `bytesieve_treemap` | 스퀘어리파이드 트리맵 배치(순수 계산, GUI 트리맵 보기 `gui_treemap`이 사용) |
| `bytesieve_cli` / `bytesieve_jsonl` | CLI 인자 해석·출력, JSON Lines 직렬화(정리 명령용 일반 이벤트 도우미 포함) |
| `bytesieve_project` | Git 기반 프로젝트 분석: 후보를 프로젝트(가장 가까운 Git 저장소, 없으면 폴더)별로 묶기, git.exe 읽기 전용 실행(`--no-optional-locks`, `core.fsmonitor=false`, 시간 제한)·없으면 `.git` 직접 읽기, 파일 마지막 수정, 휴면 판정, 후보별 복구 가능성(lockfile). 탐지 작업(`cleanup_job_start_ex`)이 같은 스레드에서 부른다 |
| `bytesieve_plan` | 정리 계획: 탐지 결과를 대상(선택 단위)·실행 항목으로 바꾸고, 선택 정책([모두 선택]은 MANUAL 제외)·요약·실행 엔진(영구 삭제는 `allow_permanent` 일 때만 먼저, 실행 직전 실행 중 프로그램·최근 변경 확인, 항목마다 `cleanup_trash_one`)을 GUI 정리 페이지(프로젝트 정리·시스템 정리·개발 캐시)와 CLI에 똑같이 제공 |
| `bytesieve_cli_clean` | `disk-scanner devenv`(개발 환경 분석·증가 기록, 읽기 전용)와 `disk-scanner find`·`clean project|system`: 인자 해석, CLI 선택(필터·`--include`·`--exclude`, 영구 삭제 항상 제외), 후보 목록(find, 읽기 전용)·dry-run·확인·실행(`--execute`), 텍스트·JSON 문서(`--json`, `"version":1`)·JSON Lines 출력, 공통 종료 코드. 탐지는 GUI와 같은 `cleanup_job`·`cleaner_analyze` |
| `bytesieve_vdisk` / `bytesieve_devhist` | WSL2·Docker 가상 디스크 찾기(레지스트리 Lxss·Docker Desktop 기본 위치, 읽기 전용, 줄이는 명령 안내), 개발 캐시·가상 디스크 용량 기록(`devenv-history.tsv`, 원자적 저장, 캐시마다 최근 60개) |
| `bytesieve_cleaner` | 시스템 정리(Cleaner): 사용자 임시 파일·오류 보고서·휴지통·브라우저 캐시·공용 개발 캐시 분석(읽기 전용, 위치 근거·캐시 위치 문제 포함, 항목마다 실제 확보 용량), 가상 디스크(분석만, `CLEANER_INFO_ONLY`), 증가 추적(`cleaner_apply_growth`), 분석 결과 → 정리 계획(`cleaner_build_plan`), 분석 작업 스레드. 분석 범위(`CleanerOps.kinds`: 0 이면 모두, GUI 는 `CLEANER_KINDS_SYSTEM`·`CLEANER_KINDS_CACHE` 로 두 페이지에 나눔). 기존 실행 API(`cleaner_run_*`)는 계획 실행 엔진을 감싼다 |
| `bytesieve_booster` | Booster: 시작 프로그램 목록·켜기/끄기(StartupApproved), 프로세스 목록·CPU 계산·보호 판정·끝내기(PID 재사용 확인) |
| `bytesieve_ui_lang` | GUI 표시 언어(한국어·영어·일본어·중국어 간체) 번역 문자열 표와 언어 결정(환경 변수 → 레지스트리 → Windows 표시 언어). 창과 무관 |
| `bytesieve_nav` | GUI 사이드바 메뉴 모델(`nav_model`): 목적별 그룹·페이지 표([설정]은 머리글 없는 footer), 키 이동(머리글 건너뜀·끝에서 멈춤), 세로 배치(footer 는 아래 끝에 붙임)와 마우스 위치 판정. 창·GDI 를 쓰지 않는 순수 계산 |
| `bytesieve_settings` | GUI 설정(`app_settings`): 테마(`system`·`light`·`dark`)·표시 언어를 `HKCU\Software\ByteSieve`에 저장·읽기(테스트는 `BYTESIEVE_SETTINGS_KEY`로 테스트 키), 실제로 어둡게 할지(테마 설정·Windows 앱 모드·고대비) 판정. 창과 무관 |

GUI(`bytesieve-gui`) 안의 파일은 다음처럼 나뉩니다. 페이지마다 "패널 자식 창 + `*_panel_verify`(자체 검사)" 형태입니다.

| 파일 | 역할 |
|---|---|
| `gui_main.c` | 창 골격(사이드바·페이지 제목·페이지 패널 배치), 페이지 전환(고른 패널만 보이기, 처음 열 때 조회·열 때마다 다시 읽기), 테마 적용(시작할 때·[설정]에서 바꿀 때·Windows 앱 모드가 바뀔 때), 자체 검사 진행. 페이지 패널은 스크롤 자리(page host) 안에 두어 자리가 페이지 최소 크기보다 작으면 가로·세로로 스크롤하고(최소 창 크기는 모니터 작업 영역 안), 배율이 바뀌면(`WM_DPICHANGED`) 글꼴·열 폭·배치를 새 배율로 다시 잡음 |
| `gui_nav` | 왼쪽 사이드바(직접 그리는 자식 창): 그리기와 마우스·키 입력만 맡고 메뉴 구성·이동·위치 판정은 `nav_model` 에 맡김 |
| `gui_scan` | 폴더 스캔 페이지(경로·스캔·트리·순위·트리맵·내보내기·기록). 변화 추적은 CLI `--track` 과 같은 `history_track` . 스캔이 끝나면 직전 기록과 비교·저장은 작업 스레드에서 하고(그동안 바쁜 상태), 끝나면 UI 스레드가 결과를 반영 |
| `gui_cleaner` | Cleaner 패널. [시스템 정리]·[개발 캐시] 두 페이지가 같은 패널을 분석 범위만 달리해 하나씩 씀 |
| `gui_settings` | 설정 페이지(테마·표시 언어·스캔 기록 자동 저장). 저장은 `app_settings`, 자동 저장은 `gui_main`을 거쳐 폴더 스캔 패널과 같은 경로 |
| `gui_drives`·`gui_cleanup`·`gui_quarantine`·`gui_monitor`·`gui_booster`·`gui_treemap` | 나머지 페이지 패널과 트리맵 보기 |
| `gui_status_text` | core 상태 번호(휴지통 이동 결과·복구 가능성, Cleaner 대상 이름·설명·분류·상태, 개발 캐시 이름·설명·위치 상태·근거, 건너뛴 이유, BitLocker 상태, Booster 시작 프로그램 위치·보호 이유·끝내기 결과)를 지금 표시 언어의 화면 문구로 연결. core 의 `*_status_text`(한국어, CLI·테스트용)는 그대로 두고 같은 분류를 `ui_lang` 표에 잇는다(한국어 결과는 core 문구와 같음, 자체 검사로 확인) |
| `gui_result` | 정리 작업이 끝나면 보이는 완료 결과 영역(색 막대·제목·결과 요약·다음 작업 단추). 제목·색은 `op_result` 판정(완료·일부 완료·완료하지 못함·취소됨·없음)으로만 정하고, 작업 중에는 숨김 |
| `gui_progress` | 진행 막대 상태(대기·진행·움직이는 막대·완료·취소·실패) 공용 처리. 실패·취소해도 막대가 비어 사라져 보이지 않게 함 |
| `gui_theme` | 창 외곽(DWM 둥근 모서리·Mica·어두운 제목 표시줄), 밝게·어둡게 색 토큰(`theme_color`, 시스템 강조색), 창 트리 전체에 모드 적용(`theme_apply_window`: 목록·트리·단추·입력 칸 테마와 색, 클래스 배경), 패널의 `WM_CTLCOLOR*` 처리, 언어별 UI 글꼴·페이지 제목 글꼴, 글자 폭에 맞춘 컨트롤 크기 |

설계와 단계는 [ui-design.md](ui-design.md)를 참고하세요.

실행 파일은 `disk-scanner`(CLI), `bytesieve-gui`(GUI), `bytesieve-bench`(성능 측정 도구)입니다.
배포하는 두 exe(`disk-scanner`·`bytesieve-gui`)에는 `res/`의 리소스(아이콘·버전 정보·앱 매니페스트)를 넣고, C 런타임을 정적으로
링크합니다(`/MT`). 버전은 `CMakeLists.txt`의 `project(ByteSieve VERSION ...)`에서 생성한 `version.h` 하나를 따릅니다.
설치 프로그램·포터블 ZIP·릴리스 자동화는 [release.md](release.md)를 참고하세요.

## 정리 계획(탐지 → 계획 → 실행)

정리는 GUI([프로젝트 정리]·[시스템 정리]·[개발 캐시])와 CLI(`disk-scanner clean`) 모두 같은 세 단계를 거칩니다. 화면은 이 계획을 보여 주고
고르기만 하며, 무엇을 고를 수 있는지·무엇을 실행하는지는 계획 모듈(`clean_plan`)이 정합니다.

| 단계 | 프로젝트 정리 | 시스템 정리 |
|---|---|---|
| 탐지 | `cleanup_job`(→ `cleanup_scan`, 보호 경로·앱 데이터 폴더 제외) | `cleaner_analyze`(작업 스레드는 `cleaner_scan_*`) |
| 계획 | `clean_plan_from_project`: 후보마다 대상 하나, 경로 판정(`cleanup_guard_check`)으로 `deny`·`ready`, 추정이면 `GUESSED` | `cleaner_build_plan`: 분석 대상마다 대상 하나(항목 여럿), `MANUAL`·`CAUTION`·`PERMANENT`·`CHECK_RECENT`, 실행 중이면 건너뛸 `app` |
| 선택 | `clean_plan_select_all`(MANUAL 제외)·`clean_plan_set_selected`·`clean_plan_select_id`, 요약은 `clean_plan_summarize` | 같음 |
| 실행 | `clean_plan_run_start`(작업 스레드) → `clean_plan_run_finish`가 항목별 결과를 계획에 되돌려 씀 | 같음(휴지통 비우기는 `allow_permanent`일 때만, 다른 항목보다 먼저) |

- 계획 대상의 순서는 탐지 결과와 같아(`origin`) GUI 목록의 행과 1:1로 맞습니다.
- 한 번 실행한 뒤에도 실패·취소한 항목이 남은 대상은 다시 고를 수 있고(선택이 남음), 다 옮긴 대상·건너뛴 대상(최근 변경·실행 중)은 다시 분석해야 합니다.
- 실행은 영구 삭제 대상을 먼저, 그다음 큰 대상부터 처리합니다.
- 프로젝트 정리는 계획에 프로젝트 분석(`clean_plan_apply_projects`)을 붙여, 미커밋 변경이 있는 프로젝트의 후보를 MANUAL([모두 선택] 제외)로,
  미푸시·휴면을 표시로, 후보마다 복구 가능성을 둡니다.
- CLI는 같은 계획 위에 정책 하나를 더합니다: 영구 삭제 대상은 어떤 경우에도 고르지 않고 `allow_permanent`도 언제나 0입니다.

## 오류 처리

- 오류가 나도 스캔을 멈추지 않고 경로와 Windows 오류 코드를 기록합니다. 종류: 접근 거부, 경로 없음, 잘못된 경로, 경로 너무 김, 메모리 부족, 기타.
- 결과 상태는 `complete`(모두 읽음), `partial`(일부 폴더를 읽지 못했거나 취소), `fatal`(대상 폴더 자체를 열지 못함) 세 가지입니다.
- `\\?\` 확장 경로로 260자를 넘는 경로(최대 32,767자)도 읽습니다.
- 정션·심볼릭 링크는 개수만 세고 따라가지 않아 순환을 막습니다.
- 다 읽지 못한 폴더는 트리에서 `[읽기 실패]`·`[일부 누락]`, 변화 추적에서 `(?)`, 정리 후보에서 '일부 누락'으로 표시합니다.

## 취소와 응답성

- 취소 요청은 폴더를 열기 직전과 폴더 하나를 읽는 도중 256개 항목마다 확인합니다.
- 진행률 알림은 100ms 간격으로 제한하고, 작업 중 창을 닫으면 작업을 취소하고 스레드가 끝날 때까지 기다린 뒤 종료합니다.
- 격리 폴더 영구 삭제 중에 창을 닫으면 지우고 있던 파일까지만 지우고 멈춘 뒤 종료합니다(남은 파일·기록은 보존).
- GUI 폴더 스캔이 끝난 뒤 직전 기록과 비교·저장은 작업 스레드에서 하고(UI 스레드는 결과 반영·표시만), 그동안은 바쁜 상태로 두어
  다시 스캔·기록 삭제·내보내기를 막습니다. 이때 창을 닫으면 저장이 끝나기를 기다린 뒤 종료합니다(기록 파일을 반쯤 쓰지 않음).
- 정리 작업 중에는 완료 결과 영역을 숨기고, 끝난 뒤 실제 처리 결과로만 판정해 보입니다(끝나지 않은 작업을 완료로 보이지 않음).

## 병렬 미리 열거

- 폴더를 열거한 쪽은 하위 폴더마다 **칸**(32바이트: 부모의 열거 기록 위치·상태)만 만들고 큐에는 (부모, 칸 번호) 16바이트 항목을 넣습니다.
  경로 사본을 가진 폴더 작업은 작업 스레드가 큐에서 꺼낼 때, 또는 호출 스레드가 그 폴더 차례가 됐을 때 비로소 만듭니다.
- 작업 스레드(기본 논리 프로세서 수, 최대 8개)는 깊이 우선 처리 순서 키로 정렬한 힙에서 **가장 먼저 필요한 폴더부터** 읽습니다.
- 미리 읽은 결과는 **1024개 폴더·2 MiB**로 제한합니다. 상한이 찼는데 큐 맨 앞이 미리 읽어 둔 것 중 가장 나중에 필요한 것보다 급하면,
  그 나중 것을 내쫓고(칸은 큐로 되돌림) 급한 것을 읽습니다. 그래서 깊은 곳에서 새로 나타난 급한 폴더가 먼 형제 폴더에 막히지 않습니다.
- 호출 스레드의 깊이 우선 스택은 하위 폴더마다 작업을 쌓지 않고, 처리를 마친 폴더 하나가 남은 하위 폴더 칸을 마지막 것부터 넘겨줍니다.
  스택에 올릴 때 남은 하위 폴더 기록만 남기고 파일 기록은 버립니다.
- 작업 스레드는 넣은 항목 수만큼만, 호출 스레드는 기다리는 작업이 끝났을 때만 깨웁니다.
- 훅은 언제나 호출 스레드에서 병렬이 아닐 때와 **같은 순서**로 불리므로, 결과·오류 순서가 스레드 수와 상관없이 같습니다(테스트로 확인).
- `ScanReport`의 진단 값(`peak_tasks`·`peak_task_bytes`·`prefetched_dirs`·`self_fetched_dirs`·`prefetch_waits`·`discarded_dirs`)으로
  메모리 상한과 분배를 확인할 수 있습니다. 직접 열거 + 미리 열거 − 버림 = 처리한 폴더 수가 항상 맞습니다(`scan_parallel` 테스트).
- 남는 메모리 바닥은 **가장 큰 폴더 하나의 목록**입니다. 폴더 하나는 처리할 때까지 목록 전체를 들고 있어야 하므로
  (`C:\Windows\WinSxS` 하위 폴더 3만 2천 개 ≈ 8 MB, `WinSxS\Manifests` 파일 4만 4천 개 ≈ 11 MB) 상한을 더 낮춰도 줄지 않습니다.

측정 결과와 병목 분석은 [performance.md](performance.md)에 있습니다.

## 디스크 사용량 분석

할당 크기는 폴더 열거 결과에 들어 있어 파일을 열지 않습니다. 압축·스파스 파일만 `GetCompressedFileSizeW`로 다시 구하고,
클라우드 자리표시자는 내려받기를 일으키지 않도록 건드리지 않습니다. 원인은 클라우드 전용 > 압축 > 스파스 > 기타 순으로 하나에만 넣으므로
`디스크 사용 = 논리 크기 + 클러스터 낭비 - (압축 + 스파스 + 클라우드 + 기타)`가 항상 성립합니다.

## 정밀 스캔(하드 링크 중복 보정)

`scan_tree_opts(..., SCAN_OPT_HARDLINKS)`(모델은 `scan_model_build_opts`, 작업은 `scan_job_start_ex`)로 켭니다.

- 열거 정보를 `FileFullDirectoryInfo` 대신 `FileIdExtdDirectoryInfo`(128비트 파일 ID)로 받고, 안 되면 `FileIdBothDirectoryInfo`(64비트),
  그것도 안 되면 ID 없이 받습니다. 볼륨 일련번호는 폴더 핸들의 `FileIdInfo`로 구합니다. 파일을 열지 않으므로 추가 I/O가 거의 없습니다.
- 파일 ID는 정밀 스캔일 때만 열거 기록(`EntryRec`)에 16바이트로 붙어, 기본 스캔의 메모리·속도는 그대로입니다.
- 같은 실제 파일인지는 호출 스레드가 훅 순서대로 (볼륨, 파일 ID) 집합을 확인해 정하므로, 병렬 미리 열거와 상관없이 같은 경로가 "처음"이 됩니다.
  처음이 아닌 경로는 `ScanEntry.hardlink_dup`로 알리고 `ScanReport.hardlink_dup_*`, `ScanNode.*_dup_*`에 따로 더합니다.
  경로마다 센 기본 값(`total_bytes`·`alloc_bytes` 등)은 바꾸지 않으므로 기본 스캔과 정밀 스캔의 차이를 한 번의 정밀 스캔으로 보여 줄 수 있습니다.
- 탐색 시작 볼륨이 `FILE_SUPPORTS_HARD_LINKS`가 아니면(FAT·exFAT) ID를 모으지 않습니다(FAT의 파일 ID는 믿을 수 없음). ID가 0이거나 없는 파일과,
  집합을 늘릴 메모리가 없을 때의 파일은 `hardlink_unknown_files`로 세고 경로마다 셉니다.
- 예: `C:\Windows`(파일 21만 개)는 경로마다 35.5 GB이지만 그중 5만 7천여 경로·10.9 GB가 하드 링크 중복이라 중복 제외 용량은 24.6 GB입니다.

## 변화 추적

폴더 트리를 깊이 4까지 저장하며(그 아래 변화는 조상 폴더 용량에 반영), 새 폴더·삭제된 폴더 안의 하위 폴더는 따로 나열하지 않습니다.
스냅숏은 임시 파일에 쓴 뒤 바꿔치기하고, 대상 폴더마다 직전 스냅숏 하나만 남깁니다.
저장 위치는 환경 변수 `BYTESIEVE_HISTORY_DIR`, 없으면 `%LOCALAPPDATA%\ByteSieve\history`입니다.
같은 폴더의 `settings.txt`에 GUI 자동 저장 설정을 둡니다(임시 파일에 쓴 뒤 바꿔치기, 없거나 손상되면 켬). CLI `--track`은 명시적 요청이므로 이 설정을 보지 않습니다.
기록 삭제는 `<16자리 16진수>.snap`(과 남은 `.snap.tmp`) 형식의 일반 파일만 지우고, 하나가 실패해도 나머지를 계속 지운 뒤 지운·실패 개수를 알려 줍니다.

## 파일 삭제 안전성

정리 후보 탐지 규칙과 휴지통 이동 보호 조건은 [cleanup.md](cleanup.md)에 있습니다.
