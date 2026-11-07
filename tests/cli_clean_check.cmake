# disk-scanner find·clean 검사(GUI 정리 탭과 같은 탐지·계획·실행을 쓰는 CLI).
#
#   cmake -DEXE=<disk-scanner> -DFIXTURE=<빈 폴더로 만들 경로> -DMODE=find|project|system|devenv -P cli_clean_check.cmake
#
# 옮기기는 BYTESIEVE_QUARANTINE_ROOT 를 픽스처로 두어 실제 휴지통이 아니라 픽스처의 격리 폴더
# (.bytesieve-trash)로 가게 한다. system 은 TEMP·TMP·LOCALAPPDATA·USERPROFILE·APPDATA 도 픽스처로 돌리고
# 개발 도구 환경 변수를 지워 실제 사용자 파일을 보지 않게 한다(실제 휴지통은 조회만 한다).
#
# find:
#   후보를 크기순으로 보여 주기만 함(텍스트·--json·--jsonl, 아무것도 옮기지 않음) → --category 거르기
#   → 실행 옵션은 인자 오류(2) → 없는 폴더는 2 이고 --json 이면 exit_code 2 인 실패 문서.
# project:
#   dry-run(기본·--dry-run)은 아무것도 옮기지 않음(텍스트·JSON Lines·--json 문서) → 대화형이 아닌 입력에서
#   --yes 없이 --execute 는 거부(2) → --category build --execute --yes 는 dist 만 옮김
#   → --category cache --jsonl --apply(예전 이름) --yes 는 __pycache__ 를 옮김
#   → --json --execute --yes 로 나머지(node_modules) 옮김 → 없는 폴더는 2. 소스 파일·package.json 은 남는다.
# system:
#   미리보기에서 공용 개발 캐시는 MANUAL 이라 고르지 않음, 휴지통은 permanent 이며 고르지 않음
#   → --include recycle_bin 은 인자 오류(2) → --include npm --exclude chrome --execute --yes 는 오래된 임시 파일과
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
    set(${out_var}_stdout "${stdout_text}" PARENT_SCOPE)
    set(${rc_var} "${rc}" PARENT_SCOPE)
endfunction()

# expect_json_type(<json> <type> <what> path...) : string(JSON TYPE) 가 type(NULL·OBJECT 등)이어야 한다.
function(expect_json_type _json _want _what)
    string(JSON _type ERROR_VARIABLE _jerr TYPE "${_json}" ${ARGN})
    if(_jerr OR NOT "${_type}" STREQUAL "${_want}")
        message(SEND_ERROR "${_what}: JSON ${ARGN} type is '${_type}' (${_jerr}), expected ${_want}")
    endif()
endfunction()

# expect_json(<json> <want> <what> path...) : string(JSON GET) 값이 want 와 같아야 한다(해석 실패도 오류).
function(expect_json _json _want _what)
    string(JSON _value ERROR_VARIABLE _jerr GET "${_json}" ${ARGN})
    if(_jerr)
        message(SEND_ERROR "${_what}: cannot read JSON ${ARGN}: ${_jerr}\n${_json}")
    elseif(NOT "${_value}" STREQUAL "${_want}")
        message(SEND_ERROR "${_what}: JSON ${ARGN} is '${_value}', expected '${_want}'")
    endif()
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

