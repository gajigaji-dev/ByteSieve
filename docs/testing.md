# 테스트

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

CTest에 등록된 테스트는 79개입니다.

| 분류 | 테스트 | 확인 내용 |
|---|---|---|
| 단위 테스트(29개) | `path`, `scanner`, `scan_alloc`, `scan_parallel`, `hardlink`, `scan_model`, `scan_history`, `export`, `scan_job`, `jsonl`, `cli`, `cli_clean`, `drive`, `sysmon`, `size_format`, `treemap`, `ui_lang`, `nav_model`, `cleanup_rules`, `project`, `devenv`, `clean_plan`, `reclaim`, `cleanup_scan`, `cleanup_trash`, `dev_cache`, `quarantine`, `cleaner`, `booster` | 아래 참고 |
| CLI·도구 검사(31개) | `no_args`, `help`, `scan_*`, `cli_*`, `bench_smoke` | 개발 환경 분석(`cli_devenv`: 사용자·기록 폴더를 픽스처로, 첫 기록 → 캐시를 키운 뒤 증감(+10000) → `--no-record`는 기록 파일을 바꾸지 않음, Docker 가상 디스크·줄이는 방법, JSON Lines, 인자 오류, 아무것도 지우지 않음), 종료 코드, 텍스트·JSONL 출력, 인자 오류, 접근 거부 픽스처, 격리 폴더 제외(`cli_skip_quarantine`·`cli_jsonl_skip_quarantine`), `--track` 기준 → 변경 → 비교(`cli_track`), 정리 후보 찾기(`cli_find`: 프로젝트마다 자기 Git 저장소를 만들어 휴면(2020년 커밋)·미커밋 변경·원격 없는 커밋·lockfile 복구 가능성·`--dormant-days`·`--no-git`(직접 읽기)·`project` 이벤트를 확인, git 이 없으면 Git 확인은 건너뜀, 크기순 목록·확인됨/추정 표시, 아무것도 옮기지 않음, `--json` 문서를 해석해 필드 확인, `--category`·`--jsonl`, 실행 옵션 거부, 없는 폴더는 2와 실패 문서), 정리 명령(`cli_clean_project`·`cli_clean_system`: 기본·`--dry-run`은 아무것도 옮기지 않고 예상 결과를 보임, `--json` dry-run·실행 문서를 해석해 `mode`·`executed`·`summary`·`totals`·대상별 `result`·`exit_code` 확인, 대화형이 아닌 입력에서 `--yes` 없는 `--execute` 거부, 예전 이름 `--apply`도 실행, 유형 거르기·JSON Lines 실행, 공용 개발 캐시는 `--include`로만, `recycle_bin` 거부, 옮긴 항목은 픽스처의 격리 폴더로), 성능 측정 도구의 스캐너 진단 출력(`bench_smoke`) |
| GUI 자체 검사(19개) | `gui_scan`, `gui_cancel`, `gui_missing_dir`, `gui_drives`, `gui_cleanup`, `gui_cleanup_cancel`, `gui_trash`, `gui_quarantine`, `gui_history`, `gui_precise`, `gui_export`, `gui_monitor`, `gui_cleaner`, `gui_booster`, `gui_nav`, `gui_lang_ko`, `gui_lang_en`, `gui_lang_ja`, `gui_lang_zh-Hans` | 숨긴 창에서 실제 작업을 실행하고 화면 상태를 검증(`gui_scan`은 트리맵, `gui_drives`는 암호화 열, `gui_cleanup`은 프로젝트 정리 페이지의 실제 확보 열 포함, 모든 검사가 고른 페이지의 패널만 보이고 제목·사이드바 선택이 맞는지도 확인). `gui_cleaner`(시스템 정리 → 개발 캐시 페이지 차례로)는 사용자 폴더·TEMP 를 픽스처로 돌리고 각 페이지에 분석 범위 밖 종류의 행이 없는지(시스템 정리에 개발 캐시 없음, 개발 캐시에 휴지통 없음), [모두 선택]이 휴지통·개발 도구 캐시를 고르지 않는지, Gradle 래퍼 캐시가 "정리 가능 · 주의"로 표시되는지, 예전 `gui_cache`가 보던 공용 캐시(npm·pip·Gradle·Cargo·NuGet)의 위치 근거와 캐시 구조가 아닌 위치(`NUGET_HTTP_CACHE_PATH` → 문서 폴더)가 이유와 함께 정리되지 않는지, 하드 링크로 캐시 밖과 나누는 파일이 실제 확보에서 빠지는지 확인한 뒤 개발 도구 캐시를 직접 체크해 정리한 항목을 픽스처의 격리 폴더로 보내며(실제 휴지통은 조회만 하고 비우지 않음, 도구 설정·설치한 도구는 남음), `gui_booster`는 목록·CPU 측정·보호 판정만 확인하고 아무것도 바꾸지 않음. `gui_nav`는 사이드바를 키보드(↓ 끝까지·↑·Home·End)와 마우스(모든 항목·같은 항목 다시·그룹 머리글)로 오가며 고른 페이지의 패널만 보이는지, 제목·선택이 맞는지, 처음 열 때 조회(드라이브·시스템 정리·개발 캐시)는 한 번만이고 격리 폴더는 열 때마다인지, 두 Cleaner 페이지의 분석 범위를 확인(조회는 세기만 하고 실제로 하지 않아 사용자 시스템을 읽지 않음). `gui_lang_*`는 언어마다 언어별 글꼴과, 최소 창 크기에서 사이드바 문구와 폴더 스캔 페이지 단추 문구가 잘리지 않고 패널 안에 있는지, 보기·열 머리글 번역을 확인 |

