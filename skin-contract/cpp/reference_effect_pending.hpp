#pragma once
#include "reference_skin_state.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

// Only the next input uses a pending value. Painting always uses host state.
// A snapshot, changed identity or external parameter change discards it.
class ReferenceEffectPending {
    struct Entry {
        ReferenceEffectControl control;
        double target, observed;
        std::uint64_t request;
        std::vector<double> sent;
    };
    std::unordered_map<std::uint64_t,Entry> entries_;
    static bool same_identity(const ReferenceEffectControl& a,const ReferenceEffectControl& b) {
        return a.channel==b.channel && a.instance==b.instance && a.channel_identity==b.channel_identity && a.effect_identity==b.effect_identity && a.parameter==b.parameter;
    }
    static bool same_value(const ReferenceEffectControl& control,double a,double b) {
        return control.parameter ? std::abs(a-b)<1e-9 : static_cast<float>(a)==static_cast<float>(b);
    }
public:
    void clear() noexcept { entries_.clear(); }
    std::optional<double> base(const ReferenceEffectControl& control) {
        const auto it=entries_.find(control.instance);
        if (it==entries_.end()) return {};
        if (!same_identity(it->second.control,control)) { entries_.erase(it); return {}; }
        return it->second.target;
    }
    void record(const ReferenceEffectControl& control,double target,std::uint64_t request) {
        auto it=entries_.find(control.instance);
        if (it==entries_.end() || !same_identity(it->second.control,control)) {
            entries_.erase(control.instance);
            it=entries_.emplace(control.instance,Entry{control,target,control.value,request,{}}).first;
        }
        auto& entry=it->second;
        entry.target=target; entry.request=request;
        entry.sent.push_back(target);
        if (entry.sent.size()>256) entry.sent.erase(entry.sent.begin());
    }
    void fail(std::uint64_t request) {
        for (auto it=entries_.begin();it!=entries_.end();) {
            if (it->second.request==request) it=entries_.erase(it); else ++it;
        }
    }
    void reconcile(const ReferenceSkinState& state) {
        for (auto it=entries_.begin();it!=entries_.end();) {
            auto& entry=it->second;
            std::optional<ReferenceEffectControl> actual;
            const auto& channels=state.channels();
            for (std::size_t channel=0;channel<channels.size() && !actual;++channel) {
                if (channels[channel].id != entry.control.channel) continue;
                for (std::size_t effect=0;effect<channels[channel].effects.size();++effect)
                    if (channels[channel].effects[effect].instance==it->first) { actual=state.effect_control(channel,effect); break; }
            }
            if (!actual || !same_identity(entry.control,*actual)) { it=entries_.erase(it); continue; }
            if (!same_value(*actual,actual->value,entry.observed)) {
                if (same_value(*actual,actual->value,entry.target)) { it=entries_.erase(it); continue; }
                if (std::none_of(entry.sent.begin(),entry.sent.end(),[&](double sent){return same_value(*actual,actual->value,sent);})) { it=entries_.erase(it); continue; }
                entry.observed=actual->value;
            }
            ++it;
        }
    }
};
