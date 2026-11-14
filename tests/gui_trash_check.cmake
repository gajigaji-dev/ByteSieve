# GUI 휴지통 이동 자체 검사(bytesieve-gui --self-test-trash).
#
#   cmake -DEXE=<bytesieve-gui> -DFIXTURE=<빈 폴더로 만들 경로> -P gui_trash_check.cmake
#
# 픽스처를 만들고 GUI 가 탐지 → 모두 선택 → 휴지통 이동 → 화면 검증을 마치게 한 뒤,
# 옮긴 항목은 원래 자리에 없고 나머지 파일과 보호 대상은 그대로인지 확인한다.
# GUI 가 stdout 에 알려 준 휴지통 항목($R… 과 짝인 $I…)만 지워 사용자 휴지통을 정리한다.
#
# FIXTURE\
#   app\package.json, app\src\main.c       (남아야 함)
#   app\node_modules\a\i.js                → 휴지통
#   app\build\o.bin                        → 휴지통
#   app\server.log                         → 휴지통
#   locked\node_modules\locked\            (접근 거부 → 일부 누락 → 보호, 남아야 함)

foreach(var EXE FIXTURE)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "${var} is required")
    endif()
endforeach()

set(LOCKED "${FIXTURE}/locked/node_modules/locked")

function(unlock)
    if(EXISTS "${LOCKED}")
        file(TO_NATIVE_PATH "${LOCKED}" native)
        execute_process(COMMAND icacls "${native}" /remove:d *S-1-1-0 OUTPUT_QUIET ERROR_QUIET)
    endif()
endfunction()

unlock()
file(REMOVE_RECURSE "${FIXTURE}")
file(WRITE "${FIXTURE}/app/package.json" "{}")
file(WRITE "${FIXTURE}/app/src/main.c" "int main(void){return 0;}")
file(WRITE "${FIXTURE}/app/node_modules/a/i.js" "module.exports = 1;")
file(WRITE "${FIXTURE}/app/build/o.bin" "0123456789")
file(WRITE "${FIXTURE}/app/server.log" "log line")
file(WRITE "${FIXTURE}/locked/node_modules/index.js" "x")
file(MAKE_DIRECTORY "${LOCKED}")
file(TO_NATIVE_PATH "${LOCKED}" native_locked)
execute_process(COMMAND icacls "${native_locked}" /deny "*S-1-1-0:(OI)(CI)F"
                RESULT_VARIABLE deny_rc OUTPUT_QUIET ERROR_QUIET)
if(NOT deny_rc EQUAL 0)
    message(FATAL_ERROR "could not deny access to ${LOCKED}")
endif()

file(TO_NATIVE_PATH "${FIXTURE}" native_fixture)
# FORCE_QUARANTINE=1(gui_trash_fallback): 픽스처를 휴지통 없는 위치처럼 다뤄(BYTESIEVE_QUARANTINE_ROOT) 휴지통을
# 쓸 수 없을 때의 건너뜀·복원 경로를 검사한다. 격리 폴더는 픽스처 안에 생긴다.
set(extra_env)
if(FORCE_QUARANTINE)
    set(extra_env "BYTESIEVE_QUARANTINE_ROOT=${native_fixture}")
endif()
# 예제 폴더는 빌드 폴더(이 저장소) 안에 있으므로, 작업 트리의 Git 상태에 결과가 좌우되지 않게 git.exe 를 쓰지 않는다.
execute_process(COMMAND ${CMAKE_COMMAND} -E env BYTESIEVE_GIT=none ${extra_env}
                        "${EXE}" --self-test-trash "${native_fixture}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 110)
if(FORCE_QUARANTINE AND NOT out MATCHES "trash-skip: [^\r\n]*test quarantine root")
    message(SEND_ERROR "expected the quarantine fallback to be reported\n${out}\n${err}")
endif()

# 이 위치에서 휴지통을 쓸 수 없으면(빌드 폴더가 이동식 드라이브, 휴지통 꺼짐·용량 부족) GUI 가 격리 폴더로
# 전환하고, 검사는 휴지통 경로를 확인할 수 없어 건너뛴다. GUI 는 격리한 테스트 파일을 원래 자리로 복원하므로
# 실제 드라이브의 격리 폴더(.bytesieve-trash)에 아무것도 남지 않는다. CTest 는 "trash-skip:" 줄로 Skipped 처리한다.
if(out MATCHES "trash-skip: [^\r\n]+")
    message(STATUS "${CMAKE_MATCH_0}")
    if(NOT rc EQUAL 0)
        message(SEND_ERROR "quarantined test files could not all be restored\n${out}\n${err}")
    endif()
    foreach(kept app/node_modules/a/i.js app/build/o.bin app/server.log app/package.json)
        if(NOT EXISTS "${FIXTURE}/${kept}")
            message(SEND_ERROR "${kept} should have been restored from the quarantine folder\n${out}")
        endif()
    endforeach()
    unlock()
    file(REMOVE_RECURSE "${FIXTURE}")
    return()
endif()

# 결과와 상관없이 테스트가 휴지통에 넣은 항목만 지운다.
set(recycled_count 0)
string(REGEX MATCHALL "recycled: [^\r\n]+" lines "${out}")
foreach(line IN LISTS lines)
    string(REGEX REPLACE "^recycled: " "" path "${line}")
    file(TO_CMAKE_PATH "${path}" path)
    if(NOT path MATCHES "/\\$[Rr][Ee][Cc][Yy][Cc][Ll][Ee]\\.[Bb][Ii][Nn]/.+/\\$R[^/]*$")
        message(SEND_ERROR "unexpected recycled path: ${path}")
        continue()
    endif()
    if(NOT EXISTS "${path}")
        message(SEND_ERROR "recycled item not found in recycle bin: ${path}")
    endif()
    string(REGEX REPLACE "/\\$R([^/]*)$" "/$I\\1" info "${path}")
    file(REMOVE_RECURSE "${path}")
    file(REMOVE "${info}")
    math(EXPR recycled_count "${recycled_count} + 1")
endforeach()

set(failed 0)
if(NOT rc EQUAL 0)
    message(SEND_ERROR "bytesieve-gui --self-test-trash exited with ${rc}\n${err}")
    set(failed 1)
endif()
string(REGEX MATCHALL "result: [^\r\n]+" result_lines "${out}")
message(STATUS "completion banner: ${result_lines}")
# 완료 결과 영역: 옮길 수 있는 3개를 모두 옮겼으므로 "완료"여야 한다(판정과 실제 결과의 비교는 자체 검사가 한다).
if(NOT result_lines STREQUAL "result: success")
    message(SEND_ERROR "expected the completion banner to report success, got '${result_lines}'")
endif()
if(NOT recycled_count EQUAL 3)
    message(SEND_ERROR "expected 3 recycled items, got ${recycled_count}\n${out}")
endif()
foreach(gone app/node_modules app/build app/server.log)
    if(EXISTS "${FIXTURE}/${gone}")
        message(SEND_ERROR "${gone} should have been moved to the recycle bin")
    endif()
endforeach()
foreach(kept app/package.json app/src/main.c locked/node_modules/index.js)
    if(NOT EXISTS "${FIXTURE}/${kept}")
        message(SEND_ERROR "${kept} should not have been touched")
    endif()
endforeach()

unlock()
file(REMOVE_RECURSE "${FIXTURE}")
