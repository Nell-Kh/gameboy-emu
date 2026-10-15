#pragma once

// Screenshot tests: compare a frame with a reference PNG from the c-sp test
// ROM collection, in shades 0-3, so the result never depends on the colours a
// frontend picks (ADR-015).

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/ppu.h"

namespace test {

[[nodiscard]] std::vector<std::uint8_t> read_file(const std::string& path);

// Passes if `frame` matches `reference_png` (160x144, using only the four DMG
// greys) pixel for pixel. On a mismatch the frame is written next to the
// reference as `<name>-actual.png` and the failure says how many pixels differ
// and where the first one is; on a match any old `-actual.png` is removed, so a
// stale one never lingers.
[[nodiscard]] testing::AssertionResult matches_screenshot(const core::Ppu::Frame& frame,
                                                          const std::string& reference_png);

}  // namespace test
