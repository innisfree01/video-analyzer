cmake_minimum_required(VERSION 3.10)
project(uvc_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -O2)

include_directories(${CMAKE_SOURCE_DIR}/include)

set(SOURCES
    src/main.c
    src/uvc_xu_ctrl.c
    src/uvc_stream.c
)

add_executable(uvc_host ${SOURCES})
target_link_libraries(uvc_host pthread)

install(TARGETS uvc_host DESTINATION bin)