if(MODE STREQUAL "find")
    # 프로젝트마다 자기 Git 저장소를 만든다(빌드 폴더가 있는 이 저장소의 상태에 좌우되지 않게).
    #   web   : 2020년 커밋·파일, package-lock.json → 휴면, node_modules 는 locked, 원격 없음
    #   api   : 지금 커밋 + 추적 안 하는 wip.js → 미커밋 변경(dirty), node_modules 는 unlocked
    #   loose : dist 만(표시 파일 없음 → guessed), 커밋함
    find_program(GIT_EXE git)
    file(WRITE "${FIXTURE}/proj/web/package.json" "{}")
    file(WRITE "${FIXTURE}/proj/web/package-lock.json" "{}")
    file(WRITE "${FIXTURE}/proj/web/node_modules/dep/index.js" "${fivek}${fivek}")
    file(WRITE "${FIXTURE}/proj/web/dist/app.js" "${fivek}")
    file(WRITE "${FIXTURE}/proj/web/.gitignore" "node_modules/\ndist/\n")
    file(WRITE "${FIXTURE}/proj/api/package.json" "{}")
    file(WRITE "${FIXTURE}/proj/api/node_modules/x/a.js" "${hundred}")
    file(WRITE "${FIXTURE}/proj/api/.gitignore" "node_modules/\n")
    file(WRITE "${FIXTURE}/proj/loose/README" "x")
    file(WRITE "${FIXTURE}/proj/loose/dist/x.js" "${hundred}")
    file(WRITE "${FIXTURE}/proj/loose/.gitignore" "dist/\n")
    file(TO_NATIVE_PATH "${FIXTURE}/proj" native_proj)
    set(env_args "BYTESIEVE_QUARANTINE_ROOT=${native_fixture}")
    set(have_git FALSE)
    if(GIT_EXE)
        set(have_git TRUE)
        set(g ${GIT_EXE} -c user.name=t -c user.email=t@example.com -c core.autocrlf=false)
        foreach(repo web api loose)
            execute_process(COMMAND ${g} -C "${FIXTURE}/proj/${repo}" init -q -b main RESULT_VARIABLE r1)
            execute_process(COMMAND ${g} -C "${FIXTURE}/proj/${repo}" add . RESULT_VARIABLE r2)
            if(repo STREQUAL "web")
                set(ENV{GIT_AUTHOR_DATE} "2020-01-15T12:00:00Z")
                set(ENV{GIT_COMMITTER_DATE} "2020-01-15T12:00:00Z")
            endif()
            execute_process(COMMAND ${g} -C "${FIXTURE}/proj/${repo}" commit -q -m init RESULT_VARIABLE r3)
            unset(ENV{GIT_AUTHOR_DATE})
            unset(ENV{GIT_COMMITTER_DATE})
            if(NOT r1 EQUAL 0 OR NOT r2 EQUAL 0 OR NOT r3 EQUAL 0)
                message(FATAL_ERROR "could not create git fixture ${repo}: ${r1} ${r2} ${r3}")
            endif()
        endforeach()
        file(WRITE "${FIXTURE}/proj/api/wip.js" "not committed")
        # web 의 파일도 2020년으로(마지막 수정까지 오래돼야 휴면)
        set(ps1 "${FIXTURE}-settime.ps1")
        file(WRITE "${ps1}" "$t = Get-Date -Year 2020 -Month 1 -Day 15\nGet-ChildItem -LiteralPath $args[0] -Recurse -Force | Where-Object { $_.FullName -notmatch '\\\\.git(\\\\|$)' } | ForEach-Object { $_.LastWriteTime = $t; $_.CreationTime = $t }\n")
        file(TO_NATIVE_PATH "${FIXTURE}/proj/web" native_web)
        execute_process(COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${ps1}" "${native_web}"
                        RESULT_VARIABLE ps_rc ERROR_VARIABLE ps_err)
        file(REMOVE "${ps1}")
        if(NOT ps_rc EQUAL 0)
            message(FATAL_ERROR "could not set fixture times: ${ps_err}")
        endif()
    else()
        message(STATUS "git not found: Git checks skipped")
        list(APPEND env_args "BYTESIEVE_GIT=none")
    endif()

    # 1. 텍스트: 프로젝트 요약, 크기순(web 의 node_modules 가 1번), 아무것도 바꾸지 않음
    run(out rc find "${native_proj}")
    expect_rc(rc 0 "find")
    expect_match(out "Projects: 3 " "find projects")
    expect_match(out "4 found, 4 cleanable" "find summary")
    expect_match(out "\n  1 +[^\n]*node_modules +node +P[0-9] +locked +confirmed" "find order, project, recovery")
    expect_match(out "build +generic +P[0-9] +rebuild +guessed" "find guessed dist")
    expect_match(out "Nothing was changed" "find is read-only")
    if(have_git)
        expect_match(out "DORMANT" "find dormant project")
        expect_match(out "UNCOMMITTED CHANGES: 1" "find dirty project")
        expect_match(out "node_modules +node +P[0-9] +unlocked +confirmed,uncommitted" "find dirty candidate")
    endif()
    expect_exists("proj/web/node_modules" TRUE)
    expect_exists("proj/loose/dist" TRUE)

    # 2. --json 문서: 후보·프로젝트
    run(out rc find "${native_proj}" --json)
    expect_rc(rc 0 "find json")
    expect_json("${out_stdout}" "1" "find json" version)
    expect_json("${out_stdout}" "find" "find json" command)
    expect_json("${out_stdout}" "complete" "find json" status)
    expect_json("${out_stdout}" "4" "find json" summary candidates)
    expect_json("${out_stdout}" "node_modules" "find json" candidates 0 category)
    expect_json("${out_stdout}" "locked" "find json" candidates 0 recovery)
    expect_json("${out_stdout}" "ON" "find json" candidates 0 confirmed)
    expect_json("${out_stdout}" "90" "find json" dormant_days)
    expect_json("${out_stdout}" "0" "find json" exit_code)
    string(JSON n_proj LENGTH "${out_stdout}" projects)
    if(NOT n_proj EQUAL 3)
        message(SEND_ERROR "find json: expected 3 projects, got ${n_proj}")
    endif()
    if(have_git)
        # 프로젝트를 루트 이름으로 찾아 확인한다
        math(EXPR last "${n_proj} - 1")
        foreach(i RANGE ${last})
            string(JSON proot GET "${out_stdout}" projects ${i} root)
            if(proot MATCHES "web$")
                expect_json("${out_stdout}" "ON" "web project" projects ${i} git)
                expect_json("${out_stdout}" "main" "web project" projects ${i} branch)
                expect_json("${out_stdout}" "1579089600" "web project" projects ${i} last_commit)
                expect_json("${out_stdout}" "ON" "web project" projects ${i} dormant)
                expect_json("${out_stdout}" "0" "web project" projects ${i} changes)
                expect_json("${out_stdout}" "OFF" "web project" projects ${i} has_remote)
                expect_json("${out_stdout}" "1" "web project" projects ${i} unpushed)
                expect_json("${out_stdout}" "git" "web project" projects ${i} git_source)
            elseif(proot MATCHES "api$")
                expect_json("${out_stdout}" "1" "api project" projects ${i} changes)
                expect_json("${out_stdout}" "OFF" "api project" projects ${i} dormant)
            endif()
        endforeach()
        # 미커밋 프로젝트의 후보는 dirty
        string(JSON n_cand LENGTH "${out_stdout}" candidates)
        math(EXPR last "${n_cand} - 1")
        set(saw_dirty FALSE)
        foreach(i RANGE ${last})
            string(JSON cpath GET "${out_stdout}" candidates ${i} path)
            if(cpath MATCHES "api.node_modules$")
                expect_json("${out_stdout}" "ON" "api candidate" candidates ${i} dirty)
                expect_json("${out_stdout}" "unlocked" "api candidate" candidates ${i} recovery)
                set(saw_dirty TRUE)
            endif()
        endforeach()
        if(NOT saw_dirty)
            message(SEND_ERROR "find json: api node_modules candidate missing")
        endif()

        # 3. 휴면 기준을 아주 길게 하면 휴면이 없다
        run(out rc find "${native_proj}" --dormant-days 36500)
        expect_rc(rc 0 "find dormant-days")
        expect_match(out "Projects: 3 \\(0 dormant after 36500 days" "find dormant-days")

        # 4. --no-git: .git 을 직접 읽는다(미커밋 변경은 모름)
        run(out rc find "${native_proj}" --no-git --json)
        expect_rc(rc 0 "find no-git")
        expect_json("${out_stdout}" "direct" "find no-git" projects 0 git_source)
        expect_json_type("${out_stdout}" "NULL" "find no-git" projects 0 changes)
        expect_json("${out_stdout}" "OFF" "find no-git" git_available)
    endif()

    # 5. 거르기 + --jsonl(프로젝트 이벤트 포함)
    run(out rc find "${native_proj}" --category build --jsonl)
    expect_rc(rc 0 "find jsonl")
    expect_match(out "\"event\":\"find_start\"" "find jsonl")
    expect_match(out "\"event\":\"project\",\"index\":1" "find jsonl project event")
    expect_match(out "\"event\":\"find_candidate\"[^\n]*\"category\":\"build\"[^\n]*\"recovery\":\"rebuild\"" "find jsonl")
    expect_match(out "\"event\":\"complete\",\"status\":\"complete\",\"candidates\":2" "find jsonl filter")
    if(out MATCHES "\"category\":\"node_modules\"")
        message(SEND_ERROR "find --category build should not list node_modules\n${out}")
    endif()

    # 6. 실행 옵션·정리 선택 옵션은 find 에 없다, --dormant-days 는 숫자
    run(out rc find "${native_proj}" --execute)
    expect_rc(rc 2 "find --execute")
    expect_match(out "never changes" "find --execute")
    run(out rc find "${native_proj}" --include-dirty)
    expect_rc(rc 2 "find --include-dirty")
    run(out rc find "${native_proj}" --dormant-days abc)
    expect_rc(rc 2 "find --dormant-days abc")

    # 7. 없는 폴더: 2, --json 이면 실패 문서
    run(out rc find "${native_fixture}\\no-such-dir" --json)
    expect_rc(rc 2 "find missing")
    expect_json("${out_stdout}" "failed" "find missing json" status)
    expect_json("${out_stdout}" "2" "find missing json" exit_code)

