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
#ifdef HAN_UI_HOST_SIM
#include "assets/weather_icon_pack.h"
#endif
#include "assets/weather_page_assets.h"
#include "dictionary_service.h"
#include "phonetics.h"
#include "qweather_service.h"

namespace {
constexpr uint32_t kInk = 0x142b57, kBg = 0xfff9f0, kGreen = 0xd9f4df, kBlue = 0xd9edfc;
constexpr uint32_t kPurple = 0xe9dffc, kOrange = 0xffe8d6, kPink = 0xffdfe3;
constexpr uint32_t kMuted = 0x61708f, kCardBorder = 0xf1e8d9;
constexpr int kPinyinPageSize = 12;
constexpr int64_t kTimerMaximumMs = (99 * 60 + 59) * 1000LL;
// A full ring is one ordinary 45-minute focus period. This becomes a user setting later;
// the elapsed counter itself remains available up to 99:59.
constexpr int64_t kTimerRingMs = 45 * 60 * 1000LL;
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
lv_obj_t* Image(lv_obj_t* parent, const char* source, int x, int y) {
    auto image = lv_image_create(parent);
    lv_image_set_src(image, source);
    lv_obj_set_pos(image, x, y);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
    return image;
}
#ifndef HAN_UI_HOST_SIM
bool SdFileAvailable(const char* path) {
    auto file = fopen(path, "rb");
    if (file == nullptr)
        return false;
    fclose(file);
    return true;
}
#endif
int64_t NowMs() { return esp_timer_get_time() / 1000; }
std::string Duration(int64_t ms) {
    auto sec = std::clamp<int64_t>(ms / 1000, 0, 99 * 60 + 59);
    char out[40];
    snprintf(out, sizeof(out), "%02lld:%02lld", sec / 60, sec % 60);
    return out;
}

bool CurrentTimerWeek(int& anchor, int& weekday) {
    const auto now = time(nullptr);
    struct tm local{};
    localtime_r(&now, &local);
    if (local.tm_year < 125)
        return false;
    weekday = (local.tm_wday + 6) % 7;
    local.tm_mday -= weekday;
    local.tm_hour = 12;
    local.tm_min = 0;
    local.tm_sec = 0;
    mktime(&local);
    anchor = (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
    return true;
}

struct WeatherView {
    std::string city;
    std::string condition;
    std::string condition_code;
    int temperature = 1000;
    int feels_like = 1000;
    int humidity = -1;
    std::string wind;
    int wind_scale = -1;
    std::string updated;
    struct Day {
        std::string date;
        std::string condition;
        std::string condition_code;
        int temperature_min = 1000;
        int temperature_max = 1000;
        int precipitation_probability = -1;
        std::string sunrise;
        std::string sunset;
    };
    std::vector<Day> days;
    std::string air_category;
    std::string air_aqi;
    std::string index_name;
    std::string index_category;
    std::string index_text;
    bool cached = false;
};

WeatherView DecodeWeather(const std::string& text) {
    WeatherView view;
    if (!text.empty() && text.front() == '{') {
        auto root = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>(
            cJSON_ParseWithLength(text.data(), text.size()), cJSON_Delete);
        auto string_value = [](cJSON* object, const char* key) -> std::string {
            auto value = object ? cJSON_GetObjectItemCaseSensitive(object, key) : nullptr;
            return cJSON_IsString(value) && value->valuestring ? value->valuestring : "";
        };
        auto number_value = [](cJSON* object, const char* key, int fallback) {
            auto value = object ? cJSON_GetObjectItemCaseSensitive(object, key) : nullptr;
            return cJSON_IsNumber(value) ? value->valueint : fallback;
        };
        auto object_value = [](cJSON* object, const char* key) {
            auto value = object ? cJSON_GetObjectItemCaseSensitive(object, key) : nullptr;
            return cJSON_IsObject(value) ? value : nullptr;
        };
        if (root) {
            view.city = string_value(root.get(), "city");
            view.updated = string_value(root.get(), "updated_at");
            view.cached = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root.get(), "cached"));
            auto current = object_value(root.get(), "current");
            view.condition = string_value(current, "condition");
            view.condition_code = string_value(current, "condition_code");
            view.temperature = number_value(current, "temperature", 1000);
            view.feels_like = number_value(current, "feels_like", 1000);
            view.humidity = number_value(current, "humidity", -1);
            view.wind = string_value(current, "wind");
            view.wind_scale = number_value(current, "wind_scale", -1);
            auto days = cJSON_GetObjectItemCaseSensitive(root.get(), "days");
            if (cJSON_IsArray(days)) {
                const int count = std::min(4, cJSON_GetArraySize(days));
                for (int index = 0; index < count; ++index) {
                    auto item = cJSON_GetArrayItem(days, index);
                    WeatherView::Day day;
                    day.date = string_value(item, "date");
                    day.condition = string_value(item, "condition");
                    day.condition_code = string_value(item, "condition_code");
                    day.temperature_min = number_value(item, "temperature_min", 1000);
                    day.temperature_max = number_value(item, "temperature_max", 1000);
                    day.precipitation_probability =
                        number_value(item, "precipitation_probability", -1);
                    day.sunrise = string_value(item, "sunrise");
                    day.sunset = string_value(item, "sunset");
                    view.days.push_back(std::move(day));
                }
            }
            auto air = object_value(root.get(), "air");
            view.air_category = string_value(air, "category");
            view.air_aqi = string_value(air, "aqi");
            auto index = object_value(root.get(), "index");
            view.index_name = string_value(index, "name");
            view.index_category = string_value(index, "category");
            view.index_text = string_value(index, "text");
            if (!view.city.empty() && !view.condition.empty())
                return view;
        }
    }

    // Read the previous cache shape too, so a firmware update still has weather before its first
    // successful refresh.
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    if (!lines.empty()) {
        view.city = lines[0];
        const std::string marker = "（缓存）";
        const auto cached = view.city.find(marker);
        if (cached != std::string::npos) {
            view.city.erase(cached);
            view.cached = true;
        }
    }
    if (lines.size() > 1) {
        const auto separator = lines[1].rfind(' ');
        view.condition = separator == std::string::npos ? lines[1] : lines[1].substr(0, separator);
        if (separator != std::string::npos)
            view.temperature = std::strtol(lines[1].c_str() + separator + 1, nullptr, 10);
    }
    if (lines.size() > 2) {
        const auto start = lines[2].find("体感 ");
        if (start != std::string::npos)
            view.feels_like = std::strtol(lines[2].c_str() + start + strlen("体感 "), nullptr, 10);
        const auto humidity = lines[2].find("湿度 ");
        if (humidity != std::string::npos)
            view.humidity = std::strtol(lines[2].c_str() + humidity + strlen("湿度 "), nullptr, 10);
    }
    if (lines.size() > 3)
        view.wind = lines[3];
    for (const auto& line : lines) {
        constexpr const char* prefix = "更新时间：";
        if (line.rfind(prefix, 0) == 0)
            view.updated = line.substr(strlen(prefix));
    }
    return view;
}

std::string WeatherConditionId(const std::string& text, const std::string& code) {
    char* end = nullptr;
    const long value = code.empty() ? -1 : std::strtol(code.c_str(), &end, 10);
    if (!code.empty() && end != code.c_str() && *end == '\0') {
        if (value == 100 || value == 150 || value == 900)
            return "sunny";
        if ((value >= 101 && value <= 103) || (value >= 151 && value <= 153))
            return "partly-cloudy";
        if (value == 104)
            return "cloudy";
        if ((value >= 302 && value <= 304) || value == 350 || value == 351)
            return "thunderstorm";
        if (value >= 300 && value <= 399)
            return "rain";
        if (value >= 400 && value <= 499)
            return "snow";
        if (value >= 500 && value <= 515)
            return "fog";
        if (value == 901)
            return "cloudy";
    }
    if (text.find("雷") != std::string::npos || text.find("冰雹") != std::string::npos)
        return "thunderstorm";
    if (text.find("雨") != std::string::npos)
        return "rain";
    if (text.find("雪") != std::string::npos)
        return "snow";
    if (text.find("雾") != std::string::npos || text.find("霾") != std::string::npos ||
        text.find("沙") != std::string::npos || text.find("尘") != std::string::npos)
        return "fog";
    if (text.find("少云") != std::string::npos || text.find("晴间多云") != std::string::npos ||
        text.find("多云") != std::string::npos)
        return "partly-cloudy";
    if (text.find("阴") != std::string::npos)
        return "cloudy";
    if (text.find("晴") != std::string::npos)
        return "sunny";
    if (text.find("风") != std::string::npos)
        return "wind";
    return "cloudy";
}

