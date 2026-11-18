#define UNICODE
#define _UNICODE
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include "classic_art.hpp"
#include "panel_drag.hpp"
#include "../reference_effect_pending.hpp"
#include "../reference_recovery.hpp"
#include "../reference_skin_layout.hpp"
#include "../reference_subscription.hpp"
#include "../reference_transport_control.hpp"
#include "../skin_protocol_limits.hpp"
#include "../native_menu/session.hpp"
#include <memory>
#include <string_view>

namespace classic {
constexpr char id[] = "cn.audcore.ttplayer";
constexpr char skin_name[] = "声声静听";
constexpr char version[] = "0.1.13";
constexpr wchar_t classic_app_icon[] = L"CLASSIC_APP_ICON";
constexpr RECT player_icon{7,5,23,21};
constexpr int transparency_step = 5;
constexpr RECT effect_list{11,27,316,91};
constexpr LONG original_channel_body_end = 112;
constexpr RECT channel_list{11,48,316,original_channel_body_end+channel_extra_height};
constexpr int effects_panel = 1;
constexpr int channels_panel = 2;
constexpr RECT legacy_progress{14,96,169,103};
constexpr RECT current_channel_name{17,32,201,57};
constexpr UINT_PTR channel_name_timer = 0x534e;
constexpr RECT volume{295,32,307,72};
constexpr int row_height = 16;
constexpr int effect_rows = (effect_list.bottom - effect_list.top) / row_height;
constexpr int channel_rows = (channel_list.bottom - channel_list.top) / row_height;
constexpr LONG effect_menu_x = 288;

LONG channel_name_offset(LONG overflow, ULONGLONG elapsed) {
    if (overflow <= 0) return 0;
    constexpr ULONGLONG pause = 1000;
    constexpr ULONGLONG pixels_per_second = 30;
    const auto travel = (static_cast<ULONGLONG>(overflow)*1000+pixels_per_second-1)/pixels_per_second;
    const auto phase = elapsed % (pause+travel+pause);
    return phase <= pause ? 0 : std::min(overflow,static_cast<LONG>((phase-pause)*pixels_per_second/1000));
}

bool relabelled_button(Action action) {
    return action == Action::channels || action == Action::effects || action == Action::bypass ||
        action == Action::remove_effect || action == Action::add;
}

struct TableColumn {
    LONG left, right;
    UINT align;

    constexpr RECT at(LONG top, LONG bottom) const { return {left,top,right,bottom}; }
};
constexpr TableColumn channel_name_col{34,111,DT_LEFT};
constexpr TableColumn channel_input_col{117,184,DT_LEFT};
constexpr TableColumn channel_gain_col{190,231,DT_RIGHT};
constexpr TableColumn channel_fx_col{237,269,DT_RIGHT};
constexpr std::array<TableColumn,3> channel_status_cols{{
    {275,286,DT_CENTER},{286,297,DT_CENTER},{297,309,DT_CENTER}
}};

constexpr BYTE window_alpha(int percent) {
    return static_cast<BYTE>(255 * (100 - std::clamp(percent, 0, 90)) / 100);
}

double volume_at(LONG y) {
    const auto height = volume.bottom - volume.top - 1;
    return -60.0 * std::clamp(y-volume.top,0L,height)/height;
}

std::wstring wide(const std::string& text) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    if (size) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::wstring channel_heading(const ReferenceLabels& labels) {
    auto name = labels.channel_template;
    if (const auto slot = name.find("{}"); slot != std::string::npos) name.erase(slot,2);
    const auto begin = name.find_first_not_of(" \t");
    if (begin == std::string::npos) return L"Ch";
    return wide(name.substr(begin,name.find_last_not_of(" \t")-begin+1));
}

std::wstring compact_input_label(const std::string& label) {
    if (label == "空输入" || label == "No in" || label == "入力なし") return {};
    const auto name = wide(label);
    // Match the compact labels used by the web skin. Keep device names and
    // any route without a complete leading channel number intact.
    constexpr const wchar_t* prefixes[]{
        L"analog input", L"analog in", L"input", L"in", L"virtual rec", L"rec", L"输入", L"入力"
    };
    const auto space = [](wchar_t ch) { return ch == L' ' || ch == L'\t'; };
    const auto digit = [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; };
    for (std::size_t i = 0; i < std::size(prefixes); ++i) {
        const auto length = wcslen(prefixes[i]);
        if (name.size() <= length || !space(name[length]) ||
            _wcsnicmp(name.c_str(),prefixes[i],length) != 0) continue;
        std::size_t start = length;
        while (start < name.size() && space(name[start])) ++start;
        auto end = start;
        while (end < name.size() && digit(name[end])) ++end;
        if (end == start) return name;
        if (end+1 < name.size() && name[end] == L'/' && digit(name[end+1])) {
            end += 2;
            while (end < name.size() && digit(name[end])) ++end;
        }
        auto suffix = end;
        while (suffix < name.size() && space(name[suffix])) ++suffix;
        if (suffix != name.size() && name[suffix] != L'·' && name[suffix] != L'(' &&
            name[suffix] != L'（') return name;
        return (i == 4 || i == 5 ? L"Rec " : L"In ") + name.substr(start,end-start);
    }
    return name;
}

struct Skin;
struct ChannelChoice {
    std::uint32_t id;
    nlohmann::json identity;
};
struct Panel {
    Skin* skin{};
    int index{};
    HWND hwnd{};
    bool wanted = true;
    int pressed = -1;
    std::vector<Button> buttons;
};
LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM) noexcept;

struct Skin : native_menu::Session<Skin> {
    audcore_skin_host host{};
    HMODULE module{};
    HICON small_icon = nullptr;
    HICON large_icon = nullptr;
    wchar_t class_name[80]{};
    bool registered = false;
    bool initialized = false;
    bool shown = false;
    bool was_minimized = false;
    bool ready = false;
    bool classic_art = false;
    std::array<Panel,3> panels{};
    std::unordered_map<std::wstring, std::unique_ptr<Bitmap>> art;
    std::unique_ptr<Bitmap> expanded_channels;
    ReferenceSkinState state;
    ReferenceSubscription subscription;
    ReferenceRecovery recovery;
    ReferenceEffectPending effect_pending;
    ReferenceTransportControl transport;
    native_menu::Locale locale;
    std::string feedback;
    std::uint64_t revision = 0;
    std::uint64_t request = 0;
    std::optional<std::uint32_t> channel_id;
    bool initial_channel_chosen = false;
    std::optional<std::uint64_t> effect_id;
    std::vector<std::size_t> visible_channels;
    int first_channel = 0;
    int first_effect = 0;
    int wheel_remainder = 0;
    int transparency = 5;
    int transparency_wheel_remainder = 0;
    std::optional<ReferenceGainControl> volume_drag;
    std::optional<double> volume_sent;
    std::wstring displayed_channel;
    ULONGLONG channel_name_started = 0;
    bool channel_name_scrolling = false;
    HFONT channel_name_font = nullptr;
    HFONT selector_font = nullptr;
    PanelDrag drag;

    bool usable() const { return shown && ready && !recovery.active(); }