예제 폴더가 빌드 폴더(이 저장소) 안에 있는 검사(`gui_trash`, `gui_quarantine`, `cli_clean_project`, `cli_clean_system`)는 `BYTESIEVE_GIT=none`으로 실행해
작업 트리의 Git 상태(미커밋 변경)에 결과가 좌우되지 않게 합니다. 사용자 폴더를 픽스처로 돌리는 검사는 Go·Yarn·Conda 등의 도구 환경 변수도 지우고 `BYTESIEVE_WSL_KEY`를 빈 레지스트리 키로 두어
이 PC의 실제 캐시·WSL 배포판을 보지 않습니다. Git 동작은 자기 저장소를 만드는 `project`·`cli_find`가 확인합니다.

## 주요 단위 테스트 범위

- `scanner`: 권한 거부, 정션과 정션 순환, 260자를 넘는 경로, 5 GiB 넘는 크기, 깊이 제한, 취소, 제외 훅
- `scan_alloc`: 디스크 사용량 원인 분류, 실제 NTFS 압축·스파스 파일, 작업 스레드 1개와 8개의 훅 호출 기록이 같은지(제외·취소 포함)
- `scan_parallel`: 하위 폴더 2,500개 픽스처에서 동시에 살아 있는 폴더 작업 수가 상한 안인지(예전 방식은 2,500개를 넘음, 1 스레드는 깊이만큼),
  작업 스레드 1개·8개의 집계·훅 호출(순서 포함)이 같은지, 직접 열거 + 미리 열거 − 버림 = 처리한 폴더 수(빠뜨리거나 두 번 쓰지 않음),
  제외 훅이 미리 열거한 하위 트리를 정리하는지, 취소 뒤 폴더를 새로 시작하지 않고 남은 작업을 버리는지.
  최적화 전후의 시간·메모리 비교는 `bytesieve-bench`로 합니다([performance.md](performance.md))
- `hardlink`: 실제 하드 링크 픽스처(같은 폴더·다른 폴더·크기 0 파일)로 기본 스캔은 그대로(중복 0), 정밀 스캔의 중복 경로 수·용량·할당,
  실제 파일마다 한 경로만 세는지, 작업 스레드 1개와 8개의 같은 결과, 모델의 폴더별 중복·부모 합계·순위(중복 제외·중복)·진행률
