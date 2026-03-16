cmake_minimum_required(VERSION 3.16)
project(study-wiki)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# Cherche le client MariaDB/MySQL
find_library(MYSQLCLIENT_LIB
    NAMES mysqlclient mariadb
    PATHS
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        /usr/lib/mysql
        /usr/lib/aarch64-linux-gnu
)

if(NOT MYSQLCLIENT_LIB)
    message(FATAL_ERROR "libmariadb/libmysqlclient introuvable")
endif()

add_executable(study-wiki src/main.cpp)

target_include_directories(study-wiki PRIVATE
    # Sources du projet
    ${CMAKE_SOURCE_DIR}/src

    # Crow — header only, crow.h est à la racine de backend/
    ${CMAKE_SOURCE_DIR}

    # Asio standalone — dans backend/AsioLib/
    ${CMAKE_SOURCE_DIR}/AsioLib

    # Headers MariaDB
    /usr/include/mysql
    /usr/include/mariadb
)

# ASIO_STANDALONE → dit à Asio de ne pas chercher Boost
# CROW_USE_BOOST=0 → dit à Crow de ne pas utiliser Boost
target_compile_definitions(study-wiki PRIVATE
    ASIO_STANDALONE
    CROW_USE_BOOST=0
)

target_link_libraries(study-wiki PRIVATE
    Threads::Threads
    ${MYSQLCLIENT_LIB}
)

target_compile_options(study-wiki PRIVATE
    -O2
    -Wall
    -Wextra
    # Désactive les warnings sur les headers externes
    # (Crow et Asio génèrent des warnings qu'on ne contrôle pas)
    -Wno-deprecated-declarations
)