    ~Skin() {
        if (small_icon) DestroyIcon(small_icon);
        if (large_icon) DestroyIcon(large_icon);
        if (channel_name_font) DeleteObject(channel_name_font);
        if (selector_font) DeleteObject(selector_font);
    }

    void stop_channel_name_scroll() {
        if (!channel_name_scrolling) return;
        if (panels[0].hwnd) KillTimer(panels[0].hwnd,channel_name_timer);
        channel_name_scrolling = false;
        channel_name_started = 0;
    }

    const Bitmap* bitmap(const wchar_t* name) const {
        if (!classic_art || !name) return nullptr;
        const auto it = art.find(name);
        return it == art.end() ? nullptr : it->second.get();
    }
    const Bitmap* background(int i) const {
        static constexpr const wchar_t* names[]{L"player_skin", L"eq_skin", L"playlist_skin"};
        if (i == channels_panel && expanded_channels) return expanded_channels.get();
        return bitmap(names[i]);
    }
    const Bitmap* button_bitmap(const Button& button) const {
        const bool playing = ready && state.transport_playing().value_or(false);
        return bitmap(button.action == Action::play && playing ? L"pause" : button.asset);
    }
    void load_art() {
        small_icon = static_cast<HICON>(LoadImageW(module,classic_app_icon,IMAGE_ICON,16,16,0));
        large_icon = static_cast<HICON>(LoadImageW(module,classic_app_icon,IMAGE_ICON,32,32,0));
        static constexpr const wchar_t* names[]{
            L"player_skin", L"eq_skin", L"playlist_skin", L"pause", L"number", L"progress2"
        };
        const auto load = [&](const wchar_t* name) {
            if (!name || art.contains(name)) return true;
            auto image = std::make_unique<Bitmap>();
            if (!image->load(module, name)) return false;
            art.emplace(name, std::move(image));
            return true;
        };
        classic_art = std::all_of(std::begin(names), std::end(names), load);
        for (const auto& panel : panels)
            for (const auto& button : panel.buttons)
                if (classic_art) classic_art = load(button.asset);
        for (int i = 0; classic_art && i < 3; ++i)
            classic_art = background(i)->size.cx == width && background(i)->size.cy == source_heights[i];
        if (classic_art) {
            expanded_channels = background(channels_panel)->expand_rows(
                channel_list.top,original_channel_body_end,channel_extra_height);
            classic_art = expanded_channels != nullptr;
        }
        if (!classic_art) art.clear();
    }

    std::optional<std::size_t> channel_index() const {
        if (!ready) return {};
        const auto& channels = state.channels();
        for (std::size_t i = 0; i < channels.size(); ++i)
            if (channels[i].id == channel_id && !channels[i].hidden) return i;
        for (std::size_t i = 0; i < channels.size(); ++i)
            if (!channels[i].hidden) return i;
        return {};
    }
    const ReferenceChannel* channel() const {
        const auto index = channel_index();
        return index ? &state.channels()[*index] : nullptr;
    }
    void choose_initial_channel() {
        if (initial_channel_chosen || !ready) return;
        const auto& channels = state.channels();
        const auto name_matches = [](std::string_view name, std::string_view label) {
            const auto start = name.find_first_not_of(" \t");
            if (start == std::string_view::npos) return false;
            const auto length = name.find_last_not_of(" \t")-start+1;
            return length == label.size() && _strnicmp(name.data()+start,label.data(),length) == 0;
        };
        auto mic = channels.end();
        for (const auto label : {"Mic","Microphone","麦克风","マイク"}) {
            mic = std::find_if(channels.begin(),channels.end(),[&](const auto& item) {
                return !item.hidden && name_matches(item.name,label);
            });
            if (mic != channels.end()) break;
        }
        // The native host derives the display name of unnamed mic channels
        // from the studio-microphone icon. Match that same identity.
        if (mic == channels.end())
            mic = std::find_if(channels.begin(),channels.end(),[](const auto& item) {
                return !item.hidden && (item.icon == "🎙️" || item.icon == "🎙");
            });
        if (mic != channels.end()) {
            if (channel() != &*mic) {
                effect_id.reset();
                first_effect = 0;
            }
            channel_id = mic->id;
        }
        initial_channel_chosen = channel_index().has_value();
        if (initial_channel_chosen) {
            visible_channels = state.visible_channels();
            reveal_channel();
        }
    }
    void main_size_changed(bool minimized) {
        if (minimized) {
            stop_channel_name_scroll();
            was_minimized = true;
        } else if (std::exchange(was_minimized,false)) {
            initial_channel_chosen = false;
            choose_initial_channel();
            sync_buttons();
        }
    }
    std::optional<std::size_t> effect_index() const {
        const auto* selected = channel();
        if (!selected) return {};
        for (std::size_t i = 0; i < selected->effects.size(); ++i)
            if (selected->effects[i].instance == effect_id) return i;
        return {};
    }
    int max_scroll() const {
        const auto* selected = channel();
        return selected ? std::max(0, static_cast<int>(selected->effects.size()) - effect_rows) : 0;
    }
    std::optional<std::size_t> effect_at(POINT p) const {
        const auto* selected = channel();
        if (!selected || !PtInRect(&effect_list, p)) return {};
        const auto index = static_cast<std::size_t>(first_effect + (p.y - effect_list.top) / row_height);
        if (index >= selected->effects.size()) return {};
        return index;
    }
    void repaint() const {
        for (const auto& panel : panels)
            if (panel.hwnd) InvalidateRect(panel.hwnd, nullptr, FALSE);
    }
    void sync_buttons() {
        visible_channels = ready ? state.visible_channels() : std::vector<std::size_t>{};
        first_channel = std::clamp(first_channel,0,max_channel_scroll());
        const auto* selected = channel();
        for (auto& panel : panels) for (auto& button : panel.buttons) {
            bool enabled = true, checked = false;
            switch (button.action) {
            case Action::mute: enabled = selected != nullptr; checked = enabled && selected->muted; break;
            case Action::bypass: enabled = selected != nullptr; checked = enabled && selected->fx_bypass; break;
            case Action::remove_effect: enabled = effect_index().has_value(); break;
            case Action::add: enabled = selected != nullptr; break;
            case Action::play: enabled = ready && state.transport_playing().has_value(); break;
            case Action::stop: enabled = ready && state.transport_playing().value_or(false); break;
            case Action::effects: checked = panels[effects_panel].wanted; break;
            case Action::channels: checked = panels[channels_panel].wanted; break;
            case Action::previous: case Action::next: enabled = false; break;
            default: break;
            }
            button.state.Sync(enabled, checked);
        }
        first_effect = std::clamp(first_effect, 0, max_scroll());
        repaint();
    }

