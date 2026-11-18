#include "ttplayer_skin.cpp"
#include <cassert>
#include <cstdio>

namespace {
void channel_table_layout() {
    using namespace classic;
    constexpr std::array columns{channel_name_col,channel_input_col,channel_gain_col,channel_fx_col,
        channel_status_cols[0],channel_status_cols[1],channel_status_cols[2]};
    LONG right = channel_list.left;
    for (const auto& col : columns) {
        assert(col.left >= right && col.right > col.left);
        right = col.right;
    }
    assert(right <= channel_list.right);
    assert(channel_gain_col.align == DT_RIGHT && channel_fx_col.align == DT_RIGHT);
    ReferenceLabels labels;
    assert(channel_heading(labels) == L"通道");
    labels.channel_template = "Ch {}";
    assert(channel_heading(labels) == L"Ch");
    labels.channel_template = "チャンネル{}";
    assert(channel_heading(labels) == L"チャンネル");

    const auto dc = CreateCompatibleDC(nullptr);
    const auto font = CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"Tahoma");
    assert(dc && font);
    const auto previous = SelectObject(dc,font);
    const auto fits = [&](const TableColumn& col, const wchar_t* value) {
        SIZE size{};
        assert(GetTextExtentPoint32W(dc,value,static_cast<int>(wcslen(value)),&size));
        assert(size.cx <= col.right-col.left && size.cy <= row_height);
    };
    fits(channel_gain_col,L"-60.0");
    fits(channel_fx_col,L"4096");
    fits(channel_name_col,L"チャンネル");
    fits(channel_input_col,L"Input");
    fits(channel_input_col,L"入力");
    fits(channel_status_cols[0],L"M");
    SelectObject(dc,previous);
    DeleteObject(font);
    DeleteDC(dc);
}

