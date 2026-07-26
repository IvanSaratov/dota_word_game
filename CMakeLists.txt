cmake_minimum_required(VERSION 3.28)
project(dota_keyboard VERSION 0.3.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(BUILD_TESTING "Build tests" ON)

find_package(OpenCV 4 REQUIRED COMPONENTS core imgcodecs imgproc)
find_package(onnxruntime CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(dk_core
  src/logger.cpp
  src/session_log.cpp
  src/text_normalizer.cpp
  src/target_tracker.cpp
  src/compound_target.cpp
  src/target_scheduler.cpp
  src/candidate_detector.cpp
  src/ctc_decoder.cpp
  src/ocr_preprocessor.cpp
  src/ocr_recognizer.cpp
  src/config.cpp
  src/delay.cpp
  src/metrics.cpp
  src/app.cpp)
target_include_directories(dk_core PUBLIC include ${OpenCV_INCLUDE_DIRS})
target_link_libraries(dk_core PUBLIC
  ${OpenCV_LIBS}
  onnxruntime::onnxruntime
  nlohmann_json::nlohmann_json)

if(WIN32)
  target_sources(dk_core PRIVATE
    src/window_locator_win32.cpp
    src/region_selector_win32.cpp
    src/dxgi_capture_win32.cpp
    src/win32_input_sink.cpp
    src/hotkeys_win32.cpp)
  target_link_libraries(dk_core PUBLIC user32 gdi32 d3d11 dxgi)

  add_executable(window_locator_smoke
    src/window_locator_win32.cpp
    src/region_selector_win32.cpp)
  target_include_directories(window_locator_smoke PRIVATE include)
  target_compile_definitions(window_locator_smoke PRIVATE DK_WINDOW_LOCATOR_SMOKE)
  target_link_libraries(window_locator_smoke PRIVATE user32 gdi32)

  add_executable(capture_smoke src/capture_smoke.cpp)
  target_link_libraries(capture_smoke PRIVATE dk_core)
  set_target_properties(capture_smoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/src"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/src"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/src"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/src")

  add_executable(input_sink_smoke src/input_sink_smoke.cpp)
  target_link_libraries(input_sink_smoke PRIVATE dk_core)

  add_executable(dota_keyboard src/main_win32.cpp)
  target_link_libraries(dota_keyboard PRIVATE dk_core)
  target_compile_definitions(dota_keyboard PRIVATE
    DK_APP_VERSION="${PROJECT_VERSION}")
endif()

if(MSVC)
  target_compile_options(dk_core PRIVATE /W4 /permissive- /utf-8)
  if(TARGET window_locator_smoke)
    target_compile_options(window_locator_smoke PRIVATE /W4 /permissive- /utf-8)
  endif()
  if(TARGET capture_smoke)
    target_compile_options(capture_smoke PRIVATE /W4 /permissive- /utf-8)
  endif()
  if(TARGET input_sink_smoke)
    target_compile_options(input_sink_smoke PRIVATE /W4 /permissive- /utf-8)
  endif()
  if(TARGET dota_keyboard)
    target_compile_options(dota_keyboard PRIVATE /W4 /permissive- /utf-8)
  endif()
else()
  target_compile_options(dk_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()

if(WIN32)
  # Keep the portable ZIP self-contained: CMake resolves the DLLs linked by the
  # executable and installs them next to the executable.  The vcpkg directory
  # is required for dependencies that are not copied into the build directory.
  # InstallRequiredSystemLibraries adds the MSVC runtime DLLs separately; they
  # are not guaranteed to appear in the executable dependency scan.
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
  include(InstallRequiredSystemLibraries)

  set(DK_RUNTIME_DEPENDENCY_DIRS)
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND DK_RUNTIME_DEPENDENCY_DIRS
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
  endif()

  install(TARGETS dota_keyboard
    RUNTIME_DEPENDENCIES
      DIRECTORIES ${DK_RUNTIME_DEPENDENCY_DIRS}
      PRE_EXCLUDE_REGEXES
        "[Aa][Pp][Ii]-[Mm][Ss]-.*"
        "[Ee][Xx][Tt]-[Mm][Ss]-.*"
      POST_EXCLUDE_REGEXES
        ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\\\/].*"
        ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Ww][Oo][Ww]64[\\\\/].*"
    RUNTIME DESTINATION ".")
  install(FILES
    assets/models/en_PP-OCRv5_rec_mobile_infer.onnx
    assets/models/ppocrv5_en_dict.txt
    DESTINATION "assets/models")
  install(FILES config/default.json DESTINATION "." RENAME "config.json")
  install(FILES README.ru.md DESTINATION ".")

  set(CPACK_GENERATOR "ZIP")
  set(CPACK_PACKAGE_NAME "Dota Keyboard")
  set(CPACK_PACKAGE_VENDOR "IvanSaratov")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "DotaKeyboard")
  set(CPACK_PACKAGE_EXECUTABLES "dota_keyboard" "Dota Keyboard")
  set(CPACK_PROJECT_CONFIG_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageProjectConfig.cmake")
  set(CPACK_INNOSETUP_ARCHITECTURE "x64")
  set(CPACK_INNOSETUP_INSTALL_ROOT "{localappdata}/Programs")
  set(CPACK_INNOSETUP_SETUP_PrivilegesRequired "lowest")
  set(CPACK_INNOSETUP_USE_MODERN_WIZARD ON)
  set(CPACK_INNOSETUP_LANGUAGES "english;russian")
  set(CPACK_INNOSETUP_IGNORE_LICENSE_PAGE ON)
  set(CPACK_INNOSETUP_RUN_EXECUTABLES "dota_keyboard")
  set(CPACK_INNOSETUP_EXTRA_SCRIPTS
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/InnoRuntimeCleanup.iss")
  set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
  include(CPack)
endif()