    std::uint64_t send(std::string_view json) {
        if (json.empty() || json.size() > skin_max_request_bytes) return 0;
        if (++request == 0) ++request;
        const audcore_skin_bytes bytes{reinterpret_cast<const std::uint8_t*>(json.data()),
                                       static_cast<std::uint32_t>(json.size())};
        return host.command(host.ctx, request, bytes) == 0 ? request : 0;
    }
    void simple(std::string_view method) {
        send(nlohmann::json{{"method", method}, {"params", nlohmann::json::object()}}.dump());
    }
    void transport_target(bool target) { transport.submitted(send(state.transport_command(!target))); }
    void adjust_transparency(int steps) {
        const int next = std::clamp(transparency + steps*transparency_step, 0, 90);
        if (next == transparency) return;
        transparency = next;
        for (const auto& panel : panels)
            if (panel.hwnd) SetLayeredWindowAttributes(panel.hwnd,0,window_alpha(transparency),LWA_ALPHA);
    }
    void update_volume(LONG y) {
        const auto selected = channel_index();
        const auto current = selected ? state.gain_control(*selected) : std::nullopt;
        if (!volume_drag || !current || current->channel != volume_drag->channel ||
            current->channel_identity != volume_drag->channel_identity) return;
        const double target = volume_at(y);
        if (target == volume_sent.value_or(current->value)) return;
        if (send(state.gain_command(*selected,target-current->value))) volume_sent = target;
    }
    void request_recovery() {
        ready = false;
        cancel_actions();
        effect_pending.clear();
        transport.clear();
        sync_buttons();
        if (recovery.start_request() && host.request_snapshot(host.ctx, subscription.token()) != 0)
            recovery.request_failed();
    }
    void receive(const audcore_skin_event& event) {
        if (event.kind == AUDCORE_SKIN_EVENT_ERROR) {
            ready = false;
            cancel_actions();
            subscription.stop(host);
            effect_pending.clear();
            transport.clear();
            recovery.reset();
            sync_buttons();
            return;
        }
        if (event.kind == AUDCORE_SKIN_EVENT_RESPONSE) {
            if (event.status != 0) effect_pending.fail(event.request_id);
            if (const auto target = transport.response(event.request_id, event.status == 0))
                transport_target(*target);
            return;
        }
        const bool snapshot = event.kind == AUDCORE_SKIN_EVENT_SNAPSHOT;
        const bool delta = event.kind == AUDCORE_SKIN_EVENT_DELTA;
        if (event.payload.len > skin_max_event_bytes || !event.payload.data) {
            if (snapshot && recovery.active()) recovery.snapshot_received(false);
            else if (snapshot || delta) request_recovery();
            return;
        }
        const std::string json(reinterpret_cast<const char*>(event.payload.data), event.payload.len);
        if (snapshot) {
            if (ready && event.revision < revision) return;
            if (!state.snapshot(json)) {
                if (recovery.active()) recovery.snapshot_received(false);
                else request_recovery();
                return;
            }
            ready = true;
                revision = event.revision;
            recovery.snapshot_received(true);
            transport.clear();
            transport.update(state.transport_playing());
            effect_pending.clear();
        } else if (delta) {
            if (ready && event.revision <= revision) return;
            bool changed = false;
            if (!ready || event.revision != revision + 1 || !state.delta(json, &changed)) {
                request_recovery();
                return;
            }
            revision = event.revision;
            if (changed) transport.update(state.transport_playing());
            effect_pending.reconcile(state);
        } else {
            if (event.kind == AUDCORE_SKIN_EVENT_TRANSIENT && ready && state.meters(json)) {
                if (panels[0].hwnd && IsWindowVisible(panels[0].hwnd))
                    InvalidateRect(panels[0].hwnd, nullptr, FALSE);
            }
            return;
        }
        const auto message = nlohmann::json::parse(json);
        const auto settings = snapshot ? message.value("i18n",nlohmann::json::object())
            : message.value("key","") == "i18n" ? message.at("value")
            : message.contains("changes") ? message.at("changes").value("i18n",nlohmann::json::object())
            : nlohmann::json::object();
        if (settings.is_object() && settings.contains("language") && settings["language"].is_string())
            locale.language = settings["language"].get<std::string>();
        clear_menu();
        choose_initial_channel();
        sync_buttons();
    }

    void open_menu(HWND hwnd, POINT point, std::optional<std::size_t> channel = {},
                   std::optional<std::size_t> effect = {}) {
        std::vector<native_menu::Item> items;
        if (!usable()) {
            items.push_back(native_menu::action(locale("正在等待声核状态…","Waiting for Audcore…","状態を取得中…")));
        } else if (channel && effect) {
            items = native_menu::effect_items(state,locale,*channel,*effect,point.x,point.y);
        } else if (channel) {
            items = native_menu::channel_items(state,locale,*channel,point.x,point.y);
        } else {
            items = native_menu::menu(state,locale,point.x,point.y,native_menu::window_topmost(hwnd),false);
        }
        if (!feedback.empty()) items.insert(items.begin(),native_menu::action(std::exchange(feedback,{})));
        show_menu(hwnd,point,std::move(items));
    }
    bool choose_channel(const ChannelChoice& choice) {
        if (!ready) return false;
        const auto& channels = state.channels();
        const auto target = std::find_if(channels.begin(),channels.end(),[&](const auto& channel) {
            return !channel.hidden && channel.id == choice.id && channel.identity == choice.identity;
        });
        if (target == channels.end()) return false;
        const bool already_current = channel() == &*target;
        channel_id = target->id;
        if (already_current) return true;
        effect_id.reset();
        first_effect = 0;
        sync_buttons();
        reveal_channel();
        return true;
    }
    int max_channel_scroll() const {
        return std::max(0,static_cast<int>(visible_channels.size())-channel_rows);
    }
    std::optional<std::size_t> channel_at(POINT point) const {
        if (!PtInRect(&channel_list,point)) return {};
        const auto row = static_cast<std::size_t>(first_channel+(point.y-channel_list.top)/row_height);
        return row < visible_channels.size() ? std::optional(visible_channels[row]) : std::nullopt;
    }
    void reveal_channel() {
        const auto current = channel_index();
        if (!current) return;
        const auto it = std::find(visible_channels.begin(),visible_channels.end(),*current);
        if (it == visible_channels.end()) return;
        const int row = static_cast<int>(it-visible_channels.begin());
        first_channel = std::clamp(std::clamp(first_channel,row-channel_rows+1,row),0,max_channel_scroll());
    }
    void step_channel(int direction) {
        const auto current = channel_index();
        if (!current || visible_channels.empty()) return;
        const auto it = std::find(visible_channels.begin(),visible_channels.end(),*current);
        if (it == visible_channels.end()) return;
        const int row = std::clamp(static_cast<int>(it-visible_channels.begin())+direction,
                                   0,static_cast<int>(visible_channels.size())-1);
        const auto& target = state.channels()[visible_channels[row]];
        choose_channel({target.id,target.identity});
    }
    void show_channels() {
        auto& list = panels[channels_panel];
        if (!list.wanted) toggle_auxiliary(channels_panel);
        reveal_channel();
        repaint();
        if (list.hwnd && IsWindowVisible(list.hwnd)) SetFocus(list.hwnd);
    }
    void adjust_effect(std::size_t index, int steps) {
        const auto selected = channel_index();
        if (!selected) return;
        if (const auto control = state.effect_control(*selected, index)) {
            const auto json = state.effect_adjust_command(*selected, index, steps, effect_pending.base(*control));
            if (const auto ticket = send(json)) {
                const auto params = nlohmann::json::parse(json).at("params");
                effect_pending.record(*control, params.at(control->parameter ? "normalized" : "wet").get<double>(), ticket);
            }
        }
    }
    void bypass_effect(std::size_t index) {
        if (const auto selected = channel_index()) {
            const auto& effect = state.channels()[*selected].effects[index];
            if (!effect.offline)
                send(state.effect_request(*selected, index, {{"method","effect.setBypass"},
                     {"params",{{"id",state.channels()[*selected].id},{"instance",effect.instance},{"bypass",!effect.bypass}}}}));
        }
    }
    void open_effect(std::size_t index) {
        if (const auto selected = channel_index()) send(state.effect_editor_command(*selected, index));
    }
    void remove_selected_effect() {
        const auto selected = channel_index();
        const auto effect = effect_index();
        if (!selected || !effect) return;
        const auto& channel = state.channels()[*selected];
        send(state.effect_request(*selected,*effect,{{"method","effect.remove"},{"params",
            {{"id",channel.id},{"instance",channel.effects[*effect].instance}}}}));
    }

