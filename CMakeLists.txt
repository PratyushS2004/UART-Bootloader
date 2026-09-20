cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_PROJECT_NAME "UART_Bootloader")
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

enable_language(C ASM)

# ---- Shared bundle: everything both targets have in common ----
add_library(common_startup INTERFACE)

target_sources(common_startup INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/system_stm32f4xx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Startup/startup_stm32f446xx.s
)

target_include_directories(common_startup INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32F4xx/Include/
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Include
)

target_compile_definitions(common_startup INTERFACE
    USE_NUCLEO_64
    STM32F446xx
    $<$<CONFIG:Debug>:DEBUG>
)

# ---- Bootloader target ----
add_executable(bootloader)
target_sources(bootloader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Bootloader/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Bootloader/flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Bootloader/uart.c
)
target_link_libraries(bootloader PRIVATE common_startup)
target_link_options(bootloader PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/Bootloader/bootloader.ld
)

# ---- Application target ----
add_executable(app)
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Application/main.c
)
target_link_libraries(app PRIVATE common_startup)
target_link_options(app PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/Application/application.ld
)

list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)