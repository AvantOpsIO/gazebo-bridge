cmake_minimum_required(VERSION 3.16)
project(gazebo_bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0>=1.18)
pkg_check_modules(GSTREAMER_APP REQUIRED gstreamer-app-1.0)
pkg_check_modules(GSTREAMER_RTSP REQUIRED gstreamer-rtsp-server-1.0)

set(GZ_TRANSPORT_PKG "" CACHE STRING "pkg-config module for gz-transport (e.g. gz-transport14)")

if(NOT GZ_TRANSPORT_PKG STREQUAL "")
  pkg_check_modules(GZTP REQUIRED "${GZ_TRANSPORT_PKG}")
else()
  set(GZTP_FOUND FALSE)
  foreach(candidate gz-transport15 gz-transport14 gz-transport13 gz-transport12)
    pkg_check_modules(GZTP QUIET "${candidate}")
    if(GZTP_FOUND)
      set(GZ_TRANSPORT_PKG "${candidate}")
      break()
    endif()
  endforeach()
endif()

if(NOT GZTP_FOUND)
  message(FATAL_ERROR "gz-transport not found via pkg-config. Install libgz-transport*-dev or pass -DGZ_TRANSPORT_PKG=gz-transportNN")
endif()

add_executable(gazebo_bridge main.cpp)
target_include_directories(
  gazebo_bridge
  PRIVATE ${GSTREAMER_INCLUDE_DIRS} ${GSTREAMER_APP_INCLUDE_DIRS} ${GSTREAMER_RTSP_INCLUDE_DIRS}
          ${GZTP_INCLUDE_DIRS}
)
target_compile_options(
  gazebo_bridge
  PRIVATE ${GSTREAMER_CFLAGS_OTHER} ${GSTREAMER_APP_CFLAGS_OTHER} ${GSTREAMER_RTSP_CFLAGS_OTHER}
          ${GZTP_CFLAGS_OTHER}
)
target_link_libraries(
  gazebo_bridge
  PRIVATE ${GSTREAMER_LIBRARIES} ${GSTREAMER_APP_LIBRARIES} ${GSTREAMER_RTSP_LIBRARIES} ${GZTP_LIBRARIES}
)