    PanelRects rects() const {
        PanelRects result{};
        for (int i = 0; i < 3; ++i) {
            const auto& panel = panels[i];
            if (panel.wanted && panel.hwnd) GetWindowRect(panel.hwnd, &result[i]);
        }
        return result;
    }
    void apply_region(const Panel& panel, UINT dpi) const {
        if (const auto* image = background(panel.index)) {
            auto region = image->region(MulDiv(width,dpi,96), MulDiv(heights[panel.index],dpi,96));
            if (region && !SetWindowRgn(panel.hwnd, region, FALSE)) DeleteObject(region);
        }
    }
    void move_panels(POINT point) {
        for (int pass = 0; pass < 2; ++pass) {
            const auto current = rects();
            const UINT dpi = GetDpiForWindow(panels[drag.panel].hwnd);
            const auto proposed = drag.proposed(point, dpi, current);
            const auto work = ttplayer::ui::DragWorkAreaForRect(proposed[drag.panel]);
            const auto delta = drag.correction(proposed, current, work, MulDiv(10,dpi,96));
            constexpr UINT flags = SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER;
            HDWP batch = BeginDeferWindowPos(drag.count);
            for (int i = 0; batch && i < drag.count; ++i) {
                const int index = drag.order[i];
                batch = DeferWindowPos(batch, panels[index].hwnd, nullptr,
                    proposed[index].left + delta.x, proposed[index].top + delta.y, 0, 0, flags);
            }
            if (!batch || !EndDeferWindowPos(batch)) return;
            const auto updated = rects();
            bool resized = false;
            for (int i = 0; i < drag.count; ++i) {
                const int index = drag.order[i];
                resized |= (updated[index].right-updated[index].left != current[index].right-current[index].left ||
                            updated[index].bottom-updated[index].top != current[index].bottom-current[index].top);
            }
            if (!resized) break;
        }
    }
    void show_auxiliaries(bool visible) {
        for (int i : {1,2}) if (panels[i].hwnd)
            ShowWindow(panels[i].hwnd, visible && panels[i].wanted ? SW_SHOWNOACTIVATE : SW_HIDE);
    }
    void toggle_auxiliary(int i) {
        cancel_actions();
        panels[i].wanted = !panels[i].wanted;
        show_auxiliaries(shown && !IsIconic(panels[0].hwnd));
        sync_buttons();
    }
    void invoke(Panel& panel, Action action) {
        const auto index = channel_index();
        switch (action) {
        case Action::open: simple("document.open"); break;
        case Action::minimize: simple("window.minimize"); break;
        case Action::close: if (panel.index == 0) simple("window.close"); else toggle_auxiliary(panel.index); break;
        case Action::play:
            if (const auto target = transport.toggle()) transport_target(*target);
            break;
        case Action::stop:
            if (state.transport_playing().value_or(false)) send(state.transport_command(true));
            break;
        case Action::settings: send(state.panel_command(false)); break;
        case Action::mute: if (index) send(state.toggle_command(*index, false)); break;
        case Action::effects: toggle_auxiliary(effects_panel); break;
        case Action::channels: toggle_auxiliary(channels_panel); break;
        case Action::bypass: if (index) send(state.fx_bypass_command(*index)); break;
        case Action::remove_effect: remove_selected_effect(); break;
        case Action::add: if (index) send(state.effect_browser_command(*index)); break;
        default: break;
        }
    }
};

void draw_button(const Skin& skin, const Button& button, HDC dc, HDC source) {
    if (relabelled_button(button.action)) {
        const int frame = button.state.Frame();
        COLORREF label_color = (frame == 1 || frame == 2) ? ink : pale;
        if (const auto* image = skin.button_bitmap(button)) {
            const LONG x = frame*(image->size.cx/4);
            const auto face = image->color_at(x,image->size.cy/2);
            if (GetRValue(face)+GetGValue(face)+GetBValue(face) > 490)
                label_color = button.state.enabled ? RGB(58,91,120) : RGB(128,151,169);
            for (LONG y = 0; y < button.bounds.bottom-button.bounds.top; ++y) {
                const LONG top = button.bounds.top+y;
                fill(dc,{button.bounds.left,top,button.bounds.right,top+1},
                     image->color_at(x,std::min(y,image->size.cy-1)));
            }
        } else {
            fill(dc,button.bounds,frame == 0 ? RGB(75,105,133) : RGB(38,74,100));
        }
        const auto font = skin.selector_font ? SelectObject(dc,skin.selector_font) : nullptr;
        text(dc,button.label,button.bounds,label_color,DT_CENTER);
        if (font) SelectObject(dc,font);
        return;
    }
    if (const auto* image = skin.button_bitmap(button)) {
        image->draw(dc, source, button.bounds.left, button.bounds.top, 4, button.state.Frame());
        return;
    }
    const auto& box = button.bounds;
    fill(dc, box, RGB(36,58,78));
    const RECT inner{box.left+1, box.top+1, box.right-1, box.bottom-1};
    const COLORREF face = button.state.Frame() == 2 ? RGB(38,74,100)
        : button.state.Frame() == 1 ? RGB(104,139,165) : RGB(75,105,133);
    fill(dc, inner, face);
    const wchar_t* label = button.action == Action::play && skin.state.transport_playing().value_or(false)
        ? L"Ⅱ" : button.label;
    if (button.action == Action::settings) {
        const auto localized = wide(skin.state.labels().settings);
        text(dc, localized.c_str(), inner, ink, DT_CENTER);
    } else {
        text(dc, label, inner, button.state.enabled ? ink : pale, DT_CENTER);
    }
}