- `scan_history`: 스냅숏 저장·읽기, 늘어남·줄어듦·새 폴더·삭제·불확실 판정, 손상된 파일,
  자동 저장 설정(기본값·저장 후 다시 읽기·손상된 설정·쓰기 실패), 기록 삭제(형식에 맞는 스냅숏만 삭제, 사용 중인 파일이 있어도 나머지 계속, 설정·다른 파일 보존),
  CLI·GUI 공통 추적 `history_track`(기준 저장 → 비교 후 저장 → 저장 끔·취소는 비교만 → 기록 폴더 자리에 파일이 있으면 FAILED와 오류 번호 → 빈 모델은 아무것도 하지 않음),
  상태 이름·문구, 변화 문구(`history_format_delta`·`history_change_text`: 부호·새 폴더·삭제됨·불확실 `(?)`)
- `export`: CSV(UTF-8 BOM, CRLF, 머리글·행 수, 크기 내림차순, 쉼표가 든 이름, `=`로 시작하는 이름의 수식 방지, 64 KiB 쓰기 버퍼를 넘는 큰 결과),
  HTML(구역, 이름 이스케이프, 순위 표 100행 제한, 통계), 실패(없는 폴더, 빈·실패한 스캔, 다른 프로그램이 연 파일에서 기존 내용 보존·임시 파일 없음),
  GUI가 쓰는 기본 파일 이름(`export_default_name`: 끝 구분자·드라이브 루트·못 쓰는 글자·빈 이름·지금 시각)과 실패 원인 문구(`export_error_text`)
- `cleanup_scan`·`cleanup_rules`: 중첩 항목, 확인됨/추정, 개발 환경 분류, 표시 파일이 없는 `bin`·`.gradle`, 보호 경로, 필터, 취소.
  개발 도구 설치 경로: 사용자 폴더·`ProgramFiles`를 픽스처로 돌려 VS Code 사용자 설치(`%LOCALAPPDATA%\Programs`)·확장(`.vscode\extensions`)·
  Visual Studio(`Program Files`, 레지스트리처럼 따옴표가 붙은 다른 위치) 안의 실행 파일·`node_modules`·`out`·`__pycache__`·`*.log`가
  후보가 아닌지(직접 탐지와 작업 스레드), 설치 폴더 안을 직접 고르면 모두 보호 표시인지, 설치 경로 값 검사(볼륨 루트·상대·UNC 거부)와 제품 이름 판정.
  앱 데이터·LevelDB: `LOCALAPPDATA`·`APPDATA`·`USERPROFILE`을 픽스처로 돌려 Chrome LevelDB(`000003.log`·`CURRENT`·`MANIFEST-*`)·VS Code 로그와
  세션 저장소·JetBrains 도구의 `node_modules`·`LocalLow`는 후보가 아니고 `Temp`·일반 로그는 후보인지(직접 탐지와 작업 스레드), 앱 데이터 목록
  없이도 앱 데이터 밖의 LevelDB 폴더(`CURRENT`·`LOCK`)와 숫자 이름 로그가 빠지는지, 앱 폴더 안을 직접 고르면 보호 표시인지
- `cleanup_trash`: 보호 판정 규칙(설치 폴더 안의 실행 파일·하위 폴더는 보호, 그 상위는 중요 경로)과 **실제 휴지통 이동·거부**(테스트가 넣은 휴지통 항목만 지워 사용자 휴지통은 그대로 둠)
- `dev_cache`: `USERPROFILE`·`LOCALAPPDATA`·`APPDATA`를 임시 픽스처로 돌리고 도구 환경 변수를 지워 실제 캐시를 보지 않는다.
  기본 위치(없음·있음, NuGet `v3-cache`·`http-cache` 둘 다), 용량·파일 수, 도구 폴더 자체·`.cargo\bin`·`gradle.properties` 제외,
  환경 변수·`.npmrc`(`${VAR}`)·`pip.ini`(구역·상대 경로)·`NuGet.Config`(상대 경로), 캐시 구조가 아닌 폴더·파일·정션·바꾸지 못한 변수,
  세기 전·취소 뒤 정리 거부, 중요 경로 거부, 프로그램 설치 경로 안의 캐시(Visual Studio `NuGetPackages`, `Programs\nodejs` 안 npm 캐시) 거부, 항목별 탐지 경로로 판정하는 휴지통 작업(밖·없음), 작업 스레드·취소
