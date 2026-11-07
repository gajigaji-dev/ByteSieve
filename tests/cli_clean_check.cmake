# disk-scanner clean 검사(GUI 정리 탭과 같은 탐지·계획·실행을 쓰는 CLI).
#
#   cmake -DEXE=<disk-scanner> -DFIXTURE=<빈 폴더로 만들 경로> -DMODE=project|system -P cli_clean_check.cmake
#
# 옮기기는 BYTESIEVE_QUARANTINE_ROOT 를 픽스처로 두어 실제 휴지통이 아니라 픽스처의 격리 폴더
# (.bytesieve-trash)로 가게 한다. system 은 TEMP·TMP·LOCALAPPDATA·USERPROFILE·APPDATA 도 픽스처로 돌리고
# 개발 도구 환경 변수를 지워 실제 사용자 파일을 보지 않게 한다(실제 휴지통은 조회만 한다).
#
# project:
#   미리보기는 아무것도 옮기지 않음(텍스트·JSON Lines) → 대화형이 아닌 입력에서 --yes 없이 --apply 는 거부(2)
#   → --category build --apply --yes 는 dist 만 옮김 → --jsonl --apply --yes 로 나머지(node_modules) 옮김
#   → 없는 폴더는 2. 소스 파일·package.json 은 남는다.
# system:
#   미리보기에서 공용 개발 캐시는 MANUAL 이라 고르지 않음, 휴지통은 permanent 이며 고르지 않음
#   → --include recycle_bin 은 인자 오류(2) → --include npm --exclude chrome --apply --yes 는 오래된 임시 파일과
#   npm 캐시만 옮김(최근 임시 파일·Chrome 캐시·고르지 않은 Gradle 래퍼는 남음).

foreach(var EXE FIXTURE MODE)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "${var} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${FIXTURE}")
file(MAKE_DIRECTORY "${FIXTURE}")
file(TO_NATIVE_PATH "${FIXTURE}" native_fixture)
string(REPEAT "x" 100 hundred)
string(REPEAT "${hundred}" 50 fivek)
file(WRITE "${FIXTURE}/answer.txt" "y\n")

# 도우미는 모두 function 이다. macro 는 인자를 다시 해석해서 정규식 이스케이프(\\( \\[)가 풀리고
# 경로의 역슬래시(C:\HP...)가 잘못된 이스케이프가 된다.

# run(<out_var> <rc_var> args...) : 픽스처 환경으로 실행한다(표준 입력은 "y" 파일 = 대화형 아님).
function(run out_var rc_var)
    execute_process(COMMAND ${CMAKE_COMMAND} -E env ${env_args} "${EXE}" ${ARGN}
                    INPUT_FILE "${FIXTURE}/answer.txt"
                    RESULT_VARIABLE rc OUTPUT_VARIABLE stdout_text ERROR_VARIABLE stderr_text
                    TIMEOUT 110)
    set(${out_var} "${stdout_text}\n${stderr_text}" PARENT_SCOPE)
    set(${rc_var} "${rc}" PARENT_SCOPE)
endfunction()

# 매개변수 이름은 호출자의 변수(rc·out)를 가리지 않게 _ 로 시작한다.
function(expect_rc _rc_var _want _what)
    if(NOT "${${_rc_var}}" STREQUAL "${_want}")
        message(SEND_ERROR "${_what}: expected exit ${_want}, got ${${_rc_var}}\n${out}")
    endif()
endfunction()

function(expect_match _text_var _pattern _what)
    if(NOT "${${_text_var}}" MATCHES "${_pattern}")
        message(SEND_ERROR "${_what}: output does not match '${_pattern}'\n${${_text_var}}")
    endif()
endfunction()

function(expect_exists rel yes)
    if(${yes} AND NOT EXISTS "${FIXTURE}/${rel}")
        message(SEND_ERROR "${rel} should exist")
    elseif(NOT ${yes} AND EXISTS "${FIXTURE}/${rel}")
        message(SEND_ERROR "${rel} should have been moved")
    endif()
endfunction()

