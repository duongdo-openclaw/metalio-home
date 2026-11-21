cmake_minimum_required(VERSION 3.16)
project(metalio_home C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(CJSON REQUIRED libcjson)
find_package(CURL REQUIRED)

# LVGL 9.5.0 (same version as the firmware's idf_component.yml), configured by ./lv_conf.h
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/lvgl EXCLUDE_FROM_ALL)

# Portable core: compiles unchanged into the ESP-IDF firmware later (no SDL, no curl).
add_library(app_core STATIC
  app/kiosk_model.cc
  app/drive_folder.cc
  app/home_gate.cc
  app/book.cc
  app/media_info.cc
  app/owner.cc
  app/settings.cc
  app/text_search.cc
  app/telex.cc
  app/dictionary.cc
  app/notes.cc
  app/vocab.cc
  app/online_library.cc
  app/ai_client.cc)
target_include_directories(app_core PUBLIC app ${CJSON_INCLUDE_DIRS})
find_package(ZLIB REQUIRED)
target_link_libraries(app_core PUBLIC ${CJSON_LIBRARIES} ZLIB::ZLIB)

# Portable LVGL screens.
add_library(app_ui STATIC
  app/ui_common.cc
  app/launcher_screen.cc
  app/smarthome_screen.cc
  app/drive_screen.cc
  app/touch_test_screen.cc
  app/paginate.cc
  app/reading_state.cc
  app/reader_screen.cc
  app/music_screen.cc
  app/recorder_screen.cc
  app/apps_screen.cc
  app/owner_screen.cc
  app/settings_screen.cc
  app/transfer_screen.cc
  app/system.cc
  app/quick_settings.cc
  app/library.cc
  app/library_screen.cc
  app/goals_screen.cc
  app/dictionary_screen.cc
  app/vocab_screen.cc
  app/notes_screen.cc
  app/share_screen.cc
  app/assistant_screen.cc)
target_link_libraries(app_ui PUBLIC app_core lvgl)

add_library(touchmap STATIC sim/touch_map.c sim/touch_profile.c)
target_include_directories(touchmap PUBLIC sim)

add_executable(metalio_sim sim/main.cpp sim/platform_sim.cc sim/audio_sim.cc sim/device_sim.cc sim/transfer_sim.cc)
target_include_directories(metalio_sim PRIVATE ${SDL2_INCLUDE_DIRS})
target_compile_definitions(metalio_sim PRIVATE ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_link_libraries(metalio_sim PRIVATE app_ui touchmap ${SDL2_LIBRARIES} CURL::libcurl pthread m)

# Unit tests (no display needed).
enable_testing()
add_executable(study_test tests/study_test.cc tests/platform_stub.cc)
target_link_libraries(study_test PRIVATE app_core)
target_compile_definitions(study_test PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_test(NAME study_test COMMAND study_test)
add_executable(ai_client_test tests/ai_client_test.cc tests/platform_stub.cc)
target_link_libraries(ai_client_test PRIVATE app_core)
add_test(NAME ai_client_test COMMAND ai_client_test)
add_executable(search_paginate_test tests/search_paginate_test.cc app/paginate.cc)
target_link_libraries(search_paginate_test PRIVATE app_core lvgl)
add_test(NAME search_paginate_test COMMAND search_paginate_test)

foreach(t touch_map_test kiosk_model_test drive_folder_test book_test telex_test)
  add_executable(${t} tests/${t}.cc)
  target_link_libraries(${t} PRIVATE app_core touchmap)
  target_compile_definitions(${t} PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
  add_test(NAME ${t} COMMAND ${t})
endforeach()
