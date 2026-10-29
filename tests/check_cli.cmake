execute_process(COMMAND "${APP}" --package express --version 4.18.2
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
    ENCODING UTF-8)
string(REPLACE "\r\n" "\n" output "${output}")
set(expected "package=express\nrepository=https://registry.npmjs.org\ntest_mode=false\n")
string(APPEND expected "version=4.18.2\noutput=graph.svg\nascii=false\nfilter=\n")
if(NOT result EQUAL 0 OR NOT output STREQUAL expected OR NOT error STREQUAL "")
    message(FATAL_ERROR "Default output failed: ${result}; ${output}; ${error}")
endif()

execute_process(COMMAND "${APP}" --package A --version 1.0.0 --test-mode
    --repository "${SOURCE_DIR}/examples/repository.json" --ascii --filter B
    --output "${SOURCE_DIR}/граф.svg"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
    ENCODING UTF-8)
if(NOT result EQUAL 0 OR NOT output MATCHES "test_mode=true" OR NOT output MATCHES "ascii=true")
    message(FATAL_ERROR "Test mode failed: ${result}; ${output}; ${error}")
endif()
if(NOT output MATCHES "граф.svg" OR EXISTS "${SOURCE_DIR}/граф.svg")
    message(FATAL_ERROR "Unicode path or stage 1 output contract failed")
endif()

execute_process(COMMAND "${APP}" --package express --version invalid
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
    ENCODING UTF-8)
if(NOT result EQUAL 2 OR NOT output STREQUAL "" OR NOT error MATCHES "--version")
    message(FATAL_ERROR "Error handling failed: ${result}; ${output}; ${error}")
endif()

execute_process(COMMAND "${APP}" --help
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
    ENCODING UTF-8)
if(NOT result EQUAL 0 OR NOT output MATCHES "--package" OR NOT error STREQUAL "")
    message(FATAL_ERROR "Help failed: ${result}; ${output}; ${error}")
endif()
