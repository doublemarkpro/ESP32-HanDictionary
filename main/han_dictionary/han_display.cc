#include "han_display.h"
#ifndef HAN_UI_HOST_SIM
#include <esp_lvgl_port.h>
#include <esp_timer.h>
#include <wifi_manager.h>
#include "application.h"
#include "assets/lang_config.h"
#include "board.h"
#include "settings.h"
#endif
#include <cJSON.h>
#include <src/misc/cache/instance/lv_image_cache.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include "assets/home_skin.h"
#include "assets/timetable_assets.h"
#include "assets/ui_assets.h"
#include "dictionary_service.h"
#include "phonetics.h"

namespace {
constexpr uint32_t kInk = 0x142b57, kBg = 0xfff9f0, kGreen = 0xd9f4df, kBlue = 0xd9edfc;
constexpr uint32_t kPurple = 0xe9dffc, kOrange = 0xffe8d6, kPink = 0xffdfe3;
const char* kSubjects[] = {"语文", "数学", "英语"};
const char* kWeekdays[] = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};
int SubjectKind(const std::string& name) {
    const char* names[] = {"语文", "数学", "英语", "科学", "美术", "体育"};
    for (int i = 0; i < 6; ++i)
        if (name == names[i])
            return i;
    return 6;
}
lv_obj_t* Image(lv_obj_t* parent, const lv_image_dsc_t* source, int x, int y) {
    auto image = lv_image_create(parent);
    lv_image_set_src(image, source);
    lv_obj_set_pos(image, x, y);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
    return image;
}
int64_t NowMs() { return esp_timer_get_time() / 1000; }
std::string Duration(int64_t ms) {
    auto sec = ms / 1000;
    char out[40];
    snprintf(out, sizeof(out), "%02lld:%02lld:%02lld", sec / 3600, sec / 60 % 60, sec % 60);
    return out;
}
const char* JString(cJSON* object, const char* key) {
    auto v = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}
}  // namespace

void HanDisplay::AttachTouch(esp_lcd_touch_handle_t touch) {
    DisplayLockGuard guard(this);
    // LVGL software rotation transforms display AND the registered pointer input.
    lv_display_set_rotation(display_, LV_DISPLAY_ROTATION_90);
    width_ = 1280;
    height_ = 720;
    lvgl_port_touch_cfg_t cfg{};
    cfg.disp = display_;
    cfg.handle = touch;
    cfg.scale.x = 1;
    cfg.scale.y = 1;
    ESP_ERROR_CHECK(lvgl_port_add_touch(&cfg) ? ESP_OK : ESP_FAIL);
}

