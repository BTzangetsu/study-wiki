cmake_minimum_required(VERSION 3.16)
project(study-wiki)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

find_program(MYSQL_CONFIG NAMES mysql_config mariadb_config)

if(MYSQL_CONFIG)
    execute_process(
        COMMAND ${MYSQL_CONFIG} --include
        OUTPUT_VARIABLE MYSQL_INCLUDE_FLAGS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND ${MYSQL_CONFIG} --libs
        OUTPUT_VARIABLE MYSQL_LINK_FLAGS
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    message(FATAL_ERROR "mysql_config introuvable")
endif()

add_executable(study-wiki src/main.cpp)

string(REPLACE "-I" "" MYSQL_INCLUDE_DIR ${MYSQL_INCLUDE_FLAGS})
separate_arguments(MYSQL_LINK_FLAGS_LIST UNIX_COMMAND ${MYSQL_LINK_FLAGS})

target_include_directories(study-wiki PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/AsioLib
    ${MYSQL_INCLUDE_DIR}
)

target_link_libraries(study-wiki PRIVATE
    Threads::Threads
    ${MYSQL_LINK_FLAGS_LIST}
)

target_compile_options(study-wiki PRIVATE
    -O2
    -Wall
    -Wextra
    -Wno-deprecated-declarations
)