void draw_channel_name(const Panel& panel, HDC dc, const std::wstring& name) {
    auto& skin = *panel.skin;
    const auto font = skin.channel_name_font ? SelectObject(dc,skin.channel_name_font) : nullptr;
    auto measured = current_channel_name;
    const auto count = static_cast<int>(name.size());
    DrawTextW(dc,name.c_str(),count,&measured,DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
    const LONG overflow = measured.right-current_channel_name.right;
    if (overflow > 0 && panel.hwnd && IsWindowVisible(panel.hwnd) && !IsIconic(panel.hwnd)) {
        const auto now = GetTickCount64();
        if (name != skin.displayed_channel || !skin.channel_name_scrolling) {
            skin.displayed_channel = name;
            skin.channel_name_started = now;
        }
        if (!skin.channel_name_scrolling)
            skin.channel_name_scrolling = SetTimer(panel.hwnd,channel_name_timer,40,nullptr) != 0;
        const LONG offset = channel_name_offset(overflow,now-skin.channel_name_started);
        const int saved = SaveDC(dc);
        if (saved) {
            const auto& area = current_channel_name;
            IntersectClipRect(dc,area.left,area.top,area.right,area.bottom);
            RECT line{area.left-offset,area.top,measured.right-offset,area.bottom};
            SetTextColor(dc,RGB(255,255,255));
            DrawTextW(dc,name.c_str(),count,&line,DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            RestoreDC(dc,saved);
        }
    } else {
        skin.stop_channel_name_scroll();
        text(dc,name.c_str(),current_channel_name,RGB(255,255,255));
    }
    if (font) SelectObject(dc,font);
}

void meter(HDC dc, RECT box, double peak) {
    const auto level = reference_layout::meter_peak_level(peak);
    for (int x = box.left + 1; x + 2 < box.right; x += 4) {
        if (x > box.left + (box.right - box.left)*level) break;
        fill(dc, {x,box.top+1,x+2,box.bottom-1}, peak > 1.0 && x > box.right-12
            ? RGB(237,127,73) : pale);
    }
}

void paint_scene(const Panel& panel, HDC dc, HDC source) {
    const auto& skin = *panel.skin;
    const RECT bounds{0,0,width,heights[panel.index]};
    static constexpr const wchar_t* titles[]{L"声声静听",L"效果器",L"通道列表"};
    if (const auto* image = skin.background(panel.index)) {
        image->draw(dc, source, 0, 0);
        // The old titles and inactive progress groove are baked into TT.experience.
        // Restore the title gradient from the same scanlines of the original.
        image->copy_patch(dc,source,{12,4,102,23},105);
        const LONG left = panel.index == 0 ? 27 : 16;
        text(dc,titles[panel.index],{left+1,5,105,24},RGB(246,251,254));
        text(dc,titles[panel.index],{left,4,104,23},RGB(63,100,128));
        if (panel.index == 0) {
            fill(dc,legacy_progress,image->color_at(175,99));
            for (const LONG x : {215L,247L,285L})
                image->copy_patch(dc,source,{x,91,x+1,108},x-1);
        }
    } else {
        fill(dc, bounds, well);
        fill(dc, {1,1,width-1,bounds.bottom-1}, RGB(146,173,193));
        for (LONG y = 2; y < bounds.bottom-2; ++y) {
            const auto shade = static_cast<BYTE>(64 + 16*y/bounds.bottom);
            fill(dc, {2,y,width-2,y+1}, RGB(shade,shade+24,shade+48));
        }
        fill(dc, {6,4,195,23}, well);
        text(dc, titles[panel.index], {12,4,193,23});
    }
    const auto selected_index = skin.channel_index();
    const auto* selected = skin.channel();
    const auto channel_name = selected_index ? wide(skin.state.display_name(*selected_index)) : L"—";
    const double left = selected ? selected->peak_l : 0;
    const double right = selected ? selected->peak_r : 0;
    switch (panel.index) {
    case 0: {
        if (skin.small_icon)
            DrawIconEx(dc,player_icon.left,player_icon.top,skin.small_icon,player_icon.right-player_icon.left,
                       player_icon.bottom-player_icon.top,0,nullptr,DI_NORMAL);
        draw_channel_name(panel,dc,channel_name);
        text(dc, selected && selected->channel_mode == 1 ? L"MONO" : L"STEREO", {205,41,277,55}, pale, DT_RIGHT);
        text(dc, skin.state.transport_playing().value_or(false) ? L"PLAY" : L"STOP", {205,63,277,77}, pale, DT_RIGHT);
        meter(dc, {18,60,166,69}, left);
        meter(dc, {18,73,166,82}, right);
        if (selected) {
            const auto level = (std::clamp(selected->gain_db,-60.,0.)+60.)/60.;
            if (const auto* image = skin.bitmap(L"progress2")) {
                const LONG top = volume.top + (volume.bottom-volume.top-image->size.cy)/2;
                const LONG bottom = top + image->size.cy;
                const int saved = SaveDC(dc);
                if (saved) {
                    IntersectClipRect(dc,volume.left,bottom-static_cast<LONG>(std::lround(level*image->size.cy)),volume.right,bottom);
                    image->draw(dc,source,volume.left,top);
                    RestoreDC(dc,saved);
                }
            } else {
                fill(dc, volume, well);
                auto track = volume;
                track.top = track.bottom-static_cast<LONG>(std::lround(level*(volume.bottom-volume.top)));
                fill(dc,track,pale);
            }
        }
        const double peak = std::max(left, right);
        const auto level = peak > 0.001
            ? std::to_wstring(std::lround(std::clamp(20 * std::log10(peak), -60., 12.))) : L"--";
        if (const auto* led = skin.bitmap(L"number")) {
            // Sonic853 DrawSkinLed uses twelve 5px glyphs, digits, colon, minus.
            LONG x = 208 - static_cast<LONG>(level.size())*5;
            for (const wchar_t ch : level) {
                led->draw(dc,source,x,95,12,ch == L'-' ? 11 : ch-L'0');
                x += 5;
            }
        } else text(dc,level.c_str(),{149,92,208,106},pale,DT_RIGHT);
        break;
    }
    case effects_panel:
        fill(dc, effect_list, well);
        if (selected) for (int row = 0; row < effect_rows; ++row) {
            const auto index = static_cast<std::size_t>(skin.first_effect + row);
            if (index >= selected->effects.size()) break;
            const auto& effect = selected->effects[index];
            const LONG y = effect_list.top + row*row_height;
            const RECT line{effect_list.left,y,effect_list.right,y+row_height};
            if (skin.effect_id == effect.instance) fill(dc, line, RGB(66,107,136));
            const COLORREF color = effect.bypass || effect.offline ? pale : ink;
            text(dc, effect.bypass ? L"○" : L"●", {16,y,31,y+row_height}, color, DT_CENTER);
            const auto name = wide(skin.state.effect_display_name(effect.instance, effect.name));
            text(dc, name.c_str(), {36,y,234,y+row_height}, color);
            const auto value = effect.offline ? L"—" : wide(skin.state.effect_value_text(*selected_index, index));
            text(dc, value.c_str(), {239,y,effect_menu_x-4,y+row_height}, color, DT_RIGHT);
            text(dc,L"⋯",{effect_menu_x,y,313,y+row_height},ink,DT_CENTER);
        }
        break;
    case channels_panel: {
        const auto& labels = skin.state.labels();
        const auto cell = [&](const TableColumn& col, LONG top, LONG bottom,
                              const wchar_t* value, COLORREF color) {
            text(dc,value,col.at(top,bottom),color,col.align);
        };
        const auto heading = [&](const TableColumn& col, const wchar_t* value) {
            cell(col,31,48,value,pale);
        };
        heading(channel_name_col,channel_heading(labels).c_str());
        heading(channel_input_col,wide(labels.input).c_str());
        heading(channel_gain_col,L"dB");
        heading(channel_fx_col,L"FX");
        constexpr const wchar_t* status_labels[]{L"M",L"S",L"R"};
        for (int i = 0; i < 3; ++i) heading(channel_status_cols[i],status_labels[i]);
        fill(dc,channel_list,well);
        for (int row = 0; row < channel_rows; ++row) {
            const auto position = static_cast<std::size_t>(skin.first_channel+row);
            if (position >= skin.visible_channels.size()) break;
            const auto index = skin.visible_channels[position];
            const auto& channel = skin.state.channels()[index];
            const LONG y = channel_list.top+row*row_height;
            const LONG bottom = y+row_height;
            if (selected_index == index) fill(dc,{11,y,316,bottom},RGB(66,107,136));
            else if (position % 2 != 0) fill(dc,{11,y,316,bottom},RGB(46,73,99));
            const auto name = wide(skin.state.display_name(index));
            const auto input_label = skin.state.input_display_name(index);
            const auto input = input_label == labels.input ? std::wstring{} : compact_input_label(input_label);
            wchar_t gain[32]{};
            swprintf_s(gain,L"%.1f",channel.gain_db);
            const auto effects = std::to_wstring(channel.effects.size());
            const auto color = channel.muted ? pale : ink;
            if (selected_index == index) text(dc,L"▶",{16,y,32,bottom},color,DT_CENTER);
            cell(channel_name_col,y,bottom,name.c_str(),color);
            cell(channel_input_col,y,bottom,input.c_str(),color);
            cell(channel_gain_col,y,bottom,gain,color);
            cell(channel_fx_col,y,bottom,effects.c_str(),channel.fx_bypass ? pale : color);
            if (channel.muted) cell(channel_status_cols[0],y,bottom,L"M",RGB(245,208,120));
            if (channel.solo) cell(channel_status_cols[1],y,bottom,L"S",RGB(185,228,143));
            if (channel.recording || channel.recording_stopping)
                cell(channel_status_cols[2],y,bottom,L"R",channel.recording_stopping ? pale : RGB(255,163,163));
        }
        break;
    }
    }
    for (const auto& button : panel.buttons) draw_button(skin, button, dc, source);
}

void paint(const Panel& panel, HDC target) {
    HDC canvas = CreateCompatibleDC(target);
    HDC source = CreateCompatibleDC(target);
    HBITMAP buffer = CreateCompatibleBitmap(target, width, heights[panel.index]);
    HFONT font = CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"Tahoma");
    if (canvas && source && buffer) {
        const auto prior_bitmap = SelectObject(canvas, buffer);
        const auto prior_font = font ? SelectObject(canvas, font) : nullptr;
        SetBkMode(canvas, TRANSPARENT);
        fill(canvas, {0,0,width,heights[panel.index]}, key);
        try { paint_scene(panel, canvas, source); } catch (...) {}
        RECT client{};
        GetClientRect(panel.hwnd, &client);
        SetStretchBltMode(target, COLORONCOLOR);
        StretchBlt(target,0,0,client.right,client.bottom,canvas,0,0,width,heights[panel.index],SRCCOPY);
        if (prior_font) SelectObject(canvas, prior_font);
        SelectObject(canvas, prior_bitmap);
    }
    if (font) DeleteObject(font);
    if (buffer) DeleteObject(buffer);
    if (source) DeleteDC(source);
    if (canvas) DeleteDC(canvas);
}

int hit_button(const Panel& panel, POINT point) {
    for (std::size_t i = 0; i < panel.buttons.size(); ++i) {
        const auto& button = panel.buttons[i];
        if (!PtInRect(&button.bounds, point)) continue;
        if (relabelled_button(button.action)) return static_cast<int>(i);
        const auto* image = panel.skin->button_bitmap(button);
        if (!image || image->opaque(point.x-button.bounds.left, point.y-button.bounds.top))
            return static_cast<int>(i);
    }
    return -1;
}

POINT logical(HWND hwnd, POINT point) {
    RECT bounds{};
    GetClientRect(hwnd, &bounds);
    // Preserve the exclusive right and bottom hit-test edges after scaling.
    const auto ratio = [](LONG value, LONG source, LONG target) {
        return value < 0 ? -1L : value * target / std::max(1L, source);
    };
    const auto* panel = reinterpret_cast<const Panel*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    return {ratio(point.x,bounds.right,width), ratio(point.y,bounds.bottom,heights[panel->index])};
}

LRESULT dispatch(Panel& panel, UINT message, WPARAM wp, LPARAM lp) {
    auto& skin = *panel.skin;
    const auto hwnd = panel.hwnd;
    if (skin.menu_message(hwnd,message,wp,lp)) return 0;
    const auto physical = [&]() -> POINT { return {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; };
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        if (HDC dc = BeginPaint(hwnd, &ps)) paint(panel, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        if (panel.index == 0 && wp == channel_name_timer) {
            if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) skin.stop_channel_name_scroll();
            else {
                RECT client{};
                GetClientRect(hwnd,&client);
                const auto& area = current_channel_name;
                const RECT damage{MulDiv(area.left,client.right,width),MulDiv(area.top,client.bottom,heights[0]),
                    MulDiv(area.right,client.right,width),MulDiv(area.bottom,client.bottom,heights[0])};
                InvalidateRect(hwnd,&damage,FALSE);
            }
            return 0;
        }
        break;
    case WM_SHOWWINDOW:
        if (panel.index == 0 && !wp) skin.stop_channel_name_scroll();
        break;
    case WM_DPICHANGED: {
        const auto& rect = *reinterpret_cast<const RECT*>(lp);
        const UINT dpi = HIWORD(wp);
        SetWindowPos(hwnd,nullptr,rect.left,rect.top,MulDiv(width,dpi,96),
                     MulDiv(heights[panel.index],dpi,96),SWP_NOACTIVATE | SWP_NOZORDER);
        skin.apply_region(panel, dpi);
        return 0;
    }
    case WM_SIZE:
        if (panel.index == 0 && skin.shown && skin.initialized) {
            skin.main_size_changed(wp == SIZE_MINIMIZED);
            skin.show_auxiliaries(wp != SIZE_MINIMIZED && IsWindowVisible(hwnd));
        }
        break;
    case WM_CLOSE:
        skin.invoke(panel,Action::close);
        return 0;
    case WM_CONTEXTMENU: {
        POINT screen = physical();
        const bool keyboard = screen.x == -1 && screen.y == -1;
        if (keyboard) { screen = {14,20}; ClientToScreen(hwnd,&screen); }
        POINT client = screen;
        ScreenToClient(hwnd,&client);
        const auto effect = panel.index == effects_panel
            ? (keyboard ? skin.effect_index() : skin.effect_at(logical(hwnd,client))) : std::nullopt;
        const auto clicked_channel = panel.index == channels_panel && !keyboard ? skin.channel_at(logical(hwnd,client)) : std::nullopt;
        const auto channel = clicked_channel ? clicked_channel : skin.channel_index();
        skin.open_menu(hwnd,screen,panel.index == 0 ? std::nullopt : channel,effect);
        return 0;
    }
    case WM_KEYDOWN: {
        POINT screen{12,22};
        ClientToScreen(hwnd,&screen);
        const auto effect = skin.effect_index();
        if (wp == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000))
            return dispatch(panel,WM_CONTEXTMENU,0,-1);
        if (wp == VK_F10) skin.open_menu(hwnd,screen);
        else if (wp == VK_F6) skin.adjust_transparency((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
        else if (wp == VK_F2) skin.toggle_auxiliary(effects_panel);
        else if (wp == VK_F3) skin.toggle_auxiliary(channels_panel);
        else if (wp == VK_F4) skin.show_channels();
        else if (wp == VK_SPACE && panel.index == 0) skin.invoke(panel,Action::play);
        else if (wp == VK_SPACE && effect && panel.index == effects_panel) skin.bypass_effect(*effect);
        else if (wp == VK_RETURN && effect && panel.index == effects_panel) skin.open_effect(*effect);
        else if (wp == VK_ESCAPE && GetCapture() == hwnd) ReleaseCapture();
        else if (panel.index == channels_panel && (wp == VK_UP || wp == VK_DOWN)) {
            skin.step_channel(wp == VK_UP ? -1 : 1);
        } else if (panel.index == effects_panel && (wp == VK_UP || wp == VK_DOWN)) {
            if (const auto* selected = skin.channel(); selected && !selected->effects.empty()) {
                const int row = effect ? static_cast<int>(*effect) + (wp == VK_UP ? -1 : 1) : skin.first_effect;
                const int next = std::clamp(row, 0, static_cast<int>(selected->effects.size())-1);
                skin.effect_id = selected->effects[next].instance;
                skin.first_effect = std::clamp(std::clamp(skin.first_effect, next-effect_rows+1, next), 0, skin.max_scroll());
                skin.sync_buttons();
            }
        } else if (panel.index == effects_panel && effect && (wp == VK_LEFT || wp == VK_RIGHT)) {
            skin.adjust_effect(*effect, wp == VK_LEFT ? -1 : 1);
        } else break;
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) {
            skin.transparency_wheel_remainder += GET_WHEEL_DELTA_WPARAM(wp);
            skin.adjust_transparency(skin.transparency_wheel_remainder / WHEEL_DELTA);
            skin.transparency_wheel_remainder %= WHEEL_DELTA;
            return 0;
        }
        if (panel.index != 0) {
            POINT client = physical();
            ScreenToClient(hwnd,&client);
            const auto point = logical(hwnd,client);
            skin.wheel_remainder += GET_WHEEL_DELTA_WPARAM(wp);
            const int steps = skin.wheel_remainder / WHEEL_DELTA;
            skin.wheel_remainder %= WHEEL_DELTA;
            if (panel.index == channels_panel) {
                skin.first_channel = std::clamp(skin.first_channel-steps,0,skin.max_channel_scroll());
                skin.repaint();
            } else if (const auto effect = skin.effect_at(point); effect && point.x >= 235 && point.x < effect_menu_x)
                skin.adjust_effect(*effect,steps);
            else {
                skin.first_effect = std::clamp(skin.first_effect-steps,0,skin.max_scroll());
                skin.repaint();
            }
            return 0;
        }
        break;
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN: {
        const auto point = logical(hwnd,physical());
        const int button = hit_button(panel,point);
        if (button >= 0) {
            if (panel.buttons[button].state.enabled) {
                SetCapture(hwnd);
                panel.pressed = button;
                panel.buttons[button].state.Down();
            }
        } else if (panel.index == 0 && PtInRect(&volume,point)) {
            if (const auto index = skin.channel_index()) {
                SetCapture(hwnd);
                skin.volume_drag = skin.state.gain_control(*index);
                skin.volume_sent.reset();
                skin.update_volume(point.y);
            }
        } else if (const auto item = panel.index == channels_panel ? skin.channel_at(point) : std::nullopt) {
            const auto& selected = skin.state.channels()[*item];
            skin.choose_channel({selected.id,selected.identity});
        } else if (const auto effect = panel.index == effects_panel ? skin.effect_at(point) : std::nullopt) {
            skin.effect_id = skin.channel()->effects[*effect].instance;
            skin.sync_buttons();
            if (point.x >= effect_menu_x && message == WM_LBUTTONDOWN) {
                POINT screen = physical();
                ClientToScreen(hwnd,&screen);
                skin.open_menu(hwnd,screen,skin.channel_index(),effect);
            } else if (message == WM_LBUTTONDBLCLK && point.x >= 32 && point.x < effect_menu_x)
                skin.open_effect(*effect);
            else if (message == WM_LBUTTONDOWN && point.x < 32) skin.bypass_effect(*effect);
        } else if (panel.index == 0 || point.y < 25 ||
                   point.y >= (panel.index == effects_panel ? effect_list.bottom : channel_list.bottom)) {
            POINT cursor{};
            GetCursorPos(&cursor);
            SetCapture(hwnd);
            skin.drag.begin(panel.index,cursor,GetDpiForWindow(hwnd),skin.rects());
        }
        InvalidateRect(hwnd,nullptr,FALSE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (panel.index == 0 && skin.volume_drag && GetCapture() == hwnd) {
            skin.update_volume(logical(hwnd,physical()).y);
            return 0;
        }
        if (skin.drag.panel == panel.index && GetCapture() == hwnd) {
            POINT cursor{};
            GetCursorPos(&cursor);
            skin.move_panels(cursor);
            return 0;
        }
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
        const int hit = hit_button(panel,logical(hwnd,physical()));
        for (int i = 0; i < static_cast<int>(panel.buttons.size()); ++i)
            panel.buttons[i].state.Move(i == hit && (panel.pressed < 0 || i == panel.pressed),
                                        i == panel.pressed);
        InvalidateRect(hwnd,nullptr,FALSE);
        return 0;
    }
    case WM_MOUSELEAVE:
        for (auto& button : panel.buttons) button.state.Move(false,false);
        InvalidateRect(hwnd,nullptr,FALSE);
        return 0;
    case WM_LBUTTONUP: {
        if (panel.index == 0 && skin.volume_drag) skin.update_volume(logical(hwnd,physical()).y);
        const int pressed = panel.pressed;
        const bool invoke = pressed >= 0 &&
            panel.buttons[pressed].state.Release(hit_button(panel,logical(hwnd,physical())) == pressed);
        if (GetCapture() == hwnd) ReleaseCapture();
        if (invoke) skin.invoke(panel,panel.buttons[pressed].action);
        InvalidateRect(hwnd,nullptr,FALSE);
        return 0;
    }
    case WM_CANCELMODE:
        if (GetCapture() == hwnd) ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        if (panel.index == 0) {
            skin.volume_drag.reset();
            skin.volume_sent.reset();
        }
        if (panel.pressed >= 0) panel.buttons[panel.pressed].state.Cancel();
        panel.pressed = -1;
        if (skin.drag.panel == panel.index) skin.drag = {};
        InvalidateRect(hwnd,nullptr,FALSE);
        return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) noexcept {
    auto* panel = reinterpret_cast<Panel*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        panel = static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        panel->hwnd = hwnd;
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(panel));
    }
    if (message == WM_NCDESTROY && panel) {
        panel->hwnd = nullptr;
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);
        panel = nullptr;
    }
    try {
        if (panel) return dispatch(*panel,message,wp,lp);
    } catch (...) {}
    return DefWindowProcW(hwnd,message,wp,lp);
}

void AUDCORE_SKIN_CALL on_event(void* user, const audcore_skin_event* event) noexcept {
    if (!event || event->size < sizeof(*event)) return;
    try { static_cast<Skin*>(user)->receive(*event); } catch (...) {}
}

void close_windows(Skin& skin) {
    skin.cancel_actions();
    skin.stop_channel_name_scroll();
    skin.initialized = false;
    if (skin.registered) {
        skin.host.unregister_window(skin.host.ctx, skin.panels[0].hwnd);
        skin.registered = false;
    }
    for (int i : {2,1,0})
        if (skin.panels[i].hwnd) DestroyWindow(skin.panels[i].hwnd);
    if (skin.class_name[0]) {
        UnregisterClassW(skin.class_name,skin.module);
        skin.class_name[0] = L'\0';
    }
}

bool create_windows(Skin& skin) {
    swprintf_s(skin.class_name,L"AudcoreTTPlayer-%p",static_cast<void*>(&skin));
    WNDCLASSW wc{};
    wc.hInstance = skin.module;
    wc.lpfnWndProc = window_proc;
    wc.lpszClassName = skin.class_name;
    wc.hCursor = LoadCursorW(nullptr,IDC_ARROW);
    wc.hIcon = skin.large_icon;
    wc.style = CS_DBLCLKS;
    if (!RegisterClassW(&wc)) return false;
    const RECT work = ttplayer::ui::DragWorkAreaForRect({});
    auto main = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_LAYERED,skin.class_name,L"声声静听 · Audcore",
                                WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,work.left+48,work.top+48,
                                width,heights[0],nullptr,nullptr,skin.module,&skin.panels[0]);
    if (!main) return false;
    if (skin.small_icon) SendMessageW(main,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(skin.small_icon));
    if (skin.large_icon) SendMessageW(main,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(skin.large_icon));
    constexpr float w = static_cast<float>(width), h = static_cast<float>(heights[0]);
    const audcore_skin_window_spec spec{sizeof(audcore_skin_window_spec),w,h,w,h,w,h,0,0};
    if (skin.host.register_window(skin.host.ctx, main, &spec) != 0) return false;
    skin.registered = true;
    const UINT main_dpi = GetDpiForWindow(main);
    skin.apply_region(skin.panels[0],main_dpi);
    RECT main_rect{};
    GetWindowRect(main,&main_rect);
    LONG top = main_rect.bottom;
    for (int i : {1,2}) {
        auto hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED,skin.class_name,i == effects_panel ? L"效果器" : L"通道列表",
                                   WS_POPUP,main_rect.left,top,MulDiv(width,main_dpi,96),
                                   MulDiv(heights[i],main_dpi,96),main,nullptr,skin.module,&skin.panels[i]);
        if (!hwnd) return false;
        const UINT dpi = GetDpiForWindow(hwnd);
        const LONG height = MulDiv(heights[i],dpi,96);
        SetWindowPos(hwnd,nullptr,main_rect.left,top,MulDiv(width,dpi,96),height,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        skin.apply_region(skin.panels[i],dpi);
        top += height;
    }
    auto positions = skin.rects();
    ttplayer::ui::RecoverWindowLayout(positions, ttplayer::ui::WindowWorkAreas());
    for (int i : {1,2})
        SetWindowPos(skin.panels[i].hwnd,nullptr,positions[i].left,positions[i].top,0,0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
    for (const auto& panel : skin.panels)
        if (!SetLayeredWindowAttributes(panel.hwnd,0,window_alpha(skin.transparency),LWA_ALPHA)) return false;
    skin.initialized = true;
    return true;
}

void* AUDCORE_SKIN_CALL create(const audcore_skin_host* host) noexcept {
    if (!host || host->header.size < sizeof(*host) || host->header.major != AUDCORE_SKIN_ABI_MAJOR ||
        !host->register_window || !host->unregister_window || !host->command ||
        !host->subscribe || !host->unsubscribe || !host->request_snapshot) return nullptr;
    try {
        auto skin = std::make_unique<Skin>();
        skin->channel_name_font = CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Tahoma");
        skin->selector_font = CreateFontW(-11,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Tahoma");
        skin->host = *host;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(&window_proc),&skin->module)) return nullptr;
        auto controls = buttons();
        for (int i = 0; i < 3; ++i) skin->panels[i] = {skin.get(),i,nullptr,true,-1,std::move(controls[i])};
        skin->load_art();
        return skin.release();
    } catch (...) { return nullptr; }
}

