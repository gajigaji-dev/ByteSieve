# GUI 스캔 뒤 직전 기록과 비교·저장(작업 스레드) 중에 창을 닫는 검사(bytesieve-gui --self-test-history-close).
#
#   cmake -DEXE=<bytesieve-gui> -DCLI=<disk-scanner> -DTARGET=<스캔할 폴더> -DHISTORY=<비울 기록 폴더>
#         -P gui_history_close_check.cmake
#
# GUI 는 비교·저장 작업을 늦춘 채 스캔하고, 작업 중 상태를 확인한 뒤 그 상태에서 창을 닫는다. 폴더 스캔 패널은
# 작업이 끝나기를 기다린 뒤 종료해야 하므로, 기록 폴더에는 반쯤 쓴 파일(임시 파일) 없이 온전한 스냅숏 하나만
# 남고, 같은 폴더를 CLI(--track)로 다시 스캔하면 그 스냅숏을 읽어 비교한다(기준 저장이 아니다).

foreach(var EXE CLI TARGET HISTORY)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "${var} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${HISTORY}")
file(TO_NATIVE_PATH "${TARGET}" native_target)
file(TO_NATIVE_PATH "${HISTORY}" native_history)

execute_process(COMMAND ${CMAKE_COMMAND} -E env "BYTESIEVE_HISTORY_DIR=${native_history}"
                        "${EXE}" --self-test-history-close "${native_target}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 110)
if(NOT rc EQUAL 0 OR NOT out MATCHES "history-close: closing while saving")
    message(FATAL_ERROR "bytesieve-gui --self-test-history-close failed (${rc})\n${out}\n${err}")
endif()

file(GLOB snaps "${HISTORY}/*.snap")
file(GLOB temps "${HISTORY}/*.tmp" "${HISTORY}/*.snap.*")
list(LENGTH snaps snap_count)
if(NOT snap_count EQUAL 1)
    message(FATAL_ERROR "expected exactly one snapshot after closing, got ${snap_count}: ${snaps}")
endif()
if(temps)
    message(FATAL_ERROR "half-written files left after closing: ${temps}")
endif()

execute_process(COMMAND "${CLI}" --track --history-dir "${native_history}" "${native_target}"
                RESULT_VARIABLE cli_rc OUTPUT_VARIABLE cli_out ERROR_VARIABLE cli_err TIMEOUT 110)
if(cli_out MATCHES "no previous snapshot")
    message(FATAL_ERROR "the snapshot saved while closing could not be read\n${cli_out}\n${cli_err}")
endif()
if(NOT cli_out MATCHES "History:")
    message(FATAL_ERROR "unexpected disk-scanner --track output (${cli_rc})\n${cli_out}\n${cli_err}")
endif()
file(REMOVE_RECURSE "${HISTORY}")