elseif(MODE STREQUAL "project")
    file(WRITE "${FIXTURE}/proj/web/package.json" "{}")
    file(WRITE "${FIXTURE}/proj/web/src/main.js" "keep")
    file(WRITE "${FIXTURE}/proj/web/node_modules/dep/index.js" "${fivek}")
    file(WRITE "${FIXTURE}/proj/web/dist/app.js" "${fivek}")
    file(WRITE "${FIXTURE}/proj/loose/dist/x.js" "${hundred}")
    file(WRITE "${FIXTURE}/proj/py/__pycache__/m.pyc" "${hundred}")
    file(TO_NATIVE_PATH "${FIXTURE}/proj" native_proj)
    # 작업 트리(이 저장소)의 Git 상태에 좌우되지 않게 git.exe 를 쓰지 않는다(Git 검사는 find 모드).
    set(env_args "BYTESIEVE_QUARANTINE_ROOT=${native_fixture}" "BYTESIEVE_GIT=none")

    # 1. dry-run(기본)
    run(out rc clean project "${native_proj}")
    expect_rc(rc 0 "preview")
    expect_match(out "Project cleanup plan" "preview")
    expect_match(out "\\[x\\] +[^\n]*node_modules" "preview selects node_modules")
    expect_match(out "guessed" "preview marks guessed dist")
    expect_match(out "Dry run - nothing was moved" "preview")
    expect_match(out "Expected result: 4 item\\(s\\) in 4 target\\(s\\)" "dry-run expected result")
    expect_exists("proj/web/node_modules" TRUE)
    expect_exists("proj/web/dist" TRUE)
    run(out rc clean project "${native_proj}" --dry-run)
    expect_rc(rc 0 "explicit dry-run")
    expect_match(out "Dry run - nothing was moved" "explicit dry-run")

    # 1-1. --json dry-run 문서: 고른 대상·예상 결과, 실행 결과는 없음
    run(out rc clean project "${native_proj}" --dry-run --json)
    expect_rc(rc 0 "json dry-run")
    expect_json("${out_stdout}" "1" "json dry-run" version)
    expect_json("${out_stdout}" "clean" "json dry-run" command)
    expect_json("${out_stdout}" "project" "json dry-run" source)
    expect_json("${out_stdout}" "dry-run" "json dry-run" mode)
    expect_json("${out_stdout}" "OFF" "json dry-run" executed)
    expect_json("${out_stdout}" "4" "json dry-run" summary selected)
    expect_json("${out_stdout}" "4" "json dry-run" summary entries)
    expect_json_type("${out_stdout}" "NULL" "json dry-run" totals)
    expect_json_type("${out_stdout}" "NULL" "json dry-run" targets 0 result)
    expect_json("${out_stdout}" "0" "json dry-run" exit_code)
    expect_exists("proj/web/node_modules" TRUE)

    # 2. JSON Lines 미리보기
    run(out rc clean project "${native_proj}" --jsonl)
    expect_rc(rc 0 "jsonl preview")
    expect_match(out "\"event\":\"clean_plan\",\"source\":\"project\"" "jsonl preview")
    expect_match(out "\"id\":\"node_modules\"[^\n]*\"selected\":true" "jsonl preview")
    expect_match(out "\"event\":\"clean_summary\",\"selected\":4" "jsonl preview")
    expect_match(out "\"mode\":\"dry-run\"" "jsonl preview mode")
    expect_match(out "\"event\":\"complete\",\"applied\":false" "jsonl preview")

    # 3. 확인할 수 없는데 --yes 가 없으면 거부
    run(out rc clean project "${native_proj}" --execute)
    expect_rc(rc 2 "execute without --yes")
    expect_match(out "add --yes" "execute without --yes")
    run(out rc clean project "${native_proj}" --execute --json)
    expect_rc(rc 2 "json execute without --yes")
    expect_json("${out_stdout}" "OFF" "json execute without --yes" executed)
    expect_json("${out_stdout}" "2" "json execute without --yes" exit_code)
    expect_exists("proj/web/node_modules" TRUE)

    # 4. 유형 거르기 + 실행: dist 둘만 옮긴다
    run(out rc clean project "${native_proj}" --category build --execute --yes)
    expect_rc(rc 0 "apply build")
    expect_match(out "Moved 2 item\\(s\\)" "apply build")
    expect_exists("proj/web/dist" FALSE)
    expect_exists("proj/loose/dist" FALSE)
    expect_exists("proj/web/node_modules" TRUE)
    expect_exists(".bytesieve-trash" TRUE)

    # 5. 예전 이름 --apply 도 실행한다(JSON Lines)
    run(out rc clean project "${native_proj}" --category cache --jsonl --apply --yes)
    expect_rc(rc 0 "jsonl apply")
    expect_match(out "\"event\":\"clean_result\"[^\n]*\"id\":\"cache\"[^\n]*\"status\":\"quarantined\"" "jsonl apply")
    expect_match(out "\"event\":\"complete\",\"applied\":true,\"moved\":1" "jsonl apply")
    expect_exists("proj/py/__pycache__" FALSE)
    expect_exists("proj/web/node_modules" TRUE)

    # 5-1. 나머지를 --json 문서로 실행
    run(out rc clean project "${native_proj}" --json --execute --yes)
    expect_rc(rc 0 "json execute")
    expect_json("${out_stdout}" "execute" "json execute" mode)
    expect_json("${out_stdout}" "ON" "json execute" executed)
    expect_json("${out_stdout}" "1" "json execute" totals moved)
    expect_json("${out_stdout}" "0" "json execute" totals failed)
    expect_json("${out_stdout}" "0" "json execute" exit_code)
    string(JSON n_targets LENGTH "${out_stdout}" targets)
    set(found_result FALSE)
    math(EXPR last "${n_targets} - 1")
    foreach(i RANGE ${last})
        string(JSON tid GET "${out_stdout}" targets ${i} id)
        string(JSON ttype TYPE "${out_stdout}" targets ${i} result)
        if(tid STREQUAL "node_modules" AND ttype STREQUAL "OBJECT")
            expect_json("${out_stdout}" "quarantined" "json execute" targets ${i} result entries 0 status)
            set(found_result TRUE)
        endif()
    endforeach()
    if(NOT found_result)
        message(SEND_ERROR "json execute: node_modules result missing\n${out_stdout}")
    endif()
    expect_exists("proj/web/node_modules" FALSE)
    expect_exists("proj/web/src/main.js" TRUE)
    expect_exists("proj/web/package.json" TRUE)

    # 6. 없는 폴더
    run(out rc clean project "${native_fixture}\\no-such-dir")
    expect_rc(rc 2 "missing folder")
    run(out rc clean project "${native_fixture}\\no-such-dir" --json)
    expect_rc(rc 2 "missing folder json")
    expect_json("${out_stdout}" "failed" "missing folder json" status)
    expect_json("${out_stdout}" "2" "missing folder json" exit_code)

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
                 --unset=npm_config_store_dir --unset=YARN_CACHE_FOLDER --unset=YARN_GLOBAL_FOLDER
                 --unset=GOMODCACHE --unset=GOPATH --unset=GOCACHE --unset=GOENV
                 --unset=CONDA_PKGS_DIRS "BYTESIEVE_WSL_KEY=Software\\ByteSieveTest\\no-wsl"
                 "TEMP=${native_temp}" "TMP=${native_temp}" "USERPROFILE=${native_home}"
                 "LOCALAPPDATA=${native_local}" "APPDATA=${native_roaming}"
                 "BYTESIEVE_QUARANTINE_ROOT=${native_fixture}" "BYTESIEVE_GIT=none")

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
    run(out rc clean system --include recycle_bin --execute --yes)
    expect_rc(rc 2 "recycle bin refused")
    expect_match(out "only available in the GUI" "recycle bin refused")

    # 3. 실행: 오래된 임시 파일 + npm 캐시만
    run(out rc clean system --include npm --exclude chrome --execute --yes)
    expect_rc(rc 0 "system apply")
    expect_match(out "Moved 2 item\\(s\\)" "system apply")
    expect_exists("temp/old.log" FALSE)
    expect_exists("local/npm-cache" FALSE)
    expect_exists("temp/fresh.txt" TRUE)
    expect_exists("local/Google/Chrome/User Data/Default/Cache" TRUE)
    expect_exists("home/.gradle/wrapper/dists" TRUE)
    expect_exists(".bytesieve-trash" TRUE)
