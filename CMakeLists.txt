cmake_minimum_required(VERSION 3.21)
# 버전은 여기 한 곳에서만 정한다(version.h·exe 버전 리소스·설치 프로그램·릴리스 이름이 따른다). docs/release.md 참고.
project(ByteSieve VERSION 1.0.0 LANGUAGES C RC)

# C 런타임을 정적으로 링크한다: 사용자 PC 에 VC++ 재배포 패키지가 없어도 exe 하나로 실행된다.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# 생성 헤더(version.h): CLI·GUI·리소스가 함께 쓴다.
configure_file(src/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h @ONLY)
set(BYTESIEVE_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

function(bytesieve_configure target)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE)
    target_include_directories(${target} PRIVATE ${BYTESIEVE_GENERATED_DIR})
    if(MSVC)
        # 리소스 파일(.rc)에는 C 컴파일러 옵션을 주지 않는다.
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:/W4 /utf-8 /std:c11>)
    endif()
endfunction()

# 배포하는 exe: 아이콘·버전 정보·앱 매니페스트 리소스(res/*.rc). 매니페스트는 리소스로 넣으므로
# 링커가 따로 만들지 않게 한다(중복 매니페스트 방지).
function(bytesieve_exe_resources target rc)
    target_sources(${target} PRIVATE ${rc})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/res)
    if(MSVC)
        target_link_options(${target} PRIVATE /MANIFEST:NO)
    endif()
endfunction()

add_library(bytesieve_path STATIC src/path.c)
target_include_directories(bytesieve_path PUBLIC src)
bytesieve_configure(bytesieve_path)

add_library(bytesieve_scanner STATIC src/scanner.c)
target_include_directories(bytesieve_scanner PUBLIC src)
target_link_libraries(bytesieve_scanner PUBLIC bytesieve_path)
bytesieve_configure(bytesieve_scanner)

add_library(bytesieve_jsonl STATIC src/jsonl.c)
target_include_directories(bytesieve_jsonl PUBLIC src)
target_link_libraries(bytesieve_jsonl PUBLIC bytesieve_scanner bytesieve_history)
bytesieve_configure(bytesieve_jsonl)

add_library(bytesieve_cli STATIC src/cli.c)
target_include_directories(bytesieve_cli PUBLIC src)
target_link_libraries(bytesieve_cli PUBLIC bytesieve_scanner bytesieve_history bytesieve_format)
bytesieve_configure(bytesieve_cli)

# GUI 연동용 데이터 모델(진행률·폴더 트리·용량 순위). 출력 코드에 의존하지 않는다.
add_library(bytesieve_model STATIC src/scan_model.c)
target_include_directories(bytesieve_model PUBLIC src)
# 격리 폴더(CLEANUP_QUARANTINE_DIR)는 폴더 스캔에서 뺀다.
target_link_libraries(bytesieve_model PUBLIC bytesieve_scanner bytesieve_cleanup_rules)
bytesieve_configure(bytesieve_model)

# 폴더별 용량 변화 추적(스냅숏 저장·비교). 모델에 의존한다.
add_library(bytesieve_history STATIC src/scan_history.c)
target_include_directories(bytesieve_history PUBLIC src)
target_link_libraries(bytesieve_history PUBLIC bytesieve_model bytesieve_format)
bytesieve_configure(bytesieve_history)

# 스캔을 별도 스레드에서 돌리는 작업(진행률 스냅샷·취소). GUI 와 테스트가 함께 쓴다.
add_library(bytesieve_job STATIC src/scan_job.c)
target_include_directories(bytesieve_job PUBLIC src)
target_link_libraries(bytesieve_job PUBLIC bytesieve_model)
bytesieve_configure(bytesieve_job)

# 정리 후보 탐지 규칙(이름 분류·프로젝트 표시 파일·시스템·프로그램 보호 경로). 파일 시스템에 접근하지 않는다
# (보호 경로는 환경 변수와 프로그램 제거 정보 레지스트리에서 읽는다).
add_library(bytesieve_cleanup_rules STATIC src/cleanup_rules.c)
target_include_directories(bytesieve_cleanup_rules PUBLIC src)
target_link_libraries(bytesieve_cleanup_rules PUBLIC bytesieve_path advapi32)
bytesieve_configure(bytesieve_cleanup_rules)

# 실제 확보 가능 용량(할당 크기·하드 링크 판정). 파일을 읽기 속성으로만 조회하며 아무것도 바꾸지 않는다.
add_library(bytesieve_reclaim STATIC src/reclaim.c)
target_include_directories(bytesieve_reclaim PUBLIC src)
target_link_libraries(bytesieve_reclaim PUBLIC bytesieve_scanner bytesieve_path)
bytesieve_configure(bytesieve_reclaim)

# 정리 후보 탐지 엔진(scan_tree_ex 기반). 읽기 전용이며 아무것도 삭제·수정하지 않는다.
add_library(bytesieve_cleanup STATIC src/cleanup_scan.c)
target_include_directories(bytesieve_cleanup PUBLIC src)
target_link_libraries(bytesieve_cleanup PUBLIC bytesieve_scanner bytesieve_cleanup_rules
                      bytesieve_reclaim)
bytesieve_configure(bytesieve_cleanup)

# Git 기반 프로젝트 분석(후보를 프로젝트별로 묶기, git.exe·.git 직접 읽기, 마지막 수정, 휴면, 복구 가능성).
# 읽기 전용이며 git 은 --no-optional-locks·core.fsmonitor=false 로만 실행한다.
add_library(bytesieve_project STATIC src/project_info.c)
target_include_directories(bytesieve_project PUBLIC src)
target_link_libraries(bytesieve_project PUBLIC bytesieve_cleanup)
bytesieve_configure(bytesieve_project)