lv_obj_t* HanDisplay::Box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color) {
    auto obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 24, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

lv_obj_t* HanDisplay::Label(lv_obj_t* parent, const char* text, int x, int y, int w,
                            const lv_font_t* font) {
    auto obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, w);
    lv_obj_set_style_text_font(obj, font ? font : &han_font_28, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(kInk), 0);
    lv_label_set_text(obj, text);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

lv_obj_t* HanDisplay::Button(lv_obj_t* parent, const char* text, int x, int y, int w, int h,
                             uint32_t color, int action) {
    auto obj = Box(parent, x, y, w, h, color);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(obj, this);
    lv_obj_set_style_bg_opa(obj, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_add_event_cb(obj, OnClick, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(action)));
    auto text_obj = Label(obj, text, 8, 0, w - 16);
    lv_obj_set_style_text_align(text_obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(text_obj, LV_ALIGN_CENTER, 0, 0);
    return obj;
}

void HanDisplay::SetupUI() {
    if (setup_ui_called_)
        return;
    LoadPreferences();
    jobs_ = xQueueCreate(4, sizeof(Job));
    ESP_ERROR_CHECK(jobs_ ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(Worker, "han_content", 8192, this, 2, nullptr) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    DisplayLockGuard guard(this);
    Display::SetupUI();
    root_ = Box(lv_display_get_screen_active(display_), 0, 0, 1280, 720, kBg);
    lv_obj_set_style_radius(root_, 0, 0);
    Image(root_, &han_footer, 0, 574);
    mascot_ = Image(root_, &han_art_book, 36, 0);
    lv_image_set_scale(mascot_, 195);
    lv_image_set_pivot(mascot_, 0, 0);
    back_ = Button(root_, "<", 24, 14, 72, 72, kGreen, 6);
    title_ = Label(root_, "小小助手", 212, 26, 470, &han_font_brand);
    date_ = Label(root_, "日期待同步", 747, 45, 208);
    clock_ = Label(root_, "—:—", 970, 36, 132, &han_font_clock);
    Box(root_, 952, 39, 1, 39, 0xd7d5d0);
    Box(root_, 1101, 39, 1, 39, 0xd7d5d0);
    auto wifi_button = Button(root_, "", 1115, 22, 72, 72, kBg, 7);
    lv_obj_set_style_bg_opa(wifi_button, LV_OPA_TRANSP, 0);
    wifi_image_ = Image(wifi_button, &han_status_wifi_off, 12, 12);
    battery_image_ = Image(root_, &han_status_battery_unknown, 1194, 34);
    body_ = Box(root_, 24, 104, 1232, 490, kBg);
    lv_obj_set_style_bg_opa(body_, LV_OPA_TRANSP, 0);
    talk_button_ = Box(root_, 466, 579, 350, 86, 0x49b7ff);
    lv_obj_add_flag(talk_button_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(talk_button_, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_style_radius(talk_button_, 43, 0);
    lv_obj_set_style_bg_grad_color(talk_button_, lv_color_hex(0x087dff), 0);
    lv_obj_set_style_bg_grad_dir(talk_button_, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(talk_button_, lv_color_hex(0xbeeaff), 0);
    lv_obj_set_style_border_width(talk_button_, 2, 0);
    lv_obj_set_style_shadow_color(talk_button_, lv_color_hex(0x5cb6f5), 0);
    lv_obj_set_style_shadow_width(talk_button_, 20, 0);
    lv_obj_set_style_shadow_opa(talk_button_, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_y(talk_button_, 6, 0);
    Image(talk_button_, &han_status_mic, 54, 17);
    talk_label_ = Label(talk_button_, "按住说话", 119, 19, 205, &han_font_talk);
    lv_obj_set_style_text_color(talk_label_, lv_color_white(), 0);
    lv_obj_add_event_cb(talk_button_, OnTalk, LV_EVENT_ALL, this);
    status_label_ = Label(root_, "", 844, 596, 365);
    notification_label_ = Label(root_, "", 844, 596, 365);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(notification_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_height(status_label_, 62);
    lv_obj_set_height(notification_label_, 62);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    message_ = Label(root_, "试试说：我想学英语音标", 240, 677, 800);
    lv_obj_set_style_text_align(message_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(message_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    tick_ = lv_timer_create(Tick, 800, this);
    Render(Page::Home);
    Queue(2, "");  // Read optional timetable/weather content off the LVGL/main tasks.
}

void HanDisplay::SetTheme(Theme* theme) {
    // A fixed children's theme owns its fonts independently of cloud font/theme updates.
    current_theme_ = theme;
}

void HanDisplay::SetChatMessage(const char*, const char* text) {
    DisplayLockGuard guard(this);
    if (message_)
        lv_label_set_text(message_, text ? text : "");
}
void HanDisplay::ClearChatMessages() { SetChatMessage("", ""); }
void HanDisplay::Toast(const char* text) { ShowNotification(text, 4500); }

void HanDisplay::Render(Page page) {
    ReleaseTalk();
    page_ = page;
    stroke_playing_ = false;
    timer_value_ = stroke_value_ = stroke_image_ = network_info_ = search_ = nullptr;
    alarm_hour_ = alarm_minute_ = nullptr;
    for (auto& label : totals_)
        label = nullptr;
    lv_obj_clean(body_);
    // Remove objects/cache references before replacing backing bytes. Late worker results are
    // ignored.
    stroke_placeholder_ = nullptr;
    expected_stroke_path_.clear();
    lv_image_cache_drop(&sd_stroke_);
    stroke_png_.clear();
    const char* titles[] = {"小小助手", "查字典", "英语音标", "课程表",
                            "作业计时", "闹钟",   "天气",     "联网设置"};
    lv_label_set_text(title_, titles[static_cast<int>(page)]);
    if (page == Page::Home) {
        lv_obj_add_flag(back_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(mascot_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(title_, 212);
        lv_obj_set_pos(body_, 38, 118);
        lv_obj_set_size(body_, 1204, 448);
    } else {
        lv_obj_remove_flag(back_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mascot_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(title_, 115);
        lv_obj_set_pos(body_, 24, 104);
        lv_obj_set_size(body_, 1232, 490);
    }
    if (page == Page::Timetable) {
        lv_obj_add_flag(talk_button_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(message_, 32, 676);
        lv_obj_set_pos(status_label_, 944, 665);
        lv_obj_set_pos(notification_label_, 944, 665);
        lv_obj_set_width(status_label_, 288);
        lv_obj_set_width(notification_label_, 288);
        lv_label_set_text(message_, "每周重复 · 课程及物品由家长填写");
    } else {
        lv_obj_remove_flag(talk_button_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(message_, 240, 677);
        lv_obj_set_pos(status_label_, 844, 596);
        lv_obj_set_pos(notification_label_, 844, 596);
        lv_obj_set_width(status_label_, 365);
        lv_obj_set_width(notification_label_, 365);
        lv_label_set_text(message_, "试试说：我想学英语音标");
    }
    lv_obj_set_y(talk_button_, page == Page::Home ? 579 : 606);
    lv_obj_set_height(talk_button_, page == Page::Home ? 86 : 66);
    lv_obj_set_y(talk_label_, page == Page::Home ? 19 : 9);
    lv_obj_set_y(lv_obj_get_child(talk_button_, 0), page == Page::Home ? 17 : 7);
    switch (page) {
        case Page::Home:
            Home();
            break;
        case Page::Dictionary:
            Dictionary();
            break;
        case Page::Phonetics:
            Phonetics();
            break;
        case Page::Timetable:
            Timetable();
            break;
        case Page::Timer:
            Timer();
            break;
        case Page::Alarm:
            Alarm();
            break;
        case Page::Weather:
            Weather();
            break;
        case Page::Network:
            Network();
            break;
    }
}

void HanDisplay::Home() {
    const char* titles[] = {"查字典", "英语音标", "课程表", "作业计时", "闹钟", "天气"};
    const uint32_t colors[] = {0x073e14, 0x211453, 0x082c51, 0x682900, 0x751315, 0x072a50};
    const lv_image_dsc_t* panels[] = {&han_panel_dictionary, &han_panel_phonetics,
                                      &han_panel_timetable,  &han_panel_timer,
                                      &han_panel_alarm,      &han_panel_weather};
    const lv_image_dsc_t* icons[] = {&han_art_book,  &han_art_headphones, &han_art_calendar,
                                     &han_art_timer, &han_art_alarm,      &han_art_weather};
    const int xs[] = {42, 69, 140, 161, 142, 119};
    const int ys[] = {75, 67, 44, 37, 40, 47};
    for (int i = 0; i < 6; ++i) {
        auto card = Button(body_, "", (i % 3) * 405, (i / 3) * 236, 394, i < 3 ? 224 : 212, kBg, i);
        lv_obj_set_style_radius(card, 28, 0);
        lv_obj_set_style_clip_corner(card, true, 0);
        lv_obj_set_style_shadow_color(card, lv_color_hex(0xdacc9d), 0);
        lv_obj_set_style_shadow_width(card, 14, 0);
        lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
        lv_obj_set_style_shadow_ofs_y(card, 5, 0);
        Image(card, panels[i], 0, 0);
        auto img = Image(card, icons[i], xs[i], ys[i]);
        if (i == 0) {
            lv_image_set_scale(img, 230);
            lv_image_set_pivot(img, 0, 0);
            auto grid = Box(card, 237, 77, 122, 128, 0xfffefa);
            lv_obj_set_style_radius(grid, 12, 0);
            lv_obj_set_style_border_width(grid, 4, 0);
            lv_obj_set_style_border_color(grid, lv_color_white(), 0);
            Box(grid, 60, 5, 1, 116, 0x9ed7aa);
            Box(grid, 4, 63, 113, 1, 0x9ed7aa);
            Image(grid, &han_home_gui, 0, 3);
        }
        if (i == 1) {
            auto ipa = Label(card, "/iː/", 155, 146, 110, &han_font_40);
            // The heading font does not contain IPA: use the existing verified phonetic font.
            lv_obj_set_style_text_font(ipa, &han_font_40, 0);
            lv_obj_set_style_text_align(ipa, LV_TEXT_ALIGN_CENTER, 0);
        }
        auto label = Label(card, titles[i], 34, 17, 350, &han_font_home);
        lv_obj_set_style_text_color(label, lv_color_hex(colors[i]), 0);
    }
}

void HanDisplay::OnTalk(lv_event_t* event) {
    auto self = static_cast<HanDisplay*>(lv_event_get_user_data(event));
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        auto& app = Application::GetInstance();
        const auto state = app.GetDeviceState();
        if (self->talk_held_ || self->talk_release_pending_)
            return;
        if (self->local_audio_ || (state != kDeviceStateIdle && state != kDeviceStateSpeaking)) {
            self->Toast("请稍候再说话");
            return;
        }
        if (!WifiManager::GetInstance().IsConnected()) {
            self->Toast("请先联网，再按住说话");
            return;
        }
        self->talk_held_ = true;
        self->talk_pressed_ms_ = NowMs();
        lv_label_set_text(self->talk_label_, "松开发送");
        app.Schedule([self] {
            if (!self->talk_held_)
                return;
            const auto state = Application::GetInstance().GetDeviceState();
            if (state != kDeviceStateIdle && state != kDeviceStateSpeaking)
                return;
            self->talk_started_ = true;
            Application::GetInstance().StartListening();
        });
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST ||
               code == LV_EVENT_DELETE) {
        self->ReleaseTalk();
    }
}

void HanDisplay::ReleaseTalk() {
    if (!talk_held_.exchange(false))
        return;
    if (talk_label_)
        lv_label_set_text(talk_label_, "按住说话");
    talk_release_pending_ = true;
    Application::GetInstance().Schedule([this] {
        auto& app = Application::GetInstance();
        if (talk_started_.exchange(false)) {
            // Cancel a not-yet-open manual session as well as stopping an active one.
            // Use the public state machine, never edit core protocol fields.
            if (app.GetDeviceState() == kDeviceStateConnecting)
                app.SetDeviceState(kDeviceStateIdle);
            app.StopListening();
        }
        talk_release_pending_ = false;
    });
}

void HanDisplay::Dictionary() {
    auto grid = Box(body_, 0, 0, 465, 375, 0xfff1e9);
    for (int i = 1; i < 2; ++i) {
        auto h = Box(grid, 14, 186, 436, 2, 0xefc8bd);
        auto v = Box(grid, 232, 14, 2, 347, 0xefc8bd);
        (void)h;
        (void)v;
    }
    if (entry_.character == "规") {
        stroke_image_ = lv_image_create(grid);
        lv_image_set_src(stroke_image_, &han_gui_strokes[0]);
        lv_obj_set_pos(stroke_image_, 82, 14);
    } else {
        stroke_image_ = lv_image_create(grid);
        lv_obj_set_pos(stroke_image_, 82, 14);
        lv_obj_add_flag(stroke_image_, LV_OBJ_FLAG_HIDDEN);
        stroke_placeholder_ = Label(grid, "笔顺资源待导入", 80, 138, 350);
    }
    stroke_value_ = Label(grid, "", 24, 323, 420);
    UpdateStroke();
    Button(body_, "上一步", 0, 391, 140, 70, kGreen, 20);
    Button(body_, "播放笔顺", 151, 391, 166, 70, kOrange, 21);
    Button(body_, "下一步", 328, 391, 137, 70, kBlue, 22);
    auto details = Box(body_, 490, 0, 742, 375, 0xffffff);
    std::string title = entry_.character + "   " + entry_.pinyin;
    Label(details, title.c_str(), 24, 20, 690, &han_font_40);
    std::string info = "部首 " + entry_.radical + "   " + std::to_string(entry_.stroke_count) +
                       "画   " + entry_.structure;
    Label(details, info.c_str(), 24, 83, 690);
    Label(details, entry_.definition.c_str(), 24, 139, 690);
    std::string words = "组词：";
    for (const auto& word : entry_.words)
        words += word + "  ";
    Label(details, words.c_str(), 24, 238, 690);
    const std::string page = entry_.page
                                 ? "新华字典第12版 · 第" + std::to_string(entry_.page) + "页"
                                 : "新华字典第12版 · 页码待核对";
    Label(details, page.c_str(), 24, 288, 690);
    Label(details,
          (entry_.source == "embedded-demo" || entry_.source == "project-authored-demo")
              ? "释义：开发示例"
              : "释义：SD 内容包",
          24, 329, 680);
    Button(body_, "查“规”", 490, 391, 220, 70, kGreen, 23);
    Button(body_, "语音查字", 730, 391, 245, 70, kBlue, 8);
    Button(body_, "字库状态", 995, 391, 237, 70, kPurple, 24);
}

void HanDisplay::UpdateStroke() {
    if (!stroke_value_ || entry_.strokes.empty())
        return;
    stroke_ = std::clamp(stroke_, 0, static_cast<int>(entry_.strokes.size()) - 1);
    auto value = std::to_string(stroke_ + 1) + " / " + std::to_string(entry_.strokes.size()) +
                 "    " + entry_.strokes[stroke_];
    lv_label_set_text(stroke_value_, value.c_str());
    if (stroke_image_ && entry_.character == "规" && stroke_ < 8)
        lv_image_set_src(stroke_image_, &han_gui_strokes[stroke_]);
    else if (stroke_image_) {
        lv_obj_add_flag(stroke_image_, LV_OBJ_FLAG_HIDDEN);
        if (stroke_placeholder_)
            lv_obj_remove_flag(stroke_placeholder_, LV_OBJ_FLAG_HIDDEN);
        expected_stroke_path_ = han::ContentStore::StrokePath(entry_.character, stroke_);
        Queue(3, expected_stroke_path_);
    }
}

bool HanDisplay::ApplyStrokeFrame(const std::string& path, std::string data) {
    DisplayLockGuard guard(this);
    if (page_ != Page::Dictionary || path.empty() || path != expected_stroke_path_ ||
        !stroke_image_ || !han::ContentStore::IsStrokePng(data))
        return false;
    lv_image_cache_drop(&sd_stroke_);
    stroke_png_ = std::move(data);
    sd_stroke_.header.magic = LV_IMAGE_HEADER_MAGIC;
    sd_stroke_.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
    sd_stroke_.header.w = sd_stroke_.header.h = 300;
    sd_stroke_.data_size = stroke_png_.size();
    sd_stroke_.data = reinterpret_cast<const uint8_t*>(stroke_png_.data());
    lv_image_set_src(stroke_image_, &sd_stroke_);
    lv_obj_remove_flag(stroke_image_, LV_OBJ_FLAG_HIDDEN);
    if (stroke_placeholder_)
        lv_obj_add_flag(stroke_placeholder_, LV_OBJ_FLAG_HIDDEN);
    return true;
}

void HanDisplay::Phonetics() {
    const char* cats[] = {"单元音", "双元音", "辅音"};
    for (int i = 0; i < 3; ++i)
        Button(body_, cats[i], i * 418, 0, 396, 66, i == category_ ? 0xc3a5f4 : kBlue, 100 + i);
    auto panel = Box(body_, 0, 86, 465, 397, kPurple);
    int shown = 0;
    for (int i = 0; i < static_cast<int>(std::size(han::kSounds)); ++i) {
        if (han::kSounds[i].category != category_)
            continue;
        const int offset = shown++;
        if (offset < sound_page_ * 6 || offset >= (sound_page_ + 1) * 6)
            continue;
        const int cell = offset % 6;
        auto btn = Button(panel, han::kSounds[i].ipa, 14 + cell % 3 * 148, 18 + cell / 3 * 122, 137,
                          112, i == sound_ ? 0xc3a5f4 : 0xffffff, 200 + i);
        lv_obj_set_style_text_font(lv_obj_get_child(btn, 0), &han_font_40, 0);
    }
    Button(panel, "上一页", 14, 271, 137, 58, kBlue, 120);
    const auto page = std::to_string(sound_page_ + 1) + " / " + std::to_string((shown + 5) / 6);
    Label(panel, page.c_str(), 190, 282, 130);
    Button(panel, "下一页", 310, 271, 137, 58, kBlue, 121);
    Label(panel, "英式音标 · 44 音学习卡", 18, 353, 430);
    auto detail = Box(body_, 490, 86, 742, 397, 0xffffff);
    auto& sound = han::kSounds[sound_];
    auto ipa = "/" + std::string(sound.ipa) + "/";
    auto big = Label(detail, ipa.c_str(), 20, 12, 700, &han_font_large);
    lv_obj_set_style_text_align(big, LV_TEXT_ALIGN_CENTER, 0);
    Button(detail, "听示范", 155, 134, 430, 64, kPurple, 110);
    for (int i = 0; i < 3; ++i)
        Button(detail, sound.words[i], 20 + i * 237, 220, 220, 70, kGreen, 111 + i);
    auto record = Button(detail, "录音跟读（后续）", 20, 314, 340, 62, kBlue, 114);
    lv_obj_add_state(record, LV_STATE_DISABLED);
    Label(detail, "音频需放入 SD 卡", 384, 331, 330);
}

void HanDisplay::Timetable() {
    lv_obj_set_size(body_, 1232, 560);
    auto table = Box(body_, 0, 0, 922, 524, 0xffffff);
    const int first_day = timetable_day_group_ ? 5 : 0;
    const int columns = timetable_day_group_ ? 2 : 5;
    const int col_width = timetable_day_group_ ? 390 : 156;
    auto row_head = Box(table, 12, 12, 108, 54, 0xf4f5e8);
    Label(row_head, timetable_row_ ? "6—8节" : "1—5节", 6, 10, 103);
    for (int c = 0; c < columns; ++c) {
        int day = first_day + c;
        const bool today = timetable_week_ == 0 && day == timetable_today_;
        const int x = 124 + c * col_width;
        if (today)
            Box(table, x, 12, col_width - 4, 438, 0xe6f4ff);
        auto head = Box(table, x, 12, col_width - 4, 54, today ? 0xc9e8ff : 0xf3f6ec);
        auto label = Label(head, kWeekdays[day], 0, 8, col_width - 4, &han_font_schedule);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        for (int r = 0; r < 5; ++r) {
            const int lesson = timetable_row_ + r;
            if (lesson >= 8)
                break;
            const auto& classes = timetable_.days[day];
            const std::string name =
                lesson < static_cast<int>(classes.size()) ? classes[lesson] : "";
            const int kind = SubjectKind(name);
            const uint32_t colors[] = {0xffded5, 0xcdeaff, 0xeadeff, 0xddf6d2,
                                       0xffe2c3, 0xcdf2ed, 0xf5f5ef};
            auto cell = Button(table, "", x + 4, 74 + r * 75, col_width - 12, 67, colors[kind],
                               700 + day * 8 + lesson);
            lv_obj_set_style_bg_grad_color(cell, lv_color_hex(0xfffbf4), 0);
            lv_obj_set_style_bg_grad_dir(cell, LV_GRAD_DIR_VER, 0);
            lv_obj_set_style_radius(cell, 18, 0);
            const lv_image_dsc_t* icons[] = {&han_subject_book, &han_subject_calculator,
                                             nullptr,           &han_subject_science,
                                             &han_subject_art,  &han_subject_sport};
            if (kind < 6 && icons[kind])
                Image(cell, icons[kind], 9, 12);
            if (kind == 2) {
                auto abc = Label(cell, "A\nBC", 8, 0, 42, &han_font_28);
                lv_obj_set_style_text_color(abc, lv_color_hex(0x9d65c4), 0);
                lv_obj_set_style_text_letter_space(abc, -3, 0);
                lv_obj_set_style_text_line_space(abc, -9, 0);
                lv_obj_set_style_text_align(abc, LV_TEXT_ALIGN_CENTER, 0);
            }
            auto text = Label(cell, name.empty() ? "—" : name.c_str(), kind < 6 ? 54 : 9, 16,
                              col_width - (kind < 6 ? 68 : 28),
                              kind < 6 ? &han_font_schedule : &han_font_28);
            lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
            lv_obj_set_height(text, 40);
        }
    }
    for (int r = 0; r < 5 && timetable_row_ + r < 8; ++r) {
        auto row = Box(table, 12, 74 + r * 75, 108, 67, 0xf9f4e5);
        auto name = "第" + std::to_string(timetable_row_ + r + 1) + "节";
        Label(row, name.c_str(), 9, 18, 98, &han_font_schedule);
    }
    Button(table, timetable_row_ ? "第1—5节" : "第6—8节", 18, 463, 177, 48, kBlue, 501);
    Button(table, timetable_day_group_ ? "周一至周五" : "查看周末", 207, 463, 198, 48, kGreen, 503);
    Label(table,
          timetable_.valid
              ? (timetable_.empty() ? "还没有课程，请导入课表" : "点击课程可查看完整名称")
              : "课表未加载或格式错误",
          422, 473, 479);
    Button(body_, timetable_week_ ? "下周 v" : "本周 v", 956, 0, 150, 54, kGreen, 502);
    auto friend_image = Image(body_, &han_art_book, 1135, 0);
    lv_image_set_pivot(friend_image, 0, 0);
    lv_image_set_scale(friend_image, 104);
    auto bag = Box(body_, 944, 71, 288, 287, 0xfff3ce);
    Image(bag, &han_subject_backpack, 16, 17);
    Label(bag, "明天要带", 67, 19, 207, &han_font_schedule);
    const int tomorrow = timetable_today_ < 0 ? -1 : (timetable_today_ + 1) % 7;
    if (tomorrow < 0) {
        Label(bag, "请先同步日期", 22, 103, 244);
    } else if (!timetable_.valid) {
        Label(bag, "请先导入课程表", 22, 103, 244);
    } else {
        const auto& items = timetable_.supplies[tomorrow];
        if (items.empty())
            Label(bag, "未填写需带物品", 22, 103, 244);
        for (int n = 0; n < 2 && supplies_page_ * 2 + n < static_cast<int>(items.size()); ++n) {
            const int i = supplies_page_ * 2 + n;
            auto item = Button(bag, "", 12, 74 + n * 80, 264, 71, 0xffffff, 600 + i);
            auto text = Label(item, items[i].c_str(), 14, 17, 193);
            lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
            lv_obj_set_height(text, 42);
            auto box = Box(item, 219, 21, 29, 29, supplies_checked_[i] ? 0x55c892 : 0xffffff);
            lv_obj_set_style_radius(box, 7, 0);
            lv_obj_set_style_border_width(box, 2, 0);
            lv_obj_set_style_border_color(box, lv_color_hex(0xb3bdb1), 0);
            if (supplies_checked_[i]) {
                static const lv_point_precise_t points[] = {{5, 14}, {11, 20}, {22, 7}};
                auto check = lv_line_create(box);
                lv_line_set_points(check, points, 3);
                lv_obj_set_style_line_width(check, 3, 0);
                lv_obj_set_style_line_color(check, lv_color_white(), 0);
                lv_obj_remove_flag(check, LV_OBJ_FLAG_CLICKABLE);
            }
        }
        if (items.size() > 2)
            Button(bag, "更多物品", 14, 238, 168, 40, kOrange, 504);
        if (items.size() <= 2)
            Label(bag, "勾选仅本次", 20, 251, 248);
    }
    auto voice = Button(body_, "", 944, 382, 288, 82, 0xc9efdb, 500);
    auto mic_circle = Box(voice, 9, 9, 64, 64, 0x43b985);
    lv_obj_set_style_radius(mic_circle, LV_RADIUS_CIRCLE, 0);
    auto mic = Image(mic_circle, &han_status_mic, 16, 10);
    lv_image_set_pivot(mic, 0, 0);
    lv_image_set_scale(mic, 180);
    Label(voice, "问明天课程", 83, 24, 201, &han_font_schedule);
    Label(body_, "好好学习\n天天向上", 1019, 470, 208, &han_font_schedule);
}

bool HanDisplay::ApplyTimetable(const std::string& json) {
    han::TimetableData data;
    const bool valid = han::TimetableData::Parse(json, data);
    DisplayLockGuard guard(this);
    timetable_ = std::move(data);
    supplies_checked_.fill(false);
    supplies_page_ = 0;
    if (page_ == Page::Timetable)
        Render(Page::Timetable);
    return valid;
}

void HanDisplay::Timer() {
    auto left = Box(body_, 0, 0, 710, 480, 0xffffff);
    for (int i = 0; i < 3; ++i)
        Button(left, kSubjects[i], 20 + i * 229, 20, 210, 66,
               i == study_.subject() ? 0x9bceff : kBlue, 300 + i);
    timer_value_ = Label(left, "00:00:00", 24, 138, 660, &han_font_large);
    lv_obj_set_style_text_align(timer_value_, LV_TEXT_ALIGN_CENTER, 0);
    Button(left, study_.running() ? "暂停" : "开始 / 继续", 24, 300, 310, 82, kOrange, 310);
    Button(left, "完成本科", 356, 300, 326, 82, kGreen, 311);
    Label(left, "返回主页后仍继续计时", 150, 415, 540);
    auto right = Box(body_, 734, 0, 498, 480, kGreen);
    Label(right, "本次作业记录", 24, 24, 445, &han_font_40);
    for (int i = 0; i < 3; ++i)
        totals_[i] = Label(right, "", 24, 103 + i * 77, 450);
    Button(right, "新一轮作业", 24, 373, 450, 72, 0xffffff, 312);
    UpdateTimer();
}

void HanDisplay::UpdateTimer() {
    if (timer_value_)
        lv_label_set_text(timer_value_,
                          Duration(study_.Elapsed(study_.subject(), NowMs())).c_str());
    for (int i = 0; i < 3; ++i)
        if (totals_[i]) {
            auto label = std::string(kSubjects[i]) + "  " + Duration(study_.Elapsed(i, NowMs())) +
                         (study_.completed(i) ? " 已完成" : "");
            lv_label_set_text(totals_[i], label.c_str());
        }
}

void HanDisplay::Alarm() {
    auto card = Box(body_, 0, 0, 1232, 480, 0xffffff);
    Label(card, "每日提醒", 28, 22, 700, &han_font_40);
    Label(card, "小时", 220, 105, 220);
    Label(card, "分钟", 530, 105, 220);
    alarm_hour_ = lv_roller_create(card);
    lv_roller_set_options(alarm_hour_,
                          "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n"
                          "18\n19\n20\n21\n22\n23",
                          LV_ROLLER_MODE_NORMAL);
    alarm_minute_ = lv_roller_create(card);
    std::string minutes;
    for (int i = 0; i < 60; ++i) {
        char s[5];
        snprintf(s, sizeof(s), "%02d", i);
        if (i)
            minutes += '\n';
        minutes += s;
    }
    lv_roller_set_options(alarm_minute_, minutes.c_str(), LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(alarm_hour_, alarm_minutes_ / 60, LV_ANIM_OFF);
    lv_roller_set_selected(alarm_minute_, alarm_minutes_ % 60, LV_ANIM_OFF);
    int x = 200;
    for (auto roller : {alarm_hour_, alarm_minute_}) {
        lv_obj_set_pos(roller, x, 156);
        lv_obj_set_width(roller, 240);
        lv_obj_set_style_text_font(roller, &han_font_40, 0);
        lv_roller_set_visible_row_count(roller, 3);
        x += 310;
    }
    Button(card, alarm_enabled_ ? "保存并保持开启" : "保存并开启", 820, 160, 370, 78, kGreen, 400);
    Button(card, alarm_ringing_ ? "停止铃声" : "关闭提醒", 820, 263, 370, 78, kPink, 401);
    Label(card, "需要设备开机且时间已同步；关机唤醒后续接入", 28, 414, 1170);
}

void HanDisplay::Weather() {
    auto card = Box(body_, 0, 0, 1232, 480, kBlue);
    auto img = lv_image_create(card);
    lv_image_set_src(img, &han_icon_weather);
    lv_obj_set_pos(img, 35, 28);
    Label(card, "天气", 190, 57, 800, &han_font_40);
    Label(card,
          weather_text_.empty()
              ? "暂无天气数据\n\n联网天气服务待接入。\n导入缓存后会明确显示更新时间。"
              : weather_text_.c_str(),
          35, 182, 1160);
}

void HanDisplay::Network() {
    auto card = Box(body_, 0, 0, 1232, 480, 0xffffff);
    Label(card, "请家长帮助联网", 28, 22, 1120, &han_font_40);
    network_info_ = Label(card, "正在读取网络状态…", 28, 100, 1130);
    Button(card, "打开手机配网", 28, 298, 430, 80, kBlue, 10);
    Label(card, "第一版沿用小智手机配网，触屏选网键盘后续接入", 28, 415, 1160);
}

void HanDisplay::OnClick(lv_event_t* e) {
    auto self = static_cast<HanDisplay*>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
    self->Action(static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));
}

void HanDisplay::Action(int a) {
    if (a == 500) {
        if (!WifiManager::GetInstance().IsConnected() ||
            Application::GetInstance().GetDeviceState() == kDeviceStateWifiConfiguring) {
            Toast("请先联网，再询问课程");
            return;
        }
        Action(8);
        Toast("试试说：明天有哪些课，要带什么？");
        return;
    }
    if (a >= 501 && a <= 504) {
        if (a == 501)
            timetable_row_ = timetable_row_ ? 0 : 5;
        if (a == 502)
            timetable_week_ = 1 - timetable_week_;
        if (a == 503)
            timetable_day_group_ = 1 - timetable_day_group_;
        if (a == 504 && timetable_today_ >= 0) {
            const auto size = timetable_.supplies[(timetable_today_ + 1) % 7].size();
            supplies_page_ = (supplies_page_ + 1) % std::max(1, static_cast<int>((size + 1) / 2));
        }
        Render(Page::Timetable);
        return;
    }
    if (a >= 600 && a < 608) {
        supplies_checked_[a - 600] = !supplies_checked_[a - 600];
        Render(Page::Timetable);
        return;
    }
    if (a >= 700 && a < 756) {
        const int day = (a - 700) / 8, lesson = (a - 700) % 8;
        const auto& classes = timetable_.days[day];
        if (lesson < static_cast<int>(classes.size()) && !classes[lesson].empty()) {
            auto text = std::string(kWeekdays[day]) + " 第" + std::to_string(lesson + 1) + "节：" +
                        classes[lesson];
            Toast(text.c_str());
        }
        return;
    }
    if (a >= 0 && a < 6) {
        Render(static_cast<Page>(a + 1));
        return;
    }
    if (a == 6) {
        Render(Page::Home);
        return;
    }
    if (a == 7) {
        Render(Page::Network);
        return;
    }
    if (a == 8) {
        if (local_audio_) {
            Toast("请听完当前发音再说话");
            return;
        }
        Application::GetInstance().ToggleChatState();
        return;
    }
    if (a == 10 && network_action_) {
        Application::GetInstance().Schedule([this] { network_action_(); });
        return;
    }
    if (a == 20 || a == 22) {
        stroke_playing_ = false;
        stroke_ += a == 20 ? -1 : 1;
        UpdateStroke();
        return;
    }
    if (a == 21) {
        stroke_playing_ = !stroke_playing_;
        if (stroke_ + 1 >= static_cast<int>(entry_.strokes.size()))
            stroke_ = -1;
        return;
    }
    if (a == 23) {
        Queue(0, "规");
        return;
    }
    if (a == 24) {
        Toast(DictionaryService::GetInstance().store().notice().c_str());
        return;
    }
    if (a >= 100 && a <= 102) {
        category_ = a - 100;
        sound_page_ = 0;
        for (int i = 0; i < static_cast<int>(std::size(han::kSounds)); ++i)
            if (han::kSounds[i].category == category_) {
                sound_ = i;
                break;
            }
        Render(Page::Phonetics);
        return;
    }
    if (a == 120 || a == 121) {
        const int pages = (han::SoundCount(category_) + 5) / 6;
        sound_page_ = (sound_page_ + (a == 121 ? 1 : pages - 1)) % pages;
        sound_ = han::SoundAt(category_, sound_page_ * 6);
        Render(Page::Phonetics);
        return;
    }
    if (a >= 200 && a < 200 + static_cast<int>(std::size(han::kSounds))) {
        sound_ = a - 200;
        Render(Page::Phonetics);
        return;
    }
    if (a >= 110 && a <= 113) {
        const auto& sound = han::kSounds[sound_];
        auto path = "phonetics/en-GB/" + std::string(sound.id) + "/" +
                    (a == 110 ? "sound" : sound.words[a - 111]) + ".ogg";
        Queue(1, path);
        return;
    }
    if (a >= 300 && a <= 302) {
        if (study_.running()) {
            Toast("请先暂停当前科目");
            return;
        }
        study_.Select(a - 300);
        Render(Page::Timer);
        return;
    }
    if (a == 310) {
        if (study_.running())
            study_.Pause(NowMs());
        else
            study_.Start(study_.subject(), NowMs());
        SaveTimer();
        Render(Page::Timer);
        return;
    }
    if (a == 311) {
        study_.Complete(NowMs());
        SaveTimer();
        Render(Page::Timer);
        return;
    }
    if (a == 312) {
        if (study_.running()) {
            Toast("请先暂停，再开始新一轮");
            return;
        }
        // Explicit second click confirms clearing this local session.
        static int64_t confirm_until = 0;
        if (NowMs() > confirm_until) {
            confirm_until = NowMs() + 5000;
            Toast("再次点击将清空本轮记录");
            return;
        }
        confirm_until = 0;
        study_ = han::StudyTimer();
        SaveTimer();
        Render(Page::Timer);
        return;
    }
    if (a == 400 || a == 401) {
        alarm_enabled_ = a == 400;
        if (a == 400)
            alarm_minutes_ =
                lv_roller_get_selected(alarm_hour_) * 60 + lv_roller_get_selected(alarm_minute_);
        alarm_ringing_ = false;
        const auto minutes = alarm_minutes_;
        const auto enabled = alarm_enabled_;
        Application::GetInstance().Schedule([minutes, enabled] {
            Settings s("han_alarm", true);
            s.SetInt("minutes", minutes);
            s.SetBool("enabled", enabled);
        });
        Render(Page::Alarm);
        Toast(a == 400 ? "每日提醒已保存" : "提醒已关闭");
    }
}

void HanDisplay::Tick(lv_timer_t* timer) {
    auto self = static_cast<HanDisplay*>(lv_timer_get_user_data(timer));
    if (self->talk_held_ && NowMs() - self->talk_pressed_ms_ >= 60000)
        self->ReleaseTalk();
    self->UpdateTimer();
    if (self->study_.running() && NowMs() - self->last_checkpoint_ms_ >= 60000) {
        self->last_checkpoint_ms_ = NowMs();
        self->SaveTimer();
    }
    if (self->stroke_playing_) {
        ++self->stroke_;
        if (self->stroke_ + 1 >= static_cast<int>(self->entry_.strokes.size()))
            self->stroke_playing_ = false;
        self->UpdateStroke();
    }
}

void HanDisplay::ShowEntry(const han::Entry& entry) {
    DisplayLockGuard guard(this);
    entry_ = entry;
    stroke_ = 0;
    if (setup_ui_called_)
        Render(Page::Dictionary);
}

bool HanDisplay::OpenPage(const std::string& page) {
    const char* names[] = {"home",  "dictionary", "phonetics", "timetable",
                           "timer", "alarm",      "weather",   "network"};
    for (int i = 0; i < 8; ++i)
        if (page == names[i]) {
            DisplayLockGuard guard(this);
            if (!setup_ui_called_)
                return false;
            Render(static_cast<Page>(i));
            return true;
        }
    return false;
}

void HanDisplay::UpdateStatusBar(bool) {
    auto& wifi = WifiManager::GetInstance();
    std::string network;
    const bool config = wifi.IsConfigMode();
    if (config)
        network = "手机连接热点：" + wifi.GetApSsid() + "\n浏览器打开：" + wifi.GetApWebUrl();
    else if (wifi.IsConnected())
        network = "已连接：" + wifi.GetSsid() + "\n需要更换网络时，打开手机配网。";
    else
        network = "尚未联网\n可以先使用本地学习功能，也可打开手机配网。";
    int level = 0;
    bool charging = false, discharging = false;
    const bool known = Board::GetInstance().GetBatteryLevel(level, charging, discharging);
    auto now = time(nullptr);
    struct tm tm{};
    localtime_r(&now, &tm);
    char clock[48] = "—:—", date[64] = "日期待同步";
    const bool valid_time = tm.tm_year >= 125;
    if (valid_time) {
        strftime(clock, sizeof(clock), "%H:%M", &tm);
        const char* weekdays[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
        snprintf(date, sizeof(date), "%d月%d日 %s", tm.tm_mon + 1, tm.tm_mday,
                 weekdays[tm.tm_wday]);
    }
    const int rssi = wifi.IsConnected() ? wifi.GetRssi() : -127;
    DisplayLockGuard guard(this);
    if (!setup_ui_called_)
        return;
    lv_label_set_text(clock_, clock);
    lv_label_set_text(date_, date);
    lv_image_set_src(wifi_image_, !wifi.IsConnected() ? &han_status_wifi_off
                                  : rssi >= -65       ? &han_status_wifi_3
                                  : rssi >= -75       ? &han_status_wifi_2
                                                      : &han_status_wifi_1);
    const lv_image_dsc_t* battery = &han_status_battery_unknown;
    if (known && level >= 0 && level <= 100) {
        battery = charging      ? &han_status_battery_charging
                  : level <= 5  ? &han_status_battery_empty
                  : level <= 20 ? &han_status_battery_low
                  : level <= 65 ? &han_status_battery_half
                                : &han_status_battery_full;
    }
    lv_image_set_src(battery_image_, battery);
    const int64_t date_key = valid_time ? static_cast<int64_t>(tm.tm_year) * 366 + tm.tm_yday : -1;
    if (date_key != timetable_date_key_) {
        timetable_date_key_ = date_key;
        timetable_today_ = valid_time ? (tm.tm_wday + 6) % 7 : -1;
        supplies_checked_.fill(false);
        supplies_page_ = 0;
        if (page_ == Page::Timetable)
            Render(Page::Timetable);
    }
    if (network_info_)
        lv_label_set_text(network_info_, network.c_str());
    // Daily alarm is based on synchronized system time; RTC wake-up is not implied.
    const int64_t day = static_cast<int64_t>(tm.tm_year) * 366 + tm.tm_yday;
    if (valid_time && alarm_enabled_ && alarm_last_day_ != day &&
        tm.tm_hour * 60 + tm.tm_min == alarm_minutes_) {
        alarm_last_day_ = day;
        alarm_ringing_ = true;
        Render(Page::Alarm);
        Toast("时间到了！");
        Application::GetInstance().Schedule(
            [] { Application::GetInstance().PlaySound(Lang::Sounds::OGG_SUCCESS); });
    }
}

void HanDisplay::LoadPreferences() {
    Settings s("han_study");
    for (int i = 0; i < 3; ++i) {
        const auto key = std::to_string(i);
        study_.Restore(i, static_cast<int64_t>(s.GetInt("s" + key, 0)) * 1000,
                       s.GetBool("c" + key, false));
    }
    Settings a("han_alarm");
    alarm_minutes_ = std::clamp(static_cast<int>(a.GetInt("minutes", 405)), 0, 1439);
    alarm_enabled_ = a.GetBool("enabled", false);
}

void HanDisplay::SaveTimer() {
    int seconds[3];
    bool completed[3];
    for (int i = 0; i < 3; ++i) {
        seconds[i] = study_.Elapsed(i, NowMs()) / 1000;
        completed[i] = study_.completed(i);
    }
    Application::GetInstance().Schedule([seconds, completed] {
        Settings s("han_study", true);
        for (int i = 0; i < 3; ++i) {
            auto k = std::to_string(i);
            s.SetInt("s" + k, seconds[i]);
            s.SetBool("c" + k, completed[i]);
        }
    });
}

void HanDisplay::Queue(int type, const std::string& value) {
    Job job{};
    job.type = type;
    if (value.size() >= sizeof(job.value)) {
        Toast("内容路径过长");
        return;
    }
    memcpy(job.value, value.data(), value.size());
    if (!jobs_ || xQueueSend(jobs_, &job, 0) != pdTRUE)
        Toast("正在处理，请稍后再试");
}

void HanDisplay::Worker(void* ptr) {
    auto self = static_cast<HanDisplay*>(ptr);
    Job job;
    auto& store = DictionaryService::GetInstance().store();
    for (;;) {
        if (xQueueReceive(self->jobs_, &job, portMAX_DELAY) != pdTRUE)
            continue;
        if (job.type == 0) {
            han::Entry entry;
            if (store.Lookup(job.value, entry))
                self->ShowEntry(entry);
            else
                self->Toast("未找到这个字，请检查内容包");
        } else if (job.type == 1) {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() != kDeviceStateIdle &&
                app.GetDeviceState() != kDeviceStateWifiConfiguring) {
                self->Toast("请先结束当前语音对话");
                continue;
            }
            std::string data;
            if (!store.ready() || !store.Read(job.value, data, 256 * 1024) ||
                data.compare(0, 4, "OggS") != 0 ||
                data.substr(0, 128).find("OpusHead") == std::string::npos) {
                self->Toast("缺少发音文件，请导入音标音频包");
                continue;
            }
            self->local_audio_ = true;
            auto& audio = app.GetAudioService();
            const bool restore_wake = audio.IsWakeWordRunning();
            audio.EnableWakeWordDetection(false);
            audio.PlaySound(data);  // Bounded file, worker may wait on decoder queue.
            const auto deadline = NowMs() + 12000;
            while (!app.GetAudioService().IsPlaybackIdle() && NowMs() < deadline)
                vTaskDelay(pdMS_TO_TICKS(50));
            if (restore_wake && app.GetDeviceState() == kDeviceStateIdle)
                audio.EnableWakeWordDetection(true);
            self->local_audio_ = false;
        } else if (job.type == 2) {
            std::string data, weather;
            if (store.ready() && store.Read("timetable.json", data, 8192)) {
                self->ApplyTimetable(data);
            }
            if (store.ready() && store.Read("weather.json", data, 4096)) {
                auto root = cJSON_Parse(data.c_str());
                auto city = std::string(JString(root, "city"));
                auto updated = std::string(JString(root, "updated_at"));
                if (!city.empty() && !updated.empty())
                    weather =
                        city + "（缓存）\n" + JString(root, "summary") + "\n\n更新时间：" + updated;
                cJSON_Delete(root);
            }
            DisplayLockGuard guard(self);
            self->weather_text_ = weather;
            if (self->page_ == Page::Timetable || self->page_ == Page::Weather)
                self->Render(self->page_);
        } else if (job.type == 3) {
            std::string data;
            if (store.ReadStroke(job.value, data))
                self->ApplyStrokeFrame(job.value, std::move(data));
        }
    }
}
