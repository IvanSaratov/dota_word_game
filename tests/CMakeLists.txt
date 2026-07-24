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
target_include_directories(ocr_preprocessor_test PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(candidate_detector_test PRIVATE ${OpenCV_LIBS})
target_compile_definitions(candidate_detector_test PRIVATE
  DK_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_compile_definitions(ocr_recognizer_test PRIVATE
  DK_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  DK_MODEL_PATH="${PROJECT_SOURCE_DIR}/assets/models/en_PP-OCRv5_rec_mobile_infer.onnx"
  DK_DICTIONARY_PATH="${PROJECT_SOURCE_DIR}/assets/models/ppocrv5_en_dict.txt")