int32_t AUDCORE_SKIN_CALL show(void* instance) noexcept {
    auto* skin = static_cast<Skin*>(instance);
    if (!skin || !skin->subscription.start(skin->host,on_event,skin)) return -1;
    try {
        if (!skin->initialized && !create_windows(*skin)) {
            close_windows(*skin);
            skin->subscription.stop(skin->host);
            return -1;
        }
        skin->shown = true;
        skin->sync_buttons();
        ShowWindow(skin->panels[0].hwnd,SW_SHOW);
        skin->show_auxiliaries(!IsIconic(skin->panels[0].hwnd));
        return 0;
    } catch (...) {
        close_windows(*skin);
        skin->subscription.stop(skin->host);
        return -1;
    }
}

void AUDCORE_SKIN_CALL hide(void* instance) noexcept {
    auto& skin = *static_cast<Skin*>(instance);
    skin.cancel_actions();
    skin.stop_channel_name_scroll();
    skin.shown = false;
    skin.ready = false;
    skin.initial_channel_chosen = false;
    skin.was_minimized = false;
    skin.recovery.reset();
    skin.transport.clear();
    skin.effect_pending.clear();
    for (const auto& panel : skin.panels)
        if (panel.hwnd && GetCapture() == panel.hwnd) ReleaseCapture();
    skin.show_auxiliaries(false);
    if (skin.panels[0].hwnd) ShowWindow(skin.panels[0].hwnd,SW_HIDE);
}

void AUDCORE_SKIN_CALL destroy(void* instance) noexcept {
    const std::unique_ptr<Skin> skin(static_cast<Skin*>(instance));
    if (!skin) return;
    skin->subscription.stop(skin->host);
    close_windows(*skin);
}

const audcore_skin_descriptor descriptor{
    {sizeof(audcore_skin_descriptor),AUDCORE_SKIN_ABI_MAJOR,AUDCORE_SKIN_ABI_MINOR},
    {reinterpret_cast<const std::uint8_t*>(id),sizeof(id)-1},
    {reinterpret_cast<const std::uint8_t*>(skin_name),sizeof(skin_name)-1},
    {reinterpret_cast<const std::uint8_t*>(version),sizeof(version)-1},
    create,show,hide,destroy,AUDCORE_SKIN_THEME_DARK,1.0f,1.0f,1.0f
};
}

extern "C" __declspec(dllexport) const audcore_skin_descriptor* AUDCORE_SKIN_CALL
audcore_skin_get_descriptor(std::uint32_t major) noexcept {
    return major == AUDCORE_SKIN_ABI_MAJOR ? &classic::descriptor : nullptr;
}