- `quarantine`: 임시 폴더를 가짜 볼륨으로 써서 격리 이동·목록, 같은 이름 여러 번 격리, 이동 실패(사용 중·없음·다른 볼륨)
  뒤 원래 항목 보존, 격리 폴더가 정션일 때 거부, 복원(같은 이름은 덮어쓰지 않음, 다른 이름 복원, 없어진 상위 폴더,
  드라이브 문자 변경), 기록 불일치(파일 없음·기록 손상·`moving` 상태·볼륨 밖 상대 경로·사용자가 넣은 파일)에서 파일 보존,
  영구 삭제(읽기 전용 파일, 안의 정션은 대상 보존, 격리 폴더 밖을 가리키게 꾸민 항목 거부), 영구 삭제 진행 알림(지운 수·용량)과
  도중 취소(남은 파일·기록 보존, 다시 영구 삭제), 정리·탐지·폴더 스캔 연동
- `drive`·`sysmon`: 가짜 조회 함수로 이동식 드라이브 오류·API 실패·이상값 재현. 암호화: 셸 속성 값 → 상태 변환(알려진 값·모르는 값·값 없음),
  상태마다 식별자·화면 문구, 목록을 만들 때 실패 항목의 용량은 지우고 암호화 상태(잠김)는 남기는지, 실제 시스템 드라이브는 관리자 권한 없이
  상태를 읽고 네트워크·광학·없는 드라이브는 조회하지 않는지(`gui_drives`도 암호화 열 문구를 확인)
