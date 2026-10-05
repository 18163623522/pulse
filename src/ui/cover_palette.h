#pragma once
// Colours of a cover picture for the Quick Look audio waveform: up to three
// distinct, reasonably saturated hues, ordered by where they sit on the
// cover from left to right, so the waveform's gradient echoes the artwork.
#include <cstdint>

namespace pulse::ui {

struct CoverPalette {
    bool valid = false;        // false for grey / black-and-white / empty covers
    uint32_t colors[3]{};      // 0xRRGGBB, gradient stops left to right
    bool operator==(const CoverPalette&) const = default;
};

// bgra: premultiplied BGRA rows (as the preview host sends them). Samples at
// most 48 x 48 points, so it is cheap enough for every decoded cover.
CoverPalette ComputeCoverPalette(const uint8_t* bgra, uint32_t width, uint32_t height, uint32_t stride);

// A stop adjusted for the theme: lighter on dark, deeper on light, never grey.
uint32_t CoverPaletteThemed(uint32_t rgb, bool dark);

} // namespace pulse::ui
