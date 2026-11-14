# 플랫폼 종속성과 확장 방향

ByteSieve는 지금 **Windows 전용**입니다(C11 + Win32 API, MSVC). 이 문서는 macOS·Linux로 넓힐 때를 대비해
현재 코드의 Windows 종속 지점과 구조를 정리하고, 확장할 때의 방향을 적은 것입니다. **아직 다른 플랫폼을 지원하지 않으며,
이 문서의 방향은 계획일 뿐 구현이 아닙니다.** 조사 기준은 2026-11-14의 `main`입니다.

## 요약

- core(스캔·정리 계획·휴지통 이동 등)는 정적 라이브러리로 나뉘어 있고, CLI(`disk-scanner`)와 GUI(`bytesieve-gui`)가 같은 core를 씁니다.
  core가 GUI에 의존하는 곳은 없습니다. 이 구조는 그대로 확장의 바탕이 됩니다.
- core 28개 소스(CLI·GUI 제외) 중 **7개는 Windows 헤더를 전혀 쓰지 않습니다**(포함한 프로젝트 헤더까지 따라가 확인):
  `scan_model`, `cleanup_scan`, `treemap`, `jsonl`, `size_format`, `nav_model`, `op_result`.
  나머지 21개는 파일 시스템·셸·레지스트리·프로세스·스레드 등 Win32 API를 직접 부릅니다.
- core 공개 헤더는 대부분 C 표준 형식(`uint64_t`, `uint32_t`, `wchar_t`)만 씁니다. 시각은 `uint64_t`(FILETIME 단위),
  오류는 `uint32_t`(HRESULT 또는 Windows 오류 번호)로 담습니다. `windows.h`를 공개 헤더에서 포함하는 core 모듈은 `booster.h` 하나입니다.
- 가장 큰 공통 종속은 **문자열 형식**입니다. 모든 경로·문구가 `wchar_t`(Windows에서 UTF-16)이며, macOS·Linux의 `wchar_t`는 32비트이고
  시스템 API는 UTF-8 `char`를 씁니다.
- 빌드는 MSVC 전용 옵션(`/W4 /utf-8 /std:c11`)·리소스(`.rc`)·Inno Setup 설치 프로그램에 묶여 있고, CI도 `windows-latest`에서만 돕니다.

## 모듈 구조

```
GUI  bytesieve-gui  gui_*.c (Win32 창·공용 컨트롤·DWM·uxtheme)
CLI  disk-scanner   main.c · cli.c · cli_clean.c · cli_devenv.c
        │                         │
        └──────── core 정적 라이브러리(CMakeLists.txt 의 bytesieve_*) ────────┘
   플랫폼 독립: scan_model · cleanup_scan · treemap · jsonl · size_format · nav_model · op_result
   Windows 종속: scanner · scan_job · scan_history · scan_export · path · reclaim · cleanup_rules · cleanup_job
                cleanup_trash · quarantine · clean_plan · cleaner · dev_cache · vdisk · devenv_history
                project_info · drive · sysmon · booster · app_settings · ui_lang
```

- `ui_lang`은 번역 표 자체는 플랫폼과 무관하고, Windows 표시 언어 번호(LANGID)를 읽는 부분만 Windows에 묶여 있습니다.
- `gui_status_text`·`gui_progress`·`gui_result` 등은 GUI 쪽이지만 core 상태를 화면 문구로 잇는 역할이라, 다른 GUI를 만들 때도
  같은 방식(core 상태 번호 → 번역 표)을 그대로 쓸 수 있습니다.

## Windows 종속 지점

