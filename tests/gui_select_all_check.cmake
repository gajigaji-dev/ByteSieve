# GUI [모두 선택]·[선택 해제] 단추 검사(bytesieve-gui --self-test-cleanup).
#
#   cmake -DEXE=<bytesieve-gui> -DFIXTURE=<빈 폴더로 만들 경로> -P gui_select_all_check.cmake
#
# 고를 수 있는 정리 후보가 있는 픽스처를 탐지해, 단추를 실제로 눌렀을 때(BM_CLICK) 보이는 후보가 모두
# 체크되고 선택 줄·실행 대상(계획)과 맞는지, 다시 눌러도·하나를 푼 뒤 눌러도 맞는지, [선택 해제]로 모두
# 풀리는지 GUI 자체 검사(cleanup_panel_verify)가 확인한다. 소스 트리는 미커밋 변경이 있으면 후보가 모두
# 직접 골라야 하는 대상이 되어 이 경로를 거치지 않으므로 픽스처를 따로 만든다.
# 정리(휴지통 이동)는 하지 않으므로 파일이 모두 그대로 남아야 한다.
#
# FIXTURE\
#   app\package.json, app\src\main.c
#   app\node_modules\a\i.js, app\build\o.bin, app\server.log   (정리 후보 3개)

foreach(var EXE FIXTURE)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "${var} is required")
    endif()
endforeach()

set(files
    app/package.json
    app/src/main.c
    app/node_modules/a/i.js
    app/build/o.bin
    app/server.log)

file(REMOVE_RECURSE "${FIXTURE}")
foreach(f IN LISTS files)
    file(WRITE "${FIXTURE}/${f}" "x")
endforeach()

file(TO_NATIVE_PATH "${FIXTURE}" native_fixture)
# 빌드 폴더(이 저장소) 안이므로 작업 트리의 Git 상태에 결과가 좌우되지 않게 git.exe 를 쓰지 않는다.
execute_process(COMMAND ${CMAKE_COMMAND} -E env BYTESIEVE_GIT=none
                        "${EXE}" --self-test-cleanup "${native_fixture}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 110)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "self-test failed (${rc})\n${out}\n${err}")
endif()
if(NOT out MATCHES "select-all candidates: 3")
    message(FATAL_ERROR "expected 3 select-all candidates\n${out}\n${err}")
endif()
foreach(f IN LISTS files)
    if(NOT EXISTS "${FIXTURE}/${f}")
        message(FATAL_ERROR "selection must not move files: ${f} is missing")
    endif()
endforeach()
file(REMOVE_RECURSE "${FIXTURE}")
