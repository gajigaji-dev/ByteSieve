# GUI 시스템 정리 탭(Cleaner + 공용 개발 캐시) 자체 검사(bytesieve-gui --self-test-cleaner).
#
#   cmake -DEXE=<bytesieve-gui> -DFIXTURE=<빈 폴더로 만들 경로> -P gui_cleaner_check.cmake
#
# TEMP·TMP·LOCALAPPDATA·USERPROFILE·APPDATA 를 픽스처로 돌리고 개발 도구 환경 변수를 지워 실제 사용자
# 파일을 보지 않게 한다. BYTESIEVE_QUARANTINE_ROOT 를 픽스처로 두어 정리한 항목이 실제 휴지통이 아니라
# 픽스처의 격리 폴더(.bytesieve-trash)로 가게 한다. GUI 는 분석 → 화면 확인(위치가 모두 픽스처 안인지,
# [모두 선택]이 휴지통 비우기·개발 도구 캐시를 고르지 않는지, 주의 캐시 표시 포함) → [모두 선택] 뒤
# 개발 도구 캐시를 직접 체크해 정리 가능한 대상을 모두 정리 → 결과를 확인한다.
# 실제 휴지통은 조회만 하고 비우지 않는다.
# 예전 [공용 캐시] 탭 검사의 경우를 함께 본다: 모든 도구의 캐시, 캐시 구조가 아닌 폴더를 가리키는
# 환경 변수(NUGET_HTTP_CACHE_PATH → mydocs, 이유와 함께 정리할 수 없는 행), 남아야 하는 도구 설정·설치한
# 도구. 실제 확보 용량은 하드 링크로 캐시 밖(outside\store.jar)과 나누는 파일을 뺀 값이어야 한다.
#
# FIXTURE\
#   temp\old.log, temp\olddir\x.bin                (3일 전)  → 격리 폴더
#   temp\fresh.txt                                 (지금)    → 남아야 함(최근 24시간)
#   local\Microsoft\Windows\WER\ReportArchive\r1\  (3일 전)  → 격리 폴더
#   local\Google\Chrome\User Data\Default\Cache\…            → 격리 폴더(Chrome 이 실행 중이면 남음)
#   local\Google\Chrome\User Data\Default\Preferences         (남아야 함)
#   local\npm-cache\_cacache\…                                → 격리 폴더
#   home\.gradle\wrapper\dists\gradle-8.5-bin\…  (주의 캐시)  → 격리 폴더
#   home\.gradle\caches\modules-2\…  (linked.jar 는 outside\store.jar 와 하드 링크) → 격리 폴더
#   home\.gradle\gradle.properties, home\.cargo\bin\cargo.exe        (남아야 함)
#   home\.cargo\registry\cache\…, home\.cargo\git\db\…               → 격리 폴더
#   home\.nuget\packages\serilog\3.0.0\… (주의 캐시)               → 격리 폴더
#   local\pip\Cache\http-v2\…                                        → 격리 폴더
#   mydocs\report.docx  (NUGET_HTTP_CACHE_PATH, 캐시 구조 아님)        (남아야 함)
#   outside\store.jar                                               (남아야 함)

foreach(var EXE FIXTURE)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "${var} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${FIXTURE}")
string(REPEAT "x" 100 hundred)
file(WRITE "${FIXTURE}/temp/old.log" "${hundred}")
file(WRITE "${FIXTURE}/temp/olddir/x.bin" "${hundred}${hundred}")
file(WRITE "${FIXTURE}/temp/fresh.txt" "new")
file(WRITE "${FIXTURE}/local/Microsoft/Windows/WER/ReportArchive/r1/Report.wer" "${hundred}")
file(WRITE "${FIXTURE}/local/Google/Chrome/User Data/Default/Preferences" "{}")
file(WRITE "${FIXTURE}/local/Google/Chrome/User Data/Default/Cache/Cache_Data/data_0"
     "${hundred}${hundred}${hundred}")
file(WRITE "${FIXTURE}/local/npm-cache/_cacache/index-v5/a" "${hundred}")
file(WRITE "${FIXTURE}/home/.gradle/wrapper/dists/gradle-8.5-bin/abc/gradle-8.5-bin.zip" "${hundred}")
string(REPEAT "${hundred}" 100 tenk)
file(WRITE "${FIXTURE}/home/.gradle/caches/modules-2/files-2.1/m.jar" "${tenk}")
file(WRITE "${FIXTURE}/outside/store.jar" "${tenk}${tenk}")
file(CREATE_LINK "${FIXTURE}/outside/store.jar"
     "${FIXTURE}/home/.gradle/caches/modules-2/files-2.1/linked.jar" RESULT link_rc)
