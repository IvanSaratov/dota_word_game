find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

function(dk_add_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE dk_core Catch2::Catch2WithMain)
  catch_discover_tests(${name} TEST_PREFIX "${name}::")
endfunction()

dk_add_test(text_normalizer_test text_normalizer_test.cpp)
dk_add_test(target_tracker_test target_tracker_test.cpp)
dk_add_test(candidate_detector_test candidate_detector_test.cpp)
dk_add_test(ctc_decoder_test ctc_decoder_test.cpp)
dk_add_test(ocr_preprocessor_test ocr_preprocessor_test.cpp)
dk_add_test(ocr_recognizer_test ocr_recognizer_test.cpp)
dk_add_test(config_test config_test.cpp)
dk_add_test(input_sink_test input_sink_test.cpp)
dk_add_test(app_test app_test.cpp)

add_executable(dxgi_capture_source_test dxgi_capture_source_test.cpp)
target_compile_definitions(dxgi_capture_source_test PRIVATE
  DK_DXGI_CAPTURE_SOURCE_PATH="${PROJECT_SOURCE_DIR}/src/dxgi_capture_win32.cpp")
add_test(NAME dxgi_capture_source_test COMMAND dxgi_capture_source_test)

add_executable(win32_input_sink_source_test win32_input_sink_source_test.cpp)
target_compile_definitions(win32_input_sink_source_test PRIVATE
  DK_WIN32_INPUT_SINK_SOURCE_PATH="${PROJECT_SOURCE_DIR}/src/win32_input_sink.cpp")
add_test(NAME win32_input_sink_source_test COMMAND win32_input_sink_source_test)

add_executable(main_win32_source_test main_win32_source_test.cpp)
target_compile_definitions(main_win32_source_test PRIVATE
  DK_MAIN_WIN32_SOURCE_PATH="${PROJECT_SOURCE_DIR}/src/main_win32.cpp")
add_test(NAME main_win32_source_test COMMAND main_win32_source_test)

add_executable(app_source_test app_source_test.cpp)
target_compile_definitions(app_source_test PRIVATE
  DK_APP_SOURCE_PATH="${PROJECT_SOURCE_DIR}/src/app.cpp")
add_test(NAME app_source_test COMMAND app_source_test)

add_executable(config_source_test config_source_test.cpp)
target_compile_definitions(config_source_test PRIVATE
  DK_CONFIG_SOURCE_PATH="${PROJECT_SOURCE_DIR}/src/config.cpp")
add_test(NAME config_source_test COMMAND config_source_test)

add_executable(input_sink_portable_test input_sink_portable_test.cpp)
target_include_directories(input_sink_portable_test PRIVATE "${PROJECT_SOURCE_DIR}/include")
add_test(NAME input_sink_portable_test COMMAND input_sink_portable_test)

target_include_directories(ocr_preprocessor_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(candidate_detector_test PRIVATE ${OpenCV_LIBS})
target_compile_definitions(candidate_detector_test PRIVATE
  DK_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_compile_definitions(ocr_recognizer_test PRIVATE
  DK_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  DK_MODEL_PATH="${PROJECT_SOURCE_DIR}/assets/models/en_PP-OCRv5_rec_mobile_infer.onnx"
  DK_DICTIONARY_PATH="${PROJECT_SOURCE_DIR}/assets/models/ppocrv5_en_dict.txt")
