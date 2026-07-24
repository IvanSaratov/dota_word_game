cmake_minimum_required(VERSION 3.28)
project(dota_keyboard VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(BUILD_TESTING "Build tests" ON)

find_package(OpenCV 4 REQUIRED COMPONENTS core imgcodecs imgproc)
find_package(onnxruntime CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(dk_core
  src/text_normalizer.cpp
  src/target_tracker.cpp
  src/candidate_detector.cpp
  src/ctc_decoder.cpp
  src/ocr_preprocessor.cpp
  src/ocr_recognizer.cpp
  src/config.cpp)
target_include_directories(dk_core PUBLIC include ${OpenCV_INCLUDE_DIRS})
target_link_libraries(dk_core PUBLIC
  ${OpenCV_LIBS}
  onnxruntime::onnxruntime
  nlohmann_json::nlohmann_json)

if(WIN32)
  target_sources(dk_core PRIVATE
    src/window_locator_win32.cpp
    src/region_selector_win32.cpp)
  target_link_libraries(dk_core PUBLIC user32 gdi32)

  add_executable(window_locator_smoke
    src/window_locator_win32.cpp
    src/region_selector_win32.cpp)
  target_include_directories(window_locator_smoke PRIVATE include)
  target_compile_definitions(window_locator_smoke PRIVATE DK_WINDOW_LOCATOR_SMOKE)
  target_link_libraries(window_locator_smoke PRIVATE user32 gdi32)
endif()

if(MSVC)
  target_compile_options(dk_core PRIVATE /W4 /permissive- /utf-8)
  if(TARGET window_locator_smoke)
    target_compile_options(window_locator_smoke PRIVATE /W4 /permissive- /utf-8)
  endif()
else()
  target_compile_options(dk_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()
