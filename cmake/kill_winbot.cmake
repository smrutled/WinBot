# Script to kill running WinBot.exe processes prior to building/linking
execute_process(
    COMMAND taskkill /F /IM WinBot.exe /T
    RESULT_VARIABLE kill_result
    OUTPUT_VARIABLE kill_out
    ERROR_VARIABLE kill_err
)

if(kill_result EQUAL 0)
    message(STATUS "Terminated running WinBot processes: ${kill_out}")
endif()
