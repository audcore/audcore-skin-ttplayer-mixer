#pragma once
#include "window_drag.h"
#include <array>

namespace classic {
using PanelRects = std::array<RECT, 3>;

struct PanelDrag {
    int panel = -1;
    POINT start{};
    UINT dpi = 96;
    PanelRects origin{};
    std::array<int, 3> parent{-1, -1, -1};
    std::array<int, 3> order{};
    int count = 0;

    void begin(int active, POINT cursor, UINT current_dpi, const PanelRects& rects) {
        *this = {};
        panel = active;
        start = cursor;
        dpi = current_dpi;
        origin = rects;
        order[count++] = active;
        // Original TTPlayer moves the attached component from the player.
        // An auxiliary-originated drag moves that auxiliary alone.
        if (active != 0) return;
        for (int at = 0; at < count; ++at) {
            const int source = order[at];
            for (int other = 1; other < 3; ++other) {
                if (parent[other] < 0 && !IsRectEmpty(&origin[other]) &&
                    ttplayer::ui::AreDragWindowsAttached(origin[source], origin[other])) {
                    parent[other] = source;
                    order[count++] = other;
                }
            }
        }
    }

    PanelRects proposed(POINT cursor, UINT current_dpi, const PanelRects& current) const {
        PanelRects result{};
        const auto scale = [&](LONG value) { return MulDiv(value, current_dpi, dpi); };
        const auto axis = [&](LONG child_start, LONG child_end, LONG parent_start, LONG parent_end,
                              LONG child_length, LONG next_start, LONG next_end) -> LONG {
            if (child_start == parent_start) return next_start;
            if (child_start == parent_end) return next_end;
            if (child_end == parent_start) return next_start - child_length;
            if (child_end == parent_end) return next_end - child_length;
            return next_start + scale(child_start - parent_start);
        };
        for (int i = 0; i < count; ++i) {
            const int index = order[i];
            const LONG w = current[index].right - current[index].left;
            const LONG h = current[index].bottom - current[index].top;
            LONG x = cursor.x - scale(start.x - origin[index].left);
            LONG y = cursor.y - scale(start.y - origin[index].top);
            if (const int p = parent[index]; p >= 0) {
                const auto& a = origin[index];
                const auto& b = origin[p];
                const auto& c = result[p];
                x = axis(a.left, a.right, b.left, b.right, w, c.left, c.right);
                y = axis(a.top, a.bottom, b.top, b.bottom, h, c.top, c.bottom);
            }
            result[index] = {x,y,x+w,y+h};
        }
        return result;
    }

    POINT correction(const PanelRects& proposed, const PanelRects& current,
                     const RECT& work, int threshold) const {
        PanelRects moving{}, stationary{};
        int a = 0, b = 0;
        for (int i = 0; i < 3; ++i) {
            if (!IsRectEmpty(&proposed[i])) moving[a++] = proposed[i];
            else if (!IsRectEmpty(&current[i])) stationary[b++] = current[i];
        }
        return ttplayer::ui::ComputeDragSnapCorrection(
            std::span(moving).first(a), std::span(stationary).first(b), work, threshold);
    }
};
}
