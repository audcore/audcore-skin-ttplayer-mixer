#pragma once

#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "button_state.hpp"

namespace classic {
constexpr LONG width = 327;
constexpr std::array<LONG, 3> source_heights{141, 99, 124};
constexpr LONG channel_extra_height = 64;
constexpr std::array<LONG, 3> heights{141, 99, source_heights[2] + channel_extra_height};
constexpr COLORREF key = RGB(255, 0, 255);
constexpr COLORREF ink = RGB(233, 244, 247);
constexpr COLORREF pale = RGB(139, 186, 198);
constexpr COLORREF well = RGB(39, 67, 95);

enum class Action {
    open, minimize, close, play, stop, previous, next,
    settings, mute, effects, channels, bypass, remove_effect, add,
};

struct Button {
    Action action;
    RECT bounds;
    const wchar_t* asset;
    const wchar_t* label;
    ttplayer::skin::SkinButtonState state;
};

// TT.experience 5.7.9 Skin.xml at 96 DPI. Four-frame bitmap dimensions
// override XML padding for both drawing and hit testing.
inline std::array<std::vector<Button>, 3> buttons() {
    using A = Action;
    return {{
        {
            {A::open,     {199,3,226,23}, L"open", L"打开"},
            {A::minimize, {259,3,286,23}, L"minimize", L"−"},
            {A::close,    {288,3,316,23}, L"close", L"×"},
            {A::mute,     {294,76,306,87}, L"mute", L"M"},
            {A::channels, {216,91,273,108}, L"playlist", L"通道列表"},
            {A::effects,  {274,91,316,108}, L"equalizer", L"效果器"},
            {A::stop,     {9,113,65,134}, L"stop", L"■"},
            {A::previous, {66,113,126,134}, L"prev", L"◀◀"},
            {A::play,     {127,113,197,134}, L"play", L"▶"},
            {A::next,     {198,113,257,134}, L"next", L"▶▶"},
            {A::settings, {258,113,318,134}, L"browser", L"设置"},
        },
        {
            {A::bypass,   {199,3,226,23}, L"eq_enabled", L"旁通"},
            {A::remove_effect, {226,3,257,23}, L"reset", L"删除"},
            {A::add,      {257,3,285,23}, L"eq_profile", L"新建"},
            {A::close,    {289,3,316,23}, L"close2", L"×"},
        },
        {
            {A::close,    {289,3,316,23}, L"close2", L"×"},
        }
    }};
}

inline void fill(HDC dc, const RECT& rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

inline void text(HDC dc, const wchar_t* value, RECT rect, COLORREF color = ink,
                 UINT align = DT_LEFT) {
    SetTextColor(dc, color);
    DrawTextW(dc, value, -1, &rect, align | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
}

class Bitmap {
public:
    Bitmap() = default;
    Bitmap(const Bitmap&) = delete;
    Bitmap& operator=(const Bitmap&) = delete;
    ~Bitmap() { if (handle) DeleteObject(handle); }

    bool load(HMODULE module, const wchar_t* name) {
        std::wstring resource = L"CLASSIC_";
        for (; *name; ++name) resource += *name >= L'a' && *name <= L'z' ? *name - L'a' + L'A' : *name;
        const auto entry = FindResourceW(module, resource.c_str(), RT_RCDATA);
        if (!entry) return false;
        const DWORD bytes = SizeofResource(module, entry);
        const auto data = static_cast<const BYTE*>(LockResource(LoadResource(module, entry)));
        BITMAPFILEHEADER file{};
        BITMAPINFOHEADER header{};
        if (!data || bytes < sizeof(file) + sizeof(header)) return false;
        std::memcpy(&file, data, sizeof(file));
        std::memcpy(&header, data + sizeof(file), sizeof(header));
        if (file.bfType != 0x4d42 || header.biSize != sizeof(header) || header.biPlanes != 1 ||
            header.biWidth < 1 || header.biWidth > width || header.biHeight < 1 ||
            header.biHeight > heights[0] || header.biBitCount != 24 || header.biCompression != BI_RGB ||
            file.bfOffBits < sizeof(file) + sizeof(header) || file.bfOffBits > bytes) return false;
        const LONG stride = (header.biWidth * 3 + 3) & ~3;
        if (bytes - file.bfOffBits < static_cast<DWORD>(stride * header.biHeight)) return false;
        if (!create_pixels({header.biWidth,header.biHeight})) return false;
        const auto source = data + file.bfOffBits;
        for (LONG y = 0; y < size.cy; ++y) {
            const auto row = source + (size.cy - 1 - y) * stride;
            for (LONG x = 0; x < size.cx; ++x)
                pixels[y * size.cx + x] = row[3*x] | (row[3*x+1] << 8) | (row[3*x+2] << 16);
        }
        return true;
    }

    std::unique_ptr<Bitmap> expand_rows(LONG top, LONG bottom, LONG extra) const {
        auto expanded = std::make_unique<Bitmap>();
        if (!expanded->create_pixels({size.cx,size.cy+extra})) return {};
        const LONG middle = bottom-top;
        for (LONG y = 0; y < expanded->size.cy; ++y) {
            const LONG source_y = y < top ? y : y >= bottom+extra ? y-extra
                : top+(y-top)*middle/(middle+extra);
            std::copy_n(pixels+source_y*size.cx,size.cx,expanded->pixels+y*size.cx);
        }
        return expanded;
    }

    bool opaque(int x, int y) const {
        // 32-bit BGRX, key RGB channels only. No alpha blending is involved.
        return x >= 0 && y >= 0 && x < size.cx && y < size.cy &&
            (pixels[y * size.cx + x] & 0x00ffffff) != 0x00ff00ff;
    }

    COLORREF color_at(LONG x, LONG y) const {
        const DWORD pixel = pixels[y * size.cx + x];
        return RGB(pixel >> 16,pixel >> 8,pixel);
    }

    void copy_patch(HDC target, HDC source, const RECT& destination, LONG source_x) const {
        const auto previous = SelectObject(source,handle);
        BitBlt(target,destination.left,destination.top,destination.right-destination.left,
               destination.bottom-destination.top,source,source_x,destination.top,SRCCOPY);
        SelectObject(source,previous);
    }

    HRGN region(LONG scaled_width, LONG scaled_height) const {
        HRGN result = CreateRectRgn(0, 0, 0, 0);
        HRGN run = CreateRectRgn(0, 0, 0, 0);
        if (!result || !run) {
            if (result) DeleteObject(result);
            if (run) DeleteObject(run);
            return nullptr;
        }
        for (LONG y = 0; y < size.cy; ++y) {
            LONG start = -1;
            for (LONG x = 0; x <= size.cx; ++x) {
                if (opaque(x, y)) {
                    if (start < 0) start = x;
                } else if (start >= 0) {
                    SetRectRgn(run, MulDiv(start, scaled_width, size.cx), MulDiv(y, scaled_height, size.cy),
                               MulDiv(x, scaled_width, size.cx), MulDiv(y+1, scaled_height, size.cy));
                    CombineRgn(result, result, run, RGN_OR);
                    start = -1;
                }
            }
        }
        DeleteObject(run);
        return result;
    }

    // Adapted from Sonic853 TTPlayer's DrawElementFrame (player_window.cpp).
    // Draw at native frame dimensions from the XML origin, then scale the
    // composed panel once. The reference assets are 24-bit BMP color-key art.
    void draw(HDC dc, HDC source, LONG x, LONG y, int frames = 1, int state = 0) const {
        const int frame_width = size.cx / frames;
        const auto previous = SelectObject(source, handle);
        TransparentBlt(dc, x, y, frame_width, size.cy, source,
                       std::clamp(state, 0, frames - 1) * frame_width, 0,
                       frame_width, size.cy, key);
        SelectObject(source, previous);
    }

    SIZE size{};

private:
    bool create_pixels(SIZE dimensions) {
        // A color DIB keeps the original BGRX pixels regardless of the DC.
        BITMAPINFO color{};
        color.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        color.bmiHeader.biWidth = dimensions.cx;
        color.bmiHeader.biHeight = -dimensions.cy;
        color.bmiHeader.biPlanes = 1;
        color.bmiHeader.biBitCount = 32;
        color.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        handle = CreateDIBSection(nullptr,&color,DIB_RGB_COLORS,&bits,nullptr,0);
        if (!handle || !bits) return false;
        size = dimensions;
        pixels = static_cast<DWORD*>(bits);
        return true;
    }

    HBITMAP handle{};
    DWORD* pixels = nullptr;
};
}