- `devenv`: 가짜 WSL 레지스트리 키(HKCU\Software\ByteSieveTest\vdisk_<pid>, 끝나면 지움)와 LOCALAPPDATA 픽스처로 가상 디스크 찾기(`\\?\` BasePath,
  파일 없는 WSL1 제외, Docker 기본 위치, WSL 목록과 같은 파일은 Docker 로 한 번만, 줄이는 명령), 증가 기록 파일(없는 파일, 저장·다시 읽기, 손상된 줄 건너뛰기,
  최근 60개 보관, 저장 실패 시 이전 파일 유지), `cleaner_apply_growth`(직전 기록과의 차이·첫 기록·추적하지 않는 대상·record 0/1).
- `dev_cache` 는 pnpm·Yarn·Maven·Go·Conda 의 기본 위치와 위치 결정(settings.xml `${user.home}`, go env 파일, `GOPATH`, `GOCACHE=off`, `CONDA_PKGS_DIRS`,
  `.npmrc` `store-dir`, 캐시 구조가 아닌 `YARN_CACHE_FOLDER`)도 확인한다.
- `project`: git.exe 로 임시 폴더에 저장소를 만들어(2020년 커밋을 bare 원격에 push 한 저장소, 원격 없이 추적 안 하는 파일이 있는 저장소,
  Git 이 아닌 폴더) 프로젝트별 묶기·루트·브랜치·마지막 커밋·마지막 수정·미커밋 변경·미푸시·원격·휴면(기준 일수 포함)·복구 가능성을 확인하고,
  git.exe 를 쓰지 않을 때(.git 직접 읽기: 브랜치·reflog 시각·원격 ref 비교, 미커밋은 모름)도 같은 픽스처로 확인한다. git 이 없으면 Git 부분은 건너뛴다.
- `clean_plan`: 프로젝트 분석 결과 붙이기(미커밋 → DIRTY·MANUAL·선택 해제, 미푸시·휴면 표시, 복구 가능성, 요약의 dirty·unpushed),
  프로젝트 후보 → 계획(순서·경로 판정·보호 경로 거부·추정 표시·용량), 선택 정책([모두 선택]은 MANUAL 제외, id 로 직접 고르기,
  고를 수 없는 대상, 요약), 실행(영구 삭제는 `allow_permanent`일 때만 그리고 다른 항목보다 먼저, 실행 중 프로그램·최근 변경은 건너뜀, 사용 중 파일은
  실패로 남기고 선택을 유지해 다시 실행하면 옮김, 다 옮긴 대상은 다시 실행하지 않음, 할 것이 없으면 실행하지 않음). 옮긴 항목은 픽스처의 격리 폴더로 간다.
- `cli_clean`: 프로젝트 분석 옵션(`--include-dirty`는 clean project 만, `--dormant-days` 범위, `--no-git`)과 `--include-dirty` 선택,
  `disk-scanner clean`·`find` 인자 해석(목록·`=` 형식·`--`, 알 수 없는 이름·옵션·명령, `--yes`는 `--execute`와 함께만, `--dry-run`·`--execute`와
  `--json`·`--jsonl`은 함께 쓸 수 없음, `--apply`는 `--execute`와 같음, `find`에는 실행·시스템 대상 옵션이 없음, 명령에 맞지 않는 옵션,
  `recycle_bin` 거부)과 선택(기본은 [모두 선택]과 같음, `--include` 이름·`dev_cache`, `--exclude`, 프로젝트 `--category`·`--eco`, 영구 삭제 항상 제외),
  `find` 거르기(보호된 후보도 보임, 시스템 대상은 아님).
- `reclaim`: 임시 폴더에 하드 링크 픽스처를 만들어, 링크가 하나인 파일은 할당 크기만큼, 링크가 모두 대상 안이면 한 번만, 대상 밖에
  링크가 남으면 공유로 세는지, 조회하지 못한 파일은 추정하는지, 확장 경로(`\\?\`)도 같은 결과인지, 그룹(항목)마다 따로 판정하는 표가
  1000개를 넣어 늘어나도 맞는지. `GetFileInformationByName` 경로와 파일을 여는 대체 경로를 모두 돌린다. 정리 후보 탐지가 pnpm 처럼 store 와
  링크를 나누는 `node_modules` 항목에서 그 파일을 공유로 빼는지(`cleanup_item_reclaim`, `total_reclaim`)도 본다.
- `cleaner`: `TEMP`·`LOCALAPPDATA`·`USERPROFILE`·`APPDATA`를 픽스처로, `BYTESIEVE_QUARANTINE_ROOT`를 픽스처로 두고 휴지통 조회·비우기와
  프로그램 실행 여부는 가짜 함수로 바꾼다(실제 휴지통은 건드리지 않음). 분석: 임시 파일 중 최근 24시간 안에 바뀐 파일·최근 파일이 든 폴더 제외,
  오류 보고서, 휴지통 용량, Chrome 프로필(Preferences 있는 폴더)만의 캐시, 없는 Edge, 실행 중인 Firefox, npm 캐시.
  선택 정책: 휴지통·개발 도구 캐시만 직접 골라야 하는 대상(`cleaner_target_manual_only`)이고, 주의(`cleaner_target_caution`)는
  개발 캐시 중 dev_cache 의 caution 인 종류(Gradle 래퍼·NuGet 전역 패키지)뿐인지.
  실행: 휴지통을 **다른 항목보다 먼저** 한 번 비우는지, 분석 뒤 바뀐 폴더는 건너뛰고 사용 중인 파일은 실패로 남기며 나머지는 계속하는지,
  프로필 설정은 남는지, 실행할 수 없는 대상은 골라도 하지 않는지, 비우기 실패에도 나머지를 계속하는지, 고르지 않으면 비우지 않는지, 분석 취소.
  분석 범위(`CleanerOps.kinds`): 시스템 범위는 개발 캐시·가상 디스크 행이 없고 같은 판정, 캐시 범위는 그 밖의 행이 없음, 한 종류만 고르기
- `booster`: 가짜 레지스트리 키(`HKCU\Software\ByteSieveTest\booster_<pid>`, 끝나면 지움)와 임시 폴더로 시작 프로그램 목록(문자열 값만,
  `desktop.ini`·하위 폴더 제외)·이미 꺼진 항목, 끄기/켜기가 등록을 지우지 않고 StartupApproved 값만 바꾸는지(32비트 항목은 `Run32`),
  StartupApproved 값 해석·만들기, 보호 판정(시스템·자신·세션 0·다른 세션·필수 이름·Windows 폴더·비슷한 이름의 폴더·정보 없음), CPU 사용률 계산
  (넘침·PID 재사용), 테스트가 띄운 자식 프로세스만 끝내기(시작 시각이 다르면 끝내지 않음, 자신은 거부, 끝난 뒤 다시 시도)
- `ui_lang`: 모든 번역 문자열이 네 언어(한국어·영어·일본어·중국어 간체)에 다 있는지, 같은 항목의 서식 지정자가 언어마다 같은 순서인지,
  폴더 상태 표시가 공백으로 시작하는지, 언어 태그 해석(지역 표기·대소문자·`_`, 번체·지원하지 않는 언어·너무 긴 값 거부),
  LANGID 변환(번체·그 밖의 언어는 영어), 환경 변수 `BYTESIEVE_LANG`이 먼저이고 해석할 수 없으면 다음 단계로 넘어가는지(레지스트리는 바꾸지 않음)
- `nav_model`: 사이드바 메뉴 표(모든 페이지가 한 번씩 자기 그룹 머리글 아래, 목적별 그룹), 네 언어 문구가 비지 않고 겹치지 않음,
  키 이동(머리글 건너뜀·끝에서 멈춤·↓로 모든 페이지를 한 번씩), 배치(행 위치·그룹 간격·전체 높이)와 마우스 위치 판정(머리글·간격·여백은 없음)
- `treemap`: 넓이가 무게에 비례하는지, 영역 안·서로 겹치지 않음·영역을 빈틈없이 채움, 논문 예(6×4에 6,6,4,3,2,2,1) 배치,
  무게 0·합 0·넓이 0 영역, 64비트에 가까운 큰 값, 무작위 무게 300개(정렬·비정렬)
- `gui_scan`의 트리맵 검사: 보기를 트리맵으로 바꾸면 목록 대신 보이는지, 블록이 영역 안에 겹치지 않고 용량에 비례하며 합이 기준 폴더와
  맞는지, 클릭(트리에서 선택)·더블클릭(확대)·Backspace(상위 폴더로)·트리 선택(기준 폴더)이 연동되는지, 색 구분마다 그리기·정리 후보 색

## GUI 자체 검사 옵션

직접 실행할 수도 있습니다(종료 코드 0 성공, 1 실패).

```powershell
build\Debug\bytesieve-gui.exe --self-test C:\some\dir
build\Debug\bytesieve-gui.exe --self-test-cancel C:\some\dir
build\Debug\bytesieve-gui.exe --self-test-changes C:\some\dir   # 직전 스냅숏 필요, 변화 목록을 stdout에 출력
build\Debug\bytesieve-gui.exe --self-test-precise C:\some\dir   # 정밀 스캔, 기본·중복 제외 집계를 stdout에 출력
build\Debug\bytesieve-gui.exe --self-test-no-autosave C:\some\dir    # 주의: 자동 저장 설정을 끔(처음 설정을 stdout에 출력)
build\Debug\bytesieve-gui.exe --self-test-clear-history C:\some\dir  # 주의: 저장된 기록을 확인 창 없이 모두 삭제
build\Debug\bytesieve-gui.exe --self-test-export C:\some\dir C:\out\result   # result.csv·result.html 저장
build\Debug\bytesieve-gui.exe --self-test-drives
build\Debug\bytesieve-gui.exe --self-test-cleanup C:\some\dir
build\Debug\bytesieve-gui.exe --self-test-cleanup-cancel C:\some\dir
build\Debug\bytesieve-gui.exe --self-test-trash C:\some\dir     # 주의: 옮길 수 있는 항목을 모두 휴지통으로 옮김
$env:BYTESIEVE_QUARANTINE_ROOT = "C:\some\dir"                # 이 폴더 안을 휴지통 없는 드라이브로 다룸
build\Debug\bytesieve-gui.exe --self-test-quarantine C:\some\dir C:\some\dir\app\server.log
                                                                 # 주의: 격리 → 복원 → 남은 항목 영구 삭제
build\Debug\bytesieve-gui.exe --self-test-monitor
build\Debug\bytesieve-gui.exe --self-test-booster        # 목록·CPU 측정·보호 판정만 확인(아무것도 바꾸지 않음)
# 주의: --self-test-cleaner 는 분석한 대상을 확인 창 없이 정리한다. 반드시 TEMP·TMP·LOCALAPPDATA·USERPROFILE·APPDATA 와
# BYTESIEVE_QUARANTINE_ROOT 를 테스트 폴더로 돌린 뒤 실행한다(gui_cleaner_check.cmake 참고). 위치가 하나라도 <dir> 밖이면 정리하지 않고 실패한다.
build\Debug\bytesieve-gui.exe --self-test-cleaner C:\fx
build\Debug\bytesieve-gui.exe --self-test-lang en      # ko·en·ja·zh-Hans, 성공하면 언어·글꼴·최소 폭을 stdout에 출력
build\Debug\bytesieve-gui.exe --self-test-nav          # 사이드바 이동·페이지 전환(조회는 세기만 함), 지나간 페이지를 stdout에 출력
```

`--self-test-cleaner`는 분석한 위치(공용 개발 캐시 포함)가 **하나라도** 지정한 폴더 밖이면(실제 사용자 파일·캐시를 볼 수 있으므로) 아무것도
옮기지 않고 실패합니다. 사용자 폴더 환경 변수를 테스트 폴더로 돌리고 `npm_config_cache`·`PIP_CACHE_DIR`·`GRADLE_USER_HOME`·`CARGO_HOME`·
`NUGET_PACKAGES` 등을 지운 뒤 실행하세요(`gui_cleaner`가 그렇게 합니다). `--self-test-cleaner`는 [시스템 정리]를 분석·정리한 뒤 이어서 [개발 캐시]를 분석·정리합니다(두 페이지 모두 같은 확인을 거침).

`--self-test-trash`는 확인 창 없이 실제로 휴지통으로 옮기므로 테스트용 폴더에만 쓰세요. `--self-test-quarantine`도 확인 창 없이
옮기고 영구 삭제하므로 테스트용 폴더에만 쓰세요(격리 폴더 페이지는 이 폴더만 보여 주어 사용자의 실제 격리 항목은 건드리지 않습니다).
`BYTESIEVE_QUARANTINE_ROOT`는 테스트용 환경 변수로, 지정한 폴더 안의 항목을 휴지통이 없는 드라이브처럼 그 폴더의 격리 폴더로 옮깁니다. 폴더 스캔 자체 검사도 스냅숏을 저장하므로,
CTest는 `BYTESIEVE_HISTORY_DIR`을 빌드 폴더로 지정해 사용자 기록 폴더를 건드리지 않습니다.
`--self-test-no-autosave`·`--self-test-clear-history`는 기록 폴더의 설정·기록을 바꾸므로 `BYTESIEVE_HISTORY_DIR`을 테스트용 폴더로 지정해 실행하세요.
`gui_history`는 기준 스캔 → 변화 비교 → 자동 저장 끄기(스냅숏 그대로, 재실행 뒤에도 꺼짐) → 기록 삭제(스냅숏과 남은 임시 파일만 지우고
설정·다른 파일 보존, 변화 열·변화 순위·분석 줄·버튼에 반영) 순으로 확인합니다.
