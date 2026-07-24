find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

function(dk_add_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE dk_core Catch2::Catch2WithMain)
  catch_discover_tests(${name} TEST_PREFIX "${name}::")
endfunction()

dk_add_test(text_normalizer_test text_normalizer_test.cpp)
dk_add_test(target_tracker_test target_tracker_test.cpp)
