cmake_minimum_required(VERSION 3.16)
project(gazebo_bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.18)
pkg_check_modules(GSTREAMER_APP REQUIRED IMPORTED_TARGET gstreamer-app-1.0)
pkg_check_modules(GSTREAMER_RTSP REQUIRED IMPORTED_TARGET gstreamer-rtsp-server-1.0)

set(GZ_TRANSPORT_PKG "" CACHE STRING "pkg-config module for gz-transport (e.g. gz-transport14)")
set(GZ_TRANSPORT_TARGET "")

if(NOT GZ_TRANSPORT_PKG STREQUAL "")
  pkg_check_modules(GZTP REQUIRED IMPORTED_TARGET "${GZ_TRANSPORT_PKG}")
  set(GZ_TRANSPORT_TARGET "PkgConfig::${GZ_TRANSPORT_PKG}")
else()
  foreach(candidate gz-transport15 gz-transport14 gz-transport13 gz-transport12)
    pkg_check_modules(GZTP QUIET IMPORTED_TARGET "${candidate}")
    if(GZTP_FOUND)
      set(GZ_TRANSPORT_TARGET "PkgConfig::${candidate}")
      set(GZ_TRANSPORT_PKG "${candidate}")
      break()
    endif()
  endforeach()
endif()

if(GZ_TRANSPORT_TARGET STREQUAL "")
  message(FATAL_ERROR "gz-transport not found via pkg-config. Install libgz-transport*-dev or pass -DGZ_TRANSPORT_PKG=gz-transportNN")
endif()

add_executable(gazebo_bridge main.cpp)
target_link_libraries(gazebo_bridge PRIVATE
  PkgConfig::gstreamer-1.0
  PkgConfig::gstreamer-app-1.0
  PkgConfig::gstreamer-rtsp-server-1.0
  ${GZ_TRANSPORT_TARGET}
)
