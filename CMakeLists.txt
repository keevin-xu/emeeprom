cmake_minimum_required(VERSION 3.16)

project(emeeprom_host_cli C)

add_executable(emeeprom_cli
    source/cy_em_eeprom.c
    host/virtual_flash.c
    host/emeeprom_cli.c
)

target_compile_definitions(emeeprom_cli PRIVATE CY_EM_EEPROM_HOST_TEST)
target_include_directories(emeeprom_cli PRIVATE host/include include)

if(MSVC)
    target_compile_options(emeeprom_cli PRIVATE /W4)
else()
    target_compile_options(emeeprom_cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(emeeprom_cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/build"
)