# 탐지를 별도 스레드에서 돌리는 작업(진행률 스냅샷·취소, 선택하면 프로젝트 분석까지).
add_library(bytesieve_cleanup_job STATIC src/cleanup_job.c)
target_include_directories(bytesieve_cleanup_job PUBLIC src)
target_link_libraries(bytesieve_cleanup_job PUBLIC bytesieve_cleanup bytesieve_project)
bytesieve_configure(bytesieve_cleanup_job)

# 휴지통이 없는 드라이브용 격리 폴더(같은 볼륨 안 이동·기록·목록·복원·영구 삭제).
add_library(bytesieve_quarantine STATIC src/quarantine.c)
target_include_directories(bytesieve_quarantine PUBLIC src)
target_link_libraries(bytesieve_quarantine PUBLIC bytesieve_cleanup_rules)
bytesieve_configure(bytesieve_quarantine)

# 정리 후보를 휴지통으로 옮기기(삭제 보호 판정·직전 확인·IFileOperation·작업 스레드).
# 휴지통을 쓸 수 없으면 격리 폴더로 옮긴다. 영구 삭제는 하지 않는다.
add_library(bytesieve_cleanup_trash STATIC src/cleanup_trash.c)
target_include_directories(bytesieve_cleanup_trash PUBLIC src)
target_link_libraries(bytesieve_cleanup_trash PUBLIC bytesieve_cleanup bytesieve_quarantine ole32
                      shell32 uuid)
bytesieve_configure(bytesieve_cleanup_trash)

# 정리 계획(탐지 결과 → 대상·선택 정책·요약 → 실행). GUI 두 정리 페이지와 CLI(disk-scanner clean)가 함께 쓴다.
add_library(bytesieve_plan STATIC src/clean_plan.c)
target_include_directories(bytesieve_plan PUBLIC src)
target_link_libraries(bytesieve_plan PUBLIC bytesieve_cleanup_trash bytesieve_project shell32 ole32)
bytesieve_configure(bytesieve_plan)

# 공용 개발 캐시(npm·pip·Gradle·Cargo·NuGet) 위치 탐지·용량 세기. 읽기 전용이며 정리는 cleanup_trash 로.
add_library(bytesieve_dev_cache STATIC src/dev_cache.c)
target_include_directories(bytesieve_dev_cache PUBLIC src)
target_link_libraries(bytesieve_dev_cache PUBLIC bytesieve_cleanup_trash shell32 ole32)
bytesieve_configure(bytesieve_dev_cache)

# WSL2·Docker 가상 디스크(.vhdx) 찾기(레지스트리 Lxss·Docker Desktop 기본 위치). 읽기 전용.
add_library(bytesieve_vdisk STATIC src/vdisk.c)
target_include_directories(bytesieve_vdisk PUBLIC src)
target_link_libraries(bytesieve_vdisk PUBLIC bytesieve_path advapi32 shell32 ole32)
bytesieve_configure(bytesieve_vdisk)

# 개발 캐시·가상 디스크 용량 기록(증가 추적, 원자적 저장).
add_library(bytesieve_devhist STATIC src/devenv_history.c)
target_include_directories(bytesieve_devhist PUBLIC src)
target_link_libraries(bytesieve_devhist PUBLIC bytesieve_path)
bytesieve_configure(bytesieve_devhist)

# Cleaner: 사용자 임시 파일·Windows 오류 보고서·휴지통·브라우저 캐시·공용 개발 캐시 분석과,
# 고른 대상만 정리(휴지통 비우기 외에는 cleanup_trash 의 안전 장치로 휴지통·격리 폴더로).
add_library(bytesieve_cleaner STATIC src/cleaner.c)
target_include_directories(bytesieve_cleaner PUBLIC src)
target_link_libraries(bytesieve_cleaner PUBLIC bytesieve_plan bytesieve_cleanup_trash bytesieve_dev_cache
                      bytesieve_vdisk bytesieve_devhist
                      bytesieve_scanner shell32 ole32)
bytesieve_configure(bytesieve_cleaner)

# Booster: 시작 프로그램 목록·켜기/끄기(StartupApproved), 프로세스 CPU·메모리, 보호 판정 후 끝내기.
add_library(bytesieve_booster STATIC src/booster.c)
target_include_directories(bytesieve_booster PUBLIC src)
target_link_libraries(bytesieve_booster PUBLIC bytesieve_path advapi32 shell32 ole32 psapi user32)
bytesieve_configure(bytesieve_booster)

# 스퀘어리파이드 트리맵 배치(순수 계산). GUI 폴더 스캔 페이지의 트리맵 보기가 쓴다.
add_library(bytesieve_treemap STATIC src/treemap.c)
target_include_directories(bytesieve_treemap PUBLIC src)
bytesieve_configure(bytesieve_treemap)

# 용량 표시 문자열("1.50 KiB"). GUI 의 스캔 화면과 드라이브 대시보드가 함께 쓴다.
add_library(bytesieve_format STATIC src/size_format.c)
target_include_directories(bytesieve_format PUBLIC src)
bytesieve_configure(bytesieve_format)

# 스캔 결과 내보내기(CSV·HTML 보고서). 모델을 읽기만 한다.
add_library(bytesieve_export STATIC src/scan_export.c)
target_include_directories(bytesieve_export PUBLIC src)
target_link_libraries(bytesieve_export PUBLIC bytesieve_model bytesieve_format)
bytesieve_configure(bytesieve_export)