| 영역 | 파일 | 쓰는 Windows 기능 | 다른 플랫폼에서 고려할 점 |
|---|---|---|---|
| 폴더 열거·크기 | `scanner.c` | 폴더 핸들(`CreateFileW`) + `GetFileInformationByHandleEx`, `FindFirstFileExW`, `GetCompressedFileSizeW`(할당 크기), 압축·스파스·클라우드 파일 속성, 재분석 지점(링크), 파일 ID(하드 링크 판정) | POSIX 열거(`opendir`·`readdir`·`fstatat`)와 `st_blocks`(할당 크기), 하드 링크는 장치·inode 번호. 클라우드·압축 속성은 대응이 없거나 다름 |
| 경로 | `path.c` 외 다수 | `\\?\` 긴 경로, `\` 구분자, 드라이브 문자, 대소문자 무시 비교(`_wcsicmp`·`CharUpperBuffW`·`CompareStringOrdinal`) | `/` 구분자, 드라이브 문자 없음, Linux는 대소문자 구분, macOS는 보통 구분 안 함. 비교 규칙을 플랫폼별로 둬야 함 |
| 휴지통 이동 | `cleanup_trash.c` | 셸 `IFileOperation`(COM), `SHGetKnownFolderPath`, `GetDriveTypeW`(고정 디스크 판정) | macOS는 휴지통 API(Foundation), Linux는 freedesktop.org 휴지통 규격. 셸이 영구 삭제하려 할 때 막는 규칙(`CLEANUP_TRASH_NO_RECYCLE`)에 해당하는 판정을 다시 정해야 함 |
| 격리 폴더 | `quarantine.c` | 같은 볼륨 안 `MoveFileExW`, `GetVolumePathNameW`(볼륨 루트), 속성(`SetFileAttributesW`), 기록 파일 쓰기 후 교체 | 같은 파일 시스템 안 `rename`, 마운트 지점 판정. 원자적 교체는 `rename`으로 가능 |
| 보호 경로·후보 판정 | `cleanup_rules.c`, `cleaner.c`, `dev_cache.c`, `vdisk.c` | 알려진 폴더(`SHGetKnownFolderPath`), 환경 변수(`%TEMP%`·`%LOCALAPPDATA%` 등), 레지스트리(설치 경로·WSL 배포판), 브라우저·개발 도구 캐시의 Windows 위치 | 위치 목록 자체가 플랫폼마다 다름(XDG 디렉터리, `~/Library/Caches` 등). 보호 경로 목록은 플랫폼별 자료로 나눠야 함 |
| 영구 삭제 | `cleaner.c`(휴지통 비우기), `quarantine.c`(격리 항목) | `SHQueryRecycleBinW`·셸 휴지통 비우기, `DeleteFileW`·`RemoveDirectoryW` | "사용자가 확인한 경우에만" 원칙과 확인 흐름은 그대로 유지해야 함 |
| 시스템 정보 | `sysmon.c`, `drive.c`, `booster.c` | `GlobalMemoryStatusEx`·`GetSystemTimes`, `GetLogicalDrives`·`GetDiskFreeSpaceExW`·`GetVolumeInformationW`, BitLocker 상태(셸 속성), 시작 프로그램(레지스트리 Run 키·시작 폴더), 프로세스(`CreateToolhelp32Snapshot`·`OpenProcess`·`TerminateProcess`) | 메모리·CPU·디스크 정보는 플랫폼별 조회 방법이 다름(`/proc`, `sysctl` 등). BitLocker·시작 프로그램·Booster는 Windows 고유 기능에 가까움 |
| 설정·기록 저장 | `app_settings.c`, `scan_history.c`, `devenv_history.c` | 레지스트리(`HKCU\Software\ByteSieve`), `%LOCALAPPDATA%\ByteSieve\history`, 파일 쓰기 후 `MoveFileExW`로 교체 | 설정은 파일(XDG config, `~/Library/Application Support`)로. 스냅숏 형식(UTF-8 텍스트)은 그대로 쓸 수 있음 |
| 외부 프로그램 | `project_info.c` | `SearchPathW`로 `git.exe` 찾기, `CreateProcessW` + 파이프 | `posix_spawn`·`fork`/`exec` + 파이프 |
| 스레드·동기화 | `scanner.c`, `scan_job.c`, `cleanup_job.c`, `clean_plan.c`, `cleanup_trash.c`, `cleaner.c`, `dev_cache.c`, GUI 일부 | `_beginthreadex`, `CRITICAL_SECTION`·`SRWLOCK`·조건 변수, `Interlocked*` | C11 `<threads.h>`·`<stdatomic.h>` 또는 pthread. 작업 → 완료 알림(GUI 창 메시지) 구조는 콜백으로 이미 분리되어 있음 |
| 문자열·인코딩 | 전체 | `wchar_t`(UTF-16), `MultiByteToWideChar`/`WideCharToMultiByte`(기록·내보내기 UTF-8) | 가장 넓게 퍼진 종속. 내부 문자열 형식을 정하는 일이 확장의 첫 결정 |
| GUI | `gui_*.c` | Win32 창·공용 컨트롤(목록·트리·진행 막대), GDI 그리기(트리맵·그래프), DWM·uxtheme(다크 모드·Mica), 모니터별 DPI | GUI는 플랫폼별로 새로 만드는 것이 현실적. 화면 상태·문구 규칙(`ui_lang`, `gui_status_text`)은 재사용 가능 |
| 빌드·배포 | `CMakeLists.txt`, `res/*.rc`, `installer/`, `tools/*.ps1`, `.github/workflows/*.yml` | MSVC 옵션(`/W4 /utf-8 /std:c11`), 리소스(아이콘·버전·매니페스트), Windows 라이브러리(`shell32`·`ole32`·`comctl32` 등), Inno Setup, PowerShell 스크립트, `windows-latest` CI | 컴파일러별 옵션 분기, 플랫폼별 대상·라이브러리 분리, 플랫폼별 패키징·CI |

## 확장할 때의 방향

실제로 다른 플랫폼을 지원하기로 정하면 아래 순서를 권합니다. 각 단계는 Windows 동작을 바꾸지 않아야 하며, 기존 테스트가 그대로 통과해야 합니다.

1. **문자열 형식 결정**: 내부 경로·문구를 지금처럼 `wchar_t`로 둘지, UTF-8 `char`로 옮길지 정합니다. core 공개 헤더 전체에 영향을
   주므로 가장 먼저 정하고, 바꾼다면 모듈 단위로 나눠 진행합니다.
2. **플랫폼 계층 분리**: 파일 시스템 열거, 경로 규칙, 휴지통 이동, 시스템 정보, 설정 저장, 프로세스 실행, 스레드를 각각 작은 인터페이스
   (`*_platform.h` 같은 헤더 + `*_win32.c` 구현)로 모읍니다. 지금 core에 흩어진 Win32 호출을 이 계층 뒤로 옮기는 일이며,
   처음에는 Windows 구현 하나만 둡니다.
3. **정리 안전 장치 이식 원칙**: `cleanup_trash.h`의 안전 장치(경로 판정 → 옮기기 직전 확인 → 옮기는 중 확인)와 "영구 삭제는 사용자 확인 뒤에만"
   원칙은 플랫폼 계층 위(core)에 둡니다. 플랫폼별로 바뀌는 것은 "어떻게 옮기는가"뿐이어야 합니다. 보호 경로 목록은 플랫폼별 자료로 둡니다.
4. **빌드 분리**: `CMakeLists.txt`에서 MSVC 옵션을 컴파일러별로 나누고, Windows 전용 라이브러리·리소스·GUI 대상을 `if(WIN32)` 안으로 모읍니다.
   플랫폼 독립 core와 그 단위 테스트부터 다른 컴파일러로 빌드되게 합니다.
5. **CLI 먼저**: 다른 플랫폼은 CLI(`disk-scanner`)부터 지원하고, GUI는 그다음에 플랫폼별로 만듭니다.
6. **테스트·CI**: 플랫폼 독립 core의 단위 테스트를 다른 OS의 CI에서도 돌립니다. 정리(삭제) 테스트는 지금처럼 임시 폴더 픽스처에서만 동작해야 합니다.

## 지금 지킬 것

다른 플랫폼을 바로 지원하지 않더라도, 새 코드를 쓸 때 아래를 지키면 나중의 분리 비용이 줄어듭니다.

- 새 Win32 호출은 이미 그 일을 맡은 core 모듈 안에 둡니다. 화면 코드(`gui_*`)에서 파일 시스템·레지스트리를 직접 다루지 않습니다.
- core 공개 헤더에는 `windows.h`나 Windows 형식(`HANDLE`, `DWORD` 등)을 새로 넣지 않습니다. 시각·크기·오류는 지금처럼
  `uint64_t`·`uint32_t`로 담습니다.
- 플랫폼과 무관한 계산(모델·순위·트리맵 배치·결과 판정·형식화 등)은 Windows 헤더 없이 쓸 수 있는 모듈에 둡니다.
- 관련 Issue에는 `platform` Label을 붙입니다.
