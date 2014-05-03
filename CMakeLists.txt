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

include(CheckCXXCompilerFlag)

check_cxx_compiler_flag(-std=c++11 REFLECT_FLAG_C11)
check_cxx_compiler_flag(-std=c++0x REFLECT_FLAG_C0X)

if (REFLECT_FLAG_C11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
elseif (REFLECT_FLAG_C0X)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
endif()


#------------------------------------------------------------------------------#
# ENV TEST
#------------------------------------------------------------------------------#

include_directories("includes")

include_directories("$ENV{HOME}/local/include")
link_directories("$ENV{HOME}/local/lib")


#------------------------------------------------------------------------------#
# LIBRARIES
#------------------------------------------------------------------------------#

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

add_library(ilka SHARED src/ilka.c)


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

function(ilka_test name)
    if(CMAKE_SOURCE_DIR STREQUAL ${PROJECT_SOURCE_DIR})
        add_executable(${name}_test tests/${name}_test.cpp)
        target_link_libraries(${name}_test ilka)
        add_test(${name} bin/${name}_test)
    endif()
endfunction()

reflect_test(ref)