# 논리 드라이브 목록·용량 조회(GetLogicalDrives, GetDiskFreeSpaceExW). 스캐너와 독립.
add_library(bytesieve_drive STATIC src/drive.c)
target_include_directories(bytesieve_drive PUBLIC src)
# 암호화 상태: 셸 속성(System.Volume.BitLockerProtection)
target_link_libraries(bytesieve_drive PUBLIC ole32 shell32 propsys)
bytesieve_configure(bytesieve_drive)

# RAM·CPU 사용률 측정(GlobalMemoryStatusEx, GetSystemTimes). UI 와 독립.
add_library(bytesieve_sysmon STATIC src/sysmon.c)
target_include_directories(bytesieve_sysmon PUBLIC src)
bytesieve_configure(bytesieve_sysmon)

# 화면 표시 언어(한국어·영어·일본어·중국어 간체)와 번역 문자열 표, 언어 결정
# (환경 변수 → 설치 프로그램이 기록한 레지스트리 값 → Windows 표시 언어). 창과 무관하다.
add_library(bytesieve_ui_lang STATIC src/ui_lang.c)
target_include_directories(bytesieve_ui_lang PUBLIC src)
target_link_libraries(bytesieve_ui_lang PUBLIC advapi32)
bytesieve_configure(bytesieve_ui_lang)

# GUI 사이드바 메뉴 모델(목적별 그룹·키 이동·위치 판정). 순수 계산이라 창 없이 단위 테스트한다.
add_library(bytesieve_nav STATIC src/nav_model.c)
target_include_directories(bytesieve_nav PUBLIC src)
target_link_libraries(bytesieve_nav PUBLIC bytesieve_ui_lang)
bytesieve_configure(bytesieve_nav)

# GUI 설정(테마·표시 언어) 저장·해석: HKCU\Software\ByteSieve 의 Theme·Language 값, 시스템 앱 모드·고대비.
# 창과 무관하다(테스트는 BYTESIEVE_SETTINGS_KEY 로 테스트 키를 쓴다).
add_library(bytesieve_settings STATIC src/app_settings.c)
target_include_directories(bytesieve_settings PUBLIC src)
target_link_libraries(bytesieve_settings PUBLIC bytesieve_ui_lang advapi32 user32)
bytesieve_configure(bytesieve_settings)

# Win32 GUI: 폴더 스캔 페이지(진행률 표시줄, 폴더 트리, 용량 순위), 드라이브 대시보드 페이지,
# 프로젝트 정리 페이지(개발 캐시·빌드 결과물·임시·로그 탐지, 선택 항목 휴지통 이동),
# 격리 폴더 페이지(휴지통 없는 드라이브에서 격리한 항목의 복원·영구 삭제),
# 모니터 페이지(RAM·CPU 실시간 사용률 막대·그래프).
# gui_main: 창 골격(사이드바·페이지 제목·페이지 전환)과 자체 검사 진행만 맡는다.
# gui_nav: 왼쪽 사이드바(목적별 메뉴, 직접 그림). gui_scan: 폴더 스캔 페이지 패널.
# gui_theme: Windows 11 창 외곽(DWM), 밝게·어둡게 색 토큰과 다크 모드 적용(uxtheme), 언어별 UI 글꼴,
# 글자 폭에 맞춘 컨트롤 크기. gui_settings: 설정 페이지(테마·표시 언어·스캔 기록).
# 시스템 정리·개발 캐시 페이지(gui_cleaner 패널 둘, 분석 범위만 다름)·프로젝트 정리(gui_cleanup)·부스터(gui_booster).
add_executable(bytesieve-gui WIN32 src/gui_main.c src/gui_nav.c src/gui_scan.c src/gui_theme.c
               src/gui_drives.c src/gui_cleanup.c src/gui_quarantine.c src/gui_monitor.c
               src/gui_treemap.c src/gui_cleaner.c src/gui_booster.c src/gui_settings.c
               src/gui_progress.c)
target_link_libraries(bytesieve-gui PRIVATE bytesieve_job bytesieve_history bytesieve_export
                      bytesieve_treemap bytesieve_ui_lang bytesieve_nav bytesieve_settings
                      bytesieve_cleaner bytesieve_booster
                      bytesieve_drive bytesieve_format bytesieve_cleanup_job
                      bytesieve_cleanup_trash bytesieve_dev_cache bytesieve_sysmon comctl32
                      comdlg32 shell32 ole32 dwmapi uxtheme)
bytesieve_configure(bytesieve-gui)
bytesieve_exe_resources(bytesieve-gui ${CMAKE_CURRENT_SOURCE_DIR}/res/gui.rc)

# 대용량 폴더 스캔의 실행 시간·메모리 측정 도구.
add_executable(bytesieve-bench src/bench.c)
target_link_libraries(bytesieve-bench PRIVATE bytesieve_model bytesieve_cleanup bytesieve_format psapi)
bytesieve_configure(bytesieve-bench)

# disk-scanner clean: GUI 정리 페이지와 같은 탐지(cleanup_job·cleaner)·계획(clean_plan)·실행을 쓰는 CLI.
add_library(bytesieve_cli_clean STATIC src/cli_clean.c src/cli_devenv.c)
target_include_directories(bytesieve_cli_clean PUBLIC src)
target_link_libraries(bytesieve_cli_clean PUBLIC bytesieve_plan bytesieve_cleaner bytesieve_cleanup_job
                      bytesieve_jsonl bytesieve_format bytesieve_cli bytesieve_history ole32)