if(MODE STREQUAL "project")
    file(WRITE "${FIXTURE}/proj/web/package.json" "{}")
    file(WRITE "${FIXTURE}/proj/web/src/main.js" "keep")
    file(WRITE "${FIXTURE}/proj/web/node_modules/dep/index.js" "${fivek}")
    file(WRITE "${FIXTURE}/proj/web/dist/app.js" "${fivek}")
    file(WRITE "${FIXTURE}/proj/loose/dist/x.js" "${hundred}")
    file(TO_NATIVE_PATH "${FIXTURE}/proj" native_proj)
    set(env_args "BYTESIEVE_QUARANTINE_ROOT=${native_fixture}")

    # 1. 미리보기
    run(out rc clean project "${native_proj}")
    expect_rc(rc 0 "preview")
    expect_match(out "Project cleanup plan" "preview")
    expect_match(out "\\[x\\] +[^\n]*node_modules" "preview selects node_modules")
    expect_match(out "guessed" "preview marks guessed dist")
    expect_match(out "Preview only - nothing was moved" "preview")
    expect_exists("proj/web/node_modules" TRUE)
    expect_exists("proj/web/dist" TRUE)

    # 2. JSON Lines 미리보기
    run(out rc clean project "${native_proj}" --jsonl)
    expect_rc(rc 0 "jsonl preview")
    expect_match(out "\"event\":\"clean_plan\",\"source\":\"project\"" "jsonl preview")
    expect_match(out "\"id\":\"node_modules\"[^\n]*\"selected\":true" "jsonl preview")
    expect_match(out "\"event\":\"clean_summary\",\"selected\":3" "jsonl preview")
    expect_match(out "\"event\":\"complete\",\"applied\":false" "jsonl preview")

    # 3. 확인할 수 없는데 --yes 가 없으면 거부
    run(out rc clean project "${native_proj}" --apply)
    expect_rc(rc 2 "apply without --yes")
    expect_match(out "add --yes" "apply without --yes")
    expect_exists("proj/web/node_modules" TRUE)

    # 4. 유형 거르기 + 실행: dist 둘만 옮긴다
    run(out rc clean project "${native_proj}" --category build --apply --yes)
    expect_rc(rc 0 "apply build")
    expect_match(out "Moved 2 item\\(s\\)" "apply build")
    expect_exists("proj/web/dist" FALSE)
    expect_exists("proj/loose/dist" FALSE)
    expect_exists("proj/web/node_modules" TRUE)
    expect_exists(".bytesieve-trash" TRUE)

    # 5. 나머지를 JSON Lines 로 실행
    run(out rc clean project "${native_proj}" --jsonl --apply --yes)
    expect_rc(rc 0 "jsonl apply")
    expect_match(out "\"event\":\"clean_result\"[^\n]*\"id\":\"node_modules\"[^\n]*\"status\":\"quarantined\"" "jsonl apply")
    expect_match(out "\"event\":\"complete\",\"applied\":true,\"moved\":1" "jsonl apply")
    expect_exists("proj/web/node_modules" FALSE)
    expect_exists("proj/web/src/main.js" TRUE)
    expect_exists("proj/web/package.json" TRUE)

    # 6. 없는 폴더
    run(out rc clean project "${native_fixture}\\no-such-dir")
    expect_rc(rc 2 "missing folder")

