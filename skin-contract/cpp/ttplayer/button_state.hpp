// Copyright (c) 2026 Sonic853. See LICENSE-Sonic853.txt.
#pragma once

namespace ttplayer::skin {
// 5.7.9 SkinButton: +34 is transient state; +3c is the check state.
// A held pointer may enter without a preceding DOWN. Leaving while held
// clears state 2, but retains capture so only this button can receive UP.
struct SkinButtonState {
    bool operator==(const SkinButtonState&) const = default;
    int transient{};
    bool checked{};
    bool enabled{true};

    void Sync(bool enable, bool check) noexcept {
        if (checked != check) { checked = check; Cancel(); }
        if (enabled != enable) { enabled = enable; transient = enable ? 0 : 3; }
    }
    void Move(bool inside, bool held) noexcept {
        if (!enabled) return;
        if (!inside) transient = 0;
        else if (held) transient = 2;
        else if (!checked) transient = 1;
    }
    void Down() noexcept { if (enabled) transient = 2; }
    void Cancel() noexcept { transient = checked ? 2 : 0; }
    bool Release(bool inside) noexcept {
        const bool invoke = enabled && inside && transient == 2;
        Cancel();
        return invoke;
    }
    int Frame() const noexcept { return checked && transient != 1 ? 2 : transient; }
};

}
