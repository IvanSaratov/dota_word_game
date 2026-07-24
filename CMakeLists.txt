cmake_minimum_required(VERSION 3.28)
project(dota_keyboard VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(BUILD_TESTING "Build tests" ON)

find_package(OpenCV 4 REQUIRED COMPONENTS core imgcodecs imgproc)
find_package(onnxruntime CONFIG REQUIRED)

add_library(dk_core
  src/text_normalizer.cpp
  src/target_tracker.cpp
  src/candidate_detector.cpp
  src/ctc_decoder.cpp
  src/ocr_recognizer.cpp)
target_include_directories(dk_core PUBLIC include ${OpenCV_INCLUDE_DIRS})
target_link_libraries(dk_core PUBLIC ${OpenCV_LIBS} onnxruntime::onnxruntime)
if(MSVC)
  target_compile_options(dk_core PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(dk_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()