elseif(MODE STREQUAL "system")
    file(WRITE "${FIXTURE}/temp/old.log" "${hundred}")
    file(WRITE "${FIXTURE}/temp/fresh.txt" "new")
    file(WRITE "${FIXTURE}/local/npm-cache/_cacache/index-v5/a" "${fivek}")
    file(WRITE "${FIXTURE}/local/Google/Chrome/User Data/Default/Preferences" "{}")
    file(WRITE "${FIXTURE}/local/Google/Chrome/User Data/Default/Cache/Cache_Data/data_0" "${fivek}")
    file(WRITE "${FIXTURE}/home/.gradle/wrapper/dists/gradle-8.5-bin/abc/g.zip" "${fivek}")
    file(MAKE_DIRECTORY "${FIXTURE}/roaming")
    # 오래된 임시 파일: 3일 전으로
    set(ps1 "${FIXTURE}-settime.ps1")
    file(WRITE "${ps1}" "$t = (Get-Date).AddDays(-3)\nforeach ($p in $args) {\n  $i = Get-Item -LiteralPath $p -Force\n  $i.CreationTime = $t\n  $i.LastWriteTime = $t\n}\n")
    file(TO_NATIVE_PATH "${FIXTURE}/temp/old.log" native_old)
    execute_process(COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${ps1}" "${native_old}"
                    RESULT_VARIABLE ps_rc ERROR_VARIABLE ps_err)
    file(REMOVE "${ps1}")
    if(NOT ps_rc EQUAL 0)
        message(FATAL_ERROR "could not set fixture times: ${ps_err}")
    endif()
    file(TO_NATIVE_PATH "${FIXTURE}/temp" native_temp)
    file(TO_NATIVE_PATH "${FIXTURE}/home" native_home)
    file(TO_NATIVE_PATH "${FIXTURE}/local" native_local)
    file(TO_NATIVE_PATH "${FIXTURE}/roaming" native_roaming)
    set(env_args --unset=npm_config_cache --unset=NPM_CONFIG_USERCONFIG --unset=PIP_CACHE_DIR
                 --unset=PIP_CONFIG_FILE --unset=GRADLE_USER_HOME --unset=CARGO_HOME
                 --unset=NUGET_PACKAGES --unset=NUGET_HTTP_CACHE_PATH
                 "TEMP=${native_temp}" "TMP=${native_temp}" "USERPROFILE=${native_home}"
                 "LOCALAPPDATA=${native_local}" "APPDATA=${native_roaming}"
                 "BYTESIEVE_QUARANTINE_ROOT=${native_fixture}")

    # 1. 미리보기: 개발 캐시는 MANUAL 이라 기본으로 고르지 않는다
    run(out rc clean system)
    expect_rc(rc 0 "system preview")
    expect_match(out "System cleanup plan" "system preview")
    expect_match(out "\\[ \\] +[^\n]*npm[^\n]*manual" "npm not selected by default")
    expect_match(out "\\[x\\] +[^\n]*user_temp" "temp selected by default")
    expect_match(out "not selected by default; use --include" "dev cache hint")
    run(out rc clean system --jsonl)
    expect_rc(rc 0 "system jsonl preview")
    expect_match(out "\"id\":\"npm\"[^\n]*\"selected\":false[^\n]*\"manual\":true" "jsonl npm")
    expect_match(out "\"id\":\"recycle_bin\"[^\n]*\"selected\":false[^\n]*\"permanent\":true" "jsonl recycle bin")
    expect_match(out "\"id\":\"user_temp\"[^\n]*\"selected\":true" "jsonl temp")
    # 모든 위치가 픽스처 안이어야 한다(실제 사용자 폴더를 보지 않음)
    string(REGEX MATCHALL "\"location\":\"[^\"]*\"" locations "${out}")
    string(REPLACE "\\" "\\\\" fixture_json "${native_fixture}")
    foreach(loc IN LISTS locations)
        if(NOT loc MATCHES "모든 드라이브" AND NOT loc MATCHES "^\"location\":null")
            string(FIND "${loc}" "${fixture_json}" pos)
            if(pos EQUAL -1)
                message(SEND_ERROR "location outside the fixture: ${loc}")
            endif()
        endif()
    endforeach()

    # 2. 휴지통 비우기는 CLI 에서 고를 수 없다
    run(out rc clean system --include recycle_bin --apply --yes)
    expect_rc(rc 2 "recycle bin refused")
    expect_match(out "only available in the GUI" "recycle bin refused")

    # 3. 실행: 오래된 임시 파일 + npm 캐시만
    run(out rc clean system --include npm --exclude chrome --apply --yes)
    expect_rc(rc 0 "system apply")
    expect_match(out "Moved 2 item\\(s\\)" "system apply")
    expect_exists("temp/old.log" FALSE)
    expect_exists("local/npm-cache" FALSE)
    expect_exists("temp/fresh.txt" TRUE)
    expect_exists("local/Google/Chrome/User Data/Default/Cache" TRUE)
    expect_exists("home/.gradle/wrapper/dists" TRUE)
    expect_exists(".bytesieve-trash" TRUE)
else()
    message(FATAL_ERROR "MODE must be project or system")
endif()
