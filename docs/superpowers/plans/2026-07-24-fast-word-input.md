# Fast Word Input Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a native Windows utility that recognizes one or two large falling English targets inside a user-selected game region and sends only their `A`–`Z` characters with minimal latency.

**Architecture:** A C++20 pipeline captures a calibrated screen region with DXGI Desktop Duplication, locates large text rows with OpenCV, recognizes each cropped row with a PP-OCRv5 ONNX model, confirms candidates across two frames, and injects one safe `SendInput` batch. Platform-independent normalization, tracking, detection, OCR decoding, and orchestration are isolated from Win32 capture, focus, hotkey, selection, and input adapters.

**Tech Stack:** C++20, CMake 3.28+, Visual Studio 2022/MSVC, vcpkg manifest mode, OpenCV 4.x, ONNX Runtime 1.x CPU, nlohmann-json 3.x, Catch2 3.x, Win32, D3D11, DXGI 1.2.

## Global Constraints

- Target Windows 10/11 x64 desktop applications; do not target UWP.
- Build with MSVC and the `x64-windows` vcpkg triplet.
- Use C++20 and compile the application as Unicode.
- Keep the work loop allocation-light; reuse capture, preprocessing, and tensor buffers after initialization.
- Never save captured frames during normal operation.
- Default to dry-run mode; live input requires `live_input=true` in configuration and an explicit `F8` start.
- Send input only while the calibrated game window is the foreground window.
- Normalize OCR output to uppercase ASCII and remove every character outside `A`–`Z`.
- Do not send `Enter`, spaces, punctuation, or modifier keys.
- Confirm the same normalized text on two consecutive frames before sending it.
- When two targets are confirmed, select the one with the larger bottom `Y` coordinate.
- Keep `F8` as the immediate start/stop key and `F7` as region recalibration.
- Pin vcpkg to baseline `40f3c709db80acf154ac4b17a1f83c564ebd022e`.
- Use `en_PP-OCRv5_rec_mobile_infer.onnx` from RapidOCR model set `v3.5.0`, SHA-256 `c3461add59bb4323ecba96a492ab75e06dda42467c9e3d0c18db5d1d21924be8`.
- Use `ppocrv5_en_dict.txt`, SHA-256 `e025a66d31f327ba0c232e03f407ae8d105e1e709e7ccb3f408aa778c24e70d6`.
- The shipped ZIP must contain the executable, required DLLs, ONNX model, dictionary, default config, and Russian quick-start guide; it must not require Python.

## Planned File Structure

```text
.
├── .github/workflows/windows.yml           # Windows build and test gate
├── .gitignore
├── CMakeLists.txt                          # Targets, dependencies, install rules
├── CMakePresets.json                       # VS 2022 debug/release presets
├── vcpkg.json                              # Reproducible dependencies
├── assets/models/README.md                 # Model provenance and checksums
├── config/default.json                     # Safe default runtime settings
├── include/dk/
│   ├── app.hpp                             # Pipeline orchestration
│   ├── candidate_detector.hpp              # OpenCV candidate locator
│   ├── config.hpp                          # JSON-backed runtime settings
│   ├── ctc_decoder.hpp                     # OCR output decoding
│   ├── dxgi_capture.hpp                    # Desktop Duplication adapter
│   ├── frame_source.hpp                    # Capture interface
│   ├── hotkeys.hpp                         # F7/F8 registration
│   ├── input_sink.hpp                      # Input interface
│   ├── metrics.hpp                         # Latency aggregation
│   ├── ocr_recognizer.hpp                  # ONNX line recognizer
│   ├── region_selector.hpp                 # Mouse calibration overlay
│   ├── target_tracker.hpp                  # Confirmation, priority, dedupe
│   ├── text_normalizer.hpp                 # A–Z-only normalization
│   ├── types.hpp                           # Shared value types
│   ├── window_locator.hpp                  # Game HWND and client bounds
│   └── win32_input_sink.hpp                # SendInput implementation
├── scripts/fetch-models.ps1                # Pinned model downloader
├── src/                                    # One implementation per header
├── tests/
│   ├── fixtures/game_single_hyperstone.jpg # User-provided reference frame
│   ├── app_test.cpp
│   ├── candidate_detector_test.cpp
│   ├── config_test.cpp
│   ├── ctc_decoder_test.cpp
│   ├── ocr_recognizer_test.cpp
│   ├── target_tracker_test.cpp
│   └── text_normalizer_test.cpp
└── README.ru.md                            # Build, calibration, safe operation
```

---

### Task 1: Build Skeleton, Shared Types, and Text Normalization

**Files:**
- Create: `.gitignore`
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`
- Create: `vcpkg.json`
- Create: `include/dk/types.hpp`
- Create: `include/dk/text_normalizer.hpp`
- Create: `src/text_normalizer.cpp`
- Create: `tests/CMakeLists.txt`
- Create: `tests/text_normalizer_test.cpp`

**Interfaces:**
- Produces: `dk::Box`, `dk::TextCandidate`, and `std::string dk::normalize_for_input(std::string_view)`.
- Consumes: no project interfaces.

- [ ] **Step 1: Add the failing normalization tests**

```cpp
// tests/text_normalizer_test.cpp
#include <catch2/catch_test_macros.hpp>
#include "dk/text_normalizer.hpp"

