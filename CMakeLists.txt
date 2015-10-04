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

add_definitions("-Wall -Wextra -Werror -pedantic -g -O3")
# add_definitions("-Wno-unused-parameter -Wno-unused-function")

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

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FILE_OFFSET_BITS=64")


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
    src/region/region.c)


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
target_link_libraries(ilka_check ilka m rt ${CHECK_LIBRARIES})

function(ilka_test name)
    add_executable(${name}_test tests/${name}_test.c)
    target_link_libraries(${name}_test ilka_check)
    add_test(${name} bin/${name}_test)
endfunction()

ilka_test(bits)
ilka_test(bit_coder)
# ilka_test(key)
ilka_test(alloc)
ilka_test(epoch)
ilka_test(persist)
