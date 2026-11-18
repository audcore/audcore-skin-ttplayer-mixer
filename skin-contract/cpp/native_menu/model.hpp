#pragma once
#include "../reference_skin_state.hpp"

namespace native_menu {
using Json = nlohmann::json;

struct Locale {
    std::string language = "zh";
    const char* operator()(const char* zh, const char* en, const char* ja) const {
        return language == "ja" ? ja : language == "en" ? en : zh;
    }
};

enum class Mark { none, check, radio };

struct Editor {
    std::string field;
    std::string initial;
    std::optional<std::pair<double, double>> range;
    double factor = 1;
};

struct Item {
    std::string label;
    std::string command;
    std::vector<Item> children;
    std::optional<Editor> editor;
    Mark mark = Mark::none;
    bool checked = false;
    bool toggle_topmost = false;
};

inline std::string request(const char* method, Json params = Json::object()) {
    return Json({{"method", method}, {"params", std::move(params)}}).dump();
}

inline Item action(std::string label, std::string command = {}, Mark mark = Mark::none, bool checked = false) {
    return {std::move(label), std::move(command), {}, {}, mark, checked};
}

inline Item group(std::string label, std::vector<Item> children) {
    return {std::move(label), {}, std::move(children), {}};
}

inline Item edit(std::string label, std::string command, Editor editor) {
    return {std::move(label), std::move(command), {}, std::move(editor)};
}

inline std::string number(double value, const char* units = "") {
    char text[64]{};
    std::snprintf(text, sizeof(text), "%.6g%s", value, units);
    return text;
}

inline std::optional<std::string> edited_command(const Item& item, std::string text) {
    const auto& editor = *item.editor;
    Json command = Json::parse(item.command);
    if (editor.range) {
        const auto start = text.find_first_not_of(" \t");
        if (start == std::string::npos) return {};
        text = text.substr(start, text.find_last_not_of(" \t") - start + 1);
        if (text.front() == '+') text.erase(0, 1);
        double value = 0;
        const auto end = text.data() + text.size();
        const auto parsed = std::from_chars(text.data(), end, value);
        if (parsed.ec != std::errc() || parsed.ptr != end || !std::isfinite(value) ||
            value < editor.range->first || value > editor.range->second) return {};
        command["params"][editor.field] = value * editor.factor;
    } else {
        command["params"][editor.field] = std::move(text);
    }
    return command.dump();
}

inline std::vector<Item> effect_items(const ReferenceSkinState& state, const Locale& tr,
                                      std::size_t index, std::size_t slot, long x, long y) {
    if (index >= state.channels().size() || slot >= state.channels()[index].effects.size()) return {};
    const auto& channel = state.channels()[index];
    const auto& effect = channel.effects[slot];
    auto effect_cmd = [&](const char* method, Json params = Json::object()) {
        params["id"] = channel.id;
        params["instance"] = effect.instance;
        return state.effect_request(index, slot, {{"method", method}, {"params", std::move(params)}});
    };
    std::vector<Item> rows{
        action(tr("旁通", "Bypass", "バイパス"),
               effect.offline ? "" : effect_cmd("effect.setBypass", {{"bypass", !effect.bypass}}), Mark::check, effect.bypass),
        action(tr("打开界面…", "Open editor…", "編集画面…"), state.effect_editor_command(index, slot)),
        action(tr("上移", "Move up", "上へ"), slot ? state.effect_move_command(index, slot, slot - 1) : ""),
        action(tr("下移", "Move down", "下へ"), slot + 1 < channel.effects.size() ? state.effect_move_command(index, slot, slot + 1) : ""),
        action(tr("移除", "Remove", "削除"), effect_cmd("effect.remove")),
        action(tr("离线、别名与快捷键…", "Offline, alias & shortcut…", "休止・別名・キー…"),
               state.effect_menu_command(index, slot, x, y))
    };
    if (!effect.offline) {
        rows.insert(rows.begin() + 2, edit(std::string(tr("混合", "Mix", "混合")) + " " + number(effect.wet * 100, "%") + "…",
                    effect_cmd("effect.setWet"), {"wet", number(effect.wet * 100), {{0, 100}}, 0.01}));
    }
    return rows;
}

inline std::vector<Item> channel_items(const ReferenceSkinState& state, const Locale& tr,
                                       std::size_t index, long x, long y) {
    if (index >= state.channels().size()) return {};
    const auto& channel = state.channels()[index];
    auto channel_cmd = [&](const char* method, Json params = Json::object()) {
        params["id"] = channel.id;
        return state.channel_request(index, {{"method", method}, {"params", std::move(params)}});
    };
    std::vector<Item> effects;
    effects.push_back(action(tr("旁通效果链", "Bypass rack", "全体バイパス"),
                             state.fx_bypass_command(index), Mark::check, channel.fx_bypass));
    effects.push_back(action(tr("添加效果器…", "Add effect…", "効果を追加…"), state.effect_browser_command(index)));
    for (std::size_t slot = 0; slot < channel.effects.size(); ++slot) {
        const auto& effect = channel.effects[slot];
        effects.push_back(group(state.effect_display_name(effect.instance,effect.name),
                                effect_items(state,tr,index,slot,x,y)));
    }

    std::vector<Item> sends{action(tr("添加发送…", "Add send…", "送信を追加…"), state.add_send_menu_command(index, x, y))};
    for (std::size_t send = 0; send < channel.sends.size(); ++send) {
        const auto& route = channel.sends[send];
        auto send_cmd = [&](const char* method, Json params = Json::object()) {
            params["id"] = channel.id;
            params["index"] = send;
            return state.send_request(index, send, {{"method", method}, {"params", std::move(params)}});
        };
        const auto enabled = !route.contains("enabled") || route.at("enabled") != false;
        const auto tap = route.contains("tap") && route.at("tap").is_number() ? route.at("tap").get<int>() : 2;
        const auto level = route.value("level_db", 0.0);
        std::vector<Item> rows{
            action(tr("启用", "Enabled", "有効"), send_cmd("channel.setSendEnabled", {{"enabled", !enabled}}), Mark::check, enabled),
            edit(std::string(tr("电平", "Level", "音量")) + " " + number(level, " dB") + "…",
                 send_cmd("channel.setSendLevel"), {"db", number(level), {{-60, 6}}}),
            group(tr("取样点", "Tap point", "送信位置"), {
                action(tr("效果前", "Pre-FX", "効果前"), send_cmd("channel.setSendTap", {{"tap", 0}}), Mark::radio, tap == 0),
                action(tr("效果后", "Post-FX", "効果後"), send_cmd("channel.setSendTap", {{"tap", 1}}), Mark::radio, tap == 1),
                action(tr("推子后", "Post-fader", "音量後"), send_cmd("channel.setSendTap", {{"tap", 2}}), Mark::radio, tap == 2)
            }),
            action(tr("删除发送", "Remove send", "送信を削除"), send_cmd("channel.removeSend"))
        };
        if (route.at("dest").value("kind", "") == "output") {
            rows.insert(rows.begin() + 1, action(tr("输出设备…", "Output device…", "出力機器…"),
                                                state.send_menu_command(index, send, x, y, true)));
        }
        const auto reason = state.send_reason(index, send);
        if (!reason.empty()) rows.insert(rows.begin(), action(reason));
        sends.push_back(group(state.send_display_name(index, send), std::move(rows)));
    }

    const bool solo_available = channel.id < 32;
    return {
        group(state.labels().input, {
            action(tr("选择输入", "Choose input", "入力選択") + (" · " + state.input_display_name(index)),
                   state.input_menu_command(index, x, y)),
            action(tr("当前输入操作…", "Input actions…", "入力操作…"), state.channel_menu_command(index, x, y, true))
        }),
        edit(std::string(tr("增益", "Gain", "利得")) + " " + number(channel.gain_db, " dB") + "…",
             channel_cmd("channel.setGain"), {"db", number(channel.gain_db), {{-60, 12}}}),
        action(state.labels().mute, state.toggle_command(index, false), Mark::check, channel.muted),
        action(state.labels().solo, solo_available ? state.toggle_command(index, true) : "", Mark::check, channel.solo),
        action(tr("独奏常驻", "Solo safe", "独奏保護"), solo_available ? channel_cmd("channel.setSoloSafe", {{"safe", !channel.solo_safe}}) : "",
               Mark::check, channel.solo_safe),
        action(tr("独占与解除独奏", "Exclusive & clear solo", "単独・解除"), solo_available ? state.solo_menu_command(index, x, y) : ""),
        action(tr("反相", "Invert phase", "位相反転"), state.phase_command(index), Mark::check, std::abs(channel.phase) > 90),
        group(state.labels().recording, {
            action(state.labels().recording, state.audio_running() == false ? "" : state.recording_command(index), Mark::check, channel.recording),
            action(tr("录音取样点与分组…", "Recording tap & group…", "録音位置・組…"), state.recording_menu_command(index, x, y))
        }),
        group(state.labels().effects, std::move(effects)),
        group(state.labels().send, std::move(sends)),
        group(tr("链接", "Link", "連動"), {
            action(tr("选择分组", "Choose group", "組を選択"), state.link_menu_command(index, x, y, false)),
            action(tr("链接设置…", "Link settings…", "連動設定…"), state.link_menu_command(index, x, y, true))
        }),
        action(tr("快捷键…", "Shortcut…", "キー設定…"), state.shortcut_menu_command(index, x, y)),
        group(tr("通道属性", "Properties", "通路設定"), {
            edit(tr("重命名…", "Rename…", "名前変更…"), channel_cmd("channel.setName"), {"name", channel.name, {}}),
            action(tr("隐藏", "Hidden", "非表示"), channel_cmd("channel.setHidden", {{"hidden", !channel.hidden}}), Mark::check, channel.hidden),
            action(tr("靠右排列", "Align right", "右寄せ"), channel_cmd("channel.setAlignRight", {{"right", !channel.align_right}}), Mark::check, channel.align_right),
            action(tr("往前移", "Move earlier", "前へ"), state.channel_order_step(index, false)),
            action(tr("往后移", "Move later", "後へ"), state.channel_order_step(index, true)),
            action(tr("格式、颜色与图标…", "Format, color & icon…", "形式・色・印…"), channel_cmd("menu.openChannelSettings")),
            action(tr("复制与导出…", "Copy & export…", "複製・書出…"), state.channel_menu_command(index, x, y)),
            action(tr("删除通道", "Remove channel", "通路を削除"), channel_cmd("channel.remove"))
        })
    };
}

inline std::vector<Item> menu(const ReferenceSkinState& state, const Locale& tr, long x, long y, bool topmost, bool floating_display = true) {
    const Json anchor{{"x", x}, {"y", y}, {"space", "screen-physical"}};
    std::vector<Item> channels;
    for (std::size_t index = 0; index < state.channels().size(); ++index) {
        const auto& channel = state.channels()[index];
        channels.push_back(group(channel.name.empty() ? state.display_name(index) : channel.name,
                                 channel_items(state,tr,index,x,y)));
    }
    channels.push_back(group(state.labels().add, {
        action(tr("单声道", "Mono", "単音"), request("channel.add", {{"mode", 1}})),
        action(tr("立体声", "Stereo", "立体音"), request("channel.add", {{"mode", 0}}))
    }));
    std::vector<Item> rows{
        group(tr("工程", "Project", "工程"), {
            action(tr("新建空白预设", "New blank preset", "空の設定"), request("document.new")),
            action(tr("打开文件…", "Open file…", "開く…"), request("document.open")),
            action(tr("保存预设", "Save preset", "保存"), request("document.save")),
            action(tr("最近文件与预设模板…", "Recent files & templates…", "履歴・ひな型…"), request("menu.openSystem", {{"anchor", anchor}})),
            action(tr("撤销", "Undo", "元に戻す"), state.history_command(false)),
            action(tr("重做", "Redo", "やり直す"), state.history_command(true))
        }),
        group(tr("所有通道", "All channels", "全通路"), std::move(channels)),
        group(tr("传输", "Transport", "再生"), {
            action(tr("播放", "Playing", "再生"), state.transport_command(), Mark::check, state.transport_playing().value_or(false)),
            action(tr("停止与重播…", "Stop & restart…", "停止・再開…"), state.transport_menu_command(x, y))
        }),
        group(tr("显示", "Display", "表示"), {
            group(tr("主题", "Theme", "配色"), {
                action(tr("深色", "Dark", "暗色"), request("appearance.setTheme", {{"theme", "dark"}}), Mark::radio, state.dark()),
                action(tr("浅色", "Light", "明色"), request("appearance.setTheme", {{"theme", "light"}}), Mark::radio, !state.dark())
            }),
            group(tr("图标缩放", "Icon scale", "表示倍率"), {
                action("75%", request("appearance.setScale", {{"scale", 0.75}}), Mark::radio, state.scale() == 0.75),
                action("100%", request("appearance.setScale", {{"scale", 1.0}}), Mark::radio, state.scale() == 1.0),
                action("125%", request("appearance.setScale", {{"scale", 1.25}}), Mark::radio, state.scale() == 1.25),
                action("150%", request("appearance.setScale", {{"scale", 1.5}}), Mark::radio, state.scale() == 1.5)
            })
        }),
        action(state.labels().settings, request("menu.openSystem", {{"anchor", anchor}, {"settingsSections", true}})),
        action(state.labels().performance + "…", state.panel_command(true)),
        {tr("保持最前", "Always on top", "最前面"), {}, {}, {}, Mark::check, topmost, true},
        action(floating_display ? tr("关闭悬浮窗", "Close floating window", "小窓を閉じる")
                               : tr("关闭窗口", "Close window", "窓を閉じる"), request("window.close"))
    };
    if (!floating_display) rows.erase(rows.begin()+3);
    return rows;
}
}