bytesieve_configure(bytesieve_cli_clean)

add_executable(disk-scanner src/main.c)
target_link_libraries(disk-scanner PRIVATE bytesieve_cli bytesieve_cli_clean bytesieve_jsonl
                      bytesieve_cleanup_rules bytesieve_format)
bytesieve_configure(disk-scanner)
bytesieve_exe_resources(disk-scanner ${CMAKE_CURRENT_SOURCE_DIR}/res/cli.rc)

# 배포 파일(설치 프로그램·포터블 ZIP 의 내용). tools/package.ps1 이 cmake --install 로 모은다.
install(TARGETS bytesieve-gui disk-scanner RUNTIME DESTINATION .)
install(FILES LICENSE DESTINATION . RENAME LICENSE.txt)
install(FILES installer/README.txt DESTINATION .)

enable_testing()

# 성능 측정 도구가 동작하고 스캐너 진단(작업 수·메모리·미리 열거)을 출력하는지(최적화 전후 비교용).
add_test(NAME bench_smoke COMMAND bytesieve-bench --mode tree --runs 1 ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_tests_properties(bench_smoke PROPERTIES
                     PASS_REGULAR_EXPRESSION "scanner: peak tasks [0-9]+ .* prefetched [0-9]+")

# 인자 없이 실행하면 종료 코드 2(비정상)여야 한다.
add_test(NAME no_args COMMAND disk-scanner)
set_tests_properties(no_args PROPERTIES WILL_FAIL TRUE)

add_test(NAME help COMMAND disk-scanner --help)
# 버전: CMake project VERSION 과 같은 값을 출력한다(릴리스 이름·설치 프로그램 버전과 일치).
add_test(NAME cli_version COMMAND disk-scanner --version)
set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "^disk-scanner ${PROJECT_VERSION}")

# 소스 디렉터리 스캔은 성공, 존재하지 않는 경로는 실패(종료 코드 2)해야 한다.
add_test(NAME scan_source_dir COMMAND disk-scanner ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME scan_missing_dir COMMAND disk-scanner ${CMAKE_CURRENT_SOURCE_DIR}/no-such-dir)
set_tests_properties(scan_missing_dir PROPERTIES WILL_FAIL TRUE)

