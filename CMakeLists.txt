# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
project(faro LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

# Optional CMake file of an out-of-tree extension (one extension per build tree)
set(FARO_EXTENSION_CMAKE "" CACHE FILEPATH "Extension CMake file included after faro_core")
# Optional extra prefix for SDL2 headers/libs (e.g. a user-local sysroot)
set(FARO_SYSROOT "" CACHE PATH "Extra sysroot searched for SDL2")

add_library(faro_core STATIC
  app/core/AppStore.cpp
  app/core/RepeatRoamService.cpp
  app/core/OfflineTileReader.cpp
  app/core/Random.cpp
  app/core/Sha256.cpp
  app/hal/PowerBoard.cpp
  console/CompanionClientMeshActions.cpp
)
target_include_directories(faro_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/app
  ${CMAKE_CURRENT_SOURCE_DIR}/app/core
  ${CMAKE_CURRENT_SOURCE_DIR}/app/hal
  ${CMAKE_CURRENT_SOURCE_DIR}/console
)
target_compile_options(faro_core PRIVATE -Wall -Wextra)

add_executable(test_core
  tests/unit/test_core.cpp
)
target_link_libraries(test_core PRIVATE faro_core m)
add_test(NAME test_core COMMAND test_core)

# Optional extension (e.g. a private edition) kept outside this repository
if(FARO_EXTENSION_CMAKE)
  include(${FARO_EXTENSION_CMAKE})
endif()

add_executable(test_companion_client
  tests/unit/test_companion_client.cpp
)
target_link_libraries(test_companion_client PRIVATE faro_core pthread m)
add_test(NAME test_companion_client COMMAND test_companion_client)

# Locate LVGL 9.3 (from external/lvgl or PlatformIO libdeps; auto-clone v9.3.0 if missing)
set(LVGL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/MeshCore/.pio/libdeps/faro_tdeck/lvgl")
if(NOT EXISTS "${LVGL_ROOT}/src/lv_init.c")
  set(LVGL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/lvgl")
endif()
if(NOT EXISTS "${LVGL_ROOT}/src/lv_init.c")
  message(STATUS "LVGL 9.3 not found locally — cloning lvgl v9.3.0 into ${LVGL_ROOT}...")
  execute_process(
    COMMAND git clone --depth 1 --branch v9.3.0 https://github.com/lvgl/lvgl.git "${LVGL_ROOT}"
    RESULT_VARIABLE LVGL_CLONE_RES
  )
  if(NOT LVGL_CLONE_RES EQUAL 0)
    message(FATAL_ERROR "Cloning LVGL v9.3.0 failed (${LVGL_CLONE_RES})")
  endif()
endif()
if(EXISTS "${LVGL_ROOT}/src/lv_init.c")
  file(GLOB_RECURSE LVGL_SOURCES
    "${LVGL_ROOT}/src/*.c"
  )
  add_library(lvgl_host STATIC ${LVGL_SOURCES})
  target_include_directories(lvgl_host PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/app/config
    ${LVGL_ROOT}
    ${LVGL_ROOT}/src
  )
  target_compile_definitions(lvgl_host PUBLIC
    LV_CONF_INCLUDE_SIMPLE=1
    LV_LVGL_H_INCLUDE_SIMPLE=1
  )
  target_compile_options(lvgl_host PRIVATE -w)

  add_library(faro_ui STATIC
    app/ui/App.cpp
    app/ui/Theme.cpp
    app/ui/i18n/Strings.cpp
    app/ui/fonts/faro_font_12.c
    app/ui/fonts/faro_font_14.c
    app/ui/fonts/faro_font_16.c
    app/ui/fonts/faro_font_mono_12.c
    sim/VirtualMeshActions.cpp
  )
  target_include_directories(faro_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/app/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/sim
  )
  target_link_libraries(faro_ui PUBLIC faro_core lvgl_host m)

  find_package(PNG REQUIRED)

  add_executable(faro-sim
    sim/main_sdl.cpp
  )
  target_link_libraries(faro-sim PRIVATE faro_ui PNG::PNG m)

  add_executable(faro-console
    console/main_console.cpp
  )
  target_link_libraries(faro-console PRIVATE faro_ui PNG::PNG pthread m)

  # Optional SDL2 windowing support (including local sysroot headers & libs)
  find_library(SDL2_LIB NAMES libSDL2-2.0.so.0 libSDL2.so SDL2-2.0 SDL2
    PATHS
      /usr/lib/aarch64-linux-gnu
      /usr/lib/x86_64-linux-gnu
      /usr/lib
      /usr/local/lib
      ${FARO_SYSROOT}/usr/lib/aarch64-linux-gnu
      ${FARO_SYSROOT}/usr/lib/x86_64-linux-gnu
  )
  find_path(SDL2_INC NAMES SDL2/SDL.h
    PATHS /usr/include /usr/local/include ${FARO_SYSROOT}/usr/include
  )
  if(SDL2_LIB AND SDL2_INC)
    set(SDL2_MULTIARCH_INCS
      ${SDL2_INC}
      /usr/include/aarch64-linux-gnu
      /usr/include/x86_64-linux-gnu
    )
    if(FARO_SYSROOT)
      list(APPEND SDL2_MULTIARCH_INCS
        ${FARO_SYSROOT}/usr/include/aarch64-linux-gnu
        ${FARO_SYSROOT}/usr/include/x86_64-linux-gnu)
    endif()
    foreach(SDL_TARGET faro-sim faro-console)
      target_include_directories(${SDL_TARGET} PRIVATE ${SDL2_MULTIARCH_INCS})
      target_link_libraries(${SDL_TARGET} PRIVATE ${SDL2_LIB})
      target_compile_definitions(${SDL_TARGET} PRIVATE FARO_HAS_SDL2=1)
    endforeach()
  endif()

  add_executable(test_ui_sim
    tests/unit/test_ui_sim.cpp
  )
  target_link_libraries(test_ui_sim PRIVATE faro_ui m)
  add_test(NAME test_ui_sim COMMAND test_ui_sim)
endif()
