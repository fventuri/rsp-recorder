cmake_minimum_required(VERSION 3.20)
project(rsp-recorder LANGUAGES C VERSION 1.0.0)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR})
find_package(LibSDRplay REQUIRED)

if (NOT LIBSDRPLAY_FOUND)
    message(FATAL_ERROR "SDRPlay development files not found...")
endif ()
message(STATUS "LIBSDRPLAY_INCLUDE_DIRS - ${LIBSDRPLAY_INCLUDE_DIRS}")
message(STATUS "LIBSDRPLAY_LIBRARIES - ${LIBSDRPLAY_LIBRARIES}")

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()
add_compile_options(-Wall -Wextra -pedantic -Werror)

set(SOURCE_FILES rsp-recorder.c config.c sdrplay-rsp.c buffers.c output.c wav.c callbacks.c streaming.c stats.c)
include_directories(${LIBSDRPLAY_INCLUDE_DIRS})

add_executable(${CMAKE_PROJECT_NAME} ${SOURCE_FILES})
if (WIN32)
    set(PTHREAD_LIBRARY libwinpthread.a)
endif ()
target_link_libraries(${CMAKE_PROJECT_NAME} ${LIBSDRPLAY_LIBRARIES} ${PTHREAD_LIBRARY})