#ifdef HAN_UI_HOST_SIM
const char* LegacyWeatherGraphicId(const std::string& id) {
    if (id == "sunny")
        return "weather-clear-day";
    if (id == "partly-cloudy")
        return "weather-partly-cloudy-day";
    if (id == "rain")
        return "weather-rain";
    if (id == "thunderstorm")
        return "weather-thunderstorm";
    if (id == "snow")
        return "weather-snow";
    if (id == "fog")
        return "weather-fog";
    if (id == "wind")
        return "weather-wind";
    return "weather-overcast";
}
#endif
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
    auto indev = static_cast<lv_indev_t*>(lvgl_port_add_touch(&cfg));
    ESP_ERROR_CHECK(indev ? ESP_OK : ESP_FAIL);
    // Waking a dark screen is intentionally different from normal UI interaction.  A deliberate
    // hold prevents a stray touch in a school bag from lighting and unlocking the display.
    lv_indev_set_long_press_time(indev, 800);
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
    if (page_ == Page::Dictionary || page_ == Page::Weather)
        Render(page_);
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
    back_ = Button(root_, "<", 24, 20, 72, 72, kGreen, 6);
    lv_obj_set_style_text_opa(lv_obj_get_child(back_, 0), LV_OPA_TRANSP, 0);
    back_image_ = Image(back_, &han_timetable_back, 12, 12);
    title_ = Label(root_, "小小助手", 212, 24, 470, &han_font_brand);
    date_ = Label(root_, "日期待同步", 747, 37, 208);
    clock_ = Label(root_, "—:—", 970, 41, 132, &han_font_clock);
    top_divider_left_ = Box(root_, 952, 36, 1, 40, 0xd7d5d0);
    top_divider_right_ = Box(root_, 1101, 36, 1, 40, 0xd7d5d0);
    wifi_button_ = Button(root_, "", 1115, 20, 72, 72, kBg, 7);
    lv_obj_set_style_bg_opa(wifi_button_, LV_OPA_TRANSP, 0);
    wifi_image_ = Image(wifi_button_, &han_status_wifi_off, 9, 9);
    lv_image_set_scale(wifi_image_, 288);
    lv_image_set_pivot(wifi_image_, 0, 0);
    battery_button_ = Button(root_, "", 1183, 20, 72, 72, kBg, 12);
    lv_obj_set_style_bg_opa(battery_button_, LV_OPA_TRANSP, 0);
    battery_image_ = Image(battery_button_, &han_status_battery_unknown, 3, 3);
    // The battery artwork is intentionally wider and shorter than the Wi-Fi mark. Scale it a
    // little more so their visible strokes, rather than their transparent 48 px canvases, carry
    // the same visual weight in the shared header.
    lv_image_set_scale(battery_image_, 352);
    lv_image_set_pivot(battery_image_, 0, 0);
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
    lv_obj_set_pos(page_icon_, 112, 27);
    lv_image_set_scale(page_icon_, 112);
    lv_image_set_pivot(page_icon_, 0, 0);
    lv_obj_add_flag(page_icon_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(page_icon_, LV_OBJ_FLAG_CLICKABLE);
    tick_ = lv_timer_create(Tick, 800, this);
    // This timer is re-aligned to the next monotonic second boundary after every callback.
    timer_tick_ = lv_timer_create(TimerTick, 1000, this);
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
    CloseWeatherIndexPopup();
    stroke_playing_ = false;
    timer_value_ = timer_progress_ = timer_today_value_ = nullptr;
    timer_last_rendered_second_ = -1;
    timer_week_bars_.fill(nullptr);
    stroke_value_ = stroke_image_ = network_info_ = search_ = nullptr;
    glyph_title_image_ = glyph_title_placeholder_ = nullptr;
    search_overlay_ = search_input_ = search_results_ = search_status_ = nullptr;
    pinyin_page_label_ = nullptr;
    definition_overlay_ = nullptr;
    pinyin_tone_buttons_.fill(nullptr);
    brightness_value_ = volume_value_ = auto_lock_value_ = nullptr;
    brightness_slider_ = volume_slider_ = auto_lock_slider_ = nullptr;
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
    lv_obj_set_pos(back_, 24, 20);
    lv_obj_set_size(back_, 72, 72);
    lv_obj_set_style_radius(back_, 24, 0);
    lv_obj_set_pos(back_image_, 12, 12);
    lv_obj_set_pos(date_, 747, 37);
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
        lv_obj_set_y(title_, 24);
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
        lv_obj_set_y(title_, 24);
        lv_obj_set_pos(body_, 24, 104);
        lv_obj_set_size(body_, 1232, 490);
        if (page == Page::Dictionary || page == Page::Phonetics || page == Page::Timer ||
            page == Page::Alarm || page == Page::Network) {
            lv_obj_add_flag(footer_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(assistant_card_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(body_, 1232, 592);
        } else if (page == Page::Weather) {
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
    constexpr uint32_t tab_colors[] = {0xe5d9ff, 0xdceeff, 0xe0f6e5};
    for (int i = 0; i < 3; ++i) {
        auto tab = Button(body_, cats[i], i * 418, 0, 396, 66,
                          i == category_ ? 0x9a78ee : tab_colors[i], 100 + i);
        auto tab_text = lv_obj_get_child(tab, 0);
        lv_obj_set_style_text_font(tab_text, &han_font_phonetics, 0);
        if (i == category_) {
            lv_obj_set_style_border_width(tab, 3, 0);
            lv_obj_set_style_border_color(tab, lv_color_hex(0x8d6dd2), 0);
            lv_obj_set_style_text_color(tab_text, lv_color_hex(0xffffff), 0);
        }
    }
    auto panel = Card(body_, 0, 86, 465, 506, 0xf0eaff);
    int shown = 0;
    for (int i = 0; i < static_cast<int>(std::size(han::kSounds)); ++i) {
        if (han::kSounds[i].category != category_)
            continue;
        const int offset = shown++;
        if (offset < sound_page_ * 6 || offset >= (sound_page_ + 1) * 6)
            continue;
        const int cell = offset % 6;
        auto btn = Button(panel, han::kSounds[i].ipa, 14 + cell % 3 * 148, 18 + cell / 3 * 135, 137,
                          122, i == sound_ ? 0xd7c7ff : 0xffffff, 200 + i);
        lv_obj_set_style_text_font(lv_obj_get_child(btn, 0), &han_font_phonetics_ipa, 0);
        if (i == sound_) {
            lv_obj_set_style_border_width(btn, 3, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x8e6ce4), 0);
        }
    }
    auto previous = Button(panel, "上一页", 14, 355, 137, 72, 0xffffff, 120);
    lv_obj_set_style_text_font(lv_obj_get_child(previous, 0), &han_font_phonetics, 0);
    const auto page = std::to_string(sound_page_ + 1) + " / " + std::to_string((shown + 5) / 6);
    auto page_label = Label(panel, page.c_str(), 168, 373, 130, &han_font_phonetics);
    lv_obj_set_style_text_align(page_label, LV_TEXT_ALIGN_CENTER, 0);
    auto next = Button(panel, "下一页", 310, 355, 137, 72, 0xded1ff, 121);
    lv_obj_set_style_text_font(lv_obj_get_child(next, 0), &han_font_phonetics, 0);

    auto detail = Card(body_, 490, 86, 742, 506, 0xffffff);
    auto& sound = han::kSounds[sound_];
    auto ipa = "/" + std::string(sound.ipa) + "/";
    auto big = Label(detail, ipa.c_str(), 20, 20, 700, &han_font_phonetics_ipa);
    lv_obj_set_style_text_align(big, LV_TEXT_ALIGN_CENTER, 0);
    auto play = Button(detail, "▶  听示范", 142, 91, 458, 78, 0xa282f2, 110);
    lv_obj_set_style_text_font(lv_obj_get_child(play, 0), &han_font_phonetics, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(play, 0), lv_color_hex(0xffffff), 0);

    constexpr uint32_t word_colors[] = {0xfff3cc, 0xe8f7df, 0xe1f2ff};
    for (int i = 0; i < 3; ++i) {
        auto word_card =
            Button(detail, sound.words[i], 20 + i * 237, 190, 220, 282, word_colors[i], 111 + i);
        auto word_label = lv_obj_get_child(word_card, 0);
        lv_obj_set_width(word_label, 204);
        lv_obj_set_style_text_font(word_label, &han_font_phonetics, 0);
        lv_obj_set_style_text_align(word_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(word_label, LV_ALIGN_TOP_MID, 0, 225);
        const auto icon_path = "S:/sdcard/handict/ui/graphics/phonetics-page/words/" +
                               std::string(sound.words[i]) + ".png";
        Image(word_card, icon_path.c_str(), 39, 53);
        auto play_badge = Box(word_card, 168, 18, 38, 38, 0xf2ecff);
        lv_obj_set_style_radius(play_badge, LV_RADIUS_CIRCLE, 0);
        auto play_icon = Label(play_badge, "▶", 5, 2, 28, &han_font_phonetics);
        lv_obj_set_style_text_align(play_icon, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(play_icon, lv_color_hex(0x8062df), 0);
    }
}

void HanDisplay::Timetable() {
    // This page deliberately owns the full 1280x720 canvas. Its proportions follow the approved
    // timetable concept rather than the denser shared application chrome.
    Image(body_, &han_timetable_mascot, 1007, 0);

    auto date_card = Card(body_, 466, 34, 340, 69, 0xffffff);
    lv_obj_set_style_radius(date_card, 35, 0);
    Image(date_card, &han_timetable_calendar, 20, 10);

    auto table = Card(body_, 17, 126, 1246, 560, 0xffffff);
    constexpr int first_day = 0;
    constexpr int columns = 5;
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
        const bool today = day == timetable_today_;
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

    // The regular Monday-to-Friday schedule always fits on one page.
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

void HanDisplay::Timer() {
    SyncTimerWeek();
    auto left = Card(body_, 0, 0, 694, 592, 0xffffff);
    lv_obj_set_style_radius(left, 30, 0);
    const uint32_t subject_colors[] = {0xe3f7ea, 0xddeeff, 0xeee5ff};
    const lv_image_dsc_t* subject_icons[] = {&han_subject_book, &han_subject_calculator,
                                             &han_subject_english};
    for (int i = 0; i < 3; ++i) {
        const bool selected = i == study_.subject();
        auto subject = Button(left, kSubjects[i], 18 + i * 226, 18, 206, 78,
                              selected ? 0x4a9df7 : subject_colors[i], 300 + i);
        lv_obj_set_style_radius(subject, 25, 0);
        lv_obj_set_style_border_width(subject, selected ? 4 : 0, 0);
        lv_obj_set_style_border_color(subject, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_outline_width(subject, selected ? 3 : 0, 0);
        lv_obj_set_style_outline_color(subject, lv_color_hex(0xb9ddff), 0);
        if (selected) {
            lv_obj_set_style_bg_grad_color(subject, lv_color_hex(0x3389ed), 0);
            lv_obj_set_style_bg_grad_dir(subject, LV_GRAD_DIR_HOR, 0);
        }
        // Centre the icon and two-character label as one 124 px-wide unit. The previous wide
        // label box centred only the text and left the combined content visibly shifted left.
        auto icon = Image(subject, subject_icons[i], 41, 18);
        auto subject_label = lv_obj_get_child(subject, 0);
        lv_obj_set_align(subject_label, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(subject_label, 93, 22);
        lv_obj_set_size(subject_label, 72, 35);
        lv_obj_set_style_text_font(subject_label, &han_font_timer, 0);
        lv_obj_set_style_text_color(subject_label, lv_color_hex(selected ? 0xffffff : kInk), 0);
        lv_image_set_scale(icon, 220);
        lv_image_set_pivot(icon, 0, 0);
    }

    timer_progress_ = lv_arc_create(left);
    lv_obj_set_pos(timer_progress_, 179, 112);
    lv_obj_set_size(timer_progress_, 336, 336);
    lv_arc_set_rotation(timer_progress_, 270);
    lv_arc_set_bg_angles(timer_progress_, 0, 360);
    lv_arc_set_range(timer_progress_, 0, static_cast<int>(kTimerRingMs / 1000));
    lv_obj_set_style_arc_width(timer_progress_, 24, LV_PART_MAIN);
    lv_obj_set_style_arc_color(timer_progress_, lv_color_hex(0xe9edf2), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(timer_progress_, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(timer_progress_, 24, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(timer_progress_, lv_color_hex(0x3f95f3), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(timer_progress_, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(timer_progress_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_remove_flag(timer_progress_, LV_OBJ_FLAG_CLICKABLE);

    timer_value_ = Label(left, "00:00", 199, 250, 296, &han_font_weather_hero);
    // The 60 px text box has the same y=280 centre as the 336 px ring. Keeping an even box
    // height avoids the half-pixel bias that an odd font line height creates.
    lv_obj_set_height(timer_value_, 60);
    lv_obj_set_style_text_align(timer_value_, LV_TEXT_ALIGN_CENTER, 0);

#ifndef HAN_UI_HOST_SIM
    auto homework_art =
        Image(left, "S:/sdcard/handict/ui/graphics/timer-page/homework-boy.png", 3, 283);
    // 345 px × 210 / 256 = 283 px, so the artwork ends at y=566 exactly with the controls.
    lv_image_set_scale(homework_art, 210);
    lv_image_set_pivot(homework_art, 0, 0);
#endif

    auto start =
        Button(left, study_.running() ? "暂停" : "开始计时", 258, 484, 230, 82, 0xff7969, 310);
    lv_obj_set_style_radius(start, 41, 0);
    lv_obj_set_style_bg_grad_color(start, lv_color_hex(0xff9b87), 0);
    lv_obj_set_style_bg_grad_dir(start, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(start, 0), &han_font_timer_title, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(start, 0), lv_color_hex(0xffffff), 0);
    auto complete = Button(left, "✓  完成本科", 498, 484, 178, 82, 0xffffff, 311);
    lv_obj_set_style_radius(complete, 41, 0);
    lv_obj_set_style_border_width(complete, 2, 0);
    lv_obj_set_style_border_color(complete, lv_color_hex(0xe3e0d9), 0);
    lv_obj_set_style_text_font(lv_obj_get_child(complete, 0), &han_font_timer, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(complete, 0), lv_color_hex(0x167b50), 0);

    auto homework = Card(body_, 714, 0, 518, 330, 0xffffff);
    lv_obj_set_style_radius(homework, 30, 0);
    const bool viewing_today = timer_view_day_ == timer_today_index_;
    const std::string homework_title =
        viewing_today ? "今日作业" : std::string(kWeekdays[timer_view_day_]) + "作业";
    Label(homework, homework_title.c_str(), 24, 18, 470, &han_font_timer_title);
    const auto viewed_seconds = TimerDaySeconds(timer_view_day_, NowMs());
    for (int i = 0; i < 3; ++i) {
        auto row = Box(homework, 18, 76 + i * 80, 482, 68, 0xfffdfa);
        lv_obj_set_style_radius(row, 20, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0xf1eadf), 0);
        auto icon = Image(row, subject_icons[i], 12, 10);
        lv_image_set_scale(icon, 224);
        lv_image_set_pivot(icon, 0, 0);
        Label(row, kSubjects[i], 72, 18, 110, &han_font_timer);
        totals_[i] = Label(row, "", 185, 18, 155, &han_font_timer);
        lv_obj_set_style_text_align(totals_[i], LV_TEXT_ALIGN_CENTER, 0);
        const bool active = viewing_today && !study_.completed(i) &&
                            ((study_.running() && i == study_.subject()) || viewed_seconds[i] > 0);
        const bool historical_record = !viewing_today && viewed_seconds[i] > 0;
        auto state = Box(row, 354, 10, 116, 48,
                         viewing_today && study_.completed(i) ? 0xe3f7ea
                         : active                             ? 0xdcecff
                         : historical_record                  ? 0xe9f7ef
                                                              : 0xf4f2f4);
        lv_obj_set_style_radius(state, 24, 0);
        auto state_text = Label(state,
                                viewing_today && study_.completed(i) ? "已完成"
                                : active                             ? "进行中"
                                : historical_record                  ? "有记录"
                                                    : (viewing_today ? "未开始" : "未记录"),
                                4, 8, 108, &han_font_timer);
        lv_obj_set_style_text_align(state_text, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(state_text,
                                    lv_color_hex(viewing_today && study_.completed(i) ? 0x27865c
                                                 : active                             ? 0x2573cd
                                                 : historical_record                  ? 0x27865c
                                                                                      : 0x75809a),
                                    0);
    }

    auto week = Card(body_, 714, 348, 518, 244, 0xffffff);
    lv_obj_set_style_radius(week, 30, 0);
    Label(week, "本周用时", 24, 18, 220, &han_font_timer_title);
    auto today_chip = Box(week, 305, 16, 193, 54, 0xf0f6ff);
    lv_obj_set_style_radius(today_chip, 27, 0);
    timer_today_value_ = Label(today_chip, "", 8, 10, 177, &han_font_timer);
    lv_obj_set_style_text_align(timer_today_value_, LV_TEXT_ALIGN_CENTER, 0);
    Box(week, 34, 191, 450, 2, 0xd9e0ea);
    static const char* day_labels[] = {"一", "二", "三", "四", "五"};
    for (int i = 0; i < 5; ++i) {
        const bool selected_day = i == timer_view_day_;
        auto day_button =
            Button(week, "", 24 + i * 94, 76, 86, 156, selected_day ? 0xecf8f1 : 0xffffff, 320 + i);
        lv_obj_set_style_radius(day_button, 22, 0);
        lv_obj_set_style_bg_opa(day_button, selected_day ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        timer_week_bars_[i] =
            Box(day_button, 26, 105, 34, 10, i == timer_today_index_ ? 0x5dc991 : 0x70aff2);
        lv_obj_set_style_radius(timer_week_bars_[i], 8, 0);
        auto day = Label(day_button, day_labels[i], 15, 122, 56, &han_font_timer);
        lv_obj_set_style_text_align(day, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(
            day, lv_color_hex(selected_day || i == timer_today_index_ ? 0x247a50 : kInk), 0);
    }
    UpdateTimer();
}

void HanDisplay::UpdateTimer() {
    const auto now_ms = NowMs();
    const int64_t current_elapsed = study_.Elapsed(study_.subject(), now_ms);
    const int64_t current_second = std::clamp<int64_t>(current_elapsed / 1000, 0, 99 * 60 + 59);
    if (timer_last_rendered_second_ == current_second)
        return;
    timer_last_rendered_second_ = current_second;
    if (timer_value_)
        SetTextIfChanged(timer_value_, Duration(current_elapsed).c_str());
    if (timer_progress_)
        lv_arc_set_value(timer_progress_,
                         static_cast<int>(std::min(current_elapsed, kTimerRingMs) / 1000));
    const auto viewed_seconds = TimerDaySeconds(timer_view_day_, now_ms);
    for (int i = 0; i < 3; ++i)
        if (totals_[i]) {
            const auto seconds = viewed_seconds[i];
            const auto minutes = seconds <= 0 ? 0 : std::max<int64_t>(1, seconds / 60);
            const auto label = minutes > 0 ? std::to_string(minutes) + "分钟" : "—";
            SetTextIfChanged(totals_[i], label.c_str());
        }

    std::array<int64_t, 5> week_seconds{};
    for (int day = 0; day < 5; ++day) {
        const auto subject_seconds = TimerDaySeconds(day, now_ms);
        for (const auto seconds : subject_seconds)
            week_seconds[day] += seconds;
    }
    const auto max_seconds =
        std::max<int64_t>(60, *std::max_element(week_seconds.begin(), week_seconds.end()));
    for (int i = 0; i < 5; ++i) {
        if (!timer_week_bars_[i])
            continue;
        const int height =
            week_seconds[i] > 0 ? 10 + static_cast<int>(week_seconds[i] * 72 / max_seconds) : 6;
        lv_obj_set_y(timer_week_bars_[i], 115 - height);
        lv_obj_set_height(timer_week_bars_[i], height);
    }
    if (timer_today_value_) {
        const auto selected_total =
            timer_view_day_ >= 0 && timer_view_day_ < 5 ? week_seconds[timer_view_day_] : 0;
        const auto day_name =
            timer_view_day_ == timer_today_index_
                ? "今天"
                : (timer_view_day_ >= 0 && timer_view_day_ < 5 ? kWeekdays[timer_view_day_]
                                                               : "本日");
        const auto text =
            std::string(day_name) + "共 " + std::to_string(selected_total / 60) + " 分钟";
        SetTextIfChanged(timer_today_value_, text.c_str());
    }
}

void HanDisplay::Alarm() {
    auto panel = Card(body_, 0, 0, 1232, 592, 0xffffff);
    lv_obj_set_style_radius(panel, 30, 0);

    // Keep a small embedded fallback behind the SD illustration. On the device the transparent
    // high-resolution artwork covers it; a missing SD asset still leaves the page usable.
    auto fallback_art = Image(panel, &han_art_alarm, 49, 52);
    lv_image_set_scale(fallback_art, 500);
    lv_image_set_pivot(fallback_art, 0, 0);
#ifndef HAN_UI_HOST_SIM
    auto alarm_art =
        Image(panel, "S:/sdcard/handict/ui/graphics/alarm-page/alarm-sunrise.png", 17, 20);
    lv_image_set_scale(alarm_art, 210);
    lv_image_set_pivot(alarm_art, 0, 0);
#endif

    auto status = Box(panel, 68, 476, 320, 58, 0xffffff);
    lv_obj_set_style_radius(status, 29, 0);
    lv_obj_set_style_border_width(status, 2, 0);
    lv_obj_set_style_border_color(status, lv_color_hex(alarm_enabled_ ? 0xb6e9c9 : 0xdce3ea), 0);
    lv_obj_set_style_shadow_color(status, lv_color_hex(alarm_enabled_ ? 0xa2dfb7 : 0xcbd3dc), 0);
    lv_obj_set_style_shadow_width(status, 10, 0);
    lv_obj_set_style_shadow_opa(status, LV_OPA_30, 0);
    auto status_icon = Box(status, 15, 10, 38, 38, alarm_enabled_ ? 0x28c978 : 0xb3bdca);
    lv_obj_set_style_radius(status_icon, LV_RADIUS_CIRCLE, 0);
    auto check = Label(status_icon, alarm_enabled_ ? "✓" : "—", 2, 0, 34, &han_font_weather);
    lv_obj_set_style_text_align(check, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(check, lv_color_hex(0xffffff), 0);
    auto status_text = Label(status,
                             alarm_ringing_   ? "正在响铃"
                             : alarm_enabled_ ? "闹钟已开启"
                                              : "闹钟未开启",
                             61, 9, 240, &han_font_weather);
    lv_obj_set_style_text_align(status_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_text, lv_color_hex(alarm_enabled_ ? 0x128349 : 0x77849a), 0);

    alarm_hour_ = lv_roller_create(panel);
    lv_roller_set_options(alarm_hour_,
                          "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n"
                          "18\n19\n20\n21\n22\n23",
                          LV_ROLLER_MODE_INFINITE);
    alarm_minute_ = lv_roller_create(panel);
    std::string minutes;
    for (int i = 0; i < 60; ++i) {
        char s[5];
        snprintf(s, sizeof(s), "%02d", i);
        if (i)
            minutes += '\n';
        minutes += s;
    }
    lv_roller_set_options(alarm_minute_, minutes.c_str(), LV_ROLLER_MODE_INFINITE);
    lv_roller_set_selected(alarm_hour_, alarm_minutes_ / 60, LV_ANIM_OFF);
    lv_roller_set_selected(alarm_minute_, alarm_minutes_ % 60, LV_ANIM_OFF);
    int x = 492;
    for (auto roller : {alarm_hour_, alarm_minute_}) {
        lv_obj_set_pos(roller, x, 46);
        lv_obj_set_width(roller, 270);
        lv_obj_set_style_text_font(roller, &han_font_weather_hero, 0);
        lv_obj_set_style_text_font(roller, &han_font_weather_hero, LV_PART_SELECTED);
        lv_obj_set_style_text_line_space(roller, 15, 0);
        lv_obj_set_style_text_line_space(roller, 15, LV_PART_SELECTED);
        lv_obj_set_style_text_color(roller, lv_color_hex(0x8c9ab2), 0);
        lv_obj_set_style_radius(roller, 26, 0);
        lv_obj_set_style_border_width(roller, 2, 0);
        lv_obj_set_style_border_color(roller, lv_color_hex(0xc9def1), 0);
        lv_obj_set_style_bg_color(roller, lv_color_hex(0xf2f8fd), 0);
        lv_obj_set_style_bg_color(roller, lv_color_hex(0xccecff), LV_PART_SELECTED);
        lv_obj_set_style_bg_grad_color(roller, lv_color_hex(0xaedbff), LV_PART_SELECTED);
        lv_obj_set_style_bg_grad_dir(roller, LV_GRAD_DIR_VER, LV_PART_SELECTED);
        lv_obj_set_style_text_color(roller, lv_color_hex(kInk), LV_PART_SELECTED);
        lv_obj_set_style_radius(roller, 18, LV_PART_SELECTED);
        lv_obj_set_style_border_width(roller, 1, LV_PART_SELECTED);
        lv_obj_set_style_border_color(roller, lv_color_hex(0x94cdf6), LV_PART_SELECTED);
        lv_obj_add_flag(roller, LV_OBJ_FLAG_SCROLL_MOMENTUM);
        lv_roller_set_visible_row_count(roller, 3);
        x += 350;
    }
    auto separator = Label(panel, ":", 778, 114, 48, &han_font_weather_hero);
    lv_obj_set_style_text_align(separator, LV_TEXT_ALIGN_CENTER, 0);

    for (int day = 0; day < 7; ++day) {
        const bool selected = (alarm_days_ & (1U << day)) != 0;
        auto button = Button(panel, kWeekdays[day], 488 + day * 101, 332, 90, 60,
                             selected ? 0xd2f8df : 0xf1f3f6, 410 + day);
        lv_obj_set_style_border_width(button, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(button, lv_color_hex(selected ? 0x8bdfaa : 0xdce3ea), 0);
        auto day_label = lv_obj_get_child(button, 0);
        lv_obj_set_style_text_font(day_label, &han_font_weather, 0);
        lv_obj_set_style_text_color(day_label, lv_color_hex(selected ? 0x078447 : 0x7b879d), 0);
    }

    auto save = Button(panel, "保存并开启", 488, 438, 330, 86, 0x28cf7d, 400);
    auto disable =
        Button(panel, alarm_ringing_ ? "停止铃声" : "关闭闹钟", 850, 438, 330, 86, 0xffabc5, 401);
    lv_obj_set_style_bg_grad_color(save, lv_color_hex(0x08b966), 0);
    lv_obj_set_style_bg_grad_dir(save, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(save, 2, 0);
    lv_obj_set_style_border_color(save, lv_color_hex(0x70e4a8), 0);
    lv_obj_set_style_shadow_color(save, lv_color_hex(0x28c978), 0);
    lv_obj_set_style_shadow_width(save, 14, 0);
    lv_obj_set_style_shadow_opa(save, LV_OPA_30, 0);
    lv_obj_set_style_bg_grad_color(disable, lv_color_hex(0xff8fb3), 0);
    lv_obj_set_style_bg_grad_dir(disable, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(disable, 2, 0);
    lv_obj_set_style_border_color(disable, lv_color_hex(0xffc7d8), 0);
    lv_obj_set_style_shadow_color(disable, lv_color_hex(0xff99b9), 0);
    lv_obj_set_style_shadow_width(disable, 14, 0);
    lv_obj_set_style_shadow_opa(disable, LV_OPA_30, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(save, 0), &han_font_weather, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(disable, 0), &han_font_weather, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(save, 0), lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(disable, 0), lv_color_hex(0xb8003b), 0);
}

void HanDisplay::Weather() {
    const auto weather = DecodeWeather(weather_text_);
    weather_index_detail_.clear();
    if (!weather.index_text.empty()) {
        if (!weather.index_name.empty())
            weather_index_detail_ += weather.index_name + "\n";
        if (!weather.index_category.empty())
            weather_index_detail_ += weather.index_category + "\n\n";
        weather_index_detail_ += weather.index_text;
    }
    auto hero = Card(body_, 0, 0, 760, 344, 0xd9edfc);
    lv_obj_set_style_radius(hero, 28, 0);
#ifdef HAN_UI_HOST_SIM
    Image(hero, &han_weather_qingdao_hero, 0, 0);
#else
    Image(hero, "S:/sdcard/handict/ui/graphics/weather-page/qingdao-hero.png", 0, 0);
#endif

    if (weather_text_.empty()) {
        auto empty = Box(hero, 24, 24, 390, 250, 0xffffff);
        lv_obj_set_style_bg_opa(empty, LV_OPA_80, 0);
        Label(empty, "还没有天气", 24, 20, 340, &han_font_40);
        auto intro =
            Label(empty, "请把和风天气配置放进 SD 卡，\n进入本页后会自动刷新。", 24, 92, 340);
        ApplyDynamicTextFont(intro);
        auto path = Label(empty, "SD:/handict/qweather.json", 24, 184, 340);
        lv_obj_set_style_text_color(path, lv_color_hex(0x3976a8), 0);
    } else {
        auto veil = Box(hero, 18, 18, 360, 292, 0xffffff);
        lv_obj_set_style_bg_opa(veil, LV_OPA_70, 0);
        // Centre the three headline rows on an even 68 px rhythm.  The fonts have
        // different line heights, so equal top coordinates would not look evenly spaced.
        constexpr int kHeroHeadlineCenters[] = {48, 116, 184};
        Label(hero, weather.city.c_str(), 42,
              kHeroHeadlineCenters[0] - han_font_weather_title.line_height / 2, 310,
              &han_font_weather_title);
        if (weather.cached) {
            auto badge = Box(hero, 230, 35, 120, 34, 0xffe7bd);
            lv_obj_set_style_radius(badge, 17, 0);
            auto badge_text = Label(badge, "缓存", 6, -1, 108, &han_font_weather);
            lv_obj_set_style_text_align(badge_text, LV_TEXT_ALIGN_CENTER, 0);
        }
        char temperature[24];
        snprintf(temperature, sizeof(temperature), "%d℃", weather.temperature);
        Label(hero, temperature, 38,
              kHeroHeadlineCenters[1] - han_font_weather_hero.line_height / 2, 320,
              &han_font_weather_hero);
        Label(hero, weather.condition.c_str(), 44,
              kHeroHeadlineCenters[2] - han_font_weather.line_height / 2, 300, &han_font_weather);
        char details[96];
        snprintf(details, sizeof(details), "体感 %d℃ · 湿度 %s", weather.feels_like,
                 weather.humidity >= 0 ? (std::to_string(weather.humidity) + "%").c_str() : "--");
        Label(hero, details, 44, 214, 310, &han_font_weather);
        std::string wind = weather.wind.empty() ? "风力 --" : weather.wind;
        if (weather.wind_scale >= 0)
            wind += " " + std::to_string(weather.wind_scale) + "级";
        Label(hero, wind.c_str(), 44, 252, 310, &han_font_weather);
        const auto condition_id = WeatherConditionId(weather.condition, weather.condition_code);
#ifdef HAN_UI_HOST_SIM
        const auto* current_art = han_weather_icon_find(LegacyWeatherGraphicId(condition_id));
        if (current_art) {
            auto art = Image(hero, current_art, 486, 45);
            lv_image_set_scale(art, 420);
            lv_image_set_pivot(art, 0, 0);
        }
#else
        const auto current_path =
            "S:/sdcard/handict/ui/graphics/weather-page/condition-" + condition_id + ".png";
        auto art = Image(hero, current_path.c_str(), 474, 43);
        lv_image_set_scale(art, 250);
        lv_image_set_pivot(art, 0, 0);
#endif
        if (!weather.updated.empty()) {
            auto update = Label(hero, ("和风天气 · " + weather.updated).c_str(), 492, 298, 242);
            lv_obj_set_style_text_color(update, lv_color_hex(kMuted), 0);
            lv_obj_set_style_text_align(update, LV_TEXT_ALIGN_RIGHT, 0);
        }
    }

    const uint32_t forecast_colors[] = {0xfffdf8, 0xf7fbff, 0xfff9ef, 0xf6fbf7};
    const char* day_names[] = {"今天", "明天", "后天", "第四天"};
    constexpr int kForecastTitleY = 13;
    constexpr int kForecastRangeY = 170;
    const int forecast_title_center = kForecastTitleY + han_font_weather.line_height / 2;
    const int forecast_range_center = kForecastRangeY + han_font_weather.line_height / 2;
    const int forecast_icon_center = (forecast_title_center + forecast_range_center + 1) / 2;
    for (int index = 0; index < 4; ++index) {
        auto card = Card(body_, index * 190, 360, 181, 232, forecast_colors[index]);
        const auto& day = index < static_cast<int>(weather.days.size()) ? weather.days[index]
                                                                        : WeatherView::Day{};
        std::string heading = day_names[index];
        if (index == 3 && day.date.size() >= 10) {
            struct tm parsed{};
            if (sscanf(day.date.c_str(), "%d-%d-%d", &parsed.tm_year, &parsed.tm_mon,
                       &parsed.tm_mday) == 3) {
                parsed.tm_year -= 1900;
                parsed.tm_mon -= 1;
                mktime(&parsed);
                static const char* names[] = {"周日", "周一", "周二", "周三",
                                              "周四", "周五", "周六"};
                heading = names[parsed.tm_wday];
            }
        }
        auto heading_label =
            Label(card, heading.c_str(), 8, kForecastTitleY, 165, &han_font_weather);
        lv_obj_set_style_text_align(heading_label, LV_TEXT_ALIGN_CENTER, 0);
        const auto condition_id = WeatherConditionId(day.condition, day.condition_code);
#ifdef HAN_UI_HOST_SIM
        const auto* art = han_weather_icon_find(LegacyWeatherGraphicId(condition_id));
        if (art) {
            constexpr int kForecastIconScale = 216;
            const int scaled_height = (art->header.h * kForecastIconScale + 255) / 256;
            auto image = Image(card, art, 50, forecast_icon_center - scaled_height / 2);
            lv_image_set_scale(image, kForecastIconScale);
            lv_image_set_pivot(image, 0, 0);
        }
#else
        const auto path =
            "S:/sdcard/handict/ui/graphics/weather-page/condition-" + condition_id + ".png";
        constexpr int kForecastIconSourceHeight = 192;
        constexpr int kForecastIconScale = 120;
        constexpr int kForecastIconHeight =
            (kForecastIconSourceHeight * kForecastIconScale + 255) / 256;
        auto image = Image(card, path.c_str(), 45, forecast_icon_center - kForecastIconHeight / 2);
        lv_image_set_scale(image, kForecastIconScale);
        lv_image_set_pivot(image, 0, 0);
#endif
        char range[32];
        if (day.temperature_min == 1000 || day.temperature_max == 1000)
            snprintf(range, sizeof(range), "--~--℃");
        else
            snprintf(range, sizeof(range), "%d~%d℃", day.temperature_min, day.temperature_max);
        auto range_label = Label(card, range, 6, kForecastRangeY, 169, &han_font_weather);
        lv_obj_set_style_text_align(range_label, LV_TEXT_ALIGN_CENTER, 0);
    }

    struct Metric {
        const char* title;
#ifdef HAN_UI_HOST_SIM
        const lv_image_dsc_t* icon;
#else
        const char* icon;
#endif
        uint32_t color;
    };
#ifdef HAN_UI_HOST_SIM
    const Metric metrics[] = {{"空气质量", &han_weather_air_quality, 0xe5f8dc},
                              {"降水概率", &han_weather_precipitation, 0xe4f3ff},
                              {"日出日落", &han_weather_sunrise_sunset, 0xfff1ce},
                              {"生活指数", &han_weather_lifestyle_index, 0xffe5e7}};
#else
    const Metric metrics[] = {
        {"空气质量", "S:/sdcard/handict/ui/graphics/weather-page/air-quality.png", 0xe5f8dc},
        {"降水概率", "S:/sdcard/handict/ui/graphics/weather-page/precipitation.png", 0xe4f3ff},
        {"日出日落", "S:/sdcard/handict/ui/graphics/weather-page/sunrise-sunset.png", 0xfff1ce},
        {"生活指数", "S:/sdcard/handict/ui/graphics/weather-page/lifestyle-index.png", 0xffe5e7},
    };
#endif
    const int metric_y[] = {0, 147, 294, 441};
    const int metric_h[] = {137, 137, 137, 151};
    for (int index = 0; index < 4; ++index) {
        auto card = Card(body_, 780, metric_y[index], 452, metric_h[index], metrics[index].color);
        Image(card, metrics[index].icon, 22, (metric_h[index] - 76) / 2);
        std::string value = "暂无数据";
        if (index == 0 && (!weather.air_category.empty() || !weather.air_aqi.empty())) {
            value = weather.air_category.empty() ? "AQI " + weather.air_aqi : weather.air_category;
            if (!weather.air_aqi.empty() && !weather.air_category.empty())
                value += " · AQI " + weather.air_aqi;
        } else if (index == 1 && !weather.days.empty() &&
                   weather.days[0].precipitation_probability >= 0) {
            value = std::to_string(weather.days[0].precipitation_probability) + "%";
        } else if (index == 2 && !weather.days.empty() &&
                   (!weather.days[0].sunrise.empty() || !weather.days[0].sunset.empty())) {
            value = (weather.days[0].sunrise.empty() ? "--:--" : weather.days[0].sunrise) + " — " +
                    (weather.days[0].sunset.empty() ? "--:--" : weather.days[0].sunset);
        } else if (index == 3 && !weather.index_text.empty()) {
            value = weather.index_category.empty() ? weather.index_name : weather.index_category;
        }
        // Keep the title/value pair centred as one block alongside the icon.  The
        // lifestyle card is taller, so calculate its offset independently too.
        constexpr int kMetricLineGap = 2;
        const int metric_text_height = han_font_weather.line_height * 2 + kMetricLineGap;
        const int metric_text_y = (metric_h[index] - metric_text_height) / 2;
        Label(card, metrics[index].title, 116, metric_text_y, 306, &han_font_weather);
        Label(card, value.c_str(), 116,
              metric_text_y + han_font_weather.line_height + kMetricLineGap, 310,
              &han_font_weather);
        if (index == 3 && !weather_index_detail_.empty()) {
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_user_data(card, this);
            lv_obj_add_event_cb(card, OnClick, LV_EVENT_CLICKED,
                                reinterpret_cast<void*>(static_cast<intptr_t>(918)));
        }
    }
}

void HanDisplay::Network() {
    if (usb_storage_active_) {
        auto card = Card(body_, 0, 0, 1232, 592, 0xffffff);
        Label(card, "USB 读卡器已开启", 28, 22, 1120, &han_font_40);
        auto message =
            Label(card,
                  "电脑现在可以访问 microSD 卡。\n\n复制或格式化完成后，请先在电脑上安全弹出，"
                  "再重启设备。",
                  28, 118, 1130);
        ApplyDynamicTextFont(message);
        Label(card, "此模式下字典内容和语音唤醒暂停", 28, 500, 1160);
        network_info_ = nullptr;
        return;
    }
    auto left = Card(body_, 0, 0, 650, 592, 0xffffff);
    Label(left, "网络与存储", 24, 16, 580, &han_font_timer_title);

    auto style_action_button = [](lv_obj_t* button) {
        auto label = lv_obj_get_child(button, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(label, &han_font_timer, 0);
    };

    auto connection = Box(left, 18, 72, 614, 202, 0xebf6ff);
#ifndef HAN_UI_HOST_SIM
    if (SdFileAvailable("/sdcard/handict/ui/graphics/settings-page/network-wifi.png")) {
        Image(connection, "S:/sdcard/handict/ui/graphics/settings-page/network-wifi.png", 18, 29);
    } else
#endif
    {
        auto wifi_art = Image(connection, &han_status_wifi_3, 38, 52);
        lv_image_set_scale(wifi_art, 420);
        lv_image_set_pivot(wifi_art, 0, 0);
    }
    network_info_ = Label(connection, "正在读取网络状态…", 184, 38, 210);
    lv_obj_set_height(network_info_, 120);
    lv_label_set_long_mode(network_info_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(network_info_, &han_font_timer, 0);
    lv_obj_set_style_text_color(network_info_, lv_color_hex(0x102347), 0);
    auto network_button = Button(connection, "手机配网", 410, 63, 184, 76, 0x86c9ff, 10);
    lv_obj_set_style_radius(network_button, 30, 0);
    style_action_button(network_button);

    auto storage = Box(left, 18, 292, 614, 202, 0xedfbf3);
#ifndef HAN_UI_HOST_SIM
    if (SdFileAvailable("/sdcard/handict/ui/graphics/settings-page/storage-usb.png")) {
        Image(storage, "S:/sdcard/handict/ui/graphics/settings-page/storage-usb.png", 10, 21);
    } else
#endif
    {
        auto storage_art = Image(storage, &han_icon_settings, 44, 50);
        lv_image_set_scale(storage_art, 270);
        lv_image_set_pivot(storage_art, 0, 0);
    }
    auto storage_title = Label(storage, "microSD卡", 184, 37, 210, &han_font_timer);
    lv_obj_set_style_text_color(storage_title, lv_color_hex(0x102347), 0);
    auto storage_hint = Label(storage, "字典、语音和插画资源", 184, 86, 218);
    lv_obj_set_style_text_color(storage_hint, lv_color_hex(kMuted), 0);
    auto storage_button = Button(storage, usb_storage_requested_ ? "正在切换…" : "USB 读卡器", 410,
                                 63, 184, 76, 0x91e6b4, 11);
    lv_obj_set_style_radius(storage_button, 30, 0);
    style_action_button(storage_button);
    auto usb_hint = Label(left, "USB 模式仅用于安全复制 SD 卡内容", 24, 526, 590);
    ApplyDynamicTextFont(usb_hint);
    lv_obj_set_style_text_color(usb_hint, lv_color_hex(kMuted), 0);

    auto right = Card(body_, 670, 0, 562, 592, 0xffffff);
    Label(right, "显示与声音", 24, 16, 510, &han_font_timer_title);

    auto make_slider_row = [this, right](int y, uint32_t background, uint32_t accent,
                                         const char* sd_icon_path, const char* lvgl_icon_path,
                                         const lv_image_dsc_t* fallback_icon, const char* title,
                                         int minimum, int maximum, int value,
                                         lv_obj_t** value_label) {
        auto row = Box(right, 18, y, 526, 112, background);
#ifndef HAN_UI_HOST_SIM
        if (SdFileAvailable(sd_icon_path)) {
            Image(row, lvgl_icon_path, 9, 18);
        } else
#endif
        {
            auto icon = Image(row, fallback_icon, 9, 18);
            lv_image_set_scale(icon, 152);
            lv_image_set_pivot(icon, 0, 0);
        }
        Label(row, title, 92, 13, 270);
        *value_label = Label(row, "", 390, 13, 112);
        lv_obj_set_style_text_align(*value_label, LV_TEXT_ALIGN_RIGHT, 0);
        auto minus = Label(row, "-", 89, 60, 34);
        lv_obj_set_style_text_align(minus, LV_TEXT_ALIGN_CENTER, 0);
        auto plus = Label(row, "+", 475, 60, 34);
        lv_obj_set_style_text_align(plus, LV_TEXT_ALIGN_CENTER, 0);
        auto slider = lv_slider_create(row);
        lv_obj_set_pos(slider, 126, 69);
        lv_obj_set_size(slider, 340, 14);
        lv_slider_set_range(slider, minimum, maximum);
        lv_slider_set_value(slider, value, LV_ANIM_OFF);
        lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0xdde5ec), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, lv_color_hex(accent), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, lv_color_hex(accent), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_pad_all(slider, 8, LV_PART_KNOB);
        lv_obj_set_style_border_width(slider, 3, LV_PART_KNOB);
        lv_obj_set_style_border_color(slider, lv_color_hex(0xffffff), LV_PART_KNOB);
        lv_obj_add_event_cb(slider, OnSettingSliderChanged, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(slider, OnSettingSliderReleased, LV_EVENT_RELEASED, this);
        return slider;
    };

    brightness_slider_ = make_slider_row(
        72, 0xfff7dd, 0xf5bb4c, "/sdcard/handict/ui/graphics/settings-page/brightness-sun.png",
        "S:/sdcard/handict/ui/graphics/settings-page/brightness-sun.png", &han_icon_weather,
        "屏幕亮度", 10, 100, brightness_setting_, &brightness_value_);
    volume_slider_ = make_slider_row(
        194, 0xe9f7ff, 0x5caeef, "/sdcard/handict/ui/graphics/settings-page/volume-speaker.png",
        "S:/sdcard/handict/ui/graphics/settings-page/volume-speaker.png", &han_icon_phonetics,
        "播放音量", 0, 100, volume_setting_, &volume_value_);
    auto_lock_slider_ = make_slider_row(
        316, 0xedfbf3, 0x52c98a, "/sdcard/handict/ui/graphics/settings-page/auto-lock.png",
        "S:/sdcard/handict/ui/graphics/settings-page/auto-lock.png", &han_icon_alarm, "自动锁屏", 1,
        30, auto_lock_minutes_, &auto_lock_value_);

    auto screen_button = Button(right, "立即关屏", 18, 456, 526, 76, 0xffabc2, 914);
    lv_obj_set_style_radius(screen_button, 30, 0);
    style_action_button(screen_button);
    auto wake_hint = Label(right, "关屏后点一下屏幕，再向上滑动解锁", 24, 547, 514);
    ApplyDynamicTextFont(wake_hint);
    lv_obj_set_style_text_color(wake_hint, lv_color_hex(kMuted), 0);
    lv_obj_set_style_text_align(wake_hint, LV_TEXT_ALIGN_CENTER, 0);
    UpdateSettingLabels();
}

void HanDisplay::UpdateSettingLabels() {
    char value[16];
    if (brightness_value_) {
        snprintf(value, sizeof(value), "%d%%", brightness_setting_);
        SetTextIfChanged(brightness_value_, value);
    }
    if (brightness_slider_)
        lv_slider_set_value(brightness_slider_, brightness_setting_, LV_ANIM_OFF);
    if (volume_value_) {
        snprintf(value, sizeof(value), "%d%%", volume_setting_);
        SetTextIfChanged(volume_value_, value);
    }
    if (volume_slider_)
        lv_slider_set_value(volume_slider_, volume_setting_, LV_ANIM_OFF);
    if (auto_lock_value_) {
        snprintf(value, sizeof(value), "%d 分钟", auto_lock_minutes_);
        SetTextIfChanged(auto_lock_value_, value);
    }
    if (auto_lock_slider_)
        lv_slider_set_value(auto_lock_slider_, auto_lock_minutes_, LV_ANIM_OFF);
}

void HanDisplay::SetScreenOff(bool off) {
    if (off && screen_off_.load())
        return;
    if (!off) {
        UnlockScreen();
        return;
    }
    DisplayLockGuard guard(this);
    if (!setup_ui_called_ || root_ == nullptr)
        return;

    if (screen_wake_overlay_ != nullptr)
        lv_obj_delete(screen_wake_overlay_);
    screen_wake_overlay_ = Box(root_, 0, 0, 1280, 720, 0x000000);
    lv_obj_set_style_radius(screen_wake_overlay_, 0, 0);
    // Keep an opaque black frame above the current page while the backlight is off. Some display
    // hardware restores the backlight as soon as touch activity is detected; a transparent wake
    // layer would briefly expose the settings page before ShowLockScreen() runs.
    lv_obj_set_style_bg_opa(screen_wake_overlay_, LV_OPA_COVER, 0);
    lv_obj_add_flag(screen_wake_overlay_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(screen_wake_overlay_, this);
    lv_obj_add_event_cb(screen_wake_overlay_, OnScreenWake, LV_EVENT_PRESSED, this);
    lv_obj_move_foreground(screen_wake_overlay_);
    screen_off_ = true;
    lock_screen_visible_ = false;
#ifndef HAN_UI_HOST_SIM
    Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
    Board::GetInstance().GetBacklight()->SetBrightness(0);
#endif
}

void HanDisplay::ShowLockScreen() {
    if (!screen_off_)
        return;
    DisplayLockGuard guard(this);
    ShowLockScreenLocked();
}

void HanDisplay::ShowLockScreenLocked() {
    if (!screen_off_)
        return;
    if (!setup_ui_called_ || root_ == nullptr)
        return;

    if (screen_wake_overlay_ != nullptr)
        lv_obj_delete(screen_wake_overlay_);
    screen_wake_overlay_ = Box(root_, 0, 0, 1280, 720, 0xfffaf1);
    lv_obj_set_style_radius(screen_wake_overlay_, 0, 0);
    lv_obj_add_flag(screen_wake_overlay_, LV_OBJ_FLAG_CLICKABLE);
    // Child labels and the card bubble gestures upward. Stop that chain here so the lock-screen
    // callback receives the swipe instead of the otherwise non-interactive page root.
    lv_obj_remove_flag(screen_wake_overlay_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(screen_wake_overlay_, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_user_data(screen_wake_overlay_, this);
    lv_obj_add_event_cb(screen_wake_overlay_, OnLockGesture, LV_EVENT_GESTURE, this);

    auto panel = Card(screen_wake_overlay_, 120, 68, 1040, 584, 0xffffff);
    lv_obj_set_style_radius(panel, 42, 0);
#ifndef HAN_UI_HOST_SIM
    if (SdFileAvailable("/sdcard/handict/ui/graphics/settings-page/lock-screen.png")) {
        Image(panel, "S:/sdcard/handict/ui/graphics/settings-page/lock-screen.png", 48, 182);
    } else
#endif
    {
        auto lock_art = Image(panel, &han_art_alarm, 70, 184);
        lv_image_set_scale(lock_art, 144);
        lv_image_set_pivot(lock_art, 0, 0);
    }

    char time_text[16] = "--:--";
    char date_text[48] = "日期待同步";
    const auto now = time(nullptr);
    struct tm tm{};
    localtime_r(&now, &tm);
    if (tm.tm_year + 1900 >= 2024) {
        strftime(time_text, sizeof(time_text), "%H:%M", &tm);
        static const char* weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
        snprintf(date_text, sizeof(date_text), "%d月%d日 周%s", tm.tm_mon + 1, tm.tm_mday,
                 weekdays[tm.tm_wday]);
    }
    auto lock_time = Label(panel, time_text, 410, 92, 520, &han_font_clock);
    lv_obj_set_style_text_align(lock_time, LV_TEXT_ALIGN_CENTER, 0);
    auto lock_date = Label(panel, date_text, 410, 186, 520, &han_font_28);
    lv_obj_set_style_text_align(lock_date, LV_TEXT_ALIGN_CENTER, 0);
    auto arrow = Label(panel, "↑", 410, 280, 520, &han_font_40);
    lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(arrow, lv_color_hex(0x45b987), 0);
    auto hint = Label(panel, "向上滑动解锁", 410, 350, 520, &han_font_40);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    auto note = Label(panel, "10 秒内未操作将自动关屏", 410, 430, 520, &han_font_28);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(note, lv_color_hex(kMuted), 0);

    lv_obj_move_foreground(screen_wake_overlay_);
    screen_off_ = false;
    lock_screen_visible_ = true;
    lock_screen_shown_ms_ = NowMs();
#ifndef HAN_UI_HOST_SIM
    // Finish drawing the lock screen before restoring the backlight so no intermediate black or
    // underlying application frame becomes visible.
    lv_refr_now(display_);
    Board::GetInstance().GetBacklight()->SetBrightness(brightness_setting_);
    Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
#endif
}

void HanDisplay::UnlockScreen() {
    if (!screen_off_ && !lock_screen_visible_)
        return;
    DisplayLockGuard guard(this);
    if (!setup_ui_called_ || root_ == nullptr)
        return;

    screen_off_ = false;
    lock_screen_visible_ = false;
    if (screen_wake_overlay_ != nullptr) {
        lv_obj_delete(screen_wake_overlay_);
        screen_wake_overlay_ = nullptr;
    }
    lv_display_trigger_activity(display_);
#ifndef HAN_UI_HOST_SIM
    Board::GetInstance().GetBacklight()->SetBrightness(brightness_setting_);
    const auto state = Application::GetInstance().GetDeviceState();
    Board::GetInstance().SetPowerSaveLevel(state == kDeviceStateIdle ? PowerSaveLevel::LOW_POWER
                                                                     : PowerSaveLevel::PERFORMANCE);
#endif
}

void HanDisplay::SaveAutoLockSetting() {
#ifndef HAN_UI_HOST_SIM
    const int minutes = auto_lock_minutes_;
    Application::GetInstance().Schedule([minutes] {
        Settings display("display", true);
        display.SetInt("lock_min", minutes);
    });
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
    if (page_ == Page::Dictionary || page_ == Page::Weather)
        Render(page_);
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

void HanDisplay::CloseWeatherIndexPopup() {
    if (weather_index_popup_ != nullptr) {
        lv_obj_delete(weather_index_popup_);
        weather_index_popup_ = nullptr;
    }
}

void HanDisplay::ShowWeatherIndexPopup() {
    CloseWeatherIndexPopup();
    if (weather_index_detail_.empty())
        return;

    weather_index_popup_ = Box(root_, 0, 0, 1280, 720, 0x142b57);
    lv_obj_set_style_radius(weather_index_popup_, 0, 0);
    lv_obj_set_style_bg_opa(weather_index_popup_, LV_OPA_40, 0);
    lv_obj_add_flag(weather_index_popup_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(weather_index_popup_, this);
    lv_obj_add_event_cb(weather_index_popup_, OnClick, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(917)));

    auto panel = Card(weather_index_popup_, 112, 70, 1056, 580, 0xfffbf7);
    lv_obj_set_style_radius(panel, 30, 0);
    Label(panel, "生活指数完整建议", 34, 24, 650, &han_font_weather_title);
    auto close = Button(panel, "关闭", 850, 18, 170, 64, kPink, 917);
    lv_obj_set_style_radius(close, 22, 0);

    auto content = Card(panel, 28, 104, 1000, 438, 0xffffff);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(content, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(content, 10, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(content, 5, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(content, lv_color_hex(0xf09aa6), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(content, LV_OPA_80, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_bottom(content, 26, 0);
    auto detail = Label(content, weather_index_detail_.c_str(), 28, 24, 924);
    ApplyDictionaryTextFont(detail);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(detail, LV_SIZE_CONTENT);
    lv_obj_set_style_text_line_space(detail, 12, 0);

    lv_obj_move_foreground(weather_index_popup_);
    lv_obj_invalidate(weather_index_popup_);
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

void HanDisplay::OnScreenWake(lv_event_t* event) {
    auto self = static_cast<HanDisplay*>(lv_event_get_user_data(event));
    if (self)
        self->ShowLockScreenLocked();
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

void HanDisplay::OnLockGesture(lv_event_t* event) {
    auto self = static_cast<HanDisplay*>(lv_event_get_user_data(event));
    auto indev = lv_event_get_indev(event);
    if (!self || !indev)
        return;
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_TOP)
        Application::GetInstance().Schedule([self] { self->UnlockScreen(); });
}

void HanDisplay::OnSettingSliderChanged(lv_event_t* event) {
    auto self = static_cast<HanDisplay*>(lv_event_get_user_data(event));
    auto slider = lv_event_get_target_obj(event);
    if (!self || !slider)
        return;
    const int value = lv_slider_get_value(slider);
    if (slider == self->brightness_slider_)
        self->brightness_setting_ = value;
    else if (slider == self->volume_slider_)
        self->volume_setting_ = value;
    else if (slider == self->auto_lock_slider_)
        self->auto_lock_minutes_ = value;
    self->UpdateSettingLabels();
}

void HanDisplay::OnSettingSliderReleased(lv_event_t* event) {
    auto self = static_cast<HanDisplay*>(lv_event_get_user_data(event));
    auto slider = lv_event_get_target_obj(event);
    if (!self || !slider)
        return;
    if (slider == self->auto_lock_slider_) {
        self->SaveAutoLockSetting();
        return;
    }
#ifndef HAN_UI_HOST_SIM
    const int brightness = self->brightness_setting_;
    const int volume = self->volume_setting_;
    const bool is_brightness = slider == self->brightness_slider_;
    Application::GetInstance().Schedule([brightness, volume, is_brightness] {
        auto& board = Board::GetInstance();
        if (is_brightness)
            board.GetBacklight()->SetBrightness(brightness, true);
        else
            board.GetAudioCodec()->SetOutputVolume(volume);
    });
#endif
}

void HanDisplay::Action(int a) {
    if (screen_off_) {
        if (a == 915)
            Application::GetInstance().Schedule([this] { ShowLockScreen(); });
        return;
    }
    if (lock_screen_visible_) {
        return;
    }
    if (usb_storage_active_)
        return;
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
    if (a == 917) {
        CloseWeatherIndexPopup();
        return;
    }
    if (a == 918) {
        ShowWeatherIndexPopup();
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
        const auto& sound = han::kSounds[sound_];
        Queue(1, "phonetics/en-GB/" + std::string(sound.id) + "/sound.ogg");
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
        if (timer_today_index_ >= 0 && timer_today_index_ < 5)
            timer_view_day_ = timer_today_index_;
        Render(Page::Timer);
        return;
    }
    if (a == 310) {
        if (study_.running())
            study_.Pause(NowMs());
        else
            study_.Start(study_.subject(), NowMs());
        if (timer_today_index_ >= 0 && timer_today_index_ < 5)
            timer_view_day_ = timer_today_index_;
        SaveTimer();
        Render(Page::Timer);
        const auto elapsed = study_.Elapsed(study_.subject(), NowMs());
        const uint32_t next_second =
            study_.running() ? static_cast<uint32_t>(1000 - elapsed % 1000) : 1000;
        lv_timer_set_period(timer_tick_, std::max<uint32_t>(20, next_second));
        lv_timer_reset(timer_tick_);
        return;
    }
    if (a == 311) {
        study_.Complete(NowMs());
        if (timer_today_index_ >= 0 && timer_today_index_ < 5)
            timer_view_day_ = timer_today_index_;
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
    if (a >= 320 && a <= 324) {
        timer_view_day_ = a - 320;
        Render(Page::Timer);
        return;
    }
    if (a >= 410 && a <= 416) {
        // Rebuilding the seven day chips must not discard a time the user has already scrolled to.
        if (alarm_hour_ && alarm_minute_)
            alarm_minutes_ =
                lv_roller_get_selected(alarm_hour_) * 60 + lv_roller_get_selected(alarm_minute_);
        alarm_days_ ^= static_cast<uint8_t>(1U << (a - 410));
        Render(Page::Alarm);
        return;
    }
    if (a == 400 || a == 401) {
        if (a == 400 && alarm_days_ == 0) {
            Toast("请至少选择一天");
            return;
        }
        alarm_enabled_ = a == 400;
        if (a == 400)
            alarm_minutes_ =
                lv_roller_get_selected(alarm_hour_) * 60 + lv_roller_get_selected(alarm_minute_);
        alarm_ringing_ = false;
        const auto minutes = alarm_minutes_;
        const auto days = alarm_days_;
        const auto enabled = alarm_enabled_;
        Application::GetInstance().Schedule([minutes, days, enabled] {
            Settings s("han_alarm", true);
            s.SetInt("minutes", minutes);
            s.SetInt("days", days);
            s.SetBool("enabled", enabled);
        });
        Render(Page::Alarm);
        Toast(a == 400 ? "闹钟已保存" : "闹钟已关闭");
        return;
    }
}

void HanDisplay::Tick(lv_timer_t* timer) {
    auto self = static_cast<HanDisplay*>(lv_timer_get_user_data(timer));
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
    if (self->lock_screen_visible_ && NowMs() - self->lock_screen_shown_ms_ >= 10000) {
        Application::GetInstance().Schedule([self] { self->SetScreenOff(true); });
        return;
    }
    if (!self->screen_off_ && !self->lock_screen_visible_ && !self->usb_storage_active_ &&
        !self->alarm_ringing_ && self->auto_lock_minutes_ > 0 &&
        lv_display_get_inactive_time(self->display_) >=
            static_cast<uint32_t>(self->auto_lock_minutes_) * 60U * 1000U) {
        Application::GetInstance().Schedule([self] { self->SetScreenOff(true); });
    }
}

void HanDisplay::TimerTick(lv_timer_t* timer) {
    auto self = static_cast<HanDisplay*>(lv_timer_get_user_data(timer));
    if (self->study_.running() &&
        self->study_.Elapsed(self->study_.subject(), NowMs()) >= kTimerMaximumMs) {
        self->study_.Pause(NowMs());
        self->SaveTimer();
        if (self->page_ == Page::Timer)
            self->Render(Page::Timer);
        return;
    }
    self->UpdateTimer();
    if (self->study_.running() && NowMs() - self->last_checkpoint_ms_ >= 60000) {
        self->last_checkpoint_ms_ = NowMs();
        self->SaveTimer();
    }
    const auto elapsed = self->study_.Elapsed(self->study_.subject(), NowMs());
    const uint32_t next_second =
        self->study_.running() ? static_cast<uint32_t>(1000 - elapsed % 1000) : 1000;
    // A busy frame can delay a callback slightly. Scheduling from the remainder prevents that
    // delay from accumulating and keeps every visible MM:SS transition on a whole second.
    lv_timer_set_period(timer, std::max<uint32_t>(20, next_second));
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
        network = "手机配网模式\n" + wifi.GetApSsid();
    else if (wifi.IsConnected())
        network = "Wi-Fi 已连接\n" + wifi.GetSsid();
    else
        network = "Wi-Fi 未连接\n可使用本地功能";
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
    // The alarm is based on synchronized system time; RTC wake-up is not implied. tm_wday starts
    // on Sunday, while the persisted weekday mask starts on Monday.
    const int64_t day = static_cast<int64_t>(tm.tm_year) * 366 + tm.tm_yday;
    const int weekday = (tm.tm_wday + 6) % 7;
    if (valid_time && alarm_enabled_ && alarm_last_day_ != day &&
        (alarm_days_ & (1U << weekday)) != 0 && tm.tm_hour * 60 + tm.tm_min == alarm_minutes_) {
        alarm_last_day_ = day;
        alarm_ringing_ = true;
        Render(Page::Alarm);
        Toast("时间到了！");
        Application::GetInstance().Schedule(
            [] { Application::GetInstance().PlaySound(Lang::Sounds::OGG_SUCCESS); });
    }
}

void HanDisplay::SyncTimerWeek() {
    int anchor = -1;
    int weekday = -1;
    if (!CurrentTimerWeek(anchor, weekday)) {
        if (timer_view_day_ < 0 || timer_view_day_ >= 5)
            timer_view_day_ = 0;
        return;
    }
    timer_today_index_ = weekday;
    if (timer_view_day_ < 0 || timer_view_day_ >= 5)
        timer_view_day_ = weekday >= 0 && weekday < 5 ? weekday : 4;
    if (timer_week_anchor_ == anchor)
        return;
    for (auto& day : timer_week_subject_seconds_)
        day.fill(0);
    timer_week_anchor_ = anchor;
    timer_view_day_ = weekday >= 0 && weekday < 5 ? weekday : 4;
    for (int i = 0; i < 3; ++i)
        timer_week_baseline_seconds_[i] = study_.Elapsed(i, NowMs()) / 1000;
}

std::array<int64_t, 3> HanDisplay::TimerDaySeconds(int day, int64_t now_ms) const {
    std::array<int64_t, 3> result{};
    if (day < 0 || day >= 5)
        return result;
    result = timer_week_subject_seconds_[day];
    if (day == timer_today_index_) {
        for (int subject = 0; subject < 3; ++subject) {
            const auto current = study_.Elapsed(subject, now_ms) / 1000;
            result[subject] +=
                std::max<int64_t>(0, current - timer_week_baseline_seconds_[subject]);
        }
    }
    return result;
}

void HanDisplay::LoadPreferences() {
    Settings s("han_study");
    for (int i = 0; i < 3; ++i) {
        const auto key = std::to_string(i);
        study_.Restore(i, static_cast<int64_t>(s.GetInt("s" + key, 0)) * 1000,
                       s.GetBool("c" + key, false));
    }
    timer_week_anchor_ = static_cast<int>(s.GetInt("week", -1));
    const bool has_subject_week = s.GetInt("wv", 0) >= 2;
    for (int day = 0; day < 5; ++day) {
        for (int subject = 0; subject < 3; ++subject) {
            const auto key = "d" + std::to_string(day) + "s" + std::to_string(subject);
            timer_week_subject_seconds_[day][subject] = std::max<int64_t>(0, s.GetInt(key, 0));
        }
        // Keep the old daily total visible after upgrading. Old data did not identify its
        // subject, so it is placed in the first row only during this one-time migration.
        if (!has_subject_week) {
            const auto legacy = std::max<int64_t>(0, s.GetInt("w" + std::to_string(day), 0));
            timer_week_subject_seconds_[day][0] = legacy;
        }
    }
    for (int subject = 0; subject < 3; ++subject) {
        const auto key = "b" + std::to_string(subject);
        const auto stored = s.GetInt(key, -1);
        timer_week_baseline_seconds_[subject] =
            stored >= 0 ? stored : study_.Elapsed(subject, NowMs()) / 1000;
    }
    SyncTimerWeek();
    Settings a("han_alarm");
    alarm_minutes_ = std::clamp(static_cast<int>(a.GetInt("minutes", 405)), 0, 1439);
    alarm_days_ =
        static_cast<uint8_t>(std::clamp(static_cast<int>(a.GetInt("days", 0x1f)), 0, 0x7f));
    alarm_enabled_ = a.GetBool("enabled", false);
    Settings display("display");
    brightness_setting_ = std::clamp(static_cast<int>(display.GetInt("brightness", 75)), 10, 100);
    auto_lock_minutes_ = std::clamp(static_cast<int>(display.GetInt("lock_min", 10)), 1, 30);
    Settings audio("audio");
    volume_setting_ = std::clamp(static_cast<int>(audio.GetInt("output_volume", 70)), 0, 100);
}

void HanDisplay::SaveTimer() {
    int seconds[3];
    bool completed[3];
    SyncTimerWeek();
    for (int i = 0; i < 3; ++i) {
        seconds[i] = study_.Elapsed(i, NowMs()) / 1000;
        completed[i] = study_.completed(i);
        if (timer_today_index_ >= 0 && timer_today_index_ < 5) {
            timer_week_subject_seconds_[timer_today_index_][i] +=
                std::max<int64_t>(0, seconds[i] - timer_week_baseline_seconds_[i]);
        }
        timer_week_baseline_seconds_[i] = seconds[i];
    }
    const auto week_subject_seconds = timer_week_subject_seconds_;
    const auto week_anchor = timer_week_anchor_;
    const auto week_baseline = timer_week_baseline_seconds_;
    Application::GetInstance().Schedule(
        [seconds, completed, week_subject_seconds, week_anchor, week_baseline] {
            Settings s("han_study", true);
            for (int i = 0; i < 3; ++i) {
                auto k = std::to_string(i);
                s.SetInt("s" + k, seconds[i]);
                s.SetBool("c" + k, completed[i]);
            }
            s.SetInt("week", week_anchor);
            s.SetInt("wv", 2);
            for (int subject = 0; subject < 3; ++subject)
                s.SetInt("b" + std::to_string(subject), static_cast<int>(week_baseline[subject]));
            for (int day = 0; day < 5; ++day) {
                int64_t total = 0;
                for (int subject = 0; subject < 3; ++subject) {
                    const auto seconds = week_subject_seconds[day][subject];
                    total += seconds;
                    const auto key = "d" + std::to_string(day) + "s" + std::to_string(subject);
                    s.SetInt(key, static_cast<int>(seconds));
                }
                // Retain the aggregate keys so a downgrade does not lose the visible totals.
                s.SetInt("w" + std::to_string(day), static_cast<int>(total));
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
            DisplayLockGuard guard(self);
            self->weather_text_ = weather;
            if (self->page_ == Page::Timetable || self->page_ == Page::Weather)
                self->Render(self->page_);
        } else if (job.type == 4) {
            std::string weather, error;
            if (!han::QWeatherService::Refresh(store, weather, error)) {
                if (weather.empty())
                    weather = han::QWeatherService::LoadCache(store);
                self->Toast(error.c_str());
            }
            DisplayLockGuard guard(self);
            if (!weather.empty())
                self->weather_text_ = weather;
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
