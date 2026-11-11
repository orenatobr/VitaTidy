cmake_minimum_required(VERSION 3.16)
project(VitaTidy C)
set(CMAKE_C_STANDARD 11)

set(CORE_SOURCES
  src/core/fsutil.c src/core/order.c src/core/pngio.c src/core/stb_impl.c src/core/badge.c
  src/core/iso9660.c src/core/livearea.c src/core/tasks.c src/core/sqlite_vfs.c)
# SQLite is used only as an in-memory database (app.db is loaded and saved as a byte buffer).
set(SQLITE_DEFS SQLITE_OS_OTHER=1 SQLITE_THREADSAFE=0 SQLITE_OMIT_LOAD_EXTENSION SQLITE_OMIT_WAL SQLITE_TEMP_STORE=3
    SQLITE_ENABLE_DESERIALIZE SQLITE_DEFAULT_MEMSTATUS=0 SQLITE_MAX_MMAP_SIZE=0)

if(VITA)
  include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
  set(VITA_APP_NAME "VitaTidy")
  set(VITA_TITLEID "VTIDY0001")
  set(VITA_VERSION "01.00")

  add_executable(vitatidy src/vita/main.c src/vita/ui.c src/core/platform_vita.c ${CORE_SOURCES} third_party/sqlite3/sqlite3.c)
  target_compile_definitions(vitatidy PRIVATE ${SQLITE_DEFS} _POSIX_C_SOURCE=200809L)
  target_compile_options(vitatidy PRIVATE -Wall -Wextra -O2)
  set_source_files_properties(third_party/sqlite3/sqlite3.c PROPERTIES COMPILE_OPTIONS -w)
  target_link_libraries(vitatidy vita2d freetype png jpeg z m c
    SceDisplay_stub SceGxm_stub SceCtrl_stub SceTouch_stub SceSysmodule_stub SceCommonDialog_stub
    ScePower_stub SceRtc_stub SceIofilemgr_stub ScePgf_stub ScePvf_stub SceAppMgr_stub)

  vita_create_self(vitatidy.self vitatidy UNSAFE)
  vita_create_vpk(vitatidy.vpk ${VITA_TITLEID} vitatidy.self
    VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
    FILE sce_sys/icon0.png sce_sys/icon0.png
    FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
    FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
    FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
    FILE data/order.txt data/order.txt
    FILE data/psp-order.txt data/psp-order.txt)
else()
  enable_testing()
  add_library(sqlite3_host STATIC third_party/sqlite3/sqlite3.c)
  target_compile_definitions(sqlite3_host PRIVATE ${SQLITE_DEFS})
  target_compile_options(sqlite3_host PRIVATE -w)

  add_executable(core_tests tests/test_core.c src/core/platform_host.c ${CORE_SOURCES})
  target_compile_options(core_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
  target_link_libraries(core_tests sqlite3_host z m)
  option(VT_SANITIZE "Build tests with ASan and UBSan" ON)
  if(VT_SANITIZE)
    target_compile_options(core_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(core_tests PRIVATE -fsanitize=address,undefined)
  endif()
  add_executable(la_cli tools/la_cli.c ${CORE_SOURCES} src/core/platform_host.c)
  target_link_libraries(la_cli sqlite3_host z m)
  add_executable(icon_cli tools/icon_cli.c ${CORE_SOURCES} src/core/platform_host.c)
  target_link_libraries(icon_cli sqlite3_host z m)
  add_executable(task_cli tools/task_cli.c ${CORE_SOURCES} src/core/platform_host.c)
  target_link_libraries(task_cli sqlite3_host z m)
  add_test(NAME core COMMAND core_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
