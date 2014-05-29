#------------------------------------------------------------------------------#
# CMakeLists.txt
# Rémi Attab (remi.attab@gmail.com), 30 Dec 2012
# FreeBSD-style copyright and disclaimer apply
#------------------------------------------------------------------------------#


#------------------------------------------------------------------------------#
# CONFIG
#------------------------------------------------------------------------------#

cmake_minimum_required(VERSION 2.6)
project(ilka)

enable_testing()

add_definitions("-Wall -Wextra -Werror -pedantic -g -O4")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)


#------------------------------------------------------------------------------#
# COMPILER TESTS
#------------------------------------------------------------------------------#

include(CheckCCompilerFlag)

check_c_compiler_flag(-std=c11 ILKA_FLAG_C99)
check_c_compiler_flag(-std=c99 ILKA_FLAG_C11)

if (ILKA_FLAG_C11)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11")
elseif (ILKA_FLAG_C99)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
endif()

include(CheckCXXCompilerFlag)

check_cxx_compiler_flag(-std=c++11 ILKA_FLAG_CPP11)
check_cxx_compiler_flag(-std=c++0x ILKA_FLAG_CPP0X)

if (ILKA_FLAG_CPP11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
elseif (ILKA_FLAG_CPP0X)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
endif()


#------------------------------------------------------------------------------#
# LIBRARIES
#------------------------------------------------------------------------------#

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/CMakeModules")

find_package(Check REQUIRED)
include_directories(${CHECK_INCLUDE_DIRS})


option(USE_TCMALLOC "Use tcmalloc for heap allocations." ON)
find_library(LIB_TCMALLOC tcmalloc)

if(USE_TCMALLOC)
    if(LIB_TCMALLOC MATCHES "NOTFOUND")
        message("WARNING: unable to find tcmalloc")
    else()
        link_libraries(tcmalloc)
        add_definitions("-fno-builtin-malloc -fno-builtin-calloc")
        add_definitions("-fno-builtin-realloc -fno-builtin-free")
    endif()
endif()


#------------------------------------------------------------------------------#
# BUILD
#------------------------------------------------------------------------------#

include_directories("${PROJECT_SOURCE_DIR}/src")

add_library(ilka SHARED
    src/ilka.c
    src/utils/utils.c
    src/trie/trie.c)


#------------------------------------------------------------------------------#
# INSTALL
#------------------------------------------------------------------------------#

install(TARGETS ilka DESTINATION lib)

install(
    FILES
    src/ilka.h
    DESTINATION
    include/ilka)


#------------------------------------------------------------------------------#
# TESTS
#------------------------------------------------------------------------------#

add_library(ilka_check SHARED tests/check.c)

function(ilka_test name)
    if(CMAKE_SOURCE_DIR STREQUAL ${PROJECT_SOURCE_DIR})
        add_executable(${name}_test tests/${name}_test.c)
        target_link_libraries(${name}_test ilka ilka_check ${CHECK_LIBRARIES})
        add_test(${name} bin/${name}_test)
    endif()
endfunction()

ilka_test(bits)