elseif(MODE STREQUAL "devenv")
    # 개발 환경 분석(읽기 전용)과 증가 기록. 사용자 폴더·기록 폴더를 픽스처로, WSL 레지스트리는 빈 키로.
    file(WRITE "${FIXTURE}/local/npm-cache/_cacache/index-v5/a" "${fivek}")
    file(WRITE "${FIXTURE}/local/Yarn/Cache/v6/npm-x/b" "${hundred}")
    file(WRITE "${FIXTURE}/home/go/pkg/mod/cache/download/c" "${hundred}")
    file(WRITE "${FIXTURE}/local/Docker/wsl/disk/docker_data.vhdx" "${fivek}")
    file(MAKE_DIRECTORY "${FIXTURE}/roaming" "${FIXTURE}/temp")
    foreach(d temp home local roaming hist)
        file(TO_NATIVE_PATH "${FIXTURE}/${d}" native_${d})
    endforeach()
    set(env_args --unset=npm_config_cache --unset=NPM_CONFIG_USERCONFIG --unset=PIP_CACHE_DIR
                 --unset=PIP_CONFIG_FILE --unset=GRADLE_USER_HOME --unset=CARGO_HOME
                 --unset=NUGET_PACKAGES --unset=NUGET_HTTP_CACHE_PATH
                 --unset=npm_config_store_dir --unset=YARN_CACHE_FOLDER --unset=YARN_GLOBAL_FOLDER
                 --unset=GOMODCACHE --unset=GOPATH --unset=GOCACHE --unset=GOENV
                 --unset=CONDA_PKGS_DIRS "BYTESIEVE_WSL_KEY=Software\\ByteSieveTest\\no-wsl"
                 "TEMP=${native_temp}" "TMP=${native_temp}" "USERPROFILE=${native_home}"
                 "LOCALAPPDATA=${native_local}" "APPDATA=${native_roaming}"
                 "BYTESIEVE_HISTORY_DIR=${native_hist}")

    # 1. 첫 실행: 모두 첫 기록, Docker 가상 디스크, 기록 저장
    run(out rc devenv --json)
    expect_rc(rc 0 "devenv first")
    expect_json("${out_stdout}" "devenv" "devenv first" command)
    expect_json("${out_stdout}" "ON" "devenv first" history recorded)
    expect_json("${out_stdout}" "0" "devenv first" history previous_records)
    expect_json("${out_stdout}" "docker" "devenv first" vdisks 0 kind)
    expect_json("${out_stdout}" "5000" "devenv first" vdisks 0 size)
    expect_json_type("${out_stdout}" "NULL" "devenv first" vdisks 0 change)
    string(JSON n_caches LENGTH "${out_stdout}" caches)
    math(EXPR last "${n_caches} - 1")
    set(seen "")
    foreach(i RANGE ${last})
        string(JSON cid GET "${out_stdout}" caches ${i} id)
        string(JSON cstate GET "${out_stdout}" caches ${i} state)
        if(cstate STREQUAL "ready")
            list(APPEND seen ${cid})
            expect_json_type("${out_stdout}" "NULL" "devenv first ${cid}" caches ${i} change)
        endif()
    endforeach()
    foreach(want npm yarn go_mod)
        if(NOT want IN_LIST seen)
            message(SEND_ERROR "devenv first: ${want} cache not found (${seen})\n${out_stdout}")
        endif()
    endforeach()
    expect_exists("hist/devenv-history.tsv" TRUE)

    # 2. npm 캐시를 키운 뒤: 직전 기록보다 늘어난 만큼
    file(WRITE "${FIXTURE}/local/npm-cache/_cacache/index-v5/b" "${fivek}${fivek}")
    run(out rc devenv --json)
    expect_rc(rc 0 "devenv second")
    string(JSON n_caches LENGTH "${out_stdout}" caches)
    math(EXPR last "${n_caches} - 1")
    set(npm_change "")
    foreach(i RANGE ${last})
        string(JSON cid GET "${out_stdout}" caches ${i} id)
        if(cid STREQUAL "npm")
            string(JSON npm_change GET "${out_stdout}" caches ${i} change bytes)
        elseif(cid STREQUAL "yarn")
            expect_json("${out_stdout}" "0" "devenv second yarn" caches ${i} change bytes)
        endif()
    endforeach()
    if(NOT npm_change STREQUAL "10000")
        message(SEND_ERROR "devenv second: npm change is '${npm_change}', expected 10000\n${out_stdout}")
    endif()
    expect_json("${out_stdout}" "0" "devenv second" vdisks 0 change bytes)

    # 3. --no-record: 비교만 하고 기록 파일은 그대로
    file(STRINGS "${FIXTURE}/hist/devenv-history.tsv" before)
    list(LENGTH before before_n)
    run(out rc devenv --no-record)
    expect_rc(rc 0 "devenv no-record")
    expect_match(out "History: not recorded \\(--no-record\\)" "devenv no-record")
    expect_match(out "How to shrink" "devenv shrink hint")
    expect_match(out "npm +npm +[^\n]*\\+9\\.77 KiB|npm +npm +[^\n]*no change" "devenv text change")
    file(STRINGS "${FIXTURE}/hist/devenv-history.tsv" after)
    list(LENGTH after after_n)
    if(NOT before_n EQUAL after_n)
        message(SEND_ERROR "devenv --no-record changed the history (${before_n} -> ${after_n})")
    endif()

    # 4. JSON Lines, 인자 오류, 읽기 전용
    run(out rc devenv --jsonl --no-record)
    expect_rc(rc 0 "devenv jsonl")
    expect_match(out "\"event\":\"devenv_cache\"[^\n]*\"id\":\"npm\"" "devenv jsonl cache")
    expect_match(out "\"event\":\"devenv_vdisk\"[^\n]*\"kind\":\"docker\"" "devenv jsonl vdisk")
    expect_match(out "\"event\":\"complete\"[^\n]*\"recorded\":false" "devenv jsonl complete")
    run(out rc devenv --bogus)
    expect_rc(rc 2 "devenv bad arg")
    run(out rc devenv --json --jsonl)
    expect_rc(rc 2 "devenv json+jsonl")
    expect_exists("local/npm-cache/_cacache/index-v5/a" TRUE)
    expect_exists("local/Docker/wsl/disk/docker_data.vhdx" TRUE)

else()
    message(FATAL_ERROR "MODE must be find, project, system or devenv")
endif()
