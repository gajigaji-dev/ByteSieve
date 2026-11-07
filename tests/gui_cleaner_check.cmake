# GUI Cleaner 탭 자체 검사(bytesieve-gui --self-test-cleaner).
#
#   cmake -DEXE=<bytesieve-gui> -DFIXTURE=<빈 폴더로 만들 경로> -P gui_cleaner_check.cmake
#
# TEMP·TMP·LOCALAPPDATA·USERPROFILE·APPDATA 를 픽스처로 돌리고 개발 도구 환경 변수를 지워 실제 사용자
# 파일을 보지 않게 한다. BYTESIEVE_QUARANTINE_ROOT 를 픽스처로 두어 정리한 항목이 실제 휴지통이 아니라
# 픽스처의 격리 폴더(.bytesieve-trash)로 가게 한다. GUI 는 분석 → 화면 확인(위치가 모두 픽스처 안인지,
# [모두 선택]이 휴지통 비우기·개발 도구 캐시를 고르지 않는지, 주의 캐시 표시 포함) → [모두 선택] 뒤
# 개발 도구 캐시를 직접 체크해 정리 가능한 대상을 모두 정리 → 결과를 확인한다.
# 실제 휴지통은 조회만 하고 비우지 않는다.
#
# FIXTURE\
#   temp\old.log, temp\olddir\x.bin                (3일 전)  → 격리 폴더
#   temp\fresh.txt                                 (지금)    → 남아야 함(최근 24시간)
#   local\Microsoft\Windows\WER\ReportArchive\r1\  (3일 전)  → 격리 폴더
#   local\Google\Chrome\User Data\Default\Cache\…            → 격리 폴더(Chrome 이 실행 중이면 남음)
#   local\Google\Chrome\User Data\Default\Preferences         (남아야 함)
#   local\npm-cache\_cacache\…                                → 격리 폴더
#   home\.gradle\wrapper\dists\gradle-8.5-bin\…  (주의 캐시)  → 격리 폴더

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
execute_process(COMMAND ${CMAKE_COMMAND} -E env
                        --unset=npm_config_cache --unset=NPM_CONFIG_USERCONFIG
                        --unset=PIP_CACHE_DIR --unset=PIP_CONFIG_FILE --unset=GRADLE_USER_HOME
                        --unset=CARGO_HOME --unset=NUGET_PACKAGES --unset=NUGET_HTTP_CACHE_PATH
                        "TEMP=${native_temp}" "TMP=${native_temp}"
                        "USERPROFILE=${native_home}" "LOCALAPPDATA=${native_local}"
                        "APPDATA=${native_roaming}" "BYTESIEVE_QUARANTINE_ROOT=${native_fixture}"
                        "${EXE}" --self-test-cleaner "${native_fixture}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 110)

if(NOT rc EQUAL 0)
    message(SEND_ERROR "bytesieve-gui --self-test-cleaner exited with ${rc}\n${out}\n${err}")
endif()
# 모두 픽스처의 격리 폴더로 가야 한다(실제 휴지통으로 간 항목이 있으면 안 됨).
if(out MATCHES "recycled: ")
    message(SEND_ERROR "items went to the real recycle bin:\n${out}")
endif()
string(REGEX MATCHALL "quarantined: [^\r\n]+" moved "${out}")
list(LENGTH moved moved_count)
if(moved_count LESS 5)
    message(SEND_ERROR "expected at least 5 quarantined items, got ${moved_count}\n${out}")
endif()
# Gradle 래퍼 배포판은 주의 캐시로 표시되어야 한다.
if(NOT out MATCHES "caution: [^\r\n]*Gradle")
    message(SEND_ERROR "Gradle wrapper was not marked as caution:\n${out}")
endif()
foreach(gone "temp/old.log" "temp/olddir" "local/Microsoft/Windows/WER/ReportArchive/r1"
             "local/npm-cache" "home/.gradle/wrapper/dists")
    if(EXISTS "${FIXTURE}/${gone}")
        message(SEND_ERROR "${gone} should have been cleaned")
    endif()
endforeach()
foreach(kept "temp/fresh.txt" "local/Google/Chrome/User Data/Default/Preferences"
             "local/Microsoft/Windows/WER/ReportArchive")
    if(NOT EXISTS "${FIXTURE}/${kept}")
        message(SEND_ERROR "${kept} should have been kept")
    endif()
endforeach()
if(NOT IS_DIRECTORY "${FIXTURE}/.bytesieve-trash")
    message(SEND_ERROR "quarantine folder was not created in the fixture")
endif()
