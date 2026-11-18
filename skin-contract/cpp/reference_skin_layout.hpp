#pragma once
#include <algorithm>
#include <cstddef>
#include <cmath>

// Layout values are logical pixels. The host window converts physical pixels
// for its current DPI before using these helpers.
namespace reference_layout {
inline double factor(unsigned dpi, double scale) noexcept { return (dpi ? dpi : 96) / 96.0 * scale; }
inline int physical(int logical, unsigned dpi, double scale) noexcept { return static_cast<int>(std::lround(logical * factor(dpi, scale))); }
inline int logical(int physical_pixel, unsigned dpi, double scale) noexcept { return static_cast<int>(std::lround(physical_pixel / factor(dpi, scale))); }
constexpr int margin = 8;
constexpr int width = 128;
constexpr int pitch = 136;
// Wheel over the gain control adjusts gain; Shift remains available for
// horizontal scrolling. Coordinates are logical content pixels.
inline bool gain_wheel_hit(int within, int content_y, int view_y, bool shift) noexcept {
    return !shift && view_y >= 48 && content_y >= 136 && content_y < 168 && within >= 8 && within < 120;
}
inline double gain_wheel_delta(int steps, bool control) noexcept { return steps * (control ? 1.0 : 0.1); }
inline bool gain_drag_hit(int within, int content_y, int view_y) noexcept {
    return view_y >= 48 && content_y >= 136 && content_y < 168 && within >= 32 && within < 96;
}
inline double gain_drag_delta(int previous_y, int current_y) noexcept { return (previous_y - current_y) * 0.1; }
// The effect browser control doubles as an append target during cross-channel
// drags, including when every visible rack row is occupied.
inline bool effect_append_header_hit(int within, int content_y, int view_y) noexcept {
    return view_y >= 48 && content_y >= 176 && content_y < 208 && within >= 64 && within < 116;
}
inline int max_scroll(std::size_t count, int viewport) noexcept {
    const int content = count ? margin + static_cast<int>(count - 1) * pitch + width + margin : 0;
    return std::max(0, content - std::max(0, viewport));
}
inline int clamp(int scroll, std::size_t count, int viewport) noexcept {
    return std::clamp(scroll, 0, max_scroll(count, viewport));
}
inline int reveal(int scroll, std::size_t count, std::size_t index, int viewport) noexcept {
    if (index >= count || viewport <= 0) return clamp(scroll, count, viewport);
    const int left = margin + static_cast<int>(index) * pitch;
    const int right = left + width;
    if (left < scroll) scroll = left;
    else if (right > scroll + viewport) scroll = right - viewport;
    return clamp(scroll, count, viewport);
}
// Right-aligned channels use available spare width without changing the scroll range.
inline int aligned_left(std::size_t index, std::size_t count, std::size_t first_right, int viewport) noexcept {
    const int content = count ? margin * 2 + static_cast<int>(count - 1) * pitch + width : 0;
    const int gap = std::max(0, viewport - content);
    return margin + static_cast<int>(index) * pitch + (index >= first_right ? gap : 0);
}
inline int aligned_hit(int x, int scroll, std::size_t count, std::size_t first_right, int viewport) noexcept {
    for (std::size_t i = 0; i < count; ++i) {
        const int left = aligned_left(i, count, first_right, viewport) - scroll;
        if (x >= left && x < left + width) return static_cast<int>(i);
    }
    return -1;
}
inline int aligned_reveal(int scroll, std::size_t count, std::size_t first_right, std::size_t index, int viewport) noexcept {
    if (index >= count || viewport <= 0) return clamp(scroll, count, viewport);
    const int left = aligned_left(index, count, first_right, viewport);
    if (left < scroll) scroll = left;
    else if (left + width > scroll + viewport) scroll = left + width - viewport;
    return clamp(scroll, count, viewport);
}
// Toolbar remains fixed; content can scroll below it in short windows.
// Meter lanes sit between mute/solo and gain, outside both hit regions.
constexpr int meter_left = 8;
constexpr int meter_right = 120;
constexpr int meter_first_top = 128;
constexpr int meter_first_bottom = 131;
constexpr int meter_second_top = 132;
constexpr int meter_second_bottom = 135;
inline double meter_peak_level(double peak) noexcept {
    return peak > 0 ? std::clamp((20. * std::log10(peak) + 60.) / 72., 0., 1.) : 0.;
}
inline int max_vertical(int viewport) noexcept { return std::max(0, 552 - std::max(48, viewport)); }
inline int clamp_vertical(int scroll, int viewport) noexcept { return std::clamp(scroll, 0, max_vertical(viewport)); }
inline int reveal_vertical(int scroll, int top, int bottom, int viewport) noexcept {
    if (top - scroll < 48) scroll = top - 48;
    if (bottom - scroll > viewport) scroll = bottom - viewport;
    return clamp_vertical(scroll, viewport);
}
inline int anchor_coordinate(int position, int viewport) noexcept {
    return std::clamp(position, 0, std::max(0, viewport - 1));
}
inline int hit(int x, int scroll, std::size_t count) noexcept {
    const int logical = x + scroll - margin;
    if (logical < 0 || logical % pitch >= width) return -1;
    const auto index = static_cast<std::size_t>(logical / pitch);
    return index < count ? static_cast<int>(index) : -1;
}
}
