# Shared Davilex Delphi-menu engine (Vakantie Racer, Europe Racer, A2 Racer IV).
# Use from a game's CMakeLists:
#   set(DAVILEX_RED_DIR ${CMAKE_SOURCE_DIR}/../Davilex-RED)
#   include(${DAVILEX_RED_DIR}/menu/davi_menu.cmake)
#   target_link_libraries(<game>-menu PRIVATE davi_menu_engine)
# Sources include "engine/<file>.h".
if(TARGET davi_menu_engine)
  return()
endif()
find_package(SDL3 REQUIRED CONFIG)
find_package(Freetype REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DAVI_MENU_FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)

file(GLOB DAVI_MENU_ENGINE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/engine/*.cpp)
add_library(davi_menu_engine STATIC ${DAVI_MENU_ENGINE_SOURCES})
target_include_directories(davi_menu_engine PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(davi_menu_engine PUBLIC SDL3::SDL3 Freetype::Freetype ZLIB::ZLIB PkgConfig::DAVI_MENU_FFMPEG)
if(APPLE)
  target_link_libraries(davi_menu_engine PUBLIC objc)  # app.cpp: activation handover to the race
endif()