void main_panel_labels(const classic::Skin& skin) {
    using namespace classic;
    const auto& panel = skin.panels[0];
    const int channels = hit_button(panel,{268,99});
    const int effects = hit_button(panel,{310,99});
    assert(channels >= 0 && panel.buttons[channels].action == Action::channels);
    assert(effects >= 0 && panel.buttons[effects].action == Action::effects);
    assert(std::wstring_view(panel.buttons[channels].label) == L"通道列表");
    assert(std::wstring_view(panel.buttons[effects].label) == L"效果器");
    assert(hit_button(panel,{244,14}) == -1);
    const int minimize = hit_button(panel,{267,14});
    assert(minimize >= 0 && panel.buttons[minimize].action == Action::minimize);
    assert(channel_name_offset(0,5000) == 0);
    assert(channel_name_offset(90,999) == 0);
    assert(channel_name_offset(90,2500) == 45);
    assert(channel_name_offset(90,4500) == 90);
    assert(channel_name_offset(90,5000) == 0);
    const auto dc = CreateCompatibleDC(nullptr);
    assert(dc && skin.channel_name_font && skin.selector_font);
    const auto old = SelectObject(dc,skin.selector_font);
    for (const int index : {channels,effects}) {
        const auto& button = panel.buttons[index];
        auto measured = button.bounds;
        DrawTextW(dc,button.label,-1,&measured,DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
        assert(measured.right <= button.bounds.right && measured.bottom <= button.bounds.bottom);
    }
    SelectObject(dc,skin.channel_name_font);
    auto measured = current_channel_name;
    DrawTextW(dc,L"麦克风",-1,&measured,DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
    assert(measured.right <= current_channel_name.right && measured.bottom <= current_channel_name.bottom);
    SelectObject(dc,old);
    DeleteDC(dc);
}

struct Mock {
    std::uint64_t requests = 0;
    std::uint64_t recovery = 0;
    std::uint64_t unsubscribed = 0;
    nlohmann::json last;
};
audcore_skin_host host_for(Mock& mock) {
    return {{sizeof(audcore_skin_host),0,2}, &mock,
        [](void*,void*,const audcore_skin_window_spec*) -> int32_t { assert(false); return -1; },
        [](void*,void*) { assert(false); },
        [](void* ctx,std::uint64_t request,audcore_skin_bytes bytes) -> int32_t {
            auto& mock = *static_cast<Mock*>(ctx);
            assert(request > mock.requests);
            mock.requests = request;
            mock.last = nlohmann::json::parse(bytes.data,bytes.data+bytes.len);
            return 0;
        },
        [](void*,audcore_skin_event_fn,void*,std::uint64_t* token) -> int32_t { *token = 27; return 0; },
        [](void* ctx,std::uint64_t token) {
            assert(token == 27);
            ++static_cast<Mock*>(ctx)->unsubscribed;
        },
        [](void* ctx,std::uint64_t token) -> int32_t {
            assert(token == 27);
            ++static_cast<Mock*>(ctx)->recovery;
            return 0;
        }};
}

void event(classic::Skin& skin, std::uint32_t kind, std::uint64_t revision, std::string_view json) {
    const audcore_skin_event value{sizeof(value),kind,revision,0,0,
        {reinterpret_cast<const std::uint8_t*>(json.data()),static_cast<std::uint32_t>(json.size())}};
    classic::on_event(&skin,&value);
}

void mic_startup() {
    Mock mock;
    auto host = host_for(mock);
    auto* skin = static_cast<classic::Skin*>(classic::create(&host));
    assert(skin);
    event(*skin,AUDCORE_SKIN_EVENT_SNAPSHOT,1,R"({"channels":[]})");
    assert(!skin->initial_channel_chosen);
    constexpr auto channels = R"({"channels":[{"id":1,"name":"Music"},{"id":2,"name":"Mic","hidden":true},{"id":3,"name":"麦克风"},{"id":4,"name":" mIC "}]})";
    event(*skin,AUDCORE_SKIN_EVENT_SNAPSHOT,2,channels);
    assert(skin->channel()->id == 4);
    assert(skin->choose_channel({1,skin->state.channels()[0].identity}));
    event(*skin,AUDCORE_SKIN_EVENT_SNAPSHOT,3,channels);
    assert(skin->channel()->id == 1);
    classic::hide(skin);
    assert(!skin->initial_channel_chosen);
    constexpr auto unnamed = R"({"channels":[{"id":0,"name":"","icon":"🎶"},{"id":1,"name":"","icon":"🎙️"},{"id":3,"name":"","icon":"🎤"}]})";
    event(*skin,AUDCORE_SKIN_EVENT_SNAPSHOT,4,unnamed);
    assert(skin->channel()->id == 1 && skin->state.display_name(1) == "麦克风");
    assert(skin->choose_channel({3,skin->state.channels()[2].identity}));
    event(*skin,AUDCORE_SKIN_EVENT_SNAPSHOT,5,unnamed);
    assert(skin->channel()->id == 3);
    skin->main_size_changed(true);
    skin->main_size_changed(false);
    assert(skin->channel()->id == 1);
    assert(skin->choose_channel({3,skin->state.channels()[2].identity}));
    skin->main_size_changed(false);
    assert(skin->channel()->id == 3);
    classic::destroy(skin);
    using classic::compact_input_label;
    assert(compact_input_label("Analog Input 11/12 (Fireface)") == L"In 11/12");
    assert(compact_input_label("Input 2 · L") == L"In 2");
    assert(compact_input_label("入力 3/4") == L"In 3/4");
    assert(compact_input_label("Virtual REC 5/6") == L"Rec 5/6");
    assert(compact_input_label("Realtek MIC") == L"Realtek MIC");
    assert(compact_input_label("Input 1/") == L"Input 1/");
    for (const char* label : {"","空输入","No in","入力なし"})
        assert(compact_input_label(label).empty());
}

void host_commands() {
    Mock mock;
    auto host = host_for(mock);
    auto* skin = static_cast<classic::Skin*>(classic::create(&host));
    assert(skin && !skin->classic_art);
    assert(skin->subscription.start(host,classic::on_event,skin));
    constexpr auto snapshot = R"({"transport":{"playing":false},"channels":[{"id":7,"name":"Vocal","chain_uuid":"vocal","rack":[{"instance":42,"name":"Compressor","wet":0.5},{"instance":43,"name":"Reverb"}]},{"id":8,"name":"Mix"}]})";
    event(*skin,AUDCORE_SKIN_EVENT_SNAPSHOT,17,snapshot);
    assert(skin->ready && skin->channel()->id == 7);
    assert(classic::window_alpha(0) == 255 && classic::window_alpha(5) == 242 &&
           classic::window_alpha(90) == 25 && classic::window_alpha(100) == 25);
    assert(skin->transparency == 5);
    skin->adjust_transparency(-1);
    assert(skin->transparency == 0);
    skin->adjust_transparency(1);
    assert(skin->transparency == 5);
    skin->invoke(skin->panels[0],classic::Action::open);
    assert(mock.last["method"] == "document.open" && mock.last["params"].empty());
    skin->volume_drag = skin->state.gain_control(0);
    skin->update_volume(classic::volume.bottom);
    assert(mock.last["method"] == "channel.setGain" && mock.last["params"]["db"] == -60);
    assert(mock.last["params"]["expectedChannel"]["chain_uuid"] == "vocal");
    skin->update_volume(classic::volume.top);
    assert(mock.last["params"]["db"] == 0);
    skin->invoke(skin->panels[0],classic::Action::play);
    assert(mock.last.at("method") == "transport.setPlaying" && mock.last["params"]["playing"] == true);
    event(*skin,AUDCORE_SKIN_EVENT_TRANSIENT,0,R"({"type":"meters","meters":[{"id":7,"peak":0.5,"peak_l":0.25,"peak_r":0.5}]})");
    assert(skin->channel()->peak > 0 && skin->revision == 17);
    assert(skin->channel_at({20,48}) == 0 && skin->channel_at({20,64}) == 1);
    assert(!skin->channel_at({20,176}));
    assert(skin->effect_at({20,27}) == 0 && skin->effect_at({20,43}) == 1);
    assert(!skin->effect_at({20,91}));
    auto& effects = skin->panels[classic::effects_panel];
    auto& channels = skin->panels[classic::channels_panel];
    skin->invoke(skin->panels[0],classic::Action::effects);
    assert(!effects.wanted && channels.wanted);
    skin->invoke(skin->panels[0],classic::Action::channels);
    assert(!channels.wanted);
    skin->show_channels();
    assert(channels.wanted && !effects.wanted);
    skin->invoke(effects,classic::Action::bypass);
    assert(mock.last["method"] == "channel.setFxBypass" && mock.last["params"]["id"] == 7);
    skin->bypass_effect(0);
    assert(mock.last["method"] == "effect.setBypass" && mock.last["params"]["instance"] == 42);
    assert(mock.last["params"]["bypass"] == true && mock.last["params"]["expectedEffect"]["channel"]["chain_uuid"] == "vocal");
    skin->adjust_effect(0,2);
    assert(mock.last["method"] == "effect.setWet" && mock.last["params"]["wet"] == 0.52);
    skin->adjust_effect(0,2);
    assert(mock.last["params"]["wet"] == 0.54);
    skin->open_effect(1);
    assert(mock.last["method"] == "effect.openEditor" && mock.last["params"]["instance"] == 43);
    const auto vocal = classic::ChannelChoice{7,skin->state.channels()[0].identity};
    const auto mix = classic::ChannelChoice{8,skin->state.channels()[1].identity};
    assert(!skin->choose_channel({8,vocal.identity}));
    assert(skin->choose_channel(mix));
    assert(skin->channel()->id == 8);
    assert(!skin->effect_at({20,27}));
    skin->step_channel(-1);
    assert(skin->channel()->id == 7 && skin->effect_at({20,27}) == 0);
    assert(skin->choose_channel(vocal) && skin->channel()->id == 7);
    skin->effect_id = 43;
    skin->invoke(effects,classic::Action::remove_effect);
    assert(mock.last["method"] == "effect.remove" && mock.last["params"]["instance"] == 43);
    assert(mock.last["params"]["expectedEffect"]["channel"]["chain_uuid"] == "vocal");
    const auto items = native_menu::effect_items(skin->state,skin->locale,0,1,-300,64);
    const auto popup = native_menu::make_popup(items);
    assert(popup && GetMenuItemCount(popup.get()) == static_cast<int>(items.size()));
    assert(nlohmann::json::parse(items.back().command)["params"]["anchor"]["x"] == -300);
    const auto root = native_menu::menu(skin->state,skin->locale,0,0,false,false);
    assert(std::none_of(root.begin(),root.end(),[](const auto& item) {return item.label == "显示";}));
    const auto topmost = std::find_if(root.begin(),root.end(),[](const auto& item) {return item.label == "保持最前";});
    assert(topmost != root.end() && topmost->toggle_topmost && !topmost->checked);
    event(*skin,AUDCORE_SKIN_EVENT_DELTA,18,R"({"type":"channel","id":7,"field":"rack","value":[{"instance":43,"name":"Reverb"},{"instance":42,"name":"Compressor","wet":0.54}]})");
    assert(skin->channel()->effects.front().instance == 43);
    event(*skin,AUDCORE_SKIN_EVENT_DELTA,20,"{}");
    event(*skin,AUDCORE_SKIN_EVENT_DELTA,21,"{}");
    assert(!skin->ready && mock.recovery == 1);
    event(*skin,AUDCORE_SKIN_EVENT_SNAPSHOT,21,snapshot);
    assert(skin->ready && skin->revision == 21);
    classic::hide(skin);
    assert(!skin->ready && mock.unsubscribed == 0);
    classic::destroy(skin);
    assert(mock.unsubscribed == 1);
}

void drag_geometry() {
    using namespace classic;
    const RECT work{-1400,-200,1000,900};
    const PanelRects stacked{{{-1000,20,-673,161},{-1000,161,-673,260},{-1000,260,-673,384}}};
    PanelDrag drag;
    drag.begin(0,{-990,30},96,stacked);
    assert(drag.count == 3 && drag.parent[1] == 0 && drag.parent[2] == 1);
    const auto group = drag.proposed({-850,130},96,stacked);
    assert(group[0].left == -860 && group[2].top == 360);
    const PanelRects resized{{{-1000,20,-509,232},{-1000,232,-509,381},{-1000,381,-509,567}}};
    const auto scaled = drag.proposed({-850,130},144,resized);
    assert(scaled[0].left == -865 && scaled[0].top == 115 &&
           scaled[1].top == scaled[0].bottom && scaled[2].top == scaled[1].bottom);
    drag.begin(1,{-990,171},96,stacked);
    assert(drag.count == 1);
    const auto detached = drag.proposed({-590,331},96,stacked);
    assert(detached[1].left == -600 && IsRectEmpty(&detached[0]) && IsRectEmpty(&detached[2]));
    auto candidate = detached;
    candidate[1] = {-1000,171,-673,270};
    assert(drag.correction(candidate,PanelRects{stacked[0],{},{}},work,10).y == -10);
    OffsetRect(&candidate[1],0,1);
    assert(drag.correction(candidate,PanelRects{stacked[0],{},{}},work,10).y == 0);
    OffsetRect(&candidate[1],-391,0);
    assert(drag.correction(candidate,PanelRects{stacked[0],{},{}},work,10).x == -9);
}

void compare_bitmap(const classic::Skin& skin, const wchar_t* stem, int frames) {
    std::wstring resource = L"CLASSIC_";
    for (const wchar_t* ch = stem; *ch; ++ch) resource += *ch >= L'a' && *ch <= L'z' ? *ch-32 : *ch;
    const auto entry = FindResourceW(skin.module,resource.c_str(),RT_RCDATA);
    assert(entry);
    const auto* data = static_cast<const BYTE*>(LockResource(LoadResource(skin.module,entry)));
    BITMAPFILEHEADER file{};
    std::memcpy(&file,data,sizeof(file));
    const HDC canvas = CreateCompatibleDC(nullptr);
    const HDC source = CreateCompatibleDC(nullptr);
    // Use the bitmaps from the exported create() path. Reloading them with
    // a test-specific color DC let a monochrome production load pass.
    const auto* bitmap = skin.bitmap(stem);
    assert(bitmap && canvas && source);
    const LONG width = bitmap->size.cx/frames, height = bitmap->size.cy;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    const auto target = CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    assert(target && bits);
    const auto old = SelectObject(canvas,target);
    const auto region = bitmap->region(bitmap->size.cx,height);
    assert(region);
    const LONG stride = (bitmap->size.cx*3+3)&~3;
    int key_count = 0;
    constexpr COLORREF backdrop = RGB(1,2,3);
    for (int frame = 0; frame < frames; ++frame) {
        classic::fill(canvas,{0,0,width,height},backdrop);
        bitmap->draw(canvas,source,0,0,frames,frame);
        for (LONG y = 0; y < height; ++y) for (LONG x = 0; x < width; ++x) {
            const auto* raw = data+file.bfOffBits+(height-y-1)*stride+(x+frame*width)*3;
            const COLORREF expected = RGB(raw[2],raw[1],raw[0]);
            const bool opaque = expected != classic::key;
            key_count += !opaque;
            assert(!!PtInRegion(region,x+frame*width,y) == opaque);
            assert(GetPixel(canvas,x,y) == (opaque ? expected : backdrop));
        }
    }
    if (std::wstring_view(stem) == L"player_skin") assert(key_count > 0);
    DeleteObject(region);
    SelectObject(canvas,old);
    DeleteObject(target);
    DeleteDC(source);
    DeleteDC(canvas);
}

void load_binary(const wchar_t* path, bool with_art, bool require_icon = false) {
    Mock mock;
    auto host = host_for(mock);
    for (int i = 0; i < 2; ++i) {
        const auto module = LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        assert(module);
        const auto descriptor = reinterpret_cast<audcore_skin_get_descriptor_fn>(
            GetProcAddress(module,"audcore_skin_get_descriptor"));
        assert(descriptor && !descriptor(1));
        const auto* skin = descriptor(0);
        assert(skin && skin->header.minor == 2 && skin->header.size == sizeof(*skin));
        assert(std::string_view(reinterpret_cast<const char*>(skin->version.data),skin->version.len) == classic::version);
        assert(std::string_view(reinterpret_cast<const char*>(skin->name.data),skin->name.len) == classic::skin_name);
        assert(FindResourceW(module,L"TTPLAYER_MIT",RT_RCDATA));
        assert(!!FindResourceW(module,L"CLASSIC_PLAYER_SKIN",RT_RCDATA) == with_art);
        assert(!FindResourceW(module,L"CLASSIC_LYRIC",RT_RCDATA));
        assert(!FindResourceW(module,L"CLASSIC_MINIMODE",RT_RCDATA));
        assert(FindResourceW(module,MAKEINTRESOURCEW(AUDCORE_NATIVE_MENU_EDITOR),RT_DIALOG));
        const auto state = skin->create(&host);
        assert(state);
        const auto& instance = *static_cast<const classic::Skin*>(state);
        assert(instance.classic_art == with_art);
        const bool has_icon = FindResourceW(module,classic::classic_app_icon,RT_GROUP_ICON) != nullptr;
        assert(!require_icon || has_icon);
        assert((instance.small_icon != nullptr) == has_icon && (instance.large_icon != nullptr) == has_icon);
        if (i == 0) main_panel_labels(instance);
        if (with_art && i == 0) {
            const auto* list_art = instance.background(classic::channels_panel);
            const auto* original_list_art = instance.bitmap(L"playlist_skin");
            assert(list_art->size.cy == 188 && classic::channel_rows == 8);
            assert(list_art->color_at(40,25) == original_list_art->color_at(40,25));
            assert(list_art->color_at(40,187) == original_list_art->color_at(40,123));
            compare_bitmap(instance,L"player_skin",1);
            compare_bitmap(instance,L"eq_skin",1);
            compare_bitmap(instance,L"playlist_skin",1);
            compare_bitmap(instance,L"play",4);
            compare_bitmap(instance,L"pause",4);
            for (const auto& panel : classic::buttons())
                for (const auto& button : panel)
                    if (button.asset && button.action != classic::Action::play)
                        compare_bitmap(instance,button.asset,4);
            compare_bitmap(instance,L"number",12);
            compare_bitmap(instance,L"progress2",1);
            const auto dc = CreateCompatibleDC(nullptr);
            const auto source = CreateCompatibleDC(nullptr);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = classic::width;
            info.bmiHeader.biHeight = -classic::heights[0];
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            void* pixels = nullptr;
            const auto canvas = CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
            assert(dc && source && canvas && pixels);
            const auto old = SelectObject(dc,canvas);
            SetBkMode(dc,TRANSPARENT);
            classic::paint_scene(instance.panels[0],dc,source);
            const auto* background = instance.background(0);
            assert(background->color_at(75,99) != background->color_at(175,99));
            assert(GetPixel(dc,75,99) == background->color_at(175,99));
            assert(GetPixel(dc,24,57) == background->color_at(24,57));
            if (has_icon) {
                assert(GetPixel(dc,15,13) != background->color_at(15,13));
                assert(GetPixel(dc,7,5) == background->color_at(7,5));
            }
            SelectObject(dc,old);
            DeleteObject(canvas);
            DeleteDC(source);
            DeleteDC(dc);
        }
        skin->hide(state);
        skin->destroy(state);
        assert(FreeLibrary(module));
    }
}
}

int wmain(int argc, wchar_t** argv) {
    assert(argc == 3 || (argc == 4 && std::wstring_view(argv[3]) == L"--icon"));
    channel_table_layout();
    mic_startup();
    host_commands();
    drag_geometry();
    load_binary(argv[1],false);
    load_binary(argv[2],true,argc == 4);
    std::puts("TTPlayer geometry, ABI lifecycle, command routing and original BMP pixels passed");
}
