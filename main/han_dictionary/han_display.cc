#include "han_display.h"
#ifndef HAN_UI_HOST_SIM
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_timer.h>
#include <cbin_font.h>
#include <src/misc/cache/lv_cache.h>
#include <wifi_manager.h>
#include "application.h"
#include "assets/lang_config.h"
#include "audio/audio_codec.h"
#include "board.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "settings.h"
#endif
#include <cJSON.h>
#include <src/misc/cache/instance/lv_image_cache.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <memory>
#include <new>
#include <vector>
#include "assets/home_skin.h"
#include "assets/timetable_assets.h"
#include "assets/ui_assets.h"
#include "dictionary_service.h"
#include "phonetics.h"
#include "qweather_service.h"

namespace {
constexpr uint32_t kInk = 0x142b57, kBg = 0xfff9f0, kGreen = 0xd9f4df, kBlue = 0xd9edfc;
constexpr uint32_t kPurple = 0xe9dffc, kOrange = 0xffe8d6, kPink = 0xffdfe3;
constexpr uint32_t kMuted = 0x61708f, kCardBorder = 0xf1e8d9;
constexpr int kPinyinPageSize = 12;
#if LV_USE_VECTOR_GRAPHIC
struct GlyphBounds {
    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();

    bool valid() const { return min_x < max_x && min_y < max_y; }
};

GlyphBounds MeasureGlyph(const han::StrokeGlyph& glyph) {
    GlyphBounds bounds;
    for (const auto& stroke : glyph.strokes) {
        for (const auto& command : stroke.commands) {
            for (int index = 0; index < command.point_count; ++index) {
                bounds.min_x = std::min(bounds.min_x, static_cast<float>(command.points[index].x));
                bounds.min_y = std::min(bounds.min_y, static_cast<float>(command.points[index].y));
                bounds.max_x = std::max(bounds.max_x, static_cast<float>(command.points[index].x));
                bounds.max_y = std::max(bounds.max_y, static_cast<float>(command.points[index].y));
            }
        }
    }
    return bounds;
}

bool DrawGlyph(lv_obj_t* canvas, const han::StrokeGlyph& glyph, int width, int height, int margin,
               int current_stroke, int only_stroke, bool show_progress) {
    if (!canvas || glyph.strokes.empty())
        return false;
    const auto bounds = MeasureGlyph(glyph);
    if (!bounds.valid())
        return false;

    lv_canvas_fill_bg(canvas, lv_color_hex(0xffffff), LV_OPA_TRANSP);
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    auto dsc = lv_draw_vector_dsc_create(&layer);
    auto path = lv_vector_path_create(width < 100 ? LV_VECTOR_PATH_QUALITY_LOW
                                                  : LV_VECTOR_PATH_QUALITY_MEDIUM);
    if (!dsc || !path) {
        if (path)
            lv_vector_path_delete(path);
        if (dsc)
            lv_draw_vector_dsc_delete(dsc);
        lv_canvas_finish_layer(canvas, &layer);
        return false;
    }

    const float ink_width = bounds.max_x - bounds.min_x;
    const float ink_height = bounds.max_y - bounds.min_y;
    const float scale =
        std::min((width - margin * 2.0f) / ink_width, (height - margin * 2.0f) / ink_height);
    const float left = (width - ink_width * scale) * 0.5f;
    const float top = (height - ink_height * scale) * 0.5f;
    auto point = [&](const han::StrokePoint& value) {
        return lv_fpoint_t{left + (value.x - bounds.min_x) * scale,
                           top + (bounds.max_y - value.y) * scale};
    };

    const int first = only_stroke >= 0 ? only_stroke : static_cast<int>(glyph.strokes.size()) - 1;
    const int last = only_stroke >= 0 ? only_stroke : 0;
    for (int stroke_index = first; stroke_index >= last; --stroke_index) {
        if (stroke_index < 0 || stroke_index >= static_cast<int>(glyph.strokes.size()))
            continue;
        lv_vector_path_clear(path);
        for (const auto& command : glyph.strokes[stroke_index].commands) {
            lv_fpoint_t points[3];
            for (int index = 0; index < command.point_count; ++index)
                points[index] = point(command.points[index]);
            switch (command.op) {
                case 0:
                    lv_vector_path_move_to(path, &points[0]);
                    break;
                case 1:
                    lv_vector_path_line_to(path, &points[0]);
                    break;
                case 2:
                    lv_vector_path_quad_to(path, &points[0], &points[1]);
                    break;
                case 3:
                    lv_vector_path_cubic_to(path, &points[0], &points[1], &points[2]);
                    break;
                case 4:
                    lv_vector_path_close(path);
                    break;
                default:
                    break;
            }
        }
        uint32_t color = kInk;
        if (only_stroke >= 0)
            color = stroke_index == current_stroke ? 0xf0544b : kInk;
        else if (show_progress)
            color = stroke_index > current_stroke    ? 0xd8ccc5
                    : stroke_index == current_stroke ? 0xf0544b
                                                     : kInk;
        lv_draw_vector_dsc_set_fill_color(dsc, lv_color_hex(color));
        lv_draw_vector_dsc_set_fill_opa(dsc, LV_OPA_COVER);
        lv_draw_vector_dsc_add_path(dsc, path);
    }
    lv_draw_vector(dsc);
    lv_vector_path_delete(path);
    lv_draw_vector_dsc_delete(dsc);
    lv_canvas_finish_layer(canvas, &layer);
    return true;
}
#endif
struct PendingStrokeGlyph {
    HanDisplay* display;
    std::string character;
    han::StrokeGlyph glyph;
};
struct PendingMissingStrokeGlyph {
    HanDisplay* display;
    std::string character;
};
void ApplyStrokeGlyphAsync(void* context) {
    std::unique_ptr<PendingStrokeGlyph> pending(static_cast<PendingStrokeGlyph*>(context));
    pending->display->ApplyStrokeGlyph(pending->character, std::move(pending->glyph));
}
void ApplyMissingStrokeGlyphAsync(void* context) {
    std::unique_ptr<PendingMissingStrokeGlyph> pending(
        static_cast<PendingMissingStrokeGlyph*>(context));
    pending->display->ApplyMissingStrokeGlyph(pending->character);
}
void SetTextIfChanged(lv_obj_t* label, const char* text) {
    if (strcmp(lv_label_get_text(label), text) != 0)
        lv_label_set_text(label, text);
}
void SetImageIfChanged(lv_obj_t* image, const lv_image_dsc_t* source) {
    if (lv_image_get_src(image) != source)
        lv_image_set_src(image, source);
}
const char* kSubjects[] = {"语文", "数学", "英语"};
const char* kWeekdays[] = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};
int SubjectKind(const std::string& name) {
    const char* names[] = {"语文", "数学", "英语", "科学", "美术", "体育", "信息",
                           "音乐", "武术", "劳动", "竖笛", "选修", "社团"};
    for (int i = 0; i < 13; ++i)
        if (name == names[i])
            return i;
    if (name == "民乐团")
        return 7;
    if (name == "班会" || name.find("社团") != std::string::npos)
        return 12;
    if (name == "阅读")
        return 0;
    return 11;
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

struct WeatherView {
    std::string city;
    std::string current;
    std::string feels;
    std::string wind;
    std::string updated;
    bool cached = false;
};

WeatherView DecodeWeather(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    WeatherView view;
    if (!lines.empty()) {
        view.city = lines[0];
        const std::string marker = "（缓存）";
        const auto cached = view.city.find(marker);
        if (cached != std::string::npos) {
            view.city.erase(cached);
            view.cached = true;
        }
    }
    if (lines.size() > 1)
        view.current = lines[1];
    if (lines.size() > 2)
        view.feels = lines[2];
    if (lines.size() > 3)
        view.wind = lines[3];
    for (const auto& line : lines) {
        constexpr const char* prefix = "更新时间：";
        if (line.rfind(prefix, 0) == 0)
            view.updated = line.substr(strlen(prefix));
    }
    return view;
}

int WeatherTemperature(const WeatherView& weather) {
    const auto degree = weather.current.find("°C");
    if (degree == std::string::npos)
        return 1000;
    auto begin = weather.current.rfind(' ', degree);
    begin = begin == std::string::npos ? 0 : begin + 1;
    char* end = nullptr;
    const auto value = std::strtol(weather.current.c_str() + begin, &end, 10);
    return end == weather.current.c_str() + begin ? 1000 : static_cast<int>(value);
}

std::string WeatherGraphicId(const std::string& text) {
    if (text.find("雷") != std::string::npos)
        return "weather-thunderstorm";
    if (text.find("冰雹") != std::string::npos)
        return "weather-hail";
    if (text.find("雨夹雪") != std::string::npos || text.find("冻雨") != std::string::npos)
        return "weather-sleet";
    if (text.find("暴雨") != std::string::npos || text.find("大雨") != std::string::npos)
        return "weather-heavy-rain";
    if (text.find("阵雨") != std::string::npos)
        return "weather-showers";
    if (text.find("小雨") != std::string::npos)
        return "weather-light-rain";
    if (text.find("雨") != std::string::npos)
        return "weather-rain";
    if (text.find("雪") != std::string::npos)
        return "weather-snow";
    if (text.find("雾") != std::string::npos || text.find("霾") != std::string::npos)
        return "weather-fog";
    if (text.find("沙") != std::string::npos || text.find("尘") != std::string::npos)
        return "weather-sand";
    if (text.find("阴") != std::string::npos)
        return "weather-overcast";
    if (text.find("多云") != std::string::npos)
        return "weather-partly-cloudy-day";
    if (text.find("晴") != std::string::npos)
        return "weather-clear-day";
    if (text.find("风") != std::string::npos)
        return "weather-wind";
    const auto temperature = WeatherTemperature(DecodeWeather(text));
    if (temperature >= 35)
        return "weather-hot";
    if (temperature <= 0)
        return "weather-cold";
    return "weather-unknown";
}

bool IsPng192(const std::string& data) {
    if (data.size() < 24 || memcmp(data.data(), "\x89PNG\r\n\x1a\n", 8) != 0)
        return false;
    const auto* bytes = reinterpret_cast<const uint8_t*>(data.data());
    const auto read_be32 = [](const uint8_t* value) {
        return (static_cast<uint32_t>(value[0]) << 24) | (static_cast<uint32_t>(value[1]) << 16) |
               (static_cast<uint32_t>(value[2]) << 8) | value[3];
    };
    return read_be32(bytes + 16) == 192 && read_be32(bytes + 20) == 192;
}

std::pair<const char*, const char*> WeatherAdvice(const WeatherView& weather) {
    const auto& text = weather.current;
    if (text.find("雨") != std::string::npos || text.find("雷") != std::string::npos)
        return {"带好雨具", "把雨伞放进书包，路滑要慢慢走。"};
    if (text.find("雪") != std::string::npos || text.find("冰") != std::string::npos)
        return {"注意保暖", "戴好帽子和手套，小心结冰路面。"};
    if (text.find("雾") != std::string::npos || text.find("霾") != std::string::npos ||
        text.find("沙") != std::string::npos)
        return {"保护口鼻", "出门戴好口罩，路上注意来往车辆。"};
    const int temperature = WeatherTemperature(weather);
    if (temperature <= 10)
        return {"多穿一件", "早晚比较凉，带上外套更舒服。"};
    if (temperature >= 30)
        return {"记得喝水", "天气偏热，户外活动要及时休息。"};
    if (text.find("晴") != std::string::npos)
        return {"适合出门", "阳光不错，活动后别忘了补充水分。"};
    return {"轻松准备", "出门前看看窗外，带好今天的学习用品。"};
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

lv_obj_t* HanDisplay::Card(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color) {
    auto card = Box(parent, x, y, w, h, color);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(kCardBorder), 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0xb8a889), 0);
    lv_obj_set_style_shadow_width(card, 12, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
    lv_obj_set_style_shadow_ofs_y(card, 4, 0);
    return card;
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

const lv_font_t* HanDisplay::DynamicTextFont() const {
#ifndef HAN_UI_HOST_SIM
    auto theme = dynamic_cast<LvglTheme*>(current_theme_);
    if (theme != nullptr && theme->text_font() != nullptr && theme->text_font()->font() != nullptr)
        return theme->text_font()->font();
#endif
    return &han_font_28;
}

const lv_font_t* HanDisplay::DictionaryTextFont() const {
#ifndef HAN_UI_HOST_SIM
    if (dictionary_font_ != nullptr)
        return dictionary_font_;
#endif
    return &han_font_28;
}

const lv_font_t* HanDisplay::DictionaryLargeFont() const {
#ifndef HAN_UI_HOST_SIM
    if (dictionary_large_font_ != nullptr)
        return dictionary_large_font_;
#endif
    return &han_font_40;
}

const lv_font_t* HanDisplay::DictionaryHeroFont() const {
#ifndef HAN_UI_HOST_SIM
    if (dictionary_hero_font_ != nullptr)
        return dictionary_hero_font_;
#endif
    return DictionaryLargeFont();
}

void HanDisplay::ApplyDynamicTextFont(lv_obj_t* label) {
    if (label != nullptr)
        lv_obj_set_style_text_font(label, DynamicTextFont(), 0);
}

void HanDisplay::ApplyDictionaryTextFont(lv_obj_t* label) {
    if (label != nullptr)
        lv_obj_set_style_text_font(label, DictionaryTextFont(), 0);
}

void HanDisplay::ApplyDictionaryLargeFont(lv_obj_t* label) {
    if (label != nullptr)
        lv_obj_set_style_text_font(label, DictionaryLargeFont(), 0);
}

void HanDisplay::ReleaseDictionaryFonts() {
#ifndef HAN_UI_HOST_SIM
    DisplayLockGuard guard(this);
    if (dictionary_font_is_ttf_) {
        if (dictionary_hero_font_ != nullptr)
            lv_tiny_ttf_destroy(dictionary_hero_font_);
        if (dictionary_large_font_ != nullptr)
            lv_tiny_ttf_destroy(dictionary_large_font_);
        if (dictionary_font_ != nullptr)
            lv_tiny_ttf_destroy(dictionary_font_);
    } else if (dictionary_font_ != nullptr) {
        cbin_font_delete(dictionary_font_);
    }
    dictionary_font_ = nullptr;
    dictionary_large_font_ = nullptr;
    dictionary_hero_font_ = nullptr;
    dictionary_font_is_ttf_ = false;
    dictionary_font_data_.clear();
#endif
}

void HanDisplay::InstallDictionaryFont(std::string data) {
#ifndef HAN_UI_HOST_SIM
    ReleaseDictionaryFonts();
    DisplayLockGuard guard(this);
    dictionary_font_data_ = std::move(data);
    dictionary_font_ = cbin_font_create(reinterpret_cast<uint8_t*>(dictionary_font_data_.data()));
    if (dictionary_font_ == nullptr) {
        dictionary_font_data_.clear();
        ESP_LOGW("HanDisplay", "Ignoring invalid SD dictionary font");
        return;
    }
    // Use the SD font for the whole dictionary label instead of mixing a 30 px common font with
    // 28 px fallback glyphs in the same line. The embedded 28 px font only covers punctuation or
    // UI text outside the full dictionary-font range.
    dictionary_font_->fallback = &han_font_28;
    ESP_LOGI("HanDisplay", "SD dictionary font loaded: %u bytes",
             static_cast<unsigned>(dictionary_font_data_.size()));
    if (page_ == Page::Dictionary)
        Render(Page::Dictionary);
#else
    (void)data;
#endif
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
#ifndef HAN_UI_HOST_SIM
    // Keep decoded PNGs across partial refresh strips and page changes. Without a cache,
    // repeatedly decoding the same artwork can starve audio processing on the device.
    // This is an eviction budget, allocated on demand (large allocations use Tab5 PSRAM).
    lv_image_cache_resize(8 * 1024 * 1024, true);
    constexpr lv_event_code_t refresh_events[] = {
        LV_EVENT_REFR_START,   LV_EVENT_REFR_READY,       LV_EVENT_FLUSH_START,
        LV_EVENT_FLUSH_FINISH, LV_EVENT_FLUSH_WAIT_START, LV_EVENT_FLUSH_WAIT_FINISH};
    for (auto event : refresh_events)
        lv_display_add_event_cb(display_, OnRefresh, event, this);
#endif
    root_ = Box(lv_display_get_screen_active(display_), 0, 0, 1280, 720, kBg);
    lv_obj_set_style_radius(root_, 0, 0);
    footer_ = Image(root_, &han_footer, 0, 574);
    mascot_ = Image(root_, &han_art_book, 36, 0);
    lv_image_set_scale(mascot_, 195);
    lv_image_set_pivot(mascot_, 0, 0);
    back_ = Button(root_, "<", 24, 14, 72, 72, kGreen, 6);
    lv_obj_set_style_text_opa(lv_obj_get_child(back_, 0), LV_OPA_TRANSP, 0);
    back_image_ = Image(back_, &han_timetable_back, 12, 12);
    title_ = Label(root_, "小小助手", 212, 26, 470, &han_font_brand);
    date_ = Label(root_, "日期待同步", 747, 45, 208);
    clock_ = Label(root_, "—:—", 970, 36, 132, &han_font_clock);
    top_divider_left_ = Box(root_, 952, 39, 1, 39, 0xd7d5d0);
    top_divider_right_ = Box(root_, 1101, 39, 1, 39, 0xd7d5d0);
    wifi_button_ = Button(root_, "", 1115, 22, 72, 72, kBg, 7);
    lv_obj_set_style_bg_opa(wifi_button_, LV_OPA_TRANSP, 0);
    wifi_image_ = Image(wifi_button_, &han_status_wifi_off, 12, 12);
    battery_button_ = Button(root_, "", 1183, 22, 72, 72, kBg, 12);
    lv_obj_set_style_bg_opa(battery_button_, LV_OPA_TRANSP, 0);
    battery_image_ = Image(battery_button_, &han_status_battery_unknown, 11, 12);
    body_ = Box(root_, 24, 104, 1232, 490, kBg);
    lv_obj_set_style_bg_opa(body_, LV_OPA_TRANSP, 0);

    // Keep assistant activity and conversation in one calm, persistent card. Voice activation is
    // wake-word based; a full-width press-to-talk control would compete with learning content.
    assistant_card_ = Card(root_, 24, 606, 1232, 90, 0xffffff);
    assistant_badge_ = Box(assistant_card_, 12, 10, 70, 70, 0xe8f4ff);
    lv_obj_set_style_radius(assistant_badge_, 20, 0);
    auto assistant_icon = Image(assistant_badge_, &han_icon_dictionary, 7, 7);
    lv_image_set_scale(assistant_icon, 112);
    lv_image_set_pivot(assistant_icon, 0, 0);
    role_box_ = Box(assistant_card_, 98, 9, 118, 31, kPurple);
    lv_obj_set_style_radius(role_box_, 16, 0);
    role_label_ = Label(role_box_, "小智", 4, -1, 110);
    lv_obj_set_style_text_align(role_label_, LV_TEXT_ALIGN_CENTER, 0);
    message_ = Label(assistant_card_, "说“你好小智”，一起开始今天的学习吧", 98, 43, 870);
    lv_obj_set_height(message_, 39);
    lv_label_set_long_mode(message_, LV_LABEL_LONG_DOT);
    status_box_ = Box(assistant_card_, 990, 14, 222, 62, 0xf1f6fb);
    lv_obj_set_style_radius(status_box_, 20, 0);
    status_label_ = Label(status_box_, "准备中", 8, 13, 206);
    notification_label_ = Label(status_box_, "", 8, 13, 206);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(notification_label_, LV_LABEL_LONG_DOT);
    lv_obj_set_height(status_label_, 38);
    lv_obj_set_height(notification_label_, 38);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    // Startup and local UI strings are all present in this embedded Source Han font. Keeping
    // these labels on one font also prevents an incomplete downloaded theme from dropping glyphs
    // before the server-side dynamic fallback is ready.
    lv_obj_set_style_text_font(status_label_, &han_font_28, 0);
    lv_obj_set_style_text_font(notification_label_, &han_font_28, 0);
    lv_obj_set_style_text_font(message_, &han_font_28, 0);
    page_icon_ = lv_image_create(root_);
    lv_obj_set_pos(page_icon_, 112, 23);
    lv_image_set_scale(page_icon_, 112);
    lv_image_set_pivot(page_icon_, 0, 0);
    lv_obj_add_flag(page_icon_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(page_icon_, LV_OBJ_FLAG_CLICKABLE);
    tick_ = lv_timer_create(Tick, 800, this);
    Render(Page::Home);
    Queue(2, "");  // Read optional timetable/weather content off the LVGL/main tasks.
    Queue(6, "");  // Load the optional indexed-dictionary font on the worker task.
}

void HanDisplay::SetTheme(Theme* theme) {
    // Product artwork and headings keep their fixed fonts. Text received from the service uses
    // the common Noto font so the server's dynamic glyph fallback can fill uncommon characters.
    DisplayLockGuard guard(this);
    current_theme_ = theme;
    lv_obj_set_style_text_font(status_label_, &han_font_28, 0);
    lv_obj_set_style_text_font(notification_label_, &han_font_28, 0);
#ifndef HAN_UI_HOST_SIM
    if (dictionary_font_ != nullptr) {
        dictionary_font_->fallback = &han_font_28;
    }
    if (dictionary_large_font_ != nullptr)
        dictionary_large_font_->fallback = &han_font_40;
    if (dictionary_hero_font_ != nullptr)
        dictionary_hero_font_->fallback = &han_font_40;
#endif
}

void HanDisplay::SetStatus(const char* status) {
    const char* shown = status ? status : "";
#ifndef HAN_UI_HOST_SIM
    if (strcmp(shown, Lang::Strings::STANDBY) == 0)
        shown = "等待唤醒";
    LvglDisplay::SetStatus(shown);
#else
    if (!shown[0])
        shown = "等待唤醒";
    MipiLcdDisplay::SetStatus(shown);
#endif
    DisplayLockGuard guard(this);
    if (status_box_)
        lv_obj_set_style_bg_color(
            status_box_,
            lv_color_hex(strstr(shown, "听")                            ? 0xd9f4df
                         : strstr(shown, "说") || strstr(shown, "回答") ? 0xffe8d6
                                                                        : 0xf1f6fb),
            0);
}

void HanDisplay::SetChatMessage(const char* role, const char* text) {
    DisplayLockGuard guard(this);
    if (!message_ || !role_label_ || !assistant_card_)
        return;
    const char* shown = text ? text : "";
    const char* who = "小智";
    uint32_t color = 0xffffff;
    if (!shown[0]) {
        shown = "说“你好小智”，一起开始今天的学习吧";
    } else if (role && strcmp(role, "user") == 0) {
        who = "我说";
        color = 0xf1fbf5;
    } else if (role && strcmp(role, "system") == 0) {
        who = "系统";
        color = 0xfff8e9;
        if (initial_banner_pending_) {
            shown = "正在准备屏幕、声音和网络，请稍候…";
            who = "准备中";
            initial_banner_pending_ = false;
        }
    }
    lv_obj_set_style_text_font(
        message_, role && strcmp(role, "assistant") == 0 ? DynamicTextFont() : &han_font_28, 0);
    lv_label_set_text(role_label_, who);
    lv_label_set_text(message_, shown);
    lv_obj_set_style_bg_color(assistant_card_, lv_color_hex(color), 0);
}
void HanDisplay::ClearChatMessages() { SetChatMessage("", ""); }
void HanDisplay::Toast(const char* text) { ShowNotification(text, 4500); }

#ifndef HAN_UI_HOST_SIM
void HanDisplay::OnRefresh(lv_event_t* event) {
    auto self = static_cast<HanDisplay*>(lv_event_get_user_data(event));
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_REFR_START) {
        self->page_refresh_active_ = self->page_refresh_pending_;
        self->page_refresh_pending_ = false;
        self->refresh_started_ms_ = NowMs();
        self->flush_count_ = 0;
        self->flush_pixels_ = 0;
        self->flush_submit_us_ = 0;
        self->flush_wait_us_ = 0;
        return;
    }
    if (!self->page_refresh_active_)
        return;
    switch (code) {
        case LV_EVENT_FLUSH_START: {
            self->flush_started_us_ = esp_timer_get_time();
            const auto area = static_cast<const lv_area_t*>(lv_event_get_param(event));
            ++self->flush_count_;
            self->flush_pixels_ += lv_area_get_size(area);
            break;
        }
        case LV_EVENT_FLUSH_FINISH:
            self->flush_submit_us_ += esp_timer_get_time() - self->flush_started_us_;
            break;
        case LV_EVENT_FLUSH_WAIT_START:
            self->flush_wait_started_us_ = esp_timer_get_time();
            break;
        case LV_EVENT_FLUSH_WAIT_FINISH:
            self->flush_wait_us_ += esp_timer_get_time() - self->flush_wait_started_us_;
            break;
        case LV_EVENT_REFR_READY: {
            const auto now_ms = NowMs();
            // The last asynchronous DMA copy can still be in flight at REFR_READY.
            // Log only the frame following Render(), not every clock/status update.
            ESP_LOGI("HanDisplay",
                     "Page %d: build=%lld ms refresh=%lld ms queued=%lld ms "
                     "flushes=%u pixels=%u submit=%lld us wait=%lld us PSRAM=%u",
                     static_cast<int>(self->page_), self->page_build_ms_,
                     now_ms - self->refresh_started_ms_, now_ms - self->page_render_started_ms_,
                     static_cast<unsigned>(self->flush_count_),
                     static_cast<unsigned>(self->flush_pixels_), self->flush_submit_us_,
                     self->flush_wait_us_,
                     static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
            self->page_refresh_active_ = false;
            break;
        }
        default:
            break;
    }
}
#endif

void HanDisplay::Render(Page page) {
#ifndef HAN_UI_HOST_SIM
    page_render_started_ms_ = NowMs();
#endif
    const bool entering_dictionary = page == Page::Dictionary && page_ != Page::Dictionary;
    page_ = page;
    if (entering_dictionary)
        stroke_ = -1;
    CloseBatteryPopup();
    stroke_playing_ = false;
    timer_value_ = stroke_value_ = stroke_image_ = network_info_ = search_ = nullptr;
    glyph_title_image_ = glyph_title_placeholder_ = nullptr;
    search_overlay_ = search_input_ = search_results_ = search_status_ = nullptr;
    pinyin_page_label_ = nullptr;
    definition_overlay_ = nullptr;
    pinyin_tone_buttons_.fill(nullptr);
    brightness_value_ = volume_value_ = brightness_bar_ = volume_bar_ = nullptr;
    alarm_hour_ = alarm_minute_ = nullptr;
    for (auto& label : totals_)
        label = nullptr;
    lv_obj_clean(body_);
    if (stroke_draw_buf_) {
        lv_draw_buf_destroy(stroke_draw_buf_);
        stroke_draw_buf_ = nullptr;
    }
    if (glyph_title_draw_buf_) {
        lv_draw_buf_destroy(glyph_title_draw_buf_);
        glyph_title_draw_buf_ = nullptr;
    }
    // Late worker results are ignored after the expected character is cleared.
    stroke_placeholder_ = nullptr;
    expected_stroke_character_.clear();
    stroke_glyph_ = {};
    const char* titles[] = {"小小助手", "小小字典", "英语音标", "课程表",
                            "作业计时", "闹钟",     "天气",     "设置"};
    const lv_image_dsc_t* page_icons[] = {
        nullptr,         &han_icon_dictionary, &han_icon_phonetics, &han_icon_timetable,
        &han_icon_timer, &han_icon_alarm,      &han_icon_weather,   &han_icon_settings};
    lv_label_set_text(title_, titles[static_cast<int>(page)]);

    // Reset the shared chrome before applying a page-specific composition.
    lv_obj_remove_flag(date_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(clock_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(top_divider_left_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(top_divider_right_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(wifi_button_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(battery_button_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(footer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(assistant_card_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(back_, 24, 14);
    lv_obj_set_size(back_, 72, 72);
    lv_obj_set_style_radius(back_, 24, 0);
    lv_obj_set_pos(back_image_, 12, 12);
    lv_obj_set_pos(date_, 747, 45);
    lv_obj_set_width(date_, 208);
    lv_obj_set_style_text_font(date_, &han_font_28, 0);
    lv_obj_set_style_text_align(date_, LV_TEXT_ALIGN_LEFT, 0);

    if (page == Page::Home) {
        lv_obj_add_flag(back_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(page_icon_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(mascot_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(title_, 212);
        lv_obj_set_pos(body_, 38, 118);
        lv_obj_set_size(body_, 1204, 448);
    } else if (page == Page::Timetable) {
        lv_obj_remove_flag(back_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(page_icon_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mascot_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(clock_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(top_divider_left_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(top_divider_right_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(wifi_button_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_button_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(footer_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(assistant_card_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(back_, 27, 18);
        lv_obj_set_size(back_, 86, 86);
        lv_obj_set_style_radius(back_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(back_image_, 19, 19);
        lv_obj_set_x(title_, 133);
        lv_obj_set_y(title_, 25);
        lv_obj_set_pos(date_, 548, 49);
        lv_obj_set_width(date_, 225);
        lv_obj_set_style_text_align(date_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(body_, 0, 0);
        lv_obj_set_size(body_, 1280, 720);
    } else {
        lv_obj_remove_flag(back_, LV_OBJ_FLAG_HIDDEN);
        lv_image_set_src(page_icon_, page_icons[static_cast<int>(page)]);
        lv_obj_remove_flag(page_icon_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mascot_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(title_, 178);
        lv_obj_set_pos(body_, 24, 104);
        lv_obj_set_size(body_, 1232, 490);
        if (page == Page::Dictionary) {
            lv_obj_add_flag(footer_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(assistant_card_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(body_, 1232, 592);
        }
    }

    if (page != Page::Timetable) {
        lv_obj_set_pos(assistant_card_, 24, page == Page::Home ? 584 : 606);
        lv_obj_set_size(assistant_card_, 1232, page == Page::Home ? 112 : 90);
        lv_obj_set_pos(assistant_badge_, 14, page == Page::Home ? 20 : 10);
        lv_obj_set_size(assistant_badge_, 70, 70);
        auto icon = lv_obj_get_child(assistant_badge_, 0);
        lv_obj_set_pos(icon, 8, 8);
        lv_image_set_scale(icon, 108);
        lv_obj_set_pos(role_box_, 100, page == Page::Home ? 20 : 11);
        lv_obj_set_size(role_box_, 116, 34);
        lv_obj_set_pos(message_, 100, page == Page::Home ? 58 : 48);
        lv_obj_set_size(message_, 850, page == Page::Home ? 42 : 34);
        lv_label_set_long_mode(message_, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(status_box_, 974, page == Page::Home ? 25 : 14);
        lv_obj_set_size(status_box_, 240, 62);
        lv_obj_set_pos(status_label_, 8, 14);
        lv_obj_set_pos(notification_label_, 8, 14);
        lv_obj_set_width(status_label_, 224);
        lv_obj_set_width(notification_label_, 224);
    }
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
    if (page == Page::Timetable) {
        // body_ covers the full screen for this design, so keep the live root labels/buttons above
        // its decorative layers.
        lv_obj_move_foreground(back_);
        lv_obj_move_foreground(title_);
        lv_obj_move_foreground(date_);
    }
    // Every page transition uses a full-screen framebuffer. Merge the changed header and body
    // into one invalid area so the top-left chrome cannot reach the panel a frame early.
    lv_obj_invalidate(root_);
#ifndef HAN_UI_HOST_SIM
    page_build_ms_ = NowMs() - page_render_started_ms_;
    page_refresh_pending_ = true;
#endif
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

void HanDisplay::Dictionary() {
    auto grid = Card(body_, 0, 0, 465, 440, 0xfffbf7);
    lv_obj_set_style_border_width(grid, 3, 0);
    lv_obj_set_style_border_color(grid, lv_color_hex(0xf6c2bd), 0);
    lv_obj_set_style_radius(grid, 22, 0);
    static const lv_point_precise_t guides[][2] = {
        {{15, 220}, {450, 220}},
        {{232, 15}, {232, 425}},
        {{15, 15}, {450, 425}},
        {{450, 15}, {15, 425}},
    };
    for (const auto& points : guides) {
        auto line = lv_line_create(grid);
        lv_line_set_points(line, points, 2);
        lv_obj_set_style_line_width(line, 2, 0);
        lv_obj_set_style_line_color(line, lv_color_hex(0xf5c9c3), 0);
        lv_obj_set_style_line_dash_width(line, 10, 0);
        lv_obj_set_style_line_dash_gap(line, 8, 0);
        lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    }
    stroke_image_ = lv_canvas_create(grid);
    stroke_draw_buf_ = lv_draw_buf_create(400, 400, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
    lv_obj_set_pos(stroke_image_, 32, 18);
    lv_obj_remove_flag(stroke_image_, LV_OBJ_FLAG_CLICKABLE);
    if (stroke_draw_buf_)
        lv_canvas_set_draw_buf(stroke_image_, stroke_draw_buf_);
    if (stroke_draw_buf_)
        lv_canvas_fill_bg(stroke_image_, lv_color_hex(0xffffff), LV_OPA_TRANSP);
    lv_obj_add_flag(stroke_image_, LV_OBJ_FLAG_HIDDEN);
    stroke_placeholder_ = Label(grid, "正在读取笔顺…", 58, 178, 350);
    lv_obj_set_style_text_align(stroke_placeholder_, LV_TEXT_ALIGN_CENTER, 0);
    auto progress = Box(body_, 341, 448, 124, 34, 0xffdfe3);
    lv_obj_set_style_radius(progress, 17, 0);
    stroke_value_ = Label(progress, "", 4, 0, 116);
    ApplyDictionaryTextFont(stroke_value_);
    lv_obj_set_style_text_align(stroke_value_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(stroke_value_, LV_ALIGN_CENTER, 0, 0);

    const uint32_t control_colors[] = {0x43b96f, 0xf56c55, 0x378eea};
    const char* control_text[] = {"上一步", "播放笔顺", "下一步"};
    const int control_x[] = {0, 151, 328};
    const int control_w[] = {140, 166, 137};
    for (int index = 0; index < 3; ++index) {
        auto control = Button(body_, control_text[index], control_x[index], 490, control_w[index],
                              76, control_colors[index], 20 + index);
        lv_obj_set_style_radius(control, 22, 0);
        lv_obj_set_style_shadow_color(control, lv_color_hex(control_colors[index]), 0);
        lv_obj_set_style_shadow_width(control, 10, 0);
        lv_obj_set_style_shadow_opa(control, LV_OPA_20, 0);
        lv_obj_set_style_shadow_ofs_y(control, 4, 0);
        auto text = lv_obj_get_child(control, 0);
        lv_obj_set_style_text_color(text, lv_color_white(), 0);
    }

    auto details = Card(body_, 490, 0, 742, 566, 0xffffff);
    lv_obj_set_style_radius(details, 24, 0);
    glyph_title_image_ = lv_canvas_create(details);
    glyph_title_draw_buf_ = lv_draw_buf_create(96, 88, LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
    lv_obj_set_pos(glyph_title_image_, 18, 1);
    lv_obj_remove_flag(glyph_title_image_, LV_OBJ_FLAG_CLICKABLE);
    if (glyph_title_draw_buf_) {
        lv_canvas_set_draw_buf(glyph_title_image_, glyph_title_draw_buf_);
        lv_obj_add_flag(glyph_title_image_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(glyph_title_image_, LV_OBJ_FLAG_HIDDEN);
    }
    glyph_title_placeholder_ =
        Label(details, entry_.character.c_str(), 18, 17, 96, DictionaryHeroFont());
    lv_obj_set_height(glyph_title_placeholder_, 56);
    lv_obj_set_style_text_align(glyph_title_placeholder_, LV_TEXT_ALIGN_CENTER, 0);
    auto pinyin = Label(details, entry_.pinyin.c_str(), 128, 25, 210, &han_font_40);
    lv_obj_set_style_text_color(pinyin, lv_color_hex(0x182b50), 0);
    lv_obj_align_to(pinyin, glyph_title_image_, LV_ALIGN_OUT_RIGHT_MID, 12, 0);

    const lv_image_dsc_t* action_icons[] = {&han_icon_definition_detail, &han_icon_pinyin_search};
    const char* action_labels[] = {"释义", "拼音"};
    const uint32_t action_colors[] = {kOrange, kBlue};
    const uint32_t action_borders[] = {0xf6c9a8, 0xadd8f5};
    const int action_codes[] = {25, 23};
    for (int index = 0; index < 2; ++index) {
        auto button = Button(details, action_labels[index], 422 + index * 164, 6, 136, 78,
                             action_colors[index], action_codes[index]);
        lv_obj_set_style_radius(button, 23, 0);
        lv_obj_set_style_border_width(button, 2, 0);
        lv_obj_set_style_border_color(button, lv_color_hex(action_borders[index]), 0);
        lv_obj_set_style_transform_scale_x(button, 235, LV_STATE_PRESSED);
        lv_obj_set_style_transform_scale_y(button, 235, LV_STATE_PRESSED);
        auto action_text = lv_obj_get_child(button, 0);
        ApplyDictionaryTextFont(action_text);
        lv_obj_set_size(action_text, 58, DictionaryTextFont()->line_height + 2);
        lv_obj_set_style_text_align(action_text, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(action_text, LV_ALIGN_RIGHT_MID, -6, 0);
        auto icon = Image(button, action_icons[index], 7, 5);
        lv_image_set_scale(icon, 176);
        lv_image_set_pivot(icon, 0, 0);
    }
    const std::string radical =
        "部首 " + (entry_.radical.empty() ? std::string("—") : entry_.radical);
    const std::string count =
        entry_.stroke_count > 0 ? std::to_string(entry_.stroke_count) + "画" : "笔画 —";
    const std::string structure = entry_.structure.empty() ? "结构 —" : entry_.structure;
    const char* info[] = {radical.c_str(), count.c_str(), structure.c_str()};
    const uint32_t info_colors[] = {kGreen, kBlue, kOrange};
    const int info_widths[] = {170, 130, 190};
    int info_x = 24;
    for (int i = 0; i < 3; ++i) {
        auto chip = Box(details, info_x, 91, info_widths[i], 44, info_colors[i]);
        lv_obj_set_style_radius(chip, 22, 0);
        auto text = Label(chip, info[i], 6, 0, info_widths[i] - 12);
        ApplyDictionaryTextFont(text);
        lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(text, LV_ALIGN_CENTER, 0, 0);
        info_x += info_widths[i] + 12;
    }
    const int dictionary_line_height = DictionaryTextFont()->line_height;
    auto meaning = Label(details, entry_.definition.c_str(), 26, 151, 690);
    ApplyDictionaryTextFont(meaning);
    lv_label_set_long_mode(meaning, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(meaning, LV_SIZE_CONTENT);
    lv_obj_update_layout(meaning);
    const int natural_meaning_height = lv_obj_get_height(meaning);
    const int meaning_height = std::clamp(natural_meaning_height, dictionary_line_height, 242);
    lv_obj_set_height(meaning, meaning_height);
    if (natural_meaning_height > meaning_height)
        lv_label_set_long_mode(meaning, LV_LABEL_LONG_DOT);

    const int words_y = 151 + meaning_height + 6;
    const int word_chip_height = std::max(46, dictionary_line_height + 8);
    const int word_row_step = word_chip_height + 8;
    auto words_title = Box(details, 24, words_y, 84, word_chip_height, kGreen);
    lv_obj_set_style_radius(words_title, word_chip_height / 2, 0);
    auto words_title_text = Label(words_title, "组词", 4, 0, 76);
    ApplyDictionaryTextFont(words_title_text);
    lv_obj_set_style_text_align(words_title_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(words_title_text, LV_ALIGN_CENTER, 0, 0);
    const int available_word_rows =
        std::clamp((549 - words_y - word_chip_height) / word_row_step + 1, 1, 3);
    int word_row = 0;
    int word_x = 120;
    for (const auto& word : entry_.words) {
        lv_point_t text_size{};
        lv_text_get_size(&text_size, word.c_str(), DictionaryTextFont(), 0, 0, LV_COORD_MAX,
                         LV_TEXT_FLAG_NONE);
        // Allocate every pill from its real rendered width. Fixed columns clipped four-character
        // words with the full SD font, while this flow layout simply wraps the next pill.
        const int chip_width = std::clamp<int>(text_size.x + 28, 84, 690);
        if (word_x + chip_width > 718) {
            ++word_row;
            word_x = 24;
        }
        if (word_row >= available_word_rows)
            break;
        auto chip = Box(details, word_x, words_y + word_row * word_row_step, chip_width,
                        word_chip_height, 0xeaf8ee);
        lv_obj_set_style_radius(chip, word_chip_height / 2, 0);
        auto text = Label(chip, word.c_str(), 10, 0, chip_width - 20);
        ApplyDictionaryTextFont(text);
        lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(text, LV_ALIGN_CENTER, 0, 0);
        word_x += chip_width + 10;
    }
    UpdateStroke();
}

void HanDisplay::OpenPinyinSearch() {
    pinyin_query_.clear();
    pinyin_search_key_.clear();
    pinyin_results_.clear();
    pinyin_status_text_.clear();
    pinyin_page_ = 0;
    pinyin_tone_ = -1;
    pinyin_tone_buttons_.fill(nullptr);
    search_overlay_ = Card(body_, 0, 0, 1232, 566, 0xfffcf6);
    lv_obj_set_style_border_width(search_overlay_, 3, 0);
    lv_obj_set_style_border_color(search_overlay_, lv_color_hex(0xd7eee0), 0);
    auto search_title = Label(search_overlay_, "拼音查字", 24, 18, 190, &han_font_40);
    ApplyDictionaryTextFont(search_title);
    auto input_box = Box(search_overlay_, 220, 14, 450, 64, 0xf2f7fb);
    lv_obj_set_style_border_width(input_box, 2, 0);
    lv_obj_set_style_border_color(input_box, lv_color_hex(0xc9dfea), 0);
    search_input_ = Label(input_box, "输入拼音，例如 han", 20, 10, 410);
    ApplyDictionaryTextFont(search_input_);
    lv_obj_set_style_text_color(search_input_, lv_color_hex(kMuted), 0);
    auto submit = Button(search_overlay_, "查找", 690, 14, 164, 64, kGreen, 1127);
    auto close = Button(search_overlay_, "关闭", 1034, 14, 174, 64, kPink, 1128);
    ApplyDictionaryTextFont(lv_obj_get_child(submit, 0));
    ApplyDictionaryTextFont(lv_obj_get_child(close, 0));

    auto tone_title = Label(search_overlay_, "音调", 28, 104, 82);
    ApplyDictionaryTextFont(tone_title);
    const char* tone_names[] = {"全部", "轻声", "一声", "二声", "三声", "四声"};
    for (int index = 0; index < 6; ++index) {
        pinyin_tone_buttons_[index] =
            Button(search_overlay_, tone_names[index], 118 + index * 147, 91, 132, 52,
                   index == 0 ? kGreen : 0xf1f4f6, 1130 + index);
        ApplyDictionaryTextFont(lv_obj_get_child(pinyin_tone_buttons_[index], 0));
        lv_obj_set_style_radius(pinyin_tone_buttons_[index], 20, 0);
    }

    search_results_ = Card(search_overlay_, 24, 164, 520, 374, 0xffffff);
    lv_obj_add_event_cb(search_results_, OnPinyinGesture, LV_EVENT_GESTURE, this);
    auto keyboard = Card(search_overlay_, 568, 164, 640, 374, 0xf3f8ff);
    const char* rows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    const int starts[] = {18, 48, 110};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; rows[row][column]; ++column) {
            char label[2] = {rows[row][column], '\0'};
            auto key = Button(keyboard, label, starts[row] + column * 60, 14 + row * 76, 52, 60,
                              0xffffff, 1100 + rows[row][column] - 'a');
            ApplyDictionaryTextFont(lv_obj_get_child(key, 0));
        }
    }
    auto backspace = Button(keyboard, "退格", 116, 246, 190, 62, kOrange, 1126);
    auto clear = Button(keyboard, "清空", 330, 246, 190, 62, kPurple, 1129);
    ApplyDictionaryTextFont(lv_obj_get_child(backspace, 0));
    ApplyDictionaryTextFont(lv_obj_get_child(clear, 0));
    RenderPinyinResults(DictionaryService::GetInstance().store().pinyin_ready()
                            ? "输入拼音，可按音调缩小候选范围"
                            : "SD 卡缺少拼音索引，请更新内容包");
    UpdatePinyinToneButtons();
}

void HanDisplay::OpenDefinitionDetails() {
    if (definition_overlay_)
        return;
    definition_overlay_ = Card(body_, 0, 0, 1232, 566, 0xfffaf3);
    lv_obj_set_style_border_width(definition_overlay_, 3, 0);
    lv_obj_set_style_border_color(definition_overlay_, lv_color_hex(0xf3d7bc), 0);

    auto icon = Image(definition_overlay_, &han_icon_definition_detail, 24, 8);
    lv_image_set_scale(icon, 170);
    lv_image_set_pivot(icon, 0, 0);
    const std::string title = entry_.character + " 的完整释义";
    auto title_label = Label(definition_overlay_, title.c_str(), 102, 24, 820);
    ApplyDictionaryTextFont(title_label);
    auto close = Button(definition_overlay_, "关闭", 1020, 18, 180, 62, kPink, 1136);
    ApplyDictionaryTextFont(lv_obj_get_child(close, 0));
    lv_obj_set_style_radius(close, 22, 0);

    auto content = Card(definition_overlay_, 24, 96, 1184, 442, 0xffffff);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(content, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(content, 10, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(content, 5, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x79b9e8), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(content, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_bottom(content, 24, 0);
    auto meaning = Label(content, entry_.definition.c_str(), 26, 22, 1116);
    ApplyDictionaryTextFont(meaning);
    lv_label_set_long_mode(meaning, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(meaning, LV_SIZE_CONTENT);
    lv_obj_set_style_text_line_space(meaning, 9, 0);
}

void HanDisplay::UpdatePinyinToneButtons() {
    for (int index = 0; index < 6; ++index) {
        if (!pinyin_tone_buttons_[index])
            continue;
        const bool selected = index == pinyin_tone_ + 1;
        lv_obj_set_style_bg_color(pinyin_tone_buttons_[index],
                                  lv_color_hex(selected ? kGreen : 0xf1f4f6), 0);
        lv_obj_set_style_border_width(pinyin_tone_buttons_[index], selected ? 2 : 0, 0);
        lv_obj_set_style_border_color(pinyin_tone_buttons_[index], lv_color_hex(0x8ad5a0), 0);
    }
}

void HanDisplay::StartPinyinSearch() {
    const auto normalized = han::ContentStore::NormalizePinyin(pinyin_query_);
    if (normalized.empty()) {
        Toast("请先输入拼音，例如 han");
        return;
    }
    pinyin_query_ = normalized;
    pinyin_search_key_ = normalized;
    if (pinyin_tone_ >= 0)
        pinyin_search_key_.push_back(static_cast<char>('0' + pinyin_tone_));
    pinyin_results_.clear();
    pinyin_page_ = 0;
    RenderPinyinResults("正在离线字库中查找…");
    Queue(7, pinyin_search_key_);
}

void HanDisplay::RenderPinyinResults(const char* status) {
    if (!search_results_)
        return;
    if (status)
        pinyin_status_text_ = status;
    lv_obj_clean(search_results_);
    pinyin_page_label_ = nullptr;
    search_status_ = Label(search_results_, pinyin_status_text_.c_str(), 20, 12, 480);
    ApplyDictionaryTextFont(search_status_);
    lv_obj_set_style_text_color(search_status_, lv_color_hex(kMuted), 0);
    lv_label_set_long_mode(search_status_, LV_LABEL_LONG_DOT);
    lv_obj_set_height(search_status_, 40);
    const int page_count = std::max(
        1, (static_cast<int>(pinyin_results_.size()) + kPinyinPageSize - 1) / kPinyinPageSize);
    pinyin_page_ = std::clamp(pinyin_page_, 0, page_count - 1);
    const int first = pinyin_page_ * kPinyinPageSize;
    const int last = std::min(first + kPinyinPageSize, static_cast<int>(pinyin_results_.size()));
    for (int index = first; index < last; ++index) {
        const int cell = index - first;
        auto button = Button(search_results_, pinyin_results_[index].c_str(), 18 + cell % 4 * 122,
                             58 + cell / 4 * 80, 108, 68,
                             cell % 3 == 0   ? kGreen
                             : cell % 3 == 1 ? kBlue
                                             : kOrange,
                             1200 + index);
        auto text = lv_obj_get_child(button, 0);
        ApplyDictionaryTextFont(text);
        lv_obj_align(text, LV_ALIGN_CENTER, 0, 0);
    }
    if (page_count > 1) {
        auto previous = Button(search_results_, "‹", 20, 310, 64, 46, kGreen, 1137);
        auto next = Button(search_results_, "›", 436, 310, 64, 46, kBlue, 1138);
        ApplyDictionaryTextFont(lv_obj_get_child(previous, 0));
        ApplyDictionaryTextFont(lv_obj_get_child(next, 0));
        const std::string page_text =
            std::to_string(pinyin_page_ + 1) + "/" + std::to_string(page_count) + " · 左右滑动翻页";
        pinyin_page_label_ = Label(search_results_, page_text.c_str(), 92, 318, 336);
        ApplyDictionaryTextFont(pinyin_page_label_);
        lv_obj_set_style_text_align(pinyin_page_label_, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void HanDisplay::ApplyPinyinResults(const std::string& query, std::vector<std::string> results) {
    if (page_ != Page::Dictionary || !search_overlay_ || query != pinyin_search_key_)
        return;
    pinyin_results_ = std::move(results);
    pinyin_page_ = 0;
    const std::string status =
        pinyin_results_.empty()
            ? "没有找到 “" + query + "” 对应的汉字"
            : query + " · 共" + std::to_string(pinyin_results_.size()) + "字，点击查看";
    RenderPinyinResults(status.c_str());
}

void HanDisplay::UpdateStroke() {
    if (!stroke_value_)
        return;
    if (entry_.strokes.empty()) {
        lv_label_set_text(stroke_value_, "无动画");
        HideStrokeArtwork();
        return;
    }
    const int stroke_count = static_cast<int>(entry_.strokes.size());
    stroke_ = std::clamp(stroke_, -1, stroke_count);
    const int completed_strokes = stroke_ < 0 ? 0 : std::min(stroke_ + 1, stroke_count);
    auto value = std::to_string(completed_strokes) + "/" + std::to_string(stroke_count);
    lv_label_set_text(stroke_value_, value.c_str());
    if (stroke_glyph_.character == entry_.character) {
        RenderStroke();
        return;
    }
    if (expected_stroke_character_ != entry_.character) {
        expected_stroke_character_ = entry_.character;
        if (!Queue(3, entry_.character))
            HideStrokeArtwork();
    }
}

void HanDisplay::HideStrokeArtwork() {
    if (stroke_image_)
        lv_obj_add_flag(stroke_image_, LV_OBJ_FLAG_HIDDEN);
    if (stroke_placeholder_)
        lv_obj_add_flag(stroke_placeholder_, LV_OBJ_FLAG_HIDDEN);
}

bool HanDisplay::ApplyMissingStrokeGlyph(const std::string& character) {
    DisplayLockGuard guard(this);
    if (page_ != Page::Dictionary || character.empty() || character != expected_stroke_character_ ||
        character != entry_.character)
        return false;
    HideStrokeArtwork();
    return true;
}

bool HanDisplay::ApplyStrokeGlyph(const std::string& character, han::StrokeGlyph glyph) {
    DisplayLockGuard guard(this);
    if (page_ != Page::Dictionary || character.empty() || character != expected_stroke_character_ ||
        character != entry_.character || !stroke_image_ || !stroke_draw_buf_ ||
        glyph.character != character || glyph.strokes.empty())
        return false;
    stroke_glyph_ = std::move(glyph);
    RenderStroke();
    return true;
}

void HanDisplay::RenderStroke() {
    if (!stroke_image_ || !stroke_draw_buf_ || stroke_glyph_.strokes.empty())
        return;
#if LV_USE_VECTOR_GRAPHIC
    if (DrawGlyph(stroke_image_, stroke_glyph_, 400, 400, 10, stroke_, -1, true)) {
        lv_obj_remove_flag(stroke_image_, LV_OBJ_FLAG_HIDDEN);
        if (stroke_placeholder_)
            lv_obj_add_flag(stroke_placeholder_, LV_OBJ_FLAG_HIDDEN);
    } else
        HideStrokeArtwork();
    if (glyph_title_image_ && glyph_title_draw_buf_ &&
        DrawGlyph(glyph_title_image_, stroke_glyph_, 96, 88, 1, -1, -1, false)) {
        lv_obj_remove_flag(glyph_title_image_, LV_OBJ_FLAG_HIDDEN);
        if (glyph_title_placeholder_)
            lv_obj_add_flag(glyph_title_placeholder_, LV_OBJ_FLAG_HIDDEN);
    }
#else
    if (stroke_placeholder_)
        lv_label_set_text(stroke_placeholder_, "矢量笔顺已加载（设备端显示）");
#endif
}

void HanDisplay::Phonetics() {
    const char* cats[] = {"单元音", "双元音", "辅音"};
    for (int i = 0; i < 3; ++i) {
        auto tab =
            Button(body_, cats[i], i * 418, 0, 396, 66, i == category_ ? 0xc3a5f4 : kBlue, 100 + i);
        if (i == category_) {
            lv_obj_set_style_border_width(tab, 3, 0);
            lv_obj_set_style_border_color(tab, lv_color_hex(0x8d6dd2), 0);
        }
    }
    auto panel = Card(body_, 0, 86, 465, 397, 0xeee6ff);
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
    auto detail = Card(body_, 490, 86, 742, 397, 0xffffff);
    auto headphones = Image(detail, &han_icon_phonetics, 650, 14);
    lv_image_set_scale(headphones, 104);
    lv_image_set_pivot(headphones, 0, 0);
    auto& sound = han::kSounds[sound_];
    auto ipa = "/" + std::string(sound.ipa) + "/";
    auto current = Box(detail, 24, 20, 170, 38, kPurple);
    lv_obj_set_style_radius(current, 19, 0);
    auto current_text = Label(current, "当前音标", 6, 1, 158);
    lv_obj_set_style_text_align(current_text, LV_TEXT_ALIGN_CENTER, 0);
    auto big = Label(detail, ipa.c_str(), 20, 45, 700, &han_font_large);
    lv_obj_set_style_text_align(big, LV_TEXT_ALIGN_CENTER, 0);
    Button(detail, "听示范", 155, 158, 430, 62, kPurple, 110);
    for (int i = 0; i < 3; ++i)
        Button(detail, sound.words[i], 20 + i * 237, 238, 220, 68, kGreen, 111 + i);
    auto record = Button(detail, "录音跟读（后续）", 20, 323, 340, 56, kBlue, 114);
    lv_obj_add_state(record, LV_STATE_DISABLED);
    auto audio_hint = Label(detail, "音频需放入 SD 卡", 384, 334, 330);
    lv_obj_set_style_text_color(audio_hint, lv_color_hex(kMuted), 0);
}

void HanDisplay::Timetable() {
    // This page deliberately owns the full 1280x720 canvas. Its proportions follow the approved
    // timetable concept rather than the denser shared application chrome.
    Image(body_, &han_timetable_mascot, 1007, 0);

    auto date_card = Card(body_, 466, 34, 340, 69, 0xffffff);
    lv_obj_set_style_radius(date_card, 35, 0);
    Image(date_card, &han_timetable_calendar, 20, 10);
    auto week = Button(body_, timetable_day_group_ ? "周末" : (timetable_week_ ? "下周" : "本周"),
                       868, 40, 126, 57, kGreen, 502);
    lv_obj_set_style_radius(week, 29, 0);
    Image(week, &han_timetable_chevron_down, 88, 13);

    auto table = Card(body_, 17, 126, 1246, 560, 0xffffff);
    const int first_day = timetable_day_group_ ? 5 : 0;
    const int columns = timetable_day_group_ ? 2 : 5;
    const int col_width = 1110 / columns;
    int lesson_count = 5;
    for (int c = 0; c < columns; ++c)
        lesson_count =
            std::max(lesson_count, static_cast<int>(timetable_.days[first_day + c].size()));
    lesson_count = std::clamp(lesson_count, 5, 8);
    const int row_pitch = 476 / lesson_count;
    const int cell_height = row_pitch - 7;
    const auto lesson_range = "1—" + std::to_string(lesson_count) + "节";
    auto row_head = Box(table, 12, 12, 108, 54, 0xf4f5e8);
    lv_obj_set_style_radius(row_head, 18, 0);
    auto range_label = Label(row_head, lesson_range.c_str(), 5, 10, 98, &han_font_28);
    lv_obj_set_style_text_align(range_label, LV_TEXT_ALIGN_CENTER, 0);
    for (int c = 0; c < columns; ++c) {
        int day = first_day + c;
        const bool today = timetable_week_ == 0 && day == timetable_today_;
        const int x = 124 + c * col_width;
        if (today)
            Box(table, x, 12, col_width - 4, 536, 0xe6f4ff);
        auto head = Box(table, x, 12, col_width - 4, 54, today ? 0xc9e8ff : 0xf3f6ec);
        lv_obj_set_style_radius(head, 18, 0);
        auto label = Label(head, kWeekdays[day], 0, 7, col_width - 4, &han_font_schedule);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        for (int lesson = 0; lesson < lesson_count; ++lesson) {
            const auto& classes = timetable_.days[day];
            const std::string name =
                lesson < static_cast<int>(classes.size()) ? classes[lesson] : "";
            const int kind = SubjectKind(name);
            const uint32_t colors[] = {0xffded5, 0xcdeaff, 0xeadeff, 0xddf6d2, 0xffe2c3,
                                       0xcdf2ed, 0xd4eef7, 0xffe2ef, 0xffefc9, 0xe4efcc,
                                       0xffe2d1, 0xeee6fa, 0xdff2e5};
            auto cell = Button(table, "", x + 4, 73 + lesson * row_pitch, col_width - 12,
                               cell_height, colors[kind], 700 + day * 8 + lesson);
            lv_obj_set_style_bg_grad_color(cell, lv_color_hex(0xfffbf4), 0);
            lv_obj_set_style_bg_grad_dir(cell, LV_GRAD_DIR_VER, 0);
            lv_obj_set_style_radius(cell, 18, 0);
            const lv_image_dsc_t* icons[] = {
                &han_subject_book,     &han_subject_calculator, &han_subject_english,
                &han_subject_science,  &han_subject_art,        &han_subject_sport,
                &han_subject_computer, &han_subject_music,      &han_subject_martial,
                &han_subject_labor,    &han_subject_flute,      &han_subject_star,
                &han_subject_club};
            if (!name.empty() && icons[kind])
                Image(cell, icons[kind], 10, (cell_height - 48) / 2);
            const bool long_name = name.size() > 6;
            auto text = Label(cell, name.empty() ? "—" : name.c_str(), name.empty() ? 0 : 66,
                              std::max(0, (cell_height - (long_name ? 32 : 39)) / 2),
                              col_width - (name.empty() ? 12 : 82),
                              long_name ? &han_font_28 : &han_font_schedule);
            lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
            lv_obj_set_height(text, 44);
            if (name.empty())
                lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
        }
    }
    for (int lesson = 0; lesson < lesson_count; ++lesson) {
        auto row = Box(table, 12, 73 + lesson * row_pitch, 108, cell_height, 0xf9f4e5);
        lv_obj_set_style_radius(row, 18, 0);
        auto name = "第" + std::to_string(lesson + 1) + "节";
        auto row_label =
            Label(row, name.c_str(), 5, std::max(0, (cell_height - 34) / 2), 98, &han_font_28);
        lv_obj_set_style_text_align(row_label, LV_TEXT_ALIGN_CENTER, 0);
    }

    // All configured lessons fit on one page. The top week selector cycles 本周 / 下周 / 周末.
    if (!timetable_.valid || timetable_.empty()) {
        auto empty = Box(table, 132, 174, 758, 118, 0xfffbf4);
        auto hint =
            Label(empty, timetable_.valid ? "还没有课程，请导入课表" : "课表未加载或格式错误", 20,
                  39, 718);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    }
}

bool HanDisplay::ApplyTimetable(const std::string& json) {
    han::TimetableData data;
    const bool valid = han::TimetableData::Parse(json, data);
    DisplayLockGuard guard(this);
    timetable_ = std::move(data);
    if (page_ == Page::Timetable)
        Render(Page::Timetable);
    return valid;
}

#ifdef HAN_UI_HOST_SIM
void HanDisplay::SetWeatherTextForTest(std::string text) {
    weather_text_ = std::move(text);
    if (setup_ui_called_)
        Render(Page::Weather);
}
#endif

void HanDisplay::ApplyWeatherArt(std::string id, std::string data) {
    if (!IsPng192(data)) {
        id.clear();
        data.clear();
    }
    if (!weather_art_data_.empty())
        lv_image_cache_drop(&weather_art_dsc_);
    weather_art_id_ = std::move(id);
    weather_art_data_ = std::move(data);
    weather_art_dsc_ = {};
    if (!weather_art_data_.empty()) {
        weather_art_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        weather_art_dsc_.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
        weather_art_dsc_.header.w = 192;
        weather_art_dsc_.header.h = 192;
        weather_art_dsc_.data_size = static_cast<uint32_t>(weather_art_data_.size());
        weather_art_dsc_.data = reinterpret_cast<const uint8_t*>(weather_art_data_.data());
    }
}

void HanDisplay::Timer() {
    auto left = Card(body_, 0, 0, 724, 480, 0xffffff);
    const uint32_t subject_colors[] = {0xffddd6, 0xd9edfc, 0xe9dffc};
    for (int i = 0; i < 3; ++i) {
        auto subject = Button(left, kSubjects[i], 20 + i * 229, 20, 210, 64,
                              i == study_.subject() ? subject_colors[i] : 0xf5f3ee, 300 + i);
        if (i == study_.subject()) {
            lv_obj_set_style_border_width(subject, 3, 0);
            lv_obj_set_style_border_color(subject, lv_color_hex(0x7aaee0), 0);
        }
    }
    auto mode = Box(left, 248, 103, 228, 42, study_.running() ? 0xd9f4df : 0xffeedf);
    lv_obj_set_style_radius(mode, 21, 0);
    auto mode_text = Label(mode, study_.running() ? "正在专注" : "准备开始", 8, 3, 212);
    lv_obj_set_style_text_align(mode_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(mode_text, lv_color_hex(study_.running() ? 0x247a50 : 0xa85c2b), 0);
    timer_value_ = Label(left, "00:00:00", 24, 154, 676, &han_font_large);
    lv_obj_set_style_text_align(timer_value_, LV_TEXT_ALIGN_CENTER, 0);
    Label(left, kSubjects[study_.subject()], 294, 269, 140, &han_font_40);
    Button(left, study_.running() ? "暂停" : "开始 / 继续", 24, 324, 324, 78, kOrange, 310);
    Button(left, "完成本科", 372, 324, 328, 78, kGreen, 311);
    auto hint = Label(left, "计时会在后台继续，专心完成一科再切换", 46, 427, 640);
    lv_obj_set_style_text_color(hint, lv_color_hex(kMuted), 0);

    auto right = Card(body_, 748, 0, 484, 480, 0xedf9ef);
    auto timer_icon = Image(right, &han_icon_timer, 388, 13);
    lv_image_set_scale(timer_icon, 112);
    lv_image_set_pivot(timer_icon, 0, 0);
    Label(right, "本次作业记录", 24, 22, 430, &han_font_40);
    Label(right, "完成一科，就点亮一颗小星星", 24, 70, 430);
    for (int i = 0; i < 3; ++i) {
        auto row = Box(right, 20, 112 + i * 76, 444, 62, 0xffffff);
        lv_obj_set_style_radius(row, 18, 0);
        auto dot = Box(row, 14, 15, 32, 32, subject_colors[i]);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        totals_[i] = Label(row, "", 58, 13, 365);
    }
    Button(right, "新一轮作业", 20, 374, 444, 70, 0xffffff, 312);
    UpdateTimer();
}

void HanDisplay::UpdateTimer() {
    if (timer_value_)
        SetTextIfChanged(timer_value_, Duration(study_.Elapsed(study_.subject(), NowMs())).c_str());
    for (int i = 0; i < 3; ++i)
        if (totals_[i]) {
            auto label = std::string(kSubjects[i]) + "  " + Duration(study_.Elapsed(i, NowMs())) +
                         (study_.completed(i) ? " 已完成" : "");
            SetTextIfChanged(totals_[i], label.c_str());
        }
}

void HanDisplay::Alarm() {
    auto picker = Card(body_, 0, 0, 760, 480, 0xffffff);
    Label(picker, "设置提醒时间", 28, 22, 520, &han_font_40);
    auto repeat = Box(picker, 574, 24, 150, 44, kPurple);
    lv_obj_set_style_radius(repeat, 22, 0);
    auto repeat_text = Label(repeat, "每天", 8, 4, 134);
    lv_obj_set_style_text_align(repeat_text, LV_TEXT_ALIGN_CENTER, 0);
    Label(picker, "小时", 116, 98, 190);
    Label(picker, "分钟", 430, 98, 190);
    alarm_hour_ = lv_roller_create(picker);
    lv_roller_set_options(alarm_hour_,
                          "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n"
                          "18\n19\n20\n21\n22\n23",
                          LV_ROLLER_MODE_NORMAL);
    alarm_minute_ = lv_roller_create(picker);
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
    int x = 90;
    for (auto roller : {alarm_hour_, alarm_minute_}) {
        lv_obj_set_pos(roller, x, 145);
        lv_obj_set_width(roller, 240);
        lv_obj_set_style_text_font(roller, &han_font_40, 0);
        lv_obj_set_style_radius(roller, 20, 0);
        lv_obj_set_style_border_width(roller, 0, 0);
        lv_obj_set_style_bg_color(roller, lv_color_hex(0xf6f3ee), 0);
        lv_obj_set_style_bg_color(roller, lv_color_hex(0x8fc9fb), LV_PART_SELECTED);
        lv_obj_set_style_text_color(roller, lv_color_hex(kInk), LV_PART_SELECTED);
        lv_roller_set_visible_row_count(roller, 3);
        x += 314;
    }
    Label(picker, ":", 353, 227, 54, &han_font_40);
    auto hint = Label(picker, "上下滑动数字，选择每天提醒的时间", 120, 416, 520);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(kMuted), 0);

    auto summary = Card(body_, 784, 0, 448, 480, alarm_ringing_ ? 0xffe0e2 : 0xfff3d7);
    auto alarm_icon = Image(summary, &han_icon_alarm, 344, 16);
    lv_image_set_scale(alarm_icon, 120);
    lv_image_set_pivot(alarm_icon, 0, 0);
    auto status = Box(summary, 24, 24, 176, 44, alarm_enabled_ ? kGreen : 0xf1eee8);
    lv_obj_set_style_radius(status, 22, 0);
    auto status_text = Label(status,
                             alarm_ringing_   ? "正在响铃"
                             : alarm_enabled_ ? "提醒已开启"
                                              : "提醒未开启",
                             8, 4, 160);
    lv_obj_set_style_text_align(status_text, LV_TEXT_ALIGN_CENTER, 0);
    char time_text[16];
    snprintf(time_text, sizeof(time_text), "%02d:%02d", alarm_minutes_ / 60, alarm_minutes_ % 60);
    auto time = Label(summary, time_text, 24, 92, 400, &han_font_large);
    lv_obj_set_style_text_align(time, LV_TEXT_ALIGN_CENTER, 0);
    Label(summary, "每天到点提醒", 112, 214, 260, &han_font_40);
    Button(summary, alarm_enabled_ ? "保存并保持开启" : "保存并开启", 24, 272, 400, 72, kGreen,
           400);
    Button(summary, alarm_ringing_ ? "停止铃声" : "关闭提醒", 24, 365, 400, 72, kPink, 401);
}

void HanDisplay::Weather() {
    const auto weather = DecodeWeather(weather_text_);
    auto hero = Card(body_, 0, 0, 492, 480, 0xd9edfc);
    lv_obj_set_style_bg_grad_color(hero, lv_color_hex(0xf1f9ff), 0);
    lv_obj_set_style_bg_grad_dir(hero, LV_GRAD_DIR_VER, 0);
    if (!weather_art_data_.empty()) {
        auto art = Image(hero, &weather_art_dsc_, 20, 20);
        lv_obj_set_style_shadow_width(art, 18, 0);
        lv_obj_set_style_shadow_opa(art, LV_OPA_20, 0);
    } else {
        auto icon_circle = Box(hero, 28, 28, 144, 144, 0xffffff);
        lv_obj_set_style_radius(icon_circle, LV_RADIUS_CIRCLE, 0);
        auto img = Image(icon_circle, &han_icon_weather, 8, 8);
        lv_image_set_scale(img, 220);
        lv_image_set_pivot(img, 0, 0);
    }
    if (weather_text_.empty()) {
        Label(hero, "还没有天气", 196, 48, 260, &han_font_40);
        auto intro =
            Label(hero, "先把和风天气配置放进 SD 卡，\n再点右侧的刷新按钮。", 36, 212, 420);
        ApplyDynamicTextFont(intro);
        auto path = Label(hero, "SD:/handict/qweather.json", 36, 335, 420);
        lv_obj_set_style_text_color(path, lv_color_hex(0x3976a8), 0);
    } else {
        auto city = Label(hero, weather.city.c_str(), 226, 35, 230, &han_font_40);
        ApplyDynamicTextFont(city);
        if (weather.cached) {
            auto badge = Box(hero, 228, 86, 126, 38, 0xffe7bd);
            lv_obj_set_style_radius(badge, 19, 0);
            auto badge_text = Label(badge, "缓存天气", 6, 1, 114);
            lv_obj_set_style_text_align(badge_text, LV_TEXT_ALIGN_CENTER, 0);
        }
        const auto separator = weather.current.rfind(' ');
        const auto condition =
            separator == std::string::npos ? weather.current : weather.current.substr(0, separator);
        const auto temperature =
            separator == std::string::npos ? std::string() : weather.current.substr(separator + 1);
        auto condition_text = Label(hero, condition.c_str(), 226, 135, 230, &han_font_40);
        ApplyDynamicTextFont(condition_text);
        auto temperature_text = Label(hero, temperature.c_str(), 34, 219, 424, &han_font_large);
        lv_obj_set_style_text_align(temperature_text, LV_TEXT_ALIGN_LEFT, 0);
        auto feels_box = Box(hero, 28, 327, 436, 50, 0xffffff);
        auto wind_box = Box(hero, 28, 386, 436, 50, 0xffffff);
        auto feels = Label(feels_box, weather.feels.c_str(), 18, 5, 400);
        auto wind = Label(wind_box, weather.wind.c_str(), 18, 5, 400);
        ApplyDynamicTextFont(feels);
        ApplyDynamicTextFont(wind);
        if (!weather.updated.empty()) {
            auto update = Label(hero, ("更新于 " + weather.updated).c_str(), 34, 443, 420);
            lv_obj_set_style_text_color(update, lv_color_hex(kMuted), 0);
        }
    }

    auto guide = Card(body_, 516, 0, 716, 480, 0xffffff);
    Label(guide, "今天怎么准备？", 28, 24, 430, &han_font_40);
    const auto advice = WeatherAdvice(weather);
    auto advice_card = Box(guide, 28, 92, 660, 154, weather_text_.empty() ? 0xf4f1ec : kOrange);
    Label(advice_card, weather_text_.empty() ? "完成天气配置" : advice.first, 24, 21, 610,
          &han_font_40);
    auto advice_text =
        Label(advice_card, weather_text_.empty() ? "API KEY 只保存在你的 SD 卡中。" : advice.second,
              24, 78, 610);
    ApplyDynamicTextFont(advice_text);
    auto source = Box(guide, 28, 275, 386, 70, 0xf4f7fb);
    Label(source, "数据来源", 18, 8, 150);
    Label(source, "和风天气", 172, 8, 195);
    auto refresh = Button(guide, "刷新天气", 434, 275, 254, 70, kGreen, 900);
    ApplyDynamicTextFont(lv_obj_get_child(refresh, 0));
    auto note = Label(guide,
                      weather.cached ? "当前显示上次缓存，联网后可刷新。"
                                     : "天气变化很快，出门前可以再刷新一次。",
                      28, 382, 660);
    ApplyDynamicTextFont(note);
    lv_obj_set_style_text_color(note, lv_color_hex(kMuted), 0);
}

void HanDisplay::Network() {
    auto card = Card(body_, 0, 0, 1232, 480, 0xffffff);
    if (usb_storage_active_) {
        Label(card, "USB 读卡器已开启", 28, 22, 1120, &han_font_40);
        auto message =
            Label(card,
                  "电脑现在可以访问 microSD 卡。\n\n复制或格式化完成后，请先在电脑上安全弹出，"
                  "再重启设备。",
                  28, 118, 1130);
        ApplyDynamicTextFont(message);
        Label(card, "此模式下字典内容和语音唤醒暂停", 28, 415, 1160);
        network_info_ = nullptr;
        return;
    }
    Label(card, "网络与存储", 28, 22, 550, &han_font_40);
    Image(card, &han_status_wifi_3, 516, 22);
    auto connection = Box(card, 28, 82, 558, 150, 0xebf6ff);
    network_info_ = Label(connection, "正在读取网络状态…", 22, 18, 514);
    ApplyDynamicTextFont(network_info_);
    Button(card, "手机配网", 28, 256, 270, 70, kBlue, 10);
    Button(card, usb_storage_requested_ ? "正在切换…" : "USB 读卡器", 316, 256, 270, 70, kGreen,
           11);
    auto usb_hint = Label(card, "USB 模式用于给内容卡复制字典、音频和课表", 28, 365, 558);
    ApplyDynamicTextFont(usb_hint);
    lv_obj_set_style_text_color(usb_hint, lv_color_hex(kMuted), 0);

    Box(card, 614, 22, 2, 420, 0xe8e3dc);
    Label(card, "显示与声音", 650, 22, 530, &han_font_40);
    auto settings_right = Image(card, &han_icon_settings, 1080, 15);
    lv_image_set_scale(settings_right, 112);
    lv_image_set_pivot(settings_right, 0, 0);
    Label(card, "屏幕亮度", 650, 95, 180);
    Button(card, "-", 836, 80, 70, 62, kBlue, 910);
    brightness_value_ = Label(card, "", 920, 95, 145);
    lv_obj_set_style_text_align(brightness_value_, LV_TEXT_ALIGN_CENTER, 0);
    Button(card, "+", 1078, 80, 70, 62, kBlue, 911);
    auto brightness_track = Box(card, 836, 151, 312, 12, 0xe5e9ef);
    lv_obj_set_style_radius(brightness_track, 6, 0);
    brightness_bar_ = Box(brightness_track, 0, 0, 1, 12, 0x63aef1);
    lv_obj_set_style_radius(brightness_bar_, 6, 0);
    Label(card, "播放音量", 650, 210, 180);
    Button(card, "-", 836, 195, 70, 62, kGreen, 912);
    volume_value_ = Label(card, "", 920, 210, 145);
    lv_obj_set_style_text_align(volume_value_, LV_TEXT_ALIGN_CENTER, 0);
    Button(card, "+", 1078, 195, 70, 62, kGreen, 913);
    auto volume_track = Box(card, 836, 266, 312, 12, 0xe5e9ef);
    lv_obj_set_style_radius(volume_track, 6, 0);
    volume_bar_ = Box(volume_track, 0, 0, 1, 12, 0x55c892);
    lv_obj_set_style_radius(volume_bar_, 6, 0);
    Button(card, "立即关屏", 650, 320, 498, 70, kPurple, 914);
    auto wake_hint = Label(card, "关屏后，轻触屏幕任意位置即可唤醒", 650, 417, 550);
    ApplyDynamicTextFont(wake_hint);
    lv_obj_set_style_text_color(wake_hint, lv_color_hex(kMuted), 0);
    UpdateSettingLabels();
}

void HanDisplay::UpdateSettingLabels() {
    char value[16];
    if (brightness_value_) {
        snprintf(value, sizeof(value), "%d%%", brightness_setting_);
        SetTextIfChanged(brightness_value_, value);
    }
    if (brightness_bar_)
        lv_obj_set_width(brightness_bar_, 312 * brightness_setting_ / 100);
    if (volume_value_) {
        snprintf(value, sizeof(value), "%d%%", volume_setting_);
        SetTextIfChanged(volume_value_, value);
    }
    if (volume_bar_)
        lv_obj_set_width(volume_bar_, 312 * volume_setting_ / 100);
}

void HanDisplay::SetScreenOff(bool off) {
    if (screen_off_.load() == off)
        return;
    DisplayLockGuard guard(this);
    if (!setup_ui_called_ || root_ == nullptr)
        return;

    if (off) {
        screen_wake_overlay_ = Box(root_, 0, 0, 1280, 720, 0x000000);
        lv_obj_set_style_radius(screen_wake_overlay_, 0, 0);
        lv_obj_set_style_bg_opa(screen_wake_overlay_, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(screen_wake_overlay_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(screen_wake_overlay_, this);
        lv_obj_add_event_cb(screen_wake_overlay_, OnClick, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<intptr_t>(915)));
        screen_off_ = true;
#ifndef HAN_UI_HOST_SIM
        Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        Board::GetInstance().GetBacklight()->SetBrightness(0);
#endif
        return;
    }

    screen_off_ = false;
    if (screen_wake_overlay_ != nullptr) {
        lv_obj_delete(screen_wake_overlay_);
        screen_wake_overlay_ = nullptr;
    }
#ifndef HAN_UI_HOST_SIM
    Board::GetInstance().GetBacklight()->SetBrightness(brightness_setting_);
    const auto state = Application::GetInstance().GetDeviceState();
    Board::GetInstance().SetPowerSaveLevel(state == kDeviceStateIdle ? PowerSaveLevel::LOW_POWER
                                                                     : PowerSaveLevel::PERFORMANCE);
#endif
}

void HanDisplay::InstallScalableDictionaryFonts(const std::string& path) {
#ifndef HAN_UI_HOST_SIM
    ReleaseDictionaryFonts();
    DisplayLockGuard guard(this);
    auto body = lv_tiny_ttf_create_file(path.c_str(), 28);
    auto large = lv_tiny_ttf_create_file(path.c_str(), 40);
    auto hero = lv_tiny_ttf_create_file(path.c_str(), 64);
    if (body == nullptr || large == nullptr || hero == nullptr) {
        if (hero != nullptr)
            lv_tiny_ttf_destroy(hero);
        if (large != nullptr)
            lv_tiny_ttf_destroy(large);
        if (body != nullptr)
            lv_tiny_ttf_destroy(body);
        ESP_LOGW("HanDisplay", "Ignoring invalid SD scalable dictionary font: %s", path.c_str());
        return;
    }
    body->fallback = &han_font_28;
    large->fallback = &han_font_40;
    hero->fallback = &han_font_40;
    dictionary_font_ = body;
    dictionary_large_font_ = large;
    dictionary_hero_font_ = hero;
    dictionary_font_is_ttf_ = true;
    ESP_LOGI("HanDisplay", "SD scalable dictionary font loaded: %s", path.c_str());
    if (page_ == Page::Dictionary)
        Render(Page::Dictionary);
#else
    (void)path;
#endif
}

void HanDisplay::CloseBatteryPopup() {
    if (battery_popup_ != nullptr) {
        lv_obj_delete(battery_popup_);
        battery_popup_ = nullptr;
    }
}

void HanDisplay::ShowBatteryPopup() {
    CloseBatteryPopup();

    bool known = false;
#ifndef HAN_UI_HOST_SIM
    BatteryInfo info;
#else
    Board::BatteryInfo info;
#endif
    known = Board::GetInstance().GetBatteryInfo(info);
    if (known) {
        battery_level_ = info.level;
        battery_voltage_mv_ = info.voltage_mv;
        battery_current_ma_ = info.current_ma;
        battery_charging_ = info.charging;
        battery_discharging_ = info.discharging;
    }

    battery_popup_ = Box(root_, 0, 0, 1280, 720, 0x142b57);
    lv_obj_set_style_radius(battery_popup_, 0, 0);
    lv_obj_set_style_bg_opa(battery_popup_, LV_OPA_40, 0);
    lv_obj_add_flag(battery_popup_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(battery_popup_, this);
    lv_obj_add_event_cb(battery_popup_, OnClick, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(916)));

    auto panel = Card(battery_popup_, 720, 92, 500, 390, 0xffffff);
    lv_obj_set_style_radius(panel, 28, 0);
    Label(panel, "电池详情", 28, 22, 260, &han_font_40);
    auto close = Button(panel, "×", 406, 18, 64, 58, 0xf1f4f8, 916);
    lv_obj_set_style_radius(close, 20, 0);

    char text[96];
    if (known && battery_voltage_mv_ > 0) {
        const char* state = battery_charging_      ? "正在充电"
                            : battery_discharging_ ? "电池供电"
                                                   : "电流接近零";
        snprintf(text, sizeof(text), "%d%%  ·  %s", battery_level_, state);
        Label(panel, text, 30, 94, 440);

        snprintf(text, sizeof(text), "%.3f V", battery_voltage_mv_ / 1000.0);
        auto voltage = Box(panel, 28, 150, 210, 112, kBlue);
        Label(voltage, "电压", 18, 12, 174);
        Label(voltage, text, 18, 53, 174, &han_font_40);

        snprintf(text, sizeof(text), "%+d mA", battery_current_ma_);
        auto current = Box(panel, 260, 150, 210, 112, kGreen);
        Label(current, "电流", 18, 12, 174);
        Label(current, text, 12, 57, 186, &han_font_28);

        const double watts =
            static_cast<double>(battery_voltage_mv_) * battery_current_ma_ / 1000000.0;
        snprintf(text, sizeof(text), "瞬时功率：%+.2f W", watts);
        Label(panel, text, 30, 292, 440);
    } else {
        Label(panel, "暂时无法读取 INA226 电池数据，请稍后再试。", 30, 130, 430);
    }
    lv_obj_move_foreground(battery_popup_);
    lv_obj_invalidate(battery_popup_);
}

void HanDisplay::OnClick(lv_event_t* e) {
    auto self = static_cast<HanDisplay*>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
    self->Action(static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));
}

void HanDisplay::OnPinyinGesture(lv_event_t* event) {
    auto self = static_cast<HanDisplay*>(lv_event_get_user_data(event));
    auto indev = lv_event_get_indev(event);
    if (!self || !indev)
        return;
    const auto direction = lv_indev_get_gesture_dir(indev);
    if (direction == LV_DIR_LEFT)
        self->Action(1138);
    else if (direction == LV_DIR_RIGHT)
        self->Action(1137);
}

void HanDisplay::Action(int a) {
    if (screen_off_) {
        Application::GetInstance().Schedule([this] { SetScreenOff(false); });
        return;
    }
    if (usb_storage_active_)
        return;
    if (a == 502) {
        if (timetable_day_group_) {
            timetable_day_group_ = 0;
            timetable_week_ = 0;
        } else if (timetable_week_) {
            timetable_day_group_ = 1;
            timetable_week_ = 0;
        } else {
            timetable_week_ = 1;
        }
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
        if (a == 5)
            Queue(4, "");
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
    if (a == 12) {
        ShowBatteryPopup();
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
    if (a == 11 && usb_storage_action_) {
        auto& app = Application::GetInstance();
        ESP_LOGI("HanDisplay", "USB storage requested (state=%d, local_audio=%d)",
                 static_cast<int>(app.GetDeviceState()), static_cast<int>(local_audio_.load()));
        if (local_audio_ || (app.GetDeviceState() != kDeviceStateIdle &&
                             app.GetDeviceState() != kDeviceStateWifiConfiguring)) {
            Toast("请先结束当前语音或播放");
            return;
        }
        if (usb_storage_requested_.exchange(true))
            return;
        Render(Page::Network);
        if (!Queue(5, "")) {
            usb_storage_requested_ = false;
            Render(Page::Network);
        }
        return;
    }
    if (a == 900) {
        Toast("正在刷新天气…");
        Queue(4, "");
        return;
    }
    if (a >= 910 && a <= 913) {
        if (a == 910 || a == 911)
            brightness_setting_ = std::clamp(brightness_setting_ + (a == 910 ? -10 : 10), 10, 100);
        else
            volume_setting_ = std::clamp(volume_setting_ + (a == 912 ? -10 : 10), 10, 100);
        UpdateSettingLabels();
#ifndef HAN_UI_HOST_SIM
        const int brightness = brightness_setting_;
        const int volume = volume_setting_;
        Application::GetInstance().Schedule([a, brightness, volume] {
            auto& board = Board::GetInstance();
            if (a == 910 || a == 911)
                board.GetBacklight()->SetBrightness(brightness, true);
            else
                board.GetAudioCodec()->SetOutputVolume(volume);
        });
#endif
        return;
    }
    if (a == 914) {
        Application::GetInstance().Schedule([this] { SetScreenOff(true); });
        return;
    }
    if (a == 916) {
        CloseBatteryPopup();
        return;
    }
    if (a >= 1100 && a < 1126) {
        if (pinyin_query_.size() < 7) {
            pinyin_query_.push_back(static_cast<char>('a' + a - 1100));
            pinyin_search_key_.clear();
            pinyin_results_.clear();
            pinyin_page_ = 0;
            lv_label_set_text(search_input_, pinyin_query_.c_str());
            lv_obj_set_style_text_color(search_input_, lv_color_hex(kInk), 0);
            RenderPinyinResults("输入完成后点查找，或直接选择音调");
        }
        return;
    }
    if (a == 1126) {
        if (!pinyin_query_.empty())
            pinyin_query_.pop_back();
        pinyin_search_key_.clear();
        pinyin_results_.clear();
        pinyin_page_ = 0;
        lv_label_set_text(search_input_,
                          pinyin_query_.empty() ? "输入拼音，例如 han" : pinyin_query_.c_str());
        lv_obj_set_style_text_color(search_input_,
                                    lv_color_hex(pinyin_query_.empty() ? kMuted : kInk), 0);
        RenderPinyinResults("输入完成后点查找，或直接选择音调");
        return;
    }
    if (a == 1129) {
        pinyin_query_.clear();
        pinyin_search_key_.clear();
        pinyin_results_.clear();
        pinyin_page_ = 0;
        lv_label_set_text(search_input_, "输入拼音，例如 han");
        lv_obj_set_style_text_color(search_input_, lv_color_hex(kMuted), 0);
        RenderPinyinResults("输入拼音，可按音调缩小候选范围");
        return;
    }
    if (a == 1127) {
        StartPinyinSearch();
        return;
    }
    if (a >= 1130 && a <= 1135) {
        pinyin_tone_ = a - 1131;
        UpdatePinyinToneButtons();
        if (!pinyin_query_.empty())
            StartPinyinSearch();
        return;
    }
    if (a == 1128) {
        if (search_overlay_)
            lv_obj_delete(search_overlay_);
        search_overlay_ = search_input_ = search_results_ = search_status_ = nullptr;
        pinyin_page_label_ = nullptr;
        pinyin_tone_buttons_.fill(nullptr);
        pinyin_search_key_.clear();
        pinyin_status_text_.clear();
        pinyin_results_.clear();
        pinyin_page_ = 0;
        return;
    }
    if (a == 1136) {
        if (definition_overlay_)
            lv_obj_delete(definition_overlay_);
        definition_overlay_ = nullptr;
        return;
    }
    if (a == 1137 || a == 1138) {
        const int page_count = std::max(
            1, (static_cast<int>(pinyin_results_.size()) + kPinyinPageSize - 1) / kPinyinPageSize);
        pinyin_page_ = std::clamp(pinyin_page_ + (a == 1137 ? -1 : 1), 0, page_count - 1);
        RenderPinyinResults(nullptr);
        return;
    }
    if (a >= 1200 && a < 1200 + static_cast<int>(pinyin_results_.size())) {
        const auto character = pinyin_results_[a - 1200];
        Toast("正在读取汉字和笔顺…");
        Queue(0, character);
        return;
    }
    if (a == 20 || a == 22) {
        stroke_playing_ = false;
        const int stroke_count = static_cast<int>(entry_.strokes.size());
        stroke_ = std::clamp(stroke_ + (a == 20 ? -1 : 1), -1, stroke_count);
        UpdateStroke();
        return;
    }
    if (a == 21) {
        if (entry_.strokes.empty())
            return;
        if (stroke_playing_) {
            stroke_playing_ = false;
            return;
        }
        if (stroke_ >= static_cast<int>(entry_.strokes.size()) - 1)
            stroke_ = -1;
        stroke_playing_ = true;
        UpdateStroke();
        return;
    }
    if (a == 23) {
        OpenPinyinSearch();
        return;
    }
    if (a == 25) {
        OpenDefinitionDetails();
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
    self->UpdateTimer();
    if (self->study_.running() && NowMs() - self->last_checkpoint_ms_ >= 60000) {
        self->last_checkpoint_ms_ = NowMs();
        self->SaveTimer();
    }
    if (self->stroke_playing_) {
        const int stroke_count = static_cast<int>(self->entry_.strokes.size());
        if (stroke_count <= 0) {
            self->stroke_playing_ = false;
        } else if (self->stroke_ < stroke_count - 1) {
            ++self->stroke_;
        } else {
            // One final state after the last highlighted stroke: all strokes are ink blue and
            // no individual stroke card remains selected.
            self->stroke_ = stroke_count;
            self->stroke_playing_ = false;
        }
        self->UpdateStroke();
    }
}

void HanDisplay::ShowEntry(const han::Entry& entry) {
    DisplayLockGuard guard(this);
    entry_ = entry;
    stroke_ = -1;
    if (setup_ui_called_)
        Render(Page::Dictionary);
}

bool HanDisplay::OpenPage(const std::string& page) {
    if (usb_storage_active_ || usb_storage_requested_)
        return false;
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
#ifndef HAN_UI_HOST_SIM
    BatteryInfo info;
#else
    Board::BatteryInfo info;
#endif
    const bool known = Board::GetInstance().GetBatteryInfo(info);
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
    SetTextIfChanged(clock_, clock);
    SetTextIfChanged(date_, date);
    SetImageIfChanged(wifi_image_, !wifi.IsConnected() ? &han_status_wifi_off
                                   : rssi >= -65       ? &han_status_wifi_3
                                   : rssi >= -75       ? &han_status_wifi_2
                                                       : &han_status_wifi_1);
    const lv_image_dsc_t* battery = &han_status_battery_unknown;
    if (known && info.level >= 0 && info.level <= 100) {
        battery_level_ = info.level;
        battery_voltage_mv_ = info.voltage_mv;
        battery_current_ma_ = info.current_ma;
        battery_charging_ = info.charging;
        battery_discharging_ = info.discharging;
        battery = info.charging      ? &han_status_battery_charging
                  : info.level <= 5  ? &han_status_battery_empty
                  : info.level <= 20 ? &han_status_battery_low
                  : info.level <= 65 ? &han_status_battery_half
                                     : &han_status_battery_full;
    }
    SetImageIfChanged(battery_image_, battery);
    const int64_t date_key = valid_time ? static_cast<int64_t>(tm.tm_year) * 366 + tm.tm_yday : -1;
    if (date_key != timetable_date_key_) {
        timetable_date_key_ = date_key;
        timetable_today_ = valid_time ? (tm.tm_wday + 6) % 7 : -1;
        if (page_ == Page::Timetable)
            Render(Page::Timetable);
    }
    if (network_info_)
        SetTextIfChanged(network_info_, network.c_str());
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
    Settings display("display");
    brightness_setting_ = std::clamp(static_cast<int>(display.GetInt("brightness", 75)), 10, 100);
    Settings audio("audio");
    volume_setting_ = std::clamp(static_cast<int>(audio.GetInt("output_volume", 70)), 10, 100);
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

bool HanDisplay::Queue(int type, const std::string& value) {
    Job job{};
    job.type = type;
    if (value.size() >= sizeof(job.value)) {
        Toast("内容路径过长");
        return false;
    }
    memcpy(job.value, value.data(), value.size());
    if (!jobs_ || xQueueSend(jobs_, &job, 0) != pdTRUE) {
        Toast("正在处理，请稍后再试");
        return false;
    }
    return true;
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
            std::string data;
            if (store.Read("timetable.json", data, 8192)) {
                const bool valid = self->ApplyTimetable(data);
#ifndef HAN_UI_HOST_SIM
                ESP_LOGI("HanDisplay", "Standalone timetable: %u bytes, %s",
                         static_cast<unsigned>(data.size()), valid ? "loaded" : "invalid JSON");
#endif
            } else {
#ifndef HAN_UI_HOST_SIM
                ESP_LOGW("HanDisplay", "Standalone timetable not found or unreadable");
#endif
            }
            auto weather = han::QWeatherService::LoadCache(store);
            std::string art_id, art_data;
            if (!weather.empty()) {
                art_id = WeatherGraphicId(weather);
                store.Read("ui/graphics/" + art_id + "/192.png", art_data, 256 * 1024);
                if (art_data.empty() && art_id != "weather-unknown") {
                    art_id = "weather-unknown";
                    store.Read("ui/graphics/weather-unknown/192.png", art_data, 256 * 1024);
                }
            }
            DisplayLockGuard guard(self);
            self->weather_text_ = weather;
            self->ApplyWeatherArt(std::move(art_id), std::move(art_data));
            if (self->page_ == Page::Timetable || self->page_ == Page::Weather)
                self->Render(self->page_);
        } else if (job.type == 4) {
            std::string weather, error;
            if (!han::QWeatherService::Refresh(store, weather, error)) {
                if (weather.empty())
                    weather = han::QWeatherService::LoadCache(store);
                self->Toast(error.c_str());
            }
            std::string art_id, art_data;
            if (!weather.empty()) {
                art_id = WeatherGraphicId(weather);
                store.Read("ui/graphics/" + art_id + "/192.png", art_data, 256 * 1024);
                if (art_data.empty() && art_id != "weather-unknown") {
                    art_id = "weather-unknown";
                    store.Read("ui/graphics/weather-unknown/192.png", art_data, 256 * 1024);
                }
            }
            DisplayLockGuard guard(self);
            if (!weather.empty())
                self->weather_text_ = weather;
            self->ApplyWeatherArt(std::move(art_id), std::move(art_data));
            if (self->page_ == Page::Weather)
                self->Render(self->page_);
        } else if (job.type == 3) {
            han::StrokeGlyph glyph;
            if (store.ReadStrokeGlyph(job.value, glyph)) {
                auto pending =
                    new (std::nothrow) PendingStrokeGlyph{self, job.value, std::move(glyph)};
                if (!pending) {
                    self->Toast("笔顺资料内存不足");
                    continue;
                }
                // ThorVG needs considerably more stack than the SD worker owns. Queue all
                // vector drawing on LVGL's enlarged render task and leave this task as I/O only.
                DisplayLockGuard guard(self);
                if (lv_async_call(ApplyStrokeGlyphAsync, pending) != LV_RESULT_OK) {
                    delete pending;
                    self->Toast("笔顺绘制排队失败");
                }
            } else {
                auto pending = new (std::nothrow) PendingMissingStrokeGlyph{self, job.value};
                if (!pending)
                    continue;
                DisplayLockGuard guard(self);
                if (lv_async_call(ApplyMissingStrokeGlyphAsync, pending) != LV_RESULT_OK)
                    delete pending;
            }
        } else if (job.type == 6) {
            // Loading a complete OpenType font on the device can block LVGL long enough to trip
            // the task watchdog. Keep the proven CBIN path active until scalable glyph loading is
            // moved off the UI task and bounded per glyph.
            std::string data;
            if (store.ReadDictionaryFont(data))
                self->InstallDictionaryFont(std::move(data));
        } else if (job.type == 7) {
            std::vector<std::string> results;
            store.SearchPinyin(job.value, results, 1024);
            DisplayLockGuard guard(self);
            self->ApplyPinyinResults(job.value, std::move(results));
        } else if (job.type == 5) {
            auto& app = Application::GetInstance();
            app.GetAudioService().EnableWakeWordDetection(false);
            // Tiny TTF keeps the SD font file open. Close all three sizes before the card is
            // unmounted and handed to USB mass storage.
            self->ReleaseDictionaryFonts();
            const auto error = self->usb_storage_action_ ? self->usb_storage_action_()
                                                         : "当前设备不支持 USB 读卡器";
            if (!error.empty()) {
                self->usb_storage_requested_ = false;
                if (app.GetDeviceState() == kDeviceStateIdle)
                    app.GetAudioService().EnableWakeWordDetection(true);
                self->Toast(error.c_str());
            } else {
                self->usb_storage_active_ = true;
                self->usb_storage_requested_ = false;
            }
            DisplayLockGuard guard(self);
            self->Render(Page::Network);
        }
    }
}