# --max-depth 는 0 이상의 정수만 허용한다.
add_test(NAME scan_max_depth COMMAND disk-scanner --max-depth 0 ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME scan_bad_max_depth COMMAND disk-scanner --max-depth -1 ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_tests_properties(scan_bad_max_depth PROPERTIES WILL_FAIL TRUE)

# 종료 코드와 출력을 정확히 검사하는 CLI 테스트(tests/cli_check.cmake).
# args 는 '|' 로 구분, stdout/stderr 는 '|' 로 구분한 정규식(모두 일치해야 함).
function(bytesieve_cli_test name expect_exit args stdout_re stderr_re)
    set(defs -DEXE=$<TARGET_FILE:disk-scanner> "-DARGS=${args}" -DEXPECT_EXIT=${expect_exit})
    if(NOT stdout_re STREQUAL "")
        list(APPEND defs "-DEXPECT_STDOUT=${stdout_re}")
    endif()
    if(NOT stderr_re STREQUAL "")
        list(APPEND defs "-DEXPECT_STDERR=${stderr_re}")
    endif()
    list(APPEND defs ${ARGN})
    add_test(NAME cli_${name}
             COMMAND ${CMAKE_COMMAND} ${defs} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_check.cmake)
endfunction()

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

bytesieve_cli_test(complete 0 "${SRC_DIR}"
    "Files: +[1-9]|Directories: +0|Total size: +[0-9.]+ [KM]iB|Errors: +0\n|Status: +complete" "")
bytesieve_cli_test(options_after_target 0 "${SRC_DIR}|--max-depth=0"
    "Max depth: +0\n|Status: +complete" "")
bytesieve_cli_test(partial 1 "@FIXTURE@"
    "Files: +1\n|Directories: +2\n|Total size: +5 bytes|Errors: +1 .access_denied 1.|Skipped dirs: +1\n|Aborted: +no|Status: +partial .1 directory could not be read"
    "error: .*locked: access_denied .Windows error 5."
    -DDENY_FIXTURE=${CMAKE_CURRENT_BINARY_DIR}/cli_partial_fixture)
# 정리한 항목을 옮겨 둔 격리 폴더(.bytesieve-trash, 1000바이트)는 세지 않는다(GUI·--track 과 같게).
bytesieve_cli_test(skip_quarantine 0 "@FIXTURE@"
    "Files: +1\n|Total size: +5 bytes|Status: +complete" ""
    -DQUARANTINE_FIXTURE=${CMAKE_CURRENT_BINARY_DIR}/cli_quarantine_fixture)
bytesieve_cli_test(fatal_missing 2 "${CMAKE_CURRENT_SOURCE_DIR}/no-such-dir"
    "Errors: +1 .not_found 1.|Aborted: +yes|Status: +fatal .scan aborted."
    "error: .*no-such-dir: not_found")
bytesieve_cli_test(fatal_not_dir 2 "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
    "Errors: +1 .invalid_path 1.|Status: +fatal" "")
bytesieve_cli_test(help 0 "--help" "Usage: disk-scanner|Exit codes:" "")
bytesieve_cli_test(help_wins 0 "--bogus|-h" "Usage: disk-scanner" "")
bytesieve_cli_test(no_args 2 "" "" "error: missing <directory>|Usage: disk-scanner")
bytesieve_cli_test(unknown_option 2 "--verbose|${SRC_DIR}" "" "error: unknown option: --verbose|Usage:")
bytesieve_cli_test(missing_value 2 "${SRC_DIR}|--max-depth" "" "error: --max-depth requires a value")
bytesieve_cli_test(bad_value 2 "--max-depth|abc|${SRC_DIR}" "" "error: invalid --max-depth value: 'abc'")
bytesieve_cli_test(duplicate_option 2 "--max-depth=1|--max-depth=2|${SRC_DIR}" ""
    "error: --max-depth given more than once")
bytesieve_cli_test(extra_argument 2 "${SRC_DIR}|${SRC_DIR}" "" "error: unexpected extra argument")

# JSON Lines 모드: stdout 은 UTF-8 이벤트 줄(start → result → complete).
bytesieve_cli_test(jsonl_scan 0 "--jsonl|${SRC_DIR}"
    "^{\"event\":\"start\",[^\n]*}\n{\"event\":\"result\",[^\n]*}\n{\"event\":\"complete\",\"status\":\"complete\",\"exit_code\":0}\n$" "")
bytesieve_cli_test(jsonl_max_depth 0 "${CMAKE_CURRENT_SOURCE_DIR}|--max-depth=0|--jsonl"
    "{\"event\":\"complete\",\"status\":\"complete\",\"exit_code\":0}" "")
bytesieve_cli_test(jsonl_missing_dir 2 "--jsonl|${CMAKE_CURRENT_SOURCE_DIR}/no-such-dir"
    "{\"event\":\"complete\",\"status\":\"failed\",\"exit_code\":2}" "")
bytesieve_cli_test(jsonl_partial 1 "--jsonl|@FIXTURE@"
    "\"error_count\":1,\"errors\":.{\"path\":\"[^\"]*locked\",\"code\":5}.|\"status\":\"partial\",\"exit_code\":1" ""
    -DDENY_FIXTURE=${CMAKE_CURRENT_BINARY_DIR}/cli_jsonl_partial_fixture)
bytesieve_cli_test(jsonl_skip_quarantine 0 "--jsonl|@FIXTURE@"
    "\"files\":1,[^\n]*\"bytes\":5,|\"status\":\"complete\",\"exit_code\":0" ""
    -DQUARANTINE_FIXTURE=${CMAKE_CURRENT_BINARY_DIR}/cli_jsonl_quarantine_fixture)

# 폴더별 용량 변화 추적: 기준 스냅숏 → 픽스처 변경 → 비교, JSON Lines changes 이벤트.
add_test(NAME cli_track
         COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:disk-scanner>
                 -DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/cli_track_fixture
                 -DHIST=${CMAKE_CURRENT_BINARY_DIR}/cli_track_history
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_track_check.cmake)

# disk-scanner find·clean: 후보 탐색, dry-run·거부·실행(픽스처의 격리 폴더로), JSON 문서·JSON Lines.
# system 은 사용자 폴더를 픽스처로 돌린다.
add_test(NAME cli_find
         COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:disk-scanner> -DMODE=find
                 -DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/cli_find_fixture
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_clean_check.cmake)
add_test(NAME cli_devenv
         COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:disk-scanner> -DMODE=devenv
                 -DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/cli_devenv_fixture
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_clean_check.cmake)
add_test(NAME cli_clean_project
         COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:disk-scanner> -DMODE=project
                 -DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/cli_clean_project_fixture
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_clean_check.cmake)
add_test(NAME cli_clean_system
         COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:disk-scanner> -DMODE=system
                 -DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/cli_clean_system_fixture
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_clean_check.cmake)

add_executable(test_path tests/test_path.c)
target_link_libraries(test_path PRIVATE bytesieve_path)
bytesieve_configure(test_path)
add_test(NAME path COMMAND test_path)

add_executable(test_scanner tests/test_scanner.c)
target_link_libraries(test_scanner PRIVATE bytesieve_scanner)
bytesieve_configure(test_scanner)
add_test(NAME scanner COMMAND test_scanner)

# 디스크 사용량 분석(논리 대 할당 크기)과 병렬 미리 열거의 순서 보장.
add_executable(test_scan_alloc tests/test_scan_alloc.c)
target_link_libraries(test_scan_alloc PRIVATE bytesieve_scanner)
bytesieve_configure(test_scan_alloc)
add_test(NAME scan_alloc COMMAND test_scan_alloc)

add_executable(test_jsonl tests/test_jsonl.c)
target_link_libraries(test_jsonl PRIVATE bytesieve_jsonl)
bytesieve_configure(test_jsonl)
add_test(NAME jsonl COMMAND test_jsonl)

add_executable(test_cli tests/test_cli.c)
target_link_libraries(test_cli PRIVATE bytesieve_cli)
bytesieve_configure(test_cli)
add_test(NAME cli COMMAND test_cli)

add_executable(test_scan_model tests/test_scan_model.c)
target_link_libraries(test_scan_model PRIVATE bytesieve_model)
bytesieve_configure(test_scan_model)
add_test(NAME scan_model COMMAND test_scan_model)

add_executable(test_scan_history tests/test_scan_history.c)
target_link_libraries(test_scan_history PRIVATE bytesieve_history)
bytesieve_configure(test_scan_history)
add_test(NAME scan_history COMMAND test_scan_history)

# 정밀 스캔(하드 링크 중복 보정): 실제 하드 링크 픽스처로 기본 합계 유지, 중복 경로·용량·할당,
# 작업 스레드 1개·8개의 같은 결과, 모델의 폴더별 중복·순위·진행률을 확인한다.
# 병렬 미리 열거: 하위 폴더 2500개 픽스처에서 동시에 살아 있는 폴더 작업 수 상한, 작업 스레드 1개·8개의
# 같은 결과, 열거 수 맞추기(직접 + 미리 - 버림 = 처리), 제외 훅·취소 뒤 정리를 확인한다.
add_executable(test_scan_parallel tests/test_scan_parallel.c)
target_link_libraries(test_scan_parallel PRIVATE bytesieve_scanner)
bytesieve_configure(test_scan_parallel)
add_test(NAME scan_parallel COMMAND test_scan_parallel)
set_tests_properties(scan_parallel PROPERTIES TIMEOUT 120)

add_executable(test_hardlink tests/test_hardlink.c)
target_link_libraries(test_hardlink PRIVATE bytesieve_model)
bytesieve_configure(test_hardlink)
add_test(NAME hardlink COMMAND test_hardlink)

add_executable(test_export tests/test_export.c)
target_link_libraries(test_export PRIVATE bytesieve_export)
bytesieve_configure(test_export)
add_test(NAME export COMMAND test_export)

add_executable(test_scan_job tests/test_scan_job.c)
target_link_libraries(test_scan_job PRIVATE bytesieve_job)
bytesieve_configure(test_scan_job)
add_test(NAME scan_job COMMAND test_scan_job)

add_executable(test_drive tests/test_drive.c)
target_link_libraries(test_drive PRIVATE bytesieve_drive)
bytesieve_configure(test_drive)
add_test(NAME drive COMMAND test_drive)

add_executable(test_sysmon tests/test_sysmon.c)
target_link_libraries(test_sysmon PRIVATE bytesieve_sysmon)
bytesieve_configure(test_sysmon)
add_test(NAME sysmon COMMAND test_sysmon)

add_executable(test_size_format tests/test_size_format.c)
target_link_libraries(test_size_format PRIVATE bytesieve_format)
bytesieve_configure(test_size_format)
add_test(NAME size_format COMMAND test_size_format)

# Cleaner: 사용자 폴더를 픽스처로 돌리고 옮긴 항목은 픽스처의 격리 폴더로(BYTESIEVE_QUARANTINE_ROOT),
# 휴지통 조회·비우기와 프로그램 실행 여부는 가짜 함수로(실제 휴지통은 건드리지 않음).
add_executable(test_cleaner tests/test_cleaner.c)
target_link_libraries(test_cleaner PRIVATE bytesieve_cleaner)
bytesieve_configure(test_cleaner)
add_test(NAME cleaner COMMAND test_cleaner)

# Booster: 가짜 레지스트리 키(HKCU\Software\ByteSieveTest)·임시 폴더로 시작 프로그램 목록·켜기/끄기,
# 보호 판정·CPU 계산, 테스트가 띄운 자식 프로세스만 끝내기.
add_executable(test_booster tests/test_booster.c)
target_link_libraries(test_booster PRIVATE bytesieve_booster)
bytesieve_configure(test_booster)
add_test(NAME booster COMMAND test_booster)

# 표시 언어: 모든 번역이 채워졌는지, 언어마다 서식 지정자가 같은지, 언어 태그·LANGID 해석, 환경 변수 우선.
add_executable(test_ui_lang tests/test_ui_lang.c)
target_link_libraries(test_ui_lang PRIVATE bytesieve_ui_lang)
bytesieve_configure(test_ui_lang)
add_test(NAME ui_lang COMMAND test_ui_lang)

add_executable(test_nav_model tests/test_nav_model.c)
target_link_libraries(test_nav_model PRIVATE bytesieve_nav)
bytesieve_configure(test_nav_model)
add_test(NAME nav_model COMMAND test_nav_model)

add_executable(test_app_settings tests/test_app_settings.c)
target_link_libraries(test_app_settings PRIVATE bytesieve_settings)
bytesieve_configure(test_app_settings)
add_test(NAME app_settings COMMAND test_app_settings)

add_executable(test_treemap tests/test_treemap.c)
target_link_libraries(test_treemap PRIVATE bytesieve_treemap)
bytesieve_configure(test_treemap)
add_test(NAME treemap COMMAND test_treemap)

add_executable(test_cleanup_rules tests/test_cleanup_rules.c)
target_link_libraries(test_cleanup_rules PRIVATE bytesieve_cleanup_rules)
bytesieve_configure(test_cleanup_rules)
add_test(NAME cleanup_rules COMMAND test_cleanup_rules)

add_executable(test_cli_clean tests/test_cli_clean.c)
target_link_libraries(test_cli_clean PRIVATE bytesieve_cli_clean)
bytesieve_configure(test_cli_clean)
add_test(NAME cli_clean COMMAND test_cli_clean)

add_executable(test_devenv tests/test_devenv.c)
target_link_libraries(test_devenv PRIVATE bytesieve_cleaner)
bytesieve_configure(test_devenv)
add_test(NAME devenv COMMAND test_devenv)

add_executable(test_project tests/test_project.c)
target_link_libraries(test_project PRIVATE bytesieve_project)
bytesieve_configure(test_project)
add_test(NAME project COMMAND test_project)

add_executable(test_clean_plan tests/test_clean_plan.c)
target_link_libraries(test_clean_plan PRIVATE bytesieve_plan)
bytesieve_configure(test_clean_plan)
add_test(NAME clean_plan COMMAND test_clean_plan)

add_executable(test_reclaim tests/test_reclaim.c)
target_link_libraries(test_reclaim PRIVATE bytesieve_cleanup)
bytesieve_configure(test_reclaim)
add_test(NAME reclaim COMMAND test_reclaim)

add_executable(test_cleanup_scan tests/test_cleanup_scan.c)
target_link_libraries(test_cleanup_scan PRIVATE bytesieve_cleanup_job)
bytesieve_configure(test_cleanup_scan)
add_test(NAME cleanup_scan COMMAND test_cleanup_scan)

# 실제로 임시 폴더의 픽스처를 휴지통으로 옮긴 뒤, 테스트가 넣은 휴지통 항목만 지운다.
add_executable(test_cleanup_trash tests/test_cleanup_trash.c)
target_link_libraries(test_cleanup_trash PRIVATE bytesieve_cleanup_trash)
bytesieve_configure(test_cleanup_trash)
add_test(NAME cleanup_trash COMMAND test_cleanup_trash)

# 공용 개발 캐시: 사용자 폴더를 임시 픽스처로 돌려 기본 위치·환경 변수·설정 파일·캐시 구조 확인·
# 링크·용량·취소·정리 판정(항목별 탐지 경로)을 확인한다. 실제 캐시는 보지 않는다.
add_executable(test_dev_cache tests/test_dev_cache.c)
target_link_libraries(test_dev_cache PRIVATE bytesieve_dev_cache)
bytesieve_configure(test_dev_cache)
add_test(NAME dev_cache COMMAND test_dev_cache)

# 격리 폴더: 임시 폴더를 가짜 볼륨으로 써서 이동·목록·복원·충돌·실패·기록 불일치·영구 삭제와
# 정리(BYTESIEVE_QUARANTINE_ROOT)·탐지·폴더 스캔 연동을 확인한다.
add_executable(test_quarantine tests/test_quarantine.c)
target_link_libraries(test_quarantine PRIVATE bytesieve_cleanup_trash bytesieve_model)
bytesieve_configure(test_quarantine)
add_test(NAME quarantine COMMAND test_quarantine)

# GUI 자체 검사: 창(숨김)에서 스캔을 끝까지 돌리거나 시작 직후 취소한 뒤
# 진행률·트리·순위 화면이 모델과 맞는지 확인하고 종료 코드로 알린다.
add_test(NAME gui_scan COMMAND bytesieve-gui --self-test ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME gui_cancel COMMAND bytesieve-gui --self-test-cancel ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME gui_missing_dir COMMAND bytesieve-gui --self-test ${CMAKE_CURRENT_SOURCE_DIR}/no-such-dir)
set_tests_properties(gui_missing_dir PROPERTIES WILL_FAIL TRUE)
# 드라이브 페이지로 전환해 조회를 마친 뒤 목록이 시스템 드라이브 정보와 맞는지 확인한다.
add_test(NAME gui_drives COMMAND bytesieve-gui --self-test-drives)
# 프로젝트 정리 페이지: 소스 트리(빌드 폴더 포함)를 탐지해 목록·합계·정렬·필터를 확인한다.
add_test(NAME gui_cleanup COMMAND bytesieve-gui --self-test-cleanup ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME gui_cleanup_cancel
         COMMAND bytesieve-gui --self-test-cleanup-cancel ${CMAKE_CURRENT_SOURCE_DIR})
# 프로젝트 정리 페이지: 임시 픽스처를 탐지해 옮길 수 있는 항목을 모두 휴지통으로 옮기고(확인 창 없이)
# 결과 화면·파일 상태를 확인한다. 보호 대상(접근 거부로 일부 누락)은 남아야 한다.
add_test(NAME gui_trash
         COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:bytesieve-gui>
                 -DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/gui_trash_fixture
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_trash_check.cmake)
# 빌드 폴더에서 휴지통을 쓸 수 없으면(이동식 드라이브, 휴지통 꺼짐·용량 부족) 휴지통 경로는 검사할 수 없으므로
# 이유를 쓰고 건너뛴다(격리한 테스트 파일은 복원해 실제 드라이브의 격리 폴더에 남기지 않음).
set_tests_properties(gui_trash PROPERTIES SKIP_REGULAR_EXPRESSION "trash-skip:")
# 위 건너뜀·복원 경로: 픽스처를 휴지통 없는 위치로 다뤄 격리로 전환되고, 이유를 알리고, 모두 원래 자리로 복원되는지.
add_test(NAME gui_trash_fallback
         COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:bytesieve-gui>
                 -DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/gui_trash_fallback_fixture -DFORCE_QUARANTINE=1
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_trash_check.cmake)
# 격리 폴더 페이지: 픽스처를 휴지통 없는 드라이브처럼 다뤄(BYTESIEVE_QUARANTINE_ROOT) 정리 → 격리 →
# 복원(같은 이름은 덮어쓰지 않고 건너뜀) → 영구 삭제까지 화면과 파일 상태를 확인한다.
add_test(NAME gui_quarantine
         COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:bytesieve-gui>
                 -DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/gui_quarantine_fixture
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_quarantine_check.cmake)
# 폴더 스캔 페이지 변화 추적: 기준 스캔 → 픽스처 변경 → 변화 순위 보기·변화 열 확인.
add_test(NAME gui_history
         COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:bytesieve-gui>
                 -DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/gui_history_fixture
                 -DHIST=${CMAKE_CURRENT_BINARY_DIR}/gui_history_check
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_history_check.cmake)
# 폴더 스캔 페이지 정밀 스캔: 하드 링크 픽스처를 정밀 스캔(보정 열·보기·분석 줄) → 기본 스캔(열 숨김) 확인.
add_test(NAME gui_precise
         COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:bytesieve-gui>
                 -DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/gui_precise_fixture
                 -DHIST=${CMAKE_CURRENT_BINARY_DIR}/gui_precise_history
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_precise_check.cmake)
# 폴더 스캔 페이지 내보내기: 스캔 → CSV·HTML 보고서 저장(대화 상자 없이) → 파일 내용 확인.
add_test(NAME gui_export
         COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:bytesieve-gui>
                 -DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/gui_export_fixture
                 -DOUT=${CMAKE_CURRENT_BINARY_DIR}/gui_export_out
                 -DHIST=${CMAKE_CURRENT_BINARY_DIR}/gui_export_history
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_export_check.cmake)
# 모니터 페이지: 몇 초 동안 RAM·CPU 를 측정해 화면·1초 갱신 간격·측정 실패 표시를 확인한다.
add_test(NAME gui_monitor COMMAND bytesieve-gui --self-test-monitor)
# 시스템 정리·개발 캐시 페이지: 사용자 폴더·TEMP 를 픽스처로 돌려 시스템 정리 분석 → 화면 확인 → 휴지통
# 비우기를 뺀 정리 가능한 대상을 정리 → 개발 캐시 분석·확인·정리(픽스처의 격리 폴더로) → 오래된 항목만 옮기고 최근 항목·프로필 설정은 남는지 확인.
add_test(NAME gui_cleaner
         COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:bytesieve-gui>
                 -DFIXTURE=${CMAKE_CURRENT_BINARY_DIR}/gui_cleaner_fixture
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_cleaner_check.cmake)
# 부스터 페이지: 시작 프로그램·프로세스 목록과 CPU 측정, 보호 대상(자신·시스템)은 고를 수 없는지(읽기만 함).
add_test(NAME gui_booster COMMAND bytesieve-gui --self-test-booster)
set_tests_properties(gui_cleaner gui_booster PROPERTIES TIMEOUT 120)
# 사이드바: 키보드(↑↓·Home·End)·마우스로 모든 페이지를 오가며 고른 페이지만 보이는지, 제목, 처음 열 때
# 조회가 한 번만 일어나는지 확인한다(조회는 세기만 하고 실제로 하지 않아 사용자 시스템을 읽지 않음).
add_test(NAME gui_nav COMMAND bytesieve-gui --self-test-nav)
set_tests_properties(gui_nav PROPERTIES PASS_REGULAR_EXPRESSION
                     "nav: scan drives cleanup cleaner cache quarantine monitor booster settings")
# 다크 모드: 테스트 레지스트리 키(사용자 설정은 건드리지 않음, 끝나면 지움)에서 시스템 따르기(밝게) → 어둡게 →
# 시스템 따르기(시스템 어둡게·밝게로 바뀜) → 밝게 고정을 거치며 목록·트리·입력 칸·창 배경 색과 저장 값을 확인한다.
add_test(NAME gui_theme
         COMMAND ${CMAKE_COMMAND} -E env --unset=BYTESIEVE_LANG
                 "BYTESIEVE_SETTINGS_KEY=Software\\ByteSieveTest\\gui_theme"
                 BYTESIEVE_SYSTEM_THEME=light
                 BYTESIEVE_HISTORY_DIR=${CMAKE_CURRENT_BINARY_DIR}/gui_theme_history
                 $<TARGET_FILE:bytesieve-gui> --self-test-theme)
set_tests_properties(gui_theme PROPERTIES PASS_REGULAR_EXPRESSION
                     "theme: light dark system-dark system-light light-fixed")
# 설정 페이지: 표시 언어 저장(다음 실행 언어, 지금 화면은 그대로)과 자동 저장 끄고 켜기(폴더 스캔 페이지·
# 설정 파일과 일치)를 테스트 레지스트리 키·테스트 기록 폴더에서 확인한다.
add_test(NAME gui_settings
         COMMAND ${CMAKE_COMMAND} -E env --unset=BYTESIEVE_LANG
                 "BYTESIEVE_SETTINGS_KEY=Software\\ByteSieveTest\\gui_settings"
                 BYTESIEVE_HISTORY_DIR=${CMAKE_CURRENT_BINARY_DIR}/gui_settings_history
                 $<TARGET_FILE:bytesieve-gui> --self-test-settings)
set_tests_properties(gui_settings PROPERTIES PASS_REGULAR_EXPRESSION
                     "settings: language ja ko system, autosave")
# 표시 언어: 언어마다 창을 만들어 번역 문구·언어별 글꼴, 최소 창 크기에서 사이드바·단추 문구가 잘리지 않고
# 페이지 패널 안에 있는지 확인한다(문구가 긴 언어에서 배치가 깨지지 않게).
foreach(lang ko en ja zh-Hans)
    add_test(NAME gui_lang_${lang} COMMAND bytesieve-gui --self-test-lang ${lang})
    set_tests_properties(gui_lang_${lang} PROPERTIES PASS_REGULAR_EXPRESSION "lang: ${lang}, font: ")
endforeach()
set_tests_properties(gui_scan gui_cancel gui_missing_dir gui_drives gui_cleanup
                     gui_cleanup_cancel gui_trash gui_quarantine gui_monitor gui_history
                     gui_export gui_precise PROPERTIES TIMEOUT 120)
# 폴더 스캔 자체 검사가 사용자 기록 폴더(%LOCALAPPDATA%)에 스냅숏을 쓰지 않게 한다.
set_tests_properties(gui_scan gui_cancel gui_missing_dir PROPERTIES
                     ENVIRONMENT "BYTESIEVE_HISTORY_DIR=${CMAKE_CURRENT_BINARY_DIR}/gui_test_history")
