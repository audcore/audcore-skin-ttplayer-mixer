#pragma once
#include <commctrl.h>
#include "win_menu.hpp"
#include "resource.h"

namespace native_menu {
inline constexpr UINT invoke_item = WM_APP + 71;
inline constexpr UINT release_menu = WM_APP + 72;

inline bool window_topmost(HWND hwnd) {
    return (GetWindowLongPtrW(GetAncestor(hwnd, GA_ROOTOWNER), GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

inline bool set_window_topmost(HWND hwnd, bool enabled) {
    // Moving the root owner between z-order bands also moves its owned panels.
    return SetWindowPos(GetAncestor(hwnd, GA_ROOTOWNER), enabled ? HWND_TOPMOST : HWND_NOTOPMOST,
                        0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE;
}

inline std::string utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const auto length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!length) throw std::runtime_error("Invalid UTF-16");
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}

inline LRESULT CALLBACK dialog_keys(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (message == WM_KEYDOWN) {
        const HWND dialog = GetParent(hwnd);
        if (wp == VK_ESCAPE || wp == VK_RETURN) {
            const WPARAM command = wp == VK_ESCAPE || GetDlgCtrlID(hwnd) == IDCANCEL ? IDCANCEL : IDOK;
            PostMessageW(dialog, WM_COMMAND, command, 0);
            return 0;
        }
        if (wp == VK_TAB) {
            SetFocus(GetNextDlgTabItem(dialog, hwnd, GetKeyState(VK_SHIFT) < 0));
            return 0;
        }
    }
    return DefSubclassProc(hwnd, message, wp, lp);
}

// Owns the row tree, popup handles and optional modeless value editor on
// the caller's UI thread. Owner provides module, locale, feedback, revision,
// usable() and send(). Route messages before other window actions, and call
// cancel_actions before closing or unloading an owner.
template<class Owner>
struct Session {
    HWND dialog{}, menu_owner{};
    std::vector<Item> rows;
    Popup popup;
    UINT_PTR menu_serial = 0;
    std::optional<Item> pending, editing;
    std::uint64_t action_revision = 0;
    void clear_menu() {
        const auto closing = std::move(popup);
        if (closing) EndMenu();
        rows.clear();
    }

    void cancel_actions() {
        clear_menu();
        pending.reset();
        menu_owner = nullptr;
        if (dialog) DestroyWindow(dialog);
        editing.reset();
    }

    void show_menu(HWND hwnd, POINT screen, std::vector<Item> items) {
        if (dialog || !hwnd) return;
        auto& owner = static_cast<Owner&>(*this);
        clear_menu();
        pending.reset();
        rows = std::move(items);
        popup = make_popup(rows);
        if (!popup) return;
        menu_owner = hwnd;
        ++menu_serial;
        action_revision = owner.revision;
        SetForegroundWindow(hwnd);
        if (!popup) return;
        const UINT alignment = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
        if (!TrackPopupMenuEx(popup.get(),alignment | TPM_RIGHTBUTTON,screen.x,screen.y,hwnd,nullptr))
            clear_menu();
    }

    bool menu_message(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto& owner = static_cast<Owner&>(*this);
        switch (message) {
        case WM_INITMENUPOPUP:
            if (popup && hwnd == menu_owner && !HIWORD(lp)) populate_popup(reinterpret_cast<HMENU>(wp));
            return true;
        case WM_MENUCOMMAND: {
            if (hwnd != menu_owner || !owner.popup || !owner.usable() || owner.action_revision != owner.revision) return true;
            MENUITEMINFOW item{sizeof(MENUITEMINFOW)};
            item.fMask = MIIM_DATA | MIIM_STATE;
            if (GetMenuItemInfoW(reinterpret_cast<HMENU>(lp), static_cast<UINT>(wp), TRUE, &item) &&
                item.dwItemData && !(item.fState & MFS_DISABLED)) {
                owner.pending = *reinterpret_cast<const Item*>(item.dwItemData);
                PostMessageW(hwnd, invoke_item, owner.menu_serial, 0);
            }
            return true;
        }
        case WM_UNINITMENUPOPUP:
            if (hwnd == menu_owner && owner.popup.get() == reinterpret_cast<HMENU>(wp)) PostMessageW(hwnd, release_menu, owner.menu_serial, 0);
            return true;
        case release_menu:
            if (hwnd == menu_owner && wp == owner.menu_serial) owner.clear_menu();
            return true;
        case invoke_item:
            if (hwnd == menu_owner && wp == owner.menu_serial && owner.usable() && owner.action_revision == owner.revision && owner.pending) {
                const auto item = std::exchange(owner.pending, {});
                owner.clear_menu();
                if (item->editor) {
                    owner.editing = *item;
                    if (const auto editor_window = CreateDialogParamW(owner.module, MAKEINTRESOURCEW(AUDCORE_NATIVE_MENU_EDITOR), hwnd,
                                                                     editor_proc, reinterpret_cast<LPARAM>(&owner))) {
                        RECT anchor{}, box{};
                        GetWindowRect(hwnd, &anchor);
                        GetWindowRect(editor_window, &box);
                        MONITORINFO monitor{sizeof(MONITORINFO)};
                        if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
                            const auto& area = monitor.rcWork;
                            const LONG x = std::clamp(anchor.left, area.left, std::max(area.left, area.right - (box.right - box.left)));
                            const LONG y = std::clamp(anchor.bottom, area.top, std::max(area.top, area.bottom - (box.bottom - box.top)));
                            SetWindowPos(editor_window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                        }
                        ShowWindow(editor_window, SW_SHOW);
                        SetFocus(GetDlgItem(editor_window, AUDCORE_NATIVE_MENU_VALUE));
                    } else owner.editing.reset();
                } else if (item->toggle_topmost) {
                    if (!set_window_topmost(hwnd, !item->checked)) MessageBeep(MB_ICONWARNING);
                } else send_menu_command(owner,item->command);
            }
            return true;
        default: return false;
        }
    }

private:
    static void send_menu_command(Owner& owner, const std::string& payload) {
        auto command = Json::parse(payload);
        if (command.at("method").get_ref<const std::string&>().starts_with("menu."))
            command["params"]["supplementalNativeMenu"] = true;
        owner.send(command.dump());
    }

    static INT_PTR CALLBACK editor_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) noexcept {
        auto* skin = reinterpret_cast<Owner*>(GetWindowLongPtrW(hwnd, DWLP_USER));
        try {
            if (message == WM_INITDIALOG) {
                skin = reinterpret_cast<Owner*>(lp);
                SetWindowLongPtrW(hwnd, DWLP_USER, lp);
                skin->dialog = hwnd;
                const auto& tr = skin->locale;
                SetWindowTextW(hwnd, wide(skin->editing->label).c_str());
                SetDlgItemTextW(hwnd, IDOK, wide(tr("确定", "OK", "決定")).c_str());
                SetDlgItemTextW(hwnd, IDCANCEL, wide(tr("取消", "Cancel", "取消")).c_str());
                const HWND value = GetDlgItem(hwnd, AUDCORE_NATIVE_MENU_VALUE);
                SendMessageW(value, EM_SETLIMITTEXT, 1024, 0);
                SetWindowTextW(value, wide(skin->editing->editor->initial).c_str());
                SendMessageW(value, EM_SETSEL, 0, -1);
                for (const int child : {AUDCORE_NATIVE_MENU_VALUE, IDOK, IDCANCEL}) {
                    SetWindowSubclass(GetDlgItem(hwnd, child), dialog_keys, 1, 0);
                }
                return TRUE;
            }
            if (!skin) return FALSE;
            if (message == WM_NCDESTROY) {
                skin->dialog = nullptr;
                skin->editing.reset();
            }
            if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wp) == IDCANCEL)) {
                DestroyWindow(hwnd);
                return TRUE;
            }
            if (message == WM_COMMAND && LOWORD(wp) == IDOK) {
                if (!skin->usable() || skin->action_revision != skin->revision) {
                    skin->feedback = skin->locale("状态已变化，请重试", "State changed. Try again.", "状態が変わりました");
                    DestroyWindow(hwnd);
                    MessageBeep(MB_ICONWARNING);
                    return TRUE;
                }
                wchar_t value[1025]{};
                const int length = GetDlgItemTextW(hwnd, AUDCORE_NATIVE_MENU_VALUE, value, 1025);
                const auto command = edited_command(*skin->editing, utf8({value, static_cast<std::size_t>(length)}));
                if (!command) {
                    MessageBeep(MB_ICONWARNING);
                    SetFocus(GetDlgItem(hwnd, AUDCORE_NATIVE_MENU_VALUE));
                    return TRUE;
                }
                DestroyWindow(hwnd);
                send_menu_command(*skin,*command);
                return TRUE;
            }
        } catch (...) { MessageBeep(MB_ICONWARNING); }
        return FALSE;
    }

};
}