TEST_CASE("normalization keeps only uppercase ASCII letters") {
    CHECK(dk::normalize_for_input("HYPERSTONE") == "HYPERSTONE");
    CHECK(dk::normalize_for_input("Don't panic!") == "DONTPANIC");
    CHECK(dk::normalize_for_input("ROCK-'N'-ROLL") == "ROCKNROLL");
    CHECK(dk::normalize_for_input(" two words ") == "TWOWORDS");
    CHECK(dk::normalize_for_input("123 -- !") == "");
}
```

- [ ] **Step 2: Add the build manifests and verify the test fails**

Use vcpkg baseline `40f3c709db80acf154ac4b17a1f83c564ebd022e` with dependencies `opencv4` (default features off; `jpeg` and `png` on), `onnxruntime`, `nlohmann-json`, and `catch2`.

```json
{
  "name": "dota-keyboard",
  "version-string": "0.1.0",
  "builtin-baseline": "40f3c709db80acf154ac4b17a1f83c564ebd022e",
  "dependencies": [
    { "name": "opencv4", "default-features": false, "features": ["jpeg", "png"] },
    "onnxruntime",
    "nlohmann-json",
    "catch2"
  ]
}
```

The root target layout must be:

```cmake
cmake_minimum_required(VERSION 3.28)
project(dota_keyboard VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(BUILD_TESTING "Build tests" ON)

add_library(dk_core src/text_normalizer.cpp)
target_include_directories(dk_core PUBLIC include)
if(MSVC)
  target_compile_options(dk_core PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(dk_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()
```

Use these presets verbatim so configure, build, test, install, and package commands agree on directories and configurations:

```json
{
  "version": 6,
  "cmakeMinimumRequired": { "major": 3, "minor": 28, "patch": 0 },
  "configurePresets": [
    {
      "name": "windows-base",
      "hidden": true,
      "generator": "Visual Studio 17 2022",
      "architecture": "x64",
      "toolchainFile": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake",
      "cacheVariables": {
        "VCPKG_TARGET_TRIPLET": "x64-windows",
        "BUILD_TESTING": "ON"
      }
    },
    {
      "name": "windows-debug",
      "inherits": "windows-base",
      "binaryDir": "${sourceDir}/build/windows-debug"
    },
    {
      "name": "windows-release",
      "inherits": "windows-base",
      "binaryDir": "${sourceDir}/build/windows-release"
    }
  ],
  "buildPresets": [
    { "name": "windows-debug", "configurePreset": "windows-debug", "configuration": "Debug" },
    { "name": "windows-release", "configurePreset": "windows-release", "configuration": "Release" }
  ],
  "testPresets": [
    {
      "name": "windows-debug",
      "configurePreset": "windows-debug",
      "configuration": "Debug",
      "output": { "outputOnFailure": true }
    },
    {
      "name": "windows-release",
      "configurePreset": "windows-release",
      "configuration": "Release",
      "output": { "outputOnFailure": true }
    }
  ],
  "packagePresets": [
    {
      "name": "windows-release",
      "configurePreset": "windows-release",
      "configuration": "Release",
      "generators": ["ZIP"]
    }
  ]
}
```

Use one discoverable Catch2 executable per focused source:

```cmake
# tests/CMakeLists.txt
find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

function(dk_add_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE dk_core Catch2::Catch2WithMain)
  catch_discover_tests(${name} TEST_PREFIX "${name}::")
endfunction()

dk_add_test(text_normalizer_test text_normalizer_test.cpp)
```

```gitignore
# .gitignore
/build/
/dist/
/config.json
/assets/models/*.onnx
/assets/models/*.txt
```

Run on Windows:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug -R text_normalizer --output-on-failure
```

Expected: compilation fails because `dk/text_normalizer.hpp` does not exist.

- [ ] **Step 3: Implement shared types and normalization**

```cpp
// include/dk/types.hpp
#pragma once
#include <string>

namespace dk {
struct Box {
    int x{};
    int y{};
    int width{};
    int height{};
    [[nodiscard]] int right() const noexcept { return x + width; }
    [[nodiscard]] int bottom() const noexcept { return y + height; }
    [[nodiscard]] float center_x() const noexcept { return x + width * 0.5F; }
    [[nodiscard]] float center_y() const noexcept { return y + height * 0.5F; }
    bool operator==(const Box&) const = default;
};

struct TextCandidate {
    std::string raw_text;
    std::string normalized_text;
    float confidence{};
    Box bounds;
};
}  // namespace dk
```

```cpp
// include/dk/text_normalizer.hpp
#pragma once
#include <string>
#include <string_view>

namespace dk {
[[nodiscard]] std::string normalize_for_input(std::string_view text);
}
```

```cpp
// src/text_normalizer.cpp
#include "dk/text_normalizer.hpp"

namespace dk {
std::string normalize_for_input(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const unsigned char ch : text) {
        if (ch >= 'a' && ch <= 'z') result.push_back(static_cast<char>(ch - 'a' + 'A'));
        else if (ch >= 'A' && ch <= 'Z') result.push_back(static_cast<char>(ch));
    }
    return result;
}
}
```

- [ ] **Step 4: Run the focused and full test suites**

Run:

```powershell
ctest --preset windows-debug -R text_normalizer --output-on-failure
ctest --preset windows-debug --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add .gitignore CMakeLists.txt CMakePresets.json vcpkg.json include/dk/types.hpp include/dk/text_normalizer.hpp src/text_normalizer.cpp tests
git commit -m "build: bootstrap native word input project"
```

---

### Task 2: Two-Frame Confirmation, Lower-Target Priority, and Dedupe

**Files:**
- Create: `include/dk/target_tracker.hpp`
- Create: `src/target_tracker.cpp`
- Create: `tests/target_tracker_test.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `dk::TextCandidate`.
- Produces: `dk::TrackerConfig`, `dk::TargetTracker::update`, and `dk::TargetTracker::mark_sent`.

- [ ] **Step 1: Write failing behavioral tests**

```cpp
// tests/target_tracker_test.cpp
#include <catch2/catch_test_macros.hpp>
#include "dk/target_tracker.hpp"

using dk::Box;
using dk::TextCandidate;

static TextCandidate word(std::string text, int y, int x = 100) {
    return {text, text, 0.95F, Box{x, y, 240, 48}};
}

TEST_CASE("tracker requires two adjacent frames") {
    dk::TargetTracker tracker({.confirm_frames = 2, .max_center_distance_px = 90.0F,
                               .unlock_missing_frames = 2});
    CHECK_FALSE(tracker.update({word("FIRST", 100)}));
    const auto ready = tracker.update({word("FIRST", 108)});
    REQUIRE(ready);
    CHECK(ready->normalized_text == "FIRST");
}

TEST_CASE("tracker chooses the lower confirmed target") {
    dk::TargetTracker tracker;
    tracker.update({word("HIGH", 100), word("LOW", 500)});
    const auto ready = tracker.update({word("HIGH", 106), word("LOW", 508)});
    REQUIRE(ready);
    CHECK(ready->normalized_text == "LOW");
}

TEST_CASE("sent target stays locked until missing") {
    dk::TargetTracker tracker;
    tracker.update({word("AGAIN", 300)});
    auto ready = tracker.update({word("AGAIN", 306)});
    REQUIRE(ready);
    tracker.mark_sent(*ready);
    CHECK_FALSE(tracker.update({word("AGAIN", 312)}));
    CHECK_FALSE(tracker.update({}));
    CHECK_FALSE(tracker.update({}));
    CHECK_FALSE(tracker.update({word("AGAIN", 100)}));
    CHECK(tracker.update({word("AGAIN", 106)}));
}
```

- [ ] **Step 2: Run and observe the missing tracker failure**

Run:

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug -R target_tracker --output-on-failure
```

Expected: compilation fails because `dk/target_tracker.hpp` is missing.

- [ ] **Step 3: Implement the tracker**

Use this public contract:

```cpp
// include/dk/target_tracker.hpp
#pragma once
#include <optional>
#include <span>
#include <vector>
#include "dk/types.hpp"

namespace dk {
struct TrackerConfig {
    int confirm_frames{2};
    float max_center_distance_px{90.0F};
    int unlock_missing_frames{2};
};

class TargetTracker {
public:
    explicit TargetTracker(TrackerConfig config = {});
    [[nodiscard]] std::optional<TextCandidate> update(std::span<const TextCandidate> candidates);
    void mark_sent(const TextCandidate& candidate);

private:
    struct Track { TextCandidate value; int seen_frames{1}; };
    struct Lock { TextCandidate value; int missing_frames{}; };
    TrackerConfig config_;
    std::vector<Track> previous_;
    std::vector<Lock> locks_;
};
}
```

Implementation rules:

1. Match current and previous items only when normalized text is equal and Euclidean center distance is at most `max_center_distance_px`.
2. Increment `seen_frames` for matches and reset it to one for new candidates.
3. Update each lock's `missing_frames`: reset to zero for a nearby equal candidate, otherwise increment; erase at `unlock_missing_frames`.
4. Exclude candidates matching an active lock.
5. Among candidates whose `seen_frames >= confirm_frames`, return the one with the greatest `bounds.bottom()`.
6. `mark_sent` adds exactly one lock for the sent text and location.

- [ ] **Step 4: Run tests**

Run:

```powershell
ctest --preset windows-debug -R target_tracker --output-on-failure
ctest --preset windows-debug --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add include/dk/target_tracker.hpp src/target_tracker.cpp tests/target_tracker_test.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: confirm and prioritize tracked targets"
```

---

### Task 3: Large Text Candidate Detection

**Files:**
- Create: `include/dk/candidate_detector.hpp`
- Create: `src/candidate_detector.cpp`
- Create: `tests/candidate_detector_test.cpp`
- Move: `2026-07-24 23.24.12.jpg` to `tests/fixtures/game_single_hyperstone.jpg`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: a BGRA, BGR, or grayscale `cv::Mat` and optional ignored normalized rectangles.
- Produces: `std::vector<dk::Box> dk::CandidateDetector::detect(const cv::Mat&) const`.

- [ ] **Step 1: Move the supplied image into the fixture directory and add failing tests**

Run:

```powershell
New-Item -ItemType Directory -Force tests/fixtures | Out-Null
Move-Item "2026-07-24 23.24.12.jpg" tests/fixtures/game_single_hyperstone.jpg
```

```cpp
// tests/candidate_detector_test.cpp
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <opencv2/imgcodecs.hpp>
#include "dk/candidate_detector.hpp"

TEST_CASE("detector finds the large HYPERSTONE row") {
    const cv::Mat image = cv::imread(DK_FIXTURE_DIR "/game_single_hyperstone.jpg");
    REQUIRE_FALSE(image.empty());
    const auto boxes = dk::CandidateDetector{}.detect(image);
    REQUIRE_FALSE(boxes.empty());

    const auto is_target = [](const dk::Box& b) {
        return b.center_x() > 250 && b.center_x() < 380 &&
               b.center_y() > 560 && b.center_y() < 640 &&
               b.width > 250 && b.height > 30;
    };
    CHECK(std::ranges::any_of(boxes, is_target));
}

TEST_CASE("detector rejects the small green duplicate") {
    const cv::Mat image = cv::imread(DK_FIXTURE_DIR "/game_single_hyperstone.jpg");
    const auto boxes = dk::CandidateDetector{}.detect(image);
    CHECK(std::ranges::none_of(boxes, [](const dk::Box& b) {
        return b.center_y() > 525 && b.center_y() < 570 && b.height < 30;
    }));
}
```

Register the test target with:

```cmake
dk_add_test(candidate_detector_test candidate_detector_test.cpp)
target_link_libraries(candidate_detector_test PRIVATE ${OpenCV_LIBS})
target_compile_definitions(candidate_detector_test PRIVATE
  DK_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
```

- [ ] **Step 2: Run and observe failure**

Run:

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug -R candidate_detector --output-on-failure
```

Expected: compilation fails because `dk/candidate_detector.hpp` is missing.

- [ ] **Step 3: Implement detector configuration and extraction**

```cpp
// include/dk/candidate_detector.hpp
#pragma once
#include <vector>
#include <opencv2/core.hpp>
#include "dk/types.hpp"

namespace dk {
struct DetectorConfig {
    int luminance_threshold{145};
    float min_char_height_ratio{0.025F};
    float max_char_height_ratio{0.075F};
    float baseline_tolerance_ratio{0.30F};
    float max_gap_ratio{1.50F};
    int min_components_per_line{3};
    int crop_padding_px{6};
    std::vector<cv::Rect2f> ignored_regions;
};

class CandidateDetector {
public:
    explicit CandidateDetector(DetectorConfig config = {});
    [[nodiscard]] virtual std::vector<Box> detect(const cv::Mat& frame) const;
    virtual ~CandidateDetector() = default;
private:
    DetectorConfig config_;
};
}
```

Implementation must:

1. Convert BGRA with `cv::COLOR_BGRA2GRAY`, BGR with `cv::COLOR_BGR2GRAY`, or reuse a one-channel frame.
2. Zero all configured ignored rectangles after scaling normalized coordinates by frame width and height.
3. Apply `cv::threshold(gray, mask, luminance_threshold, 255, cv::THRESH_BINARY)`.
4. Call `cv::connectedComponentsWithStats`.
5. Keep components whose height is within the configured relative range, width is no more than `1.4 * height`, and pixel area is at least `0.06 * width * height`.
6. Sort components by vertical center, then horizontal position.
7. Group components when their vertical centers differ by at most `baseline_tolerance_ratio * median_height` and horizontal gap is at most `max_gap_ratio * median_height`.
8. Keep groups with at least `min_components_per_line`, merge their boxes, add padding, and clamp to the frame.
9. Sort final boxes by `bottom()` descending.

Do not use an OCR detector model in this task.

- [ ] **Step 4: Tune only against explicit fixture assertions**

Run:

```powershell
ctest --preset windows-debug -R candidate_detector --output-on-failure
```

Expected: both fixture tests pass. If a threshold changes, add it to `DetectorConfig`; do not hard-code image-specific coordinates.

- [ ] **Step 5: Run the full suite and commit**

```powershell
ctest --preset windows-debug --output-on-failure
```

```bash
git add CMakeLists.txt tests/CMakeLists.txt include/dk/candidate_detector.hpp src/candidate_detector.cpp tests/candidate_detector_test.cpp tests/fixtures/game_single_hyperstone.jpg
git commit -m "feat: detect large game text candidates"
```

---

### Task 4: PP-OCRv5 Model Fetching, CTC Decoding, and Line Recognition

**Files:**
- Create: `scripts/fetch-models.ps1`
- Create: `assets/models/README.md`
- Create: `include/dk/ctc_decoder.hpp`
- Create: `src/ctc_decoder.cpp`
- Create: `include/dk/ocr_recognizer.hpp`
- Create: `src/ocr_recognizer.cpp`
- Create: `tests/ctc_decoder_test.cpp`
- Create: `tests/ocr_recognizer_test.cpp`
- Modify: `.gitignore`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: candidate crops, pinned ONNX model, and dictionary.
- Produces: `dk::CtcResult dk::decode_ctc(...)` and `dk::OcrResult dk::OcrRecognizer::recognize(const cv::Mat&)`.

- [ ] **Step 1: Write failing CTC tests**

```cpp
// tests/ctc_decoder_test.cpp
#include <catch2/catch_test_macros.hpp>
#include "dk/ctc_decoder.hpp"

TEST_CASE("CTC decoder removes blanks and repeated classes") {
    const std::vector<std::string> chars{"", "A", "B", "!", " "};
    const std::vector<int64_t> ids{0, 1, 1, 0, 2, 3, 4};
    const std::vector<float> scores{.9F, .95F, .8F, .9F, .92F, .88F, .9F};
    const auto result = dk::decode_ctc(ids, scores, chars, 0);
    CHECK(result.text == "AB! ");
    CHECK(result.confidence == Catch::Approx((.95F + .92F + .88F + .9F) / 4.0F));
}
```

- [ ] **Step 2: Implement and verify CTC decoding**

```cpp
// include/dk/ctc_decoder.hpp
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dk {
struct CtcResult { std::string text; float confidence{}; };
[[nodiscard]] CtcResult decode_ctc(
    std::span<const int64_t> class_ids,
    std::span<const float> class_scores,
    std::span<const std::string> dictionary,
    int64_t blank_id = 0);
}
```

`decode_ctc` must reject different input lengths, skip blank classes, collapse consecutive repeated nonblank classes, bounds-check dictionary indexes, and average only emitted-class scores.

Run:

```powershell
ctest --preset windows-debug -R ctc_decoder --output-on-failure
```

Expected: pass.

- [ ] **Step 3: Add the pinned downloader**

```powershell
# scripts/fetch-models.ps1
$ErrorActionPreference = "Stop"
$modelDir = Join-Path $PSScriptRoot "..\assets\models"
New-Item -ItemType Directory -Force -Path $modelDir | Out-Null

$files = @(
  @{
    Name = "en_PP-OCRv5_rec_mobile_infer.onnx"
    Url = "https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.5.0/onnx/PP-OCRv5/rec/en_PP-OCRv5_rec_mobile_infer.onnx"
    Sha256 = "c3461add59bb4323ecba96a492ab75e06dda42467c9e3d0c18db5d1d21924be8"
  },
  @{
    Name = "ppocrv5_en_dict.txt"
    Url = "https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.5.0/paddle/PP-OCRv5/rec/en_PP-OCRv5_rec_mobile_infer/ppocrv5_en_dict.txt"
    Sha256 = "e025a66d31f327ba0c232e03f407ae8d105e1e709e7ccb3f408aa778c24e70d6"
  }
)

foreach ($file in $files) {
  $destination = Join-Path $modelDir $file.Name
  Invoke-WebRequest -Uri $file.Url -OutFile $destination
  $actual = (Get-FileHash -Algorithm SHA256 $destination).Hash.ToLowerInvariant()
  if ($actual -ne $file.Sha256) {
    Remove-Item $destination
    throw "Checksum mismatch for $($file.Name): $actual"
  }
}
```

Ignore the two downloaded files in Git, document their RapidOCR/PaddleOCR provenance and Apache-2.0 model licensing in `assets/models/README.md`, then run:

```powershell
pwsh -File scripts/fetch-models.ps1
```

Expected: both files exist and both hashes match.

- [ ] **Step 4: Write the failing recognizer integration test**

```cpp
// tests/ocr_recognizer_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <opencv2/imgcodecs.hpp>
#include "dk/ocr_recognizer.hpp"
#include "dk/text_normalizer.hpp"

TEST_CASE("recognizer reads the supplied target crop") {
    const cv::Mat image = cv::imread(DK_FIXTURE_DIR "/game_single_hyperstone.jpg");
    REQUIRE_FALSE(image.empty());
    const cv::Mat crop = image(cv::Rect{140, 565, 350, 65});
    dk::OcrRecognizer recognizer(DK_MODEL_PATH, DK_DICTIONARY_PATH);
    const auto result = recognizer.recognize(crop);
    CHECK(dk::normalize_for_input(result.text) == "HYPERSTONE");
    CHECK(result.confidence >= 0.70F);
}
```

Register the target with paths supplied by CMake rather than hard-coded inside C++:

```cmake
dk_add_test(ocr_recognizer_test ocr_recognizer_test.cpp)
target_compile_definitions(ocr_recognizer_test PRIVATE
  DK_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  DK_MODEL_PATH="${PROJECT_SOURCE_DIR}/assets/models/en_PP-OCRv5_rec_mobile_infer.onnx"
  DK_DICTIONARY_PATH="${PROJECT_SOURCE_DIR}/assets/models/ppocrv5_en_dict.txt")
```

Run:

```powershell
cmake --build --preset windows-debug
ctest --preset windows-debug -R ocr_recognizer --output-on-failure
```

Expected: fail because `OcrRecognizer` is missing.

- [ ] **Step 5: Implement CPU line recognition**

```cpp
// include/dk/ocr_recognizer.hpp
#pragma once
#include <filesystem>
#include <memory>
#include <opencv2/core.hpp>

namespace dk {
struct OcrResult { std::string text; float confidence{}; };

class LineRecognizer {
public:
    virtual ~LineRecognizer() = default;
    [[nodiscard]] virtual OcrResult recognize(const cv::Mat& line) = 0;
};

class OcrRecognizer final : public LineRecognizer {
public:
    OcrRecognizer(const std::filesystem::path& model, const std::filesystem::path& dictionary);
    ~OcrRecognizer();
    OcrRecognizer(OcrRecognizer&&) noexcept;
    OcrRecognizer& operator=(OcrRecognizer&&) noexcept;
    [[nodiscard]] OcrResult recognize(const cv::Mat& line) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
```

The implementation must:

1. Create one process-lifetime `Ort::Env`, CPU `Ort::SessionOptions`, `ORT_ENABLE_ALL`, one intra-op thread, and one `Ort::Session`.
2. Read input/output names and shapes from the session rather than hard-code node names.
3. Load the dictionary as UTF-8 lines and construct the class list required by the model: blank at index zero, dictionary entries, then a trailing space only if output class count requires it.
4. Convert the crop to RGB, resize to height 48 while preserving aspect ratio, clamp width to 320, right-pad with zeros, normalize each channel with `(value / 255.0F - 0.5F) / 0.5F`, and write NCHW floats into a reused buffer.
5. Create a CPU tensor with shape `[1, 3, 48, padded_width]`.
6. Call `Ort::Session::Run`.
7. Interpret output shape `[1, time_steps, classes]`, select the maximum class at each step, apply softmax only when rows are not already probabilities, and call `decode_ctc`.
8. Return an empty, zero-confidence result for an empty crop; throw a descriptive exception for incompatible model shapes.

- [ ] **Step 6: Verify recognition and commit**

Run:

```powershell
ctest --preset windows-debug -R "ctc_decoder|ocr_recognizer" --output-on-failure
ctest --preset windows-debug --output-on-failure
```

Expected: all tests pass and the fixture reads `HYPERSTONE`.

```bash
git add .gitignore CMakeLists.txt tests/CMakeLists.txt scripts/fetch-models.ps1 assets/models/README.md include/dk/ctc_decoder.hpp src/ctc_decoder.cpp include/dk/ocr_recognizer.hpp src/ocr_recognizer.cpp tests/ctc_decoder_test.cpp tests/ocr_recognizer_test.cpp
git commit -m "feat: recognize candidate rows with PP-OCRv5"
```

---

### Task 5: Configuration, Window Binding, and Mouse Region Calibration

**Files:**
- Create: `config/default.json`
- Create: `include/dk/config.hpp`
- Create: `src/config.cpp`
- Create: `include/dk/window_locator.hpp`
- Create: `src/window_locator_win32.cpp`
- Create: `include/dk/region_selector.hpp`
- Create: `src/region_selector_win32.cpp`
- Create: `tests/config_test.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `dk::AppConfig`, `load_config`, `save_config`, `WindowLocator::foreground`, `WindowLocator::client_screen_bounds`, and `RegionSelector::select`.
- Consumes: `dk::Box`.

- [ ] **Step 1: Write failing configuration tests**

```cpp
// tests/config_test.cpp
#include <catch2/catch_test_macros.hpp>
#include "dk/config.hpp"

TEST_CASE("default configuration is safe") {
    const auto config = dk::AppConfig::defaults();
    CHECK_FALSE(config.live_input);
    CHECK(config.min_ocr_confidence == Catch::Approx(0.80F));
    CHECK(config.tracker.confirm_frames == 2);
    CHECK(config.hotkeys.calibrate == 0x76);  // F7
    CHECK(config.hotkeys.toggle == 0x77);     // F8
}

TEST_CASE("region round-trips in client-relative coordinates") {
    auto config = dk::AppConfig::defaults();
    config.window_title = L"Dota 2";
    config.region = dk::Box{100, 120, 900, 700};
    const auto json = dk::serialize_config(config);
    const auto restored = dk::parse_config(json);
    CHECK(restored.window_title == L"Dota 2");
    CHECK(restored.region == dk::Box{100, 120, 900, 700});
}
```

- [ ] **Step 2: Implement JSON configuration and validation**

`AppConfig` must include:

```cpp
struct HotkeyConfig { unsigned calibrate{0x76}; unsigned toggle{0x77}; };
struct AppConfig {
    std::wstring window_title;
    Box region;
    bool region_configured{false};
    bool live_input{false};
    float min_ocr_confidence{0.80F};
    int inter_key_delay_us{0};
    DetectorConfig detector;
    TrackerConfig tracker;
    HotkeyConfig hotkeys;
    static AppConfig defaults();
};
```

Serialize the title as UTF-8 JSON, reject nonpositive configured region sizes, confidence outside `[0,1]`, negative delays, and hotkey collisions. Write through a temporary sibling file and atomically replace the old config.

Run:

```powershell
ctest --preset windows-debug -R config --output-on-failure
```

Expected: pass.

- [ ] **Step 3: Implement foreground window binding**

`WindowLocator::foreground()` must capture `GetForegroundWindow()`, window title, process ID, and client rectangle converted to screen coordinates with `ClientToScreen`. Reject desktop/shell windows, minimized windows, and zero-area clients. Later focus checks must compare `HWND`, not only title.

Add a `window_locator_smoke` executable that prints the active window title and client bounds. Run it with Notepad foreground and verify nonzero bounds.

- [ ] **Step 4: Implement the calibration overlay**

`RegionSelector::select(HWND owner, const Box& client_screen_bounds)` returns `std::optional<Box>` in client-relative coordinates.

The Win32 implementation must:

1. Register a private overlay window class once.
2. Create a borderless `WS_POPUP`, `WS_EX_TOPMOST | WS_EX_TOOLWINDOW` overlay covering only the client bounds.
3. Paint a translucent dark fill and a bright selection rectangle.
4. On `WM_LBUTTONDOWN`, capture the mouse and store the first point.
5. On `WM_MOUSEMOVE`, update and repaint the normalized rectangle.
6. On `WM_LBUTTONUP`, release capture and accept rectangles at least `100x100`.
7. Cancel on `Escape`, owner destruction, or focus loss.
8. Destroy the overlay and restore the original game window foreground state.

Manual check: press the smoke executable's calibration command, drag a rectangle, and verify printed coordinates are relative to the foreground client area.

- [ ] **Step 5: Run tests and commit**

```powershell
ctest --preset windows-debug --output-on-failure
```

```bash
git add config/default.json include/dk/config.hpp src/config.cpp include/dk/window_locator.hpp src/window_locator_win32.cpp include/dk/region_selector.hpp src/region_selector_win32.cpp tests/config_test.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: calibrate a client-relative game region"
```

---

### Task 6: DXGI Region Capture

**Files:**
- Create: `include/dk/frame_source.hpp`
- Create: `include/dk/dxgi_capture.hpp`
- Create: `src/dxgi_capture_win32.cpp`
- Create: `src/capture_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `FrameSource::next_frame()` and `DxgiCapture`.
- Consumes: a validated region in screen coordinates.

- [ ] **Step 1: Define a mockable frame source**

```cpp
// include/dk/frame_source.hpp
#pragma once
#include <chrono>
#include <optional>
#include <opencv2/core.hpp>

namespace dk {
struct CapturedFrame {
    cv::Mat bgra;
    std::chrono::steady_clock::time_point captured_at;
};
class FrameSource {
public:
    virtual ~FrameSource() = default;
    [[nodiscard]] virtual std::optional<CapturedFrame> next_frame() = 0;
};
}
```

- [ ] **Step 2: Implement DXGI initialization and region validation**

`DxgiCapture(Box screen_region)` must:

1. Create a D3D11 device with `D3D11_CREATE_DEVICE_BGRA_SUPPORT`.
2. Enumerate adapters and outputs and select the single output containing the entire region.
3. Reject regions spanning monitors with a descriptive error.
4. Call `IDXGIOutput1::DuplicateOutput`.
5. Allocate one CPU-readable staging texture exactly the region size using `DXGI_FORMAT_B8G8R8A8_UNORM`.
6. Preallocate the returned `cv::Mat`.

Link `d3d11`, `dxgi`, and `user32`.

- [ ] **Step 3: Implement nonblocking frame acquisition**

`next_frame()` must:

1. Call `AcquireNextFrame(5, ...)`.
2. Return `std::nullopt` for `DXGI_ERROR_WAIT_TIMEOUT`.
3. Recreate duplication after `DXGI_ERROR_ACCESS_LOST`.
4. Query the acquired resource as `ID3D11Texture2D`.
5. `CopySubresourceRegion` only the selected region into the staging texture.
6. Map with `D3D11_MAP_READ`, copy row-by-row while honoring `RowPitch`, unmap, and always call `ReleaseFrame`.
7. Return a `CapturedFrame` whose `cv::Mat` owns or reuses memory that remains valid until the next call.

Use RAII guards for `ReleaseFrame` and `Unmap`; every early return must release both resources correctly.

- [ ] **Step 4: Add and run a Windows smoke benchmark**

`capture_smoke` accepts `x y width height frames`, captures 300 frames, and prints:

```text
frames=300 mean_ms=<value> p95_ms=<value> size=<width>x<height>
```

Run:

```powershell
.\build\windows-debug\src\capture_smoke.exe 100 100 800 600 300
```

Expected: 300 nonempty BGRA frames; no D3D debug-layer errors; capture mean below one display interval.

- [ ] **Step 5: Commit**

```bash
git add include/dk/frame_source.hpp include/dk/dxgi_capture.hpp src/dxgi_capture_win32.cpp src/capture_smoke.cpp CMakeLists.txt
git commit -m "feat: capture calibrated regions with DXGI"
```

---

### Task 7: Focus-Safe SendInput and Global Hotkeys

**Files:**
- Create: `include/dk/input_sink.hpp`
- Create: `include/dk/win32_input_sink.hpp`
- Create: `src/win32_input_sink.cpp`
- Create: `include/dk/hotkeys.hpp`
- Create: `src/hotkeys_win32.cpp`
- Create: `tests/input_sink_test.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `InputSink::send_letters`, `Win32InputSink`, and `Hotkeys::poll`.
- Consumes: a bound game `HWND` and normalized ASCII text.

- [ ] **Step 1: Define the interface and write validation tests**

```cpp
// include/dk/input_sink.hpp
#pragma once
#include <string_view>

namespace dk {
enum class SendStatus { sent, not_foreground, invalid_text, blocked, partial };
class InputSink {
public:
    virtual ~InputSink() = default;
    virtual SendStatus send_letters(std::string_view letters) = 0;
};
}
```

Test a platform-independent `validate_send_text` helper:

```cpp
CHECK(dk::validate_send_text("HYPERSTONE"));
CHECK_FALSE(dk::validate_send_text(""));
CHECK_FALSE(dk::validate_send_text("TWO WORDS"));
CHECK_FALSE(dk::validate_send_text("DON'T"));
CHECK_FALSE(dk::validate_send_text("abc"));
```

- [ ] **Step 2: Implement focus-safe batched input**

`Win32InputSink(HWND target, int inter_key_delay_us)` must:

1. Return `not_foreground` unless `GetForegroundWindow() == target`.
2. Reject anything outside nonempty `A`–`Z`.
3. With zero delay, build one `std::vector<INPUT>` containing key-down/key-up pairs and call `SendInput` once.
4. With a configured delay, send one pair at a time and wait with a high-resolution waitable timer; do not use a busy loop.
5. Return `sent` only when every requested event was accepted.
6. Include `GetLastError()` in the diagnostic for zero/partial results and mention UIPI when the target runs elevated.
7. Never send `Enter`, `Shift`, space, or punctuation.

- [ ] **Step 3: Implement F7/F8 registration**

Create a message-only Win32 window, call `RegisterHotKey` for configured virtual keys, and expose:

```cpp
enum class HotkeyEvent { none, calibrate, toggle, quit };
class Hotkeys {
public:
    Hotkeys(unsigned calibrate_vk, unsigned toggle_vk);
    [[nodiscard]] HotkeyEvent poll(std::chrono::milliseconds timeout);
};
```

Map `WM_HOTKEY` to `calibrate`/`toggle`, `WM_QUIT` to `quit`, unregister both keys in the destructor, and fail startup with a clear message if either key is already registered.

- [ ] **Step 4: Run tests and a Notepad smoke check**

In a dedicated smoke executable, bind Notepad, wait for `F8`, and send `TEST` once. Verify:

- foreground Notepad receives `TEST`;
- another foreground window receives nothing;
- `F7` and `F8` events print exactly once per key press.

- [ ] **Step 5: Commit**

```bash
git add include/dk/input_sink.hpp include/dk/win32_input_sink.hpp src/win32_input_sink.cpp include/dk/hotkeys.hpp src/hotkeys_win32.cpp tests/input_sink_test.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add guarded keyboard injection and hotkeys"
```

---

### Task 8: Pipeline Orchestration, Dry Run, and Latency Metrics

**Files:**
- Create: `include/dk/metrics.hpp`
- Create: `src/metrics.cpp`
- Create: `include/dk/app.hpp`
- Create: `src/app.cpp`
- Create: `src/main_win32.cpp`
- Create: `tests/app_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `FrameSource`, `CandidateDetector`, `OcrRecognizer`, `TargetTracker`, `InputSink`, `AppConfig`.
- Produces: `App::process_one_frame`, `App::run`, stage metrics, and `dota_keyboard.exe`.

- [ ] **Step 1: Write an end-to-end test with fakes**

```cpp
// tests/app_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <deque>
#include "dk/app.hpp"

class FakeFrameSource final : public dk::FrameSource {
public:
    explicit FakeFrameSource(int count) : remaining_(count) {}
    std::optional<dk::CapturedFrame> next_frame() override {
        if (remaining_-- <= 0) return std::nullopt;
        return dk::CapturedFrame{
            cv::Mat(720, 1000, CV_8UC4, cv::Scalar{}),
            std::chrono::steady_clock::now()
        };
    }
private:
    int remaining_;
};

class FakeDetector final : public dk::CandidateDetector {
public:
    explicit FakeDetector(std::vector<dk::Box> boxes) : boxes_(std::move(boxes)) {}
    std::vector<dk::Box> detect(const cv::Mat&) const override { return boxes_; }
private:
    std::vector<dk::Box> boxes_;
};

class FakeRecognizer final : public dk::LineRecognizer {
public:
    explicit FakeRecognizer(std::deque<dk::OcrResult> results) : results_(std::move(results)) {}
    dk::OcrResult recognize(const cv::Mat&) override {
        REQUIRE_FALSE(results_.empty());
        auto result = results_.front();
        results_.pop_front();
        return result;
    }
private:
    std::deque<dk::OcrResult> results_;
};

class FakeInputSink final : public dk::InputSink {
public:
    dk::SendStatus send_letters(std::string_view text) override {
        sent.emplace_back(text);
        return dk::SendStatus::sent;
    }
    std::vector<std::string> sent;
};

TEST_CASE("app confirms, normalizes, and sends one lower target") {
    auto config = dk::AppConfig::defaults();
    config.live_input = true;
    FakeFrameSource frames{2};
    FakeDetector detector{{{50, 100, 250, 48}, {80, 500, 330, 48}}};
    FakeRecognizer ocr{{
        {"DON'T PANIC!", .94F}, {"ROCK-'N'-ROLL", .96F},
        {"DON'T PANIC!", .94F}, {"ROCK-'N'-ROLL", .96F}
    }};
    FakeInputSink input;
    dk::App app(config, frames, detector, ocr, input);

    app.process_one_frame();
    app.process_one_frame();

    REQUIRE(input.sent.size() == 1);
    CHECK(input.sent.front() == "ROCKNROLL");
}

TEST_CASE("dry run recognizes but does not send") {
    auto config = dk::AppConfig::defaults();
    config.live_input = false;
    FakeFrameSource frames{2};
    FakeDetector detector{{{50, 300, 300, 48}}};
    FakeRecognizer ocr{{{"HYPERSTONE", .97F}, {"HYPERSTONE", .97F}}};
    FakeInputSink input;
    dk::App app(config, frames, detector, ocr, input);
    app.process_one_frame();
    app.process_one_frame();
    CHECK(input.sent.empty());
    REQUIRE(app.last_result());
    CHECK(app.last_result()->normalized_text == "HYPERSTONE");
}
```

- [ ] **Step 2: Implement metrics**

`LatencyMetrics` maintains the latest 512 samples for `capture`, `detect`, `ocr`, and `total`, using `steady_clock`. `summary()` returns count, mean, median, and p95 in milliseconds. Calculate p95 from a copied/sorted sample vector so collection remains constant time.

- [ ] **Step 3: Implement one-frame processing**

Use this exact application boundary:

```cpp
// include/dk/app.hpp
#pragma once
#include <optional>
#include "dk/candidate_detector.hpp"
#include "dk/config.hpp"
#include "dk/frame_source.hpp"
#include "dk/input_sink.hpp"
#include "dk/metrics.hpp"
#include "dk/ocr_recognizer.hpp"
#include "dk/target_tracker.hpp"

namespace dk {
class App {
public:
    App(const AppConfig& config, FrameSource& frames, CandidateDetector& detector,
        LineRecognizer& recognizer, InputSink& input);
    bool process_one_frame();
    [[nodiscard]] const std::optional<TextCandidate>& last_result() const noexcept;
    [[nodiscard]] const LatencyMetrics& metrics() const noexcept;
private:
    AppConfig config_;
    FrameSource& frames_;
    CandidateDetector& detector_;
    LineRecognizer& recognizer_;
    InputSink& input_;
    TargetTracker tracker_;
    LatencyMetrics metrics_;
    std::optional<TextCandidate> last_result_;
};
}
```

`App::process_one_frame()` must:

1. Request one frame; return cleanly on timeout.
2. Detect candidate boxes and crop each box without copying where possible.
3. Run recognition per crop.
4. Normalize OCR text and reject empty text or confidence below config.
5. Convert crop-relative boxes to selected-region coordinates.
6. Pass candidates to `TargetTracker`.
7. Log raw text, normalized text, confidence, box, and stage timings.
8. If a target is confirmed:
   - in dry run, log `[DRY] would type <TEXT>` and mark it sent in the tracker;
   - in live mode, call `InputSink::send_letters`;
   - mark sent only for `SendStatus::sent`;
   - stop live mode on `partial` or `blocked`.

- [ ] **Step 4: Implement the application loop**

`main_win32.cpp` must:

1. Load `config.json`, falling back to `config/default.json`.
2. Print `DRY RUN` or `LIVE INPUT ENABLED` prominently.
3. Bind the foreground game window on calibration and save its title plus client-relative region.
4. Recompute screen coordinates from the current client bounds before constructing/reconstructing capture.
5. Handle `F7` by stopping capture, showing the region selector, saving config, and rebuilding capture.
6. Handle `F8` by toggling stopped/running state; never silently change dry/live mode.
7. Stop when the window is destroyed, the region becomes invalid, input is blocked, or the console closes.
8. Print metric summaries every five seconds and on shutdown.

- [ ] **Step 5: Run tests**

Run:

```powershell
ctest --preset windows-debug -R app --output-on-failure
ctest --preset windows-debug --output-on-failure
```

Expected: all tests pass; fake input receives only normalized lower targets and never receives input in dry mode.

- [ ] **Step 6: Commit**

```bash
git add include/dk/metrics.hpp src/metrics.cpp include/dk/app.hpp src/app.cpp src/main_win32.cpp tests/app_test.cpp CMakeLists.txt
git commit -m "feat: assemble low-latency recognition pipeline"
```

---

### Task 9: Windows CI, Packaging, Documentation, and Real-Game Acceptance

**Files:**
- Create: `.github/workflows/windows.yml`
- Create: `README.ru.md`
- Modify: `CMakeLists.txt`
- Modify: `config/default.json`

**Interfaces:**
- Consumes: all application targets and assets.
- Produces: tested `dota-keyboard-<version>-windows-x64.zip`.

- [ ] **Step 1: Add Windows CI**

The workflow must run on `windows-2022`, cache vcpkg binary artifacts, execute:

```powershell
pwsh -File scripts/fetch-models.ps1
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release --output-on-failure
cmake --install build/windows-release --prefix dist
cpack --preset windows-release
```

Upload the generated ZIP and test results. Do not enable live input in CI.

- [ ] **Step 2: Add install and CPack rules**

Install:

- `dota_keyboard.exe`;
- runtime DLLs discovered by CMake;
- both model files;
- `config/default.json` as `config.json`;
- `README.ru.md`.

Use a ZIP generator and verify the archive contains no source, test fixture, vcpkg tree, or Python files.

- [ ] **Step 3: Write the Russian quick-start guide**

Document exact steps:

1. Unpack ZIP.
2. Start the game in borderless windowed mode.
3. Start the utility at the same integrity level as the game.
4. Focus the game, press `F7`, and drag only the mini-game region.
5. Leave `live_input=false`, press `F8`, and compare console recognition.
6. Stop with `F8`.
7. Set `live_input=true` only after dry-run validation.
8. Restart, focus the game, and press `F8`.

Include troubleshooting for missing model/DLL, hotkey collision, `DXGI_ERROR_ACCESS_LOST`, multi-monitor region crossing, elevated game/UIPI, low confidence, duplicate targets, and missed key events.

- [ ] **Step 4: Execute the real-game dry-run acceptance matrix**

Record a pass/fail table in `README.ru.md` or a linked release note for:

- one short word;
- one long word;
- phrase with spaces;
- apostrophe and hyphen;
- one target near each edge of the selected region;
- two targets at different heights;
- partially yellow/white target;
- focus switched away;
- `F8` emergency stop.

Required result: no HUD/micro-label input, normalized output is correct, lower target wins, focus loss prevents input, and `F8` stops before another send.

- [ ] **Step 5: Benchmark the live pipeline**

Collect at least 300 confirmed targets and report mean and p95 for capture, detect, OCR, and end-to-end processing. Acceptance target: p95 end-to-end processing at or below 40 ms after a frame becomes available. If it misses:

1. verify Release build;
2. verify only selected ROI is copied;
3. reuse all buffers and the ONNX session;
4. lower ONNX thread count contention;
5. profile before changing confirmation or confidence safety rules.

- [ ] **Step 6: Final verification**

Run:

```powershell
cmake --preset windows-release --fresh
cmake --build --preset windows-release
ctest --preset windows-release --output-on-failure
pwsh -File scripts/fetch-models.ps1
cmake --install build/windows-release --prefix dist
cpack --preset windows-release
```

Expected: clean configure, build, all tests pass, model hashes match, ZIP is created, and a clean Windows machine launches it without Python.

- [ ] **Step 7: Commit**

```bash
git add .github/workflows/windows.yml README.ru.md CMakeLists.txt config/default.json
git commit -m "docs: package and verify Windows word input utility"
```

## Primary References

- Microsoft Desktop Duplication API: <https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api>
- `IDXGIOutputDuplication::AcquireNextFrame`: <https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutputduplication-acquirenextframe>
- Win32 `SendInput`: <https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput>
- ONNX Runtime C++: <https://onnxruntime.ai/docs/get-started/with-cpp.html>
- OpenCV 4.13 documentation: <https://docs.opencv.org/4.13.0/>
- RapidOCR model list: <https://rapidai.github.io/RapidOCRDocs/latest/model_list/>
- PaddleOCR PP-OCRv5 English recognition model: <https://github.com/PaddlePaddle/PaddleOCR/blob/main/docs/version3.x/algorithm/PP-OCRv5/PP-OCRv5_multi_languages.en.md>
