#pragma once
#include <cstdint>
#include <optional>

// Serialize requests and retain only the parity of additional clicks. A host
// response acknowledges submission; rendering still follows host snapshots.
class ReferenceTransportControl {
    std::optional<bool> playing_, target_;
    std::uint64_t request_ = 0;
    bool queued_ = false;
public:
    void clear() noexcept { playing_.reset(); target_.reset(); request_ = 0; queued_ = false; }
    void update(std::optional<bool> value) noexcept { if (!value) clear(); else playing_ = value; }
    std::optional<bool> toggle() noexcept {
        if (!playing_) return {};
        if (target_) { queued_ = !queued_; return {}; }
        target_ = !*playing_; return target_;
    }
    void submitted(std::uint64_t request) noexcept { if (!request) { target_.reset(); queued_ = false; } else request_ = request; }
    std::optional<bool> response(std::uint64_t request, bool success) noexcept {
        if (!target_ || request != request_) return {};
        request_ = 0;
        if (!success) { target_.reset(); queued_ = false; return {}; }
        playing_ = target_; target_.reset();
        if (!queued_) return {};
        queued_ = false; target_ = !*playing_; return target_;
    }
};