if(NOT link_rc EQUAL 0)
    message(FATAL_ERROR "could not create hard link: ${link_rc}")
endif()
file(WRITE "${FIXTURE}/home/.gradle/gradle.properties" "secret=keep")
file(WRITE "${FIXTURE}/home/.cargo/registry/cache/c.crate" "${hundred}")
file(WRITE "${FIXTURE}/home/.cargo/git/db/d" "git")
file(WRITE "${FIXTURE}/home/.cargo/bin/cargo.exe" "keep")
file(WRITE "${FIXTURE}/home/.nuget/packages/serilog/3.0.0/.nupkg.metadata" "{}")
file(WRITE "${FIXTURE}/local/pip/Cache/http-v2/b" "${hundred}")
file(WRITE "${FIXTURE}/mydocs/report.docx" "doc")
file(MAKE_DIRECTORY "${FIXTURE}/home" "${FIXTURE}/roaming")

# 오래된 항목: 파일을 먼저, 폴더는 안을 다 만든 뒤에 시각을 3일 전으로 바꾼다.
set(ps1 "${FIXTURE}-settime.ps1")
file(WRITE "${ps1}" "$t = (Get-Date).AddDays(-3)\nforeach ($p in $args) {\n  $i = Get-Item -LiteralPath $p -Force\n  $i.CreationTime = $t\n  $i.LastWriteTime = $t\n}\n")
set(old_paths
    "${FIXTURE}/temp/old.log" "${FIXTURE}/temp/olddir/x.bin" "${FIXTURE}/temp/olddir"
    "${FIXTURE}/local/Microsoft/Windows/WER/ReportArchive/r1/Report.wer"
    "${FIXTURE}/local/Microsoft/Windows/WER/ReportArchive/r1")
set(native_old "")
foreach(p IN LISTS old_paths)
    file(TO_NATIVE_PATH "${p}" np)
    list(APPEND native_old "${np}")
endforeach()
execute_process(COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${ps1}" ${native_old}
                RESULT_VARIABLE ps_rc ERROR_VARIABLE ps_err)
file(REMOVE "${ps1}")
if(NOT ps_rc EQUAL 0)
    message(FATAL_ERROR "could not set fixture times: ${ps_err}")
endif()

file(TO_NATIVE_PATH "${FIXTURE}" native_fixture)
file(TO_NATIVE_PATH "${FIXTURE}/temp" native_temp)
file(TO_NATIVE_PATH "${FIXTURE}/home" native_home)
file(TO_NATIVE_PATH "${FIXTURE}/local" native_local)
file(TO_NATIVE_PATH "${FIXTURE}/roaming" native_roaming)
file(TO_NATIVE_PATH "${FIXTURE}/mydocs" native_docs)
# 증가 기록: 기록 폴더를 픽스처로 두고 npm 캐시의 예전 기록(1바이트, 2022년)을 미리 넣는다.
file(TO_NATIVE_PATH "${FIXTURE}/hist" native_hist)
file(WRITE "${FIXTURE}/hist/devenv-history.tsv"
     "# bytesieve devenv history v1\n133000000000000000\tnpm\t1\t${native_local}\\npm-cache\n")
