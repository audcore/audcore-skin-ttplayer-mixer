#pragma once
#include <windows.h>
#include "model.hpp"
#include <memory>

namespace native_menu {
inline std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(count, L'\0');
    if (count) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}

inline std::wstring menu_label(const std::string& text) {
    std::wstring result;
    for (const auto ch : wide(text)) {
        if (ch == L'&') result += L'&';
        result += ch < L' ' ? L' ' : ch;
    }
    return result;
}

struct DestroyPopup {
    void operator()(HMENU menu) const noexcept { DestroyMenu(menu); }
};
using Popup = std::unique_ptr<std::remove_pointer_t<HMENU>, DestroyPopup>;

inline Popup empty_popup(const std::vector<Item>* rows) {
    Popup popup(CreatePopupMenu());
    if (!popup) return {};
    MENUINFO style{sizeof(MENUINFO)};
    style.fMask = MIM_STYLE | MIM_MENUDATA;
    style.dwStyle = MNS_NOTIFYBYPOS | MNS_MODELESS;
    style.dwMenuData = reinterpret_cast<ULONG_PTR>(rows);
    if (!SetMenuInfo(popup.get(), &style)) return {};
    return popup;
}

inline bool append_items(HMENU popup, const std::vector<Item>& rows) {
    UINT position = 0;
    for (const auto& row : rows) {
        auto label = menu_label(row.label);
        MENUITEMINFOW item{sizeof(MENUITEMINFOW)};
        item.fMask = MIIM_STRING | MIIM_FTYPE | MIIM_STATE | MIIM_DATA;
        item.fType = row.mark == Mark::radio ? MFT_RADIOCHECK : MFT_STRING;
        item.fState = row.checked ? MFS_CHECKED : MFS_UNCHECKED;
        item.dwTypeData = label.data();
        Popup child;
        if (!row.children.empty()) {
            child = empty_popup(&row.children);
            if (!child || !AppendMenuW(child.get(), MF_STRING | MF_GRAYED, 0, L"…")) return false;
            item.fMask |= MIIM_SUBMENU;
            item.hSubMenu = child.get();
        } else if (row.command.empty() && !row.toggle_topmost) {
            item.fState |= MFS_DISABLED;
        } else {
            item.dwItemData = reinterpret_cast<ULONG_PTR>(&row);
        }
        if (!InsertMenuItemW(popup, position++, TRUE, &item)) return false;
        child.release();
    }
    return true;
}

// A project can contain thousands of sends. Keep the model available while
// creating Win32 submenu handles one level at a time when the user opens them.
inline bool populate_popup(HMENU popup) {
    MENUINFO data{sizeof(MENUINFO)};
    data.fMask = MIM_MENUDATA;
    if (!GetMenuInfo(popup, &data)) return false;
    const auto rows = reinterpret_cast<const std::vector<Item>*>(data.dwMenuData);
    if (!rows) return true;
    data.dwMenuData = 0;
    if (!SetMenuInfo(popup, &data) || !DeleteMenu(popup, 0, MF_BYPOSITION)) return false;
    if (append_items(popup, *rows)) return true;
    while (GetMenuItemCount(popup) > 0 && DeleteMenu(popup, 0, MF_BYPOSITION)) {}
    AppendMenuW(popup, MF_STRING | MF_GRAYED, 0, L"…");
    return false;
}

inline Popup make_popup(const std::vector<Item>& rows) {
    auto popup = empty_popup(nullptr);
    if (!popup || !append_items(popup.get(), rows)) return {};
    return popup;
}
}
