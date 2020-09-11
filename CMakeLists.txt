cmake_minimum_required(VERSION 3.5.1)
set(HAVE_FLAG_SEARCH_PATHS_FIRST 0)

project(multiplex-tcp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -pthread")
include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)

conan_basic_setup()

add_executable(multiplex-tcp multiplex-tcp.cpp)
target_link_libraries(multiplex-tcp ${CONAN_LIBS})

IF ("${CMAKE_SYSTEM_NAME}" MATCHES "Windows")
   target_link_libraries(multiplex-tcp ws2_32 wsock32)
ENDIF ()