execute_process(COMMAND ${CMAKE_COMMAND} -E env
                        "BYTESIEVE_HISTORY_DIR=${native_hist}"
                        --unset=npm_config_cache --unset=NPM_CONFIG_USERCONFIG
                        --unset=PIP_CACHE_DIR --unset=PIP_CONFIG_FILE --unset=GRADLE_USER_HOME
                        --unset=CARGO_HOME --unset=NUGET_PACKAGES
                        --unset=npm_config_store_dir --unset=YARN_CACHE_FOLDER --unset=YARN_GLOBAL_FOLDER
                        --unset=GOMODCACHE --unset=GOPATH --unset=GOCACHE --unset=GOENV
                        --unset=CONDA_PKGS_DIRS "BYTESIEVE_WSL_KEY=Software\\ByteSieveTest\\no-wsl"
                        "NUGET_HTTP_CACHE_PATH=${native_docs}"
                        "TEMP=${native_temp}" "TMP=${native_temp}"
                        "USERPROFILE=${native_home}" "LOCALAPPDATA=${native_local}"
                        "APPDATA=${native_roaming}" "BYTESIEVE_QUARANTINE_ROOT=${native_fixture}"
                        "${EXE}" --self-test-cleaner "${native_fixture}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 110)

if(NOT rc EQUAL 0)
    message(SEND_ERROR "bytesieve-gui --self-test-cleaner exited with ${rc}\n${out}\n${err}")
endif()
string(REGEX MATCHALL "result: [^\r\n]+" result_lines "${out}")
message(STATUS "completion banner: ${result_lines}")
# 완료 결과 영역: 시스템 정리·개발 캐시 정리 모두 픽스처의 대상을 다 옮겼으므로 "완료".
if(NOT result_lines STREQUAL "result: success;result: success")
    message(SEND_ERROR "expected success on both cleaner pages, got '${result_lines}'")
endif()
# 모두 픽스처의 격리 폴더로 가야 한다(실제 휴지통으로 간 항목이 있으면 안 됨).
if(out MATCHES "recycled: ")
    message(SEND_ERROR "items went to the real recycle bin:\n${out}")
endif()
string(REGEX MATCHALL "quarantined: [^\r\n]+" moved "${out}")
list(LENGTH moved moved_count)
if(moved_count LESS 10)
    message(SEND_ERROR "expected at least 10 quarantined items, got ${moved_count}\n${out}")
endif()
# 캐시 구조가 아닌 폴더를 가리키는 환경 변수: 이유(unrecognized)와 위치 근거가 보이고 정리하지 않는다.
if(NOT out MATCHES "dev: nuget_http = unrecognized \\(env NUGET_HTTP_CACHE_PATH\\)")
    message(SEND_ERROR "unrecognized NUGET_HTTP_CACHE_PATH row missing:\n${out}")
endif()
if(NOT out MATCHES "dev: npm = found \\(default\\)")
    message(SEND_ERROR "npm cache row missing:\n${out}")
endif()
# 실제 확보: Gradle 캐시는 store.jar 와 나누는 linked.jar(20000바이트)를 공유로 빼야 한다.
# MFT 에 상주하는 작은 파일(100바이트 npm 캐시 등)은 지워도 공간이 늘지 않아 실제 확보 0 이다.
string(REGEX MATCH "reclaim: gradle = ([0-9]+)/([0-9]+) shared ([0-9]+)" gradle_line "${out}")
if(NOT gradle_line)
    message(SEND_ERROR "Gradle reclaim line missing:\n${out}")
else()
    set(g_reclaim "${CMAKE_MATCH_1}")
    set(g_bytes "${CMAKE_MATCH_2}")
    set(g_shared "${CMAKE_MATCH_3}")
    if(NOT g_bytes EQUAL 30000 OR g_shared LESS 20000 OR NOT g_reclaim LESS 20000)
        message(SEND_ERROR "unexpected Gradle reclaim: ${gradle_line}")
    endif()
endif()
# Gradle 래퍼 배포판은 주의 캐시로 표시되어야 한다.
if(NOT out MATCHES "caution: gradle_wrapper")
    message(SEND_ERROR "Gradle wrapper was not marked as caution:\n${out}")
endif()
foreach(gone "temp/old.log" "temp/olddir" "local/Microsoft/Windows/WER/ReportArchive/r1"
             "local/npm-cache" "home/.gradle/wrapper/dists" "home/.gradle/caches"
             "home/.cargo/registry" "home/.cargo/git" "home/.nuget/packages" "local/pip/Cache")
    if(EXISTS "${FIXTURE}/${gone}")
        message(SEND_ERROR "${gone} should have been cleaned")
    endif()
endforeach()
foreach(kept "temp/fresh.txt" "local/Google/Chrome/User Data/Default/Preferences"
             "local/Microsoft/Windows/WER/ReportArchive" "home/.gradle/gradle.properties"
             "home/.cargo/bin/cargo.exe" "mydocs/report.docx" "outside/store.jar")
    if(NOT EXISTS "${FIXTURE}/${kept}")
        message(SEND_ERROR "${kept} should have been kept")
    endif()
endforeach()
# 증가 기록: npm 은 100바이트 - 예전 1바이트 = +99, 처음 보는 캐시는 "첫 기록". 기록 파일에 지금 값이 더해진다.
if(NOT out MATCHES "growth: npm = 99")
    message(SEND_ERROR "npm growth against the seeded record missing:\n${out}")
endif()
if(NOT out MATCHES "growth: pip = first")
    message(SEND_ERROR "first record for pip missing:\n${out}")
endif()
file(STRINGS "${FIXTURE}/hist/devenv-history.tsv" hist_lines)
list(LENGTH hist_lines hist_count)
if(hist_count LESS 6)
    message(SEND_ERROR "devenv history was not updated (${hist_count} lines)")
endif()
if(NOT IS_DIRECTORY "${FIXTURE}/.bytesieve-trash")
    message(SEND_ERROR "quarantine folder was not created in the fixture")
endif()
