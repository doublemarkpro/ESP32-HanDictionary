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
#include "assets/flip_clock_assets.h"
#include "assets/home_skin.h"
#include "assets/timetable_assets.h"
#include "assets/ui_assets.h"
#ifdef HAN_UI_HOST_SIM
#include "assets/weather_icon_pack.h"
#endif
#include "assets/weather_page_assets.h"
#include "dictionary_service.h"
#include "lunar_calendar.h"
#ifndef HAN_UI_HOST_SIM
#include "mqtt_message_board.h"
#endif
#include "phonetics.h"
#include "qweather_service.h"

namespace {
constexpr uint32_t kInk = 0x142b57, kBg = 0xfff9f0, kGreen = 0xd9f4df, kBlue = 0xd9edfc;
constexpr uint32_t kPurple = 0xe9dffc, kOrange = 0xffe8d6, kPink = 0xffdfe3;
constexpr uint32_t kMuted = 0x61708f, kCardBorder = 0xf1e8d9;
constexpr uint32_t kDarkBackground = 0x111a29;
constexpr uint32_t kDarkSurface = 0x1b2940;
constexpr uint32_t kDarkSurfaceRaised = 0x243650;
constexpr uint32_t kDarkText = 0xf2f6fa;
constexpr uint32_t kDarkMuted = 0xa9bad0;
constexpr uint32_t kDarkBorder = 0x344b69;
std::atomic<bool> g_dark_theme_enabled{false};

uint8_t Red(uint32_t color) {
    return static_cast<uint8_t>((color >> 16) & 0xff);
}

uint8_t Green(uint32_t color) {
    return static_cast<uint8_t>((color >> 8) & 0xff);
}

uint8_t Blue(uint32_t color) {
    return static_cast<uint8_t>(color & 0xff);
}

uint32_t DarkTintedSurface(uint32_t color, bool raised = false) {
    const int red = Red(color), green = Green(color), blue = Blue(color);
    const int maximum = std::max({red, green, blue});
    const int minimum = std::min({red, green, blue});
    if (maximum - minimum < 18)
        return raised ? kDarkSurfaceRaised : kDarkSurface;
    if (green > red + 8 && green > blue)
        return raised ? 0x23493f : 0x183b35;
    if (blue > red + 8 && blue >= green)
        return raised ? 0x244565 : 0x1a3550;
    if (red > blue + 12 && green > blue + 4)
        return raised ? 0x5a4129 : 0x473322;
    if (red > green + 8)
        return raised ? 0x573344 : 0x432936;
    return raised ? kDarkSurfaceRaised : kDarkSurface;
}

lv_color_t ThemeFill(uint32_t color) {
    if (!g_dark_theme_enabled.load())
        return lv_color_hex(color);
    if (color == 0x000000)
        return lv_color_hex(color);
    if (color == kBg)
        return lv_color_hex(kDarkBackground);
    if (color == kGreen)
        return lv_color_hex(0x183b35);
    if (color == kBlue)
        return lv_color_hex(0x1a3550);
    if (color == kPurple)
        return lv_color_hex(0x302a55);
    if (color == kOrange)
        return lv_color_hex(0x473322);
    if (color == kPink)
        return lv_color_hex(0x432936);
    const int red = Red(color), green = Green(color), blue = Blue(color);
    const int luminance = (red * 299 + green * 587 + blue * 114) / 1000;
    if (luminance >= 216)
        return lv_color_hex(DarkTintedSurface(color, luminance < 242));
    // Saturated controls keep their identity in night mode, with a slightly lower luminance.
    if (std::max({red, green, blue}) - std::min({red, green, blue}) > 52) {
        return lv_color_make(static_cast<uint8_t>(red * 4 / 5),
                             static_cast<uint8_t>(green * 4 / 5),
                             static_cast<uint8_t>(blue * 4 / 5));
    }
    return lv_color_hex(color);
}

lv_color_t ThemeText(uint32_t color) {
    if (!g_dark_theme_enabled.load())
        return lv_color_hex(color);
    if (color == kMuted)
        return lv_color_hex(kDarkMuted);
    const int red = Red(color), green = Green(color), blue = Blue(color);
    const int luminance = (red * 299 + green * 587 + blue * 114) / 1000;
    if (color == kInk || luminance < 92)
        return lv_color_hex(kDarkText);
    if (luminance < 160)
        return lv_color_hex(kDarkMuted);
    return lv_color_hex(color);
}

lv_color_t ThemeBorder(uint32_t color) {
    if (!g_dark_theme_enabled.load())
        return lv_color_hex(color);
    const int red = Red(color), green = Green(color), blue = Blue(color);
    if (std::max({red, green, blue}) - std::min({red, green, blue}) > 55)
        return ThemeFill(color);
    return lv_color_hex(kDarkBorder);
}

lv_color_t ThemeShadow(uint32_t color) {
    return g_dark_theme_enabled.load() ? lv_color_hex(0x050a12) : lv_color_hex(color);
}
constexpr int kPinyinPageSize = 12;
constexpr int64_t kTimerMaximumMs = (99 * 60 + 59) * 1000LL;
struct AlarmRingtone {
    const char* name;
    const char* path;
};
constexpr AlarmRingtone kAlarmRingtones[] = {
    {"晨光", "alarms/morning-glow.ogg"},
    {"轻步", "alarms/light-steps.ogg"},
    {"小铃", "alarms/tiny-bells.ogg"},
    {"启程", "alarms/ready-go.ogg"},
};
constexpr size_t kAlarmAudioLimit = 192 * 1024;
constexpr int64_t kAlarmMaximumRingMs = 10 * 60 * 1000LL;
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
        lv_draw_vector_dsc_set_fill_color(dsc, ThemeText(color));
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
    lv_obj_set_style_bg_color(obj, ThemeFill(color), 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

lv_obj_t* HanDisplay::Card(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color) {
    auto card = Box(parent, x, y, w, h, color);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, ThemeBorder(kCardBorder), 0);
    lv_obj_set_style_shadow_color(card, ThemeShadow(0xb8a889), 0);
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
    lv_obj_set_style_text_color(obj, ThemeText(kInk), 0);
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
    lv_obj_set_height(clock_, 48);
    lv_obj_add_flag(clock_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(clock_, this);
    lv_obj_add_event_cb(clock_, OnClick, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(13)));
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
    // LVGL bubbles gestures to the first ancestor without GESTURE_BUBBLE. Every ordinary child
    // gets that flag by default, so the real recipient is the active screen rather than body_.
    // Listen there and gate the handler by page_ instead of relying on a child hit target.
    lv_obj_add_event_cb(lv_display_get_screen_active(display_), OnSettingsGesture, LV_EVENT_GESTURE,
                        this);

    // Keep assistant activity and conversation in one calm, persistent card. Voice activation is
    // wake-word based; a full-width press-to-talk control would compete with learning content.
    assistant_card_ = Card(root_, 24, 606, 1232, 90, 0xf4fbff);
    lv_obj_set_style_radius(assistant_card_, 30, 0);
    lv_obj_set_style_border_width(assistant_card_, 2, 0);
    lv_obj_set_style_border_color(assistant_card_, ThemeBorder(0xc9e8df), 0);
    lv_obj_set_style_bg_grad_color(assistant_card_, ThemeFill(0xfffbef), 0);
    lv_obj_set_style_bg_grad_dir(assistant_card_, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_shadow_color(assistant_card_, ThemeShadow(0x8fbcb2), 0);
    lv_obj_set_style_shadow_width(assistant_card_, 18, 0);
    lv_obj_set_style_shadow_opa(assistant_card_, LV_OPA_20, 0);
    lv_obj_set_style_shadow_ofs_y(assistant_card_, 5, 0);
    assistant_badge_ = Box(assistant_card_, 12, 7, 76, 76, 0xdff6ee);
    lv_obj_set_style_radius(assistant_badge_, 24, 0);
    lv_obj_set_style_border_width(assistant_badge_, 3, 0);
    lv_obj_set_style_border_color(assistant_badge_, ThemeBorder(0xffffff), 0);
    lv_obj_set_style_bg_grad_color(assistant_badge_, ThemeFill(0xddeeff), 0);
    lv_obj_set_style_bg_grad_dir(assistant_badge_, LV_GRAD_DIR_VER, 0);
    auto assistant_icon = Image(assistant_badge_, &han_art_book, 2, 11);
    lv_image_set_scale(assistant_icon, 96);
    lv_image_set_pivot(assistant_icon, 0, 0);
    role_box_ = Box(assistant_card_, 104, 9, 128, 34, 0xc8e9ff);
    lv_obj_set_style_radius(role_box_, 17, 0);
    lv_obj_set_style_bg_grad_color(role_box_, ThemeFill(0xe3d8ff), 0);
    lv_obj_set_style_bg_grad_dir(role_box_, LV_GRAD_DIR_HOR, 0);
    role_label_ = Label(role_box_, "小智", 0, 0, 128);
    lv_obj_set_style_text_align(role_label_, LV_TEXT_ALIGN_CENTER, 0);
    // Center against the label's real font metrics. A fixed y-position makes the Han glyphs look
    // low inside the capsule even when the bounding boxes are mathematically centered.
    lv_obj_align(role_label_, LV_ALIGN_CENTER, 0, -1);
    message_ = Label(assistant_card_, "说“你好小智”，我来帮你学习", 104, 44, 842);
    lv_obj_set_height(message_, 39);
    lv_label_set_long_mode(message_, LV_LABEL_LONG_DOT);
    status_box_ = Box(assistant_card_, 964, 12, 248, 66, 0xe9f4ff);
    lv_obj_set_style_radius(status_box_, 25, 0);
    lv_obj_set_style_border_width(status_box_, 1, 0);
    lv_obj_set_style_border_color(status_box_, ThemeBorder(0xc9ddf3), 0);
    lv_obj_set_style_bg_grad_color(status_box_, ThemeFill(0xe4f8ef), 0);
    lv_obj_set_style_bg_grad_dir(status_box_, LV_GRAD_DIR_HOR, 0);
    status_label_ = Label(status_box_, "准备中", 12, 14, 178);
    notification_label_ = Label(status_box_, "", 12, 14, 178);
    // The microphone artwork is white, so keep it on a saturated circular chip instead of relying
    // on the pale status gradient for contrast.
    status_mic_badge_ = Box(status_box_, 198, 8, 50, 50, 0x477da8);
    lv_obj_set_style_radius(status_mic_badge_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(status_mic_badge_, 2, 0);
    lv_obj_set_style_border_color(status_mic_badge_, ThemeBorder(0xffffff), 0);
    lv_obj_set_style_shadow_color(status_mic_badge_, ThemeShadow(0x4a7796), 0);
    lv_obj_set_style_shadow_width(status_mic_badge_, 7, 0);
    lv_obj_set_style_shadow_opa(status_mic_badge_, LV_OPA_20, 0);
    auto status_mic = Image(status_mic_badge_, &han_status_mic, 1, 1);
    lv_image_set_scale(status_mic, 250);
    lv_image_set_pivot(status_mic, 0, 0);
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

    // A wake-up on a feature page uses a modal conversation surface. It is a root child so page
    // rebuilds cannot delete it, while the translucent scrim keeps the current learning context
    // visible. Conversation rows live in a bounded scroll view, preserving useful context without
    // allowing an unbounded LVGL or heap footprint.
    assistant_dialog_scrim_ = Box(root_, 0, 0, 1280, 720, 0x173d51);
    lv_obj_set_style_radius(assistant_dialog_scrim_, 0, 0);
    lv_obj_set_style_bg_opa(assistant_dialog_scrim_, LV_OPA_40, 0);
    lv_obj_add_flag(assistant_dialog_scrim_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(assistant_dialog_scrim_, this);

    assistant_dialog_ = Card(assistant_dialog_scrim_, 104, 78, 1072, 610, 0xfafcff);
    lv_obj_set_style_radius(assistant_dialog_, 36, 0);
    lv_obj_set_style_border_width(assistant_dialog_, 3, 0);
    lv_obj_set_style_border_color(assistant_dialog_, ThemeBorder(0xb7dcf4), 0);
    lv_obj_set_style_bg_grad_color(assistant_dialog_, ThemeFill(0xfffdf5), 0);
    lv_obj_set_style_bg_grad_dir(assistant_dialog_, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_color(assistant_dialog_, ThemeShadow(0x315d72), 0);
    lv_obj_set_style_shadow_width(assistant_dialog_, 30, 0);
    lv_obj_set_style_shadow_opa(assistant_dialog_, LV_OPA_30, 0);
    lv_obj_add_flag(assistant_dialog_, LV_OBJ_FLAG_CLICKABLE);

    auto dialog_header = Box(assistant_dialog_, 0, 0, 1072, 108, 0xdff8f0);
    lv_obj_set_style_radius(dialog_header, 33, 0);
    lv_obj_set_style_bg_grad_color(dialog_header, ThemeFill(0xe3f2ff), 0);
    lv_obj_set_style_bg_grad_dir(dialog_header, LV_GRAD_DIR_HOR, 0);
    auto dialog_avatar = Box(dialog_header, 24, 16, 76, 76, 0xd8f4ea);
    lv_obj_set_style_radius(dialog_avatar, 24, 0);
    lv_obj_set_style_border_width(dialog_avatar, 3, 0);
    lv_obj_set_style_border_color(dialog_avatar, ThemeBorder(0xffffff), 0);
    auto dialog_avatar_image = Image(dialog_avatar, &han_art_book, 0, 0);
    lv_image_set_scale(dialog_avatar_image, 90);
    lv_image_set_pivot(dialog_avatar_image, 0, 0);
    lv_obj_align(dialog_avatar_image, LV_ALIGN_CENTER, 0, 2);
    auto dialog_title = Label(dialog_header, "小智对话", 120, 10, 310, &han_font_40);
    lv_obj_set_height(dialog_title, 48);
    auto dialog_subtitle = Label(dialog_header, "我在听，请继续说", 120, 58, 370);
    lv_obj_set_height(dialog_subtitle, 36);
    assistant_dialog_status_box_ = Box(dialog_header, 624, 26, 188, 56, 0xccefe0);
    lv_obj_set_style_radius(assistant_dialog_status_box_, 28, 0);
    assistant_dialog_status_ = Label(assistant_dialog_status_box_, "聆听中", 52, 0, 128);
    lv_obj_set_style_text_align(assistant_dialog_status_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(assistant_dialog_status_, LV_ALIGN_CENTER, 22, -1);
    assistant_dialog_mic_badge_ = Box(assistant_dialog_status_box_, 10, 8, 40, 40, 0x237b5c);
    lv_obj_set_style_radius(assistant_dialog_mic_badge_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(assistant_dialog_mic_badge_, 2, 0);
    lv_obj_set_style_border_color(assistant_dialog_mic_badge_, ThemeBorder(0xffffff), 0);
    auto dialog_mic = Image(assistant_dialog_mic_badge_, &han_status_mic, 0, 0);
    lv_image_set_scale(dialog_mic, 160);
    lv_image_set_pivot(dialog_mic, 0, 0);
    lv_obj_align(dialog_mic, LV_ALIGN_CENTER, 0, 0);

    auto dialog_stop = Button(dialog_header, "停止对话", 832, 18, 214, 72, 0xff7e73, 15);
    lv_obj_set_style_radius(dialog_stop, 30, 0);
    lv_obj_set_style_shadow_color(dialog_stop, ThemeShadow(0xe55e58), 0);
    lv_obj_set_style_shadow_width(dialog_stop, 12, 0);
    lv_obj_set_style_shadow_opa(dialog_stop, LV_OPA_20, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(dialog_stop, 0), ThemeText(0xffffff), 0);

    assistant_dialog_history_ = Box(assistant_dialog_, 22, 124, 1028, 354, 0xffffff);
    lv_obj_set_style_radius(assistant_dialog_history_, 28, 0);
    lv_obj_set_style_border_width(assistant_dialog_history_, 2, 0);
    lv_obj_set_style_border_color(assistant_dialog_history_, ThemeBorder(0xd7e7f1), 0);
    lv_obj_add_flag(assistant_dialog_history_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(assistant_dialog_history_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(assistant_dialog_history_, LV_DIR_VER);
    lv_obj_add_flag(assistant_dialog_history_, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scrollbar_mode(assistant_dialog_history_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(assistant_dialog_history_, 10, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(assistant_dialog_history_, 5, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(assistant_dialog_history_, ThemeFill(0x58aaf4), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(assistant_dialog_history_, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_bottom(assistant_dialog_history_, 18, 0);

    assistant_dialog_navigation_ = Box(assistant_dialog_, 56, 370, 960, 92, 0xfff2d8);
    lv_obj_set_style_radius(assistant_dialog_navigation_, 23, 0);
    lv_obj_set_style_border_width(assistant_dialog_navigation_, 2, 0);
    lv_obj_set_style_border_color(assistant_dialog_navigation_, ThemeBorder(0xf0d49a), 0);
    assistant_dialog_navigation_title_ =
        Label(assistant_dialog_navigation_, "正在打开“小小字典”", 24, 10, 912);
    lv_obj_set_height(assistant_dialog_navigation_title_, 38);
    assistant_dialog_navigation_detail_ =
        Label(assistant_dialog_navigation_, "打开汉字并自动播放笔顺", 24, 49, 912);
    lv_obj_set_height(assistant_dialog_navigation_detail_, 36);
    lv_obj_set_style_text_color(assistant_dialog_navigation_detail_, ThemeText(0x735829), 0);
    lv_obj_add_flag(assistant_dialog_navigation_, LV_OBJ_FLAG_HIDDEN);

    auto dialog_hint_bar = Box(assistant_dialog_, 22, 494, 1028, 92, 0xfff7e8);
    lv_obj_set_style_radius(dialog_hint_bar, 28, 0);
    lv_obj_set_style_border_width(dialog_hint_bar, 1, 0);
    lv_obj_set_style_border_color(dialog_hint_bar, ThemeBorder(0xf2dfbd), 0);
    const int wave_heights[] = {20, 34, 54, 72, 54, 34, 20};
    const uint32_t wave_colors[] = {0x43c47a, 0x43c47a, 0x58aaf4, 0x338ef0,
                                    0x58aaf4, 0x43c47a, 0x43c47a};
    for (int index = 0; index < 7; ++index) {
        auto bar = Box(dialog_hint_bar, 346 + index * 18, (92 - wave_heights[index]) / 2, 8,
                       wave_heights[index], wave_colors[index]);
        lv_obj_set_style_radius(bar, 4, 0);
    }
    assistant_dialog_hint_ = Label(dialog_hint_bar, "正在聆听，请继续说…", 500, 27, 420);
    lv_obj_set_height(assistant_dialog_hint_, 42);
    ApplyDynamicTextFont(assistant_dialog_navigation_title_);
    ApplyDynamicTextFont(assistant_dialog_navigation_detail_);
    ApplyDynamicTextFont(assistant_dialog_hint_);
    lv_obj_add_flag(assistant_dialog_scrim_, LV_OBJ_FLAG_HIDDEN);
    tick_ = lv_timer_create(Tick, 800, this);
    // This timer is re-aligned to the next monotonic second boundary after every callback.
    timer_tick_ = lv_timer_create(TimerTick, 1000, this);
    // Poll the wall clock without accumulating timer error. The callback is intentionally cheap
    // while the full-screen clock is not visible.
    clock_tick_ = lv_timer_create(ClockTick, 100, this);
    // LoadPreferences() resolves the persisted/automatic mode before objects are created. Apply
    // the few permanent image and shared-chrome styles now; page-owned controls are themed while
    // they are built by Render().
    ApplyTheme(dark_theme_, false);
    Render(Page::Home);
    Queue(2, "");  // Read optional timetable/weather content off the LVGL/main tasks.
    Queue(6, "");  // Load the optional indexed-dictionary font on the worker task.
#ifndef HAN_UI_HOST_SIM
    han::MqttMessageBoard::GetInstance().SetChangedCallback([this] { mqtt_board_dirty_ = true; });
    Queue(11, "");  // Load the optional MQTT message-board configuration from microSD.
#endif
}

void HanDisplay::SetTheme(Theme* theme) {
    // Product artwork and headings keep their fixed fonts. Text received from the service uses
    // the common Noto font so the server's dynamic glyph fallback can fill uncommon characters.
    DisplayLockGuard guard(this);
    current_theme_ = theme;
    lv_obj_set_style_text_font(status_label_, &han_font_28, 0);
    lv_obj_set_style_text_font(notification_label_, &han_font_28, 0);
    RebuildAssistantHistory();
    ApplyDynamicTextFont(assistant_dialog_hint_);
    ApplyDynamicTextFont(assistant_dialog_navigation_title_);
    ApplyDynamicTextFont(assistant_dialog_navigation_detail_);
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
    const bool connecting = status && strcmp(status, Lang::Strings::CONNECTING) == 0;
    const bool standby = status && strcmp(status, Lang::Strings::STANDBY) == 0;
    if (strcmp(shown, Lang::Strings::STANDBY) == 0)
        shown = "等待唤醒";
    LvglDisplay::SetStatus(shown);
#else
    const bool connecting = strstr(shown, "连接") != nullptr;
    const bool standby = strstr(shown, "唤醒") != nullptr;
    if (!shown[0])
        shown = "等待唤醒";
    MipiLcdDisplay::SetStatus(shown);
#endif
    DisplayLockGuard guard(this);
    const bool listening = strstr(shown, "听") != nullptr;
    const bool speaking = strstr(shown, "说") || strstr(shown, "回答");
    if (status_box_) {
        lv_obj_set_style_bg_color(status_box_,
                                  ThemeFill(listening  ? 0xd8f5e5
                                               : speaking ? 0xffecd9
                                                          : 0xe9f4ff),
                                  0);
        lv_obj_set_style_bg_grad_color(status_box_,
                                       ThemeFill(listening  ? 0xbfead6
                                                    : speaking ? 0xffddcf
                                                               : 0xe4f8ef),
                                       0);
        if (status_mic_badge_) {
            lv_obj_set_style_bg_color(status_mic_badge_,
                                      ThemeFill(listening  ? 0x237b5c
                                                   : speaking ? 0xb95635
                                                              : 0x477da8),
                                      0);
        }
    }
    if (page_ != Page::Home && (connecting || listening || speaking)) {
        if (!assistant_dialog_active_) {
            lv_obj_add_flag(assistant_dialog_navigation_, LV_OBJ_FLAG_HIDDEN);
            assistant_navigation_pending_ = false;
            assistant_dialog_hide_at_ms_ = 0;
        }
        ShowAssistantDialog();
        const char* dialog_status = listening ? "聆听中" : speaking ? "正在回答" : "连接中";
        lv_label_set_text(assistant_dialog_status_, dialog_status);
        const uint32_t dialog_color = listening ? 0x237b5c : speaking ? 0xb95635 : 0x477da8;
        const uint32_t dialog_pill = listening ? 0xccefe0 : speaking ? 0xffdfcf : 0xd9eaf8;
        lv_obj_set_style_bg_color(assistant_dialog_mic_badge_, ThemeFill(dialog_color), 0);
        lv_obj_set_style_bg_color(assistant_dialog_status_box_, ThemeFill(dialog_pill), 0);
        lv_label_set_text(assistant_dialog_hint_, listening  ? "正在聆听，请继续说…"
                                                  : speaking ? "小智正在回答，请稍候…"
                                                             : "正在连接小智…");
    } else if (standby && !assistant_navigation_pending_) {
        HideAssistantDialog();
    }
}

void HanDisplay::SetChatMessage(const char* role, const char* text) {
    DisplayLockGuard guard(this);
    if (!message_ || !role_label_ || !assistant_card_)
        return;
    const char* shown = text ? text : "";
    const char* who = "小智";
    uint32_t color = 0xf4fbff;
    uint32_t gradient = 0xfffbef;
    uint32_t role_color = 0xc8e9ff;
    uint32_t role_gradient = 0xe3d8ff;
    if (!shown[0]) {
        shown = "说“你好小智”，我来帮你学习";
    } else if (role && strcmp(role, "user") == 0) {
        who = "我说";
        color = 0xeefaf4;
        gradient = 0xf9fff4;
        role_color = 0xbfead6;
        role_gradient = 0xdff6ee;
    } else if (role && strcmp(role, "system") == 0) {
        who = "系统";
        color = 0xfff8e9;
        gradient = 0xfffdf5;
        role_color = 0xffdfad;
        role_gradient = 0xffefcc;
        if (initial_banner_pending_) {
            shown = "正在准备屏幕、声音和网络，请稍候…";
            who = "准备中";
            initial_banner_pending_ = false;
        }
    }
    lv_obj_set_style_text_font(
        message_, role && strcmp(role, "assistant") == 0 ? DynamicTextFont() : &han_font_28, 0);
    lv_label_set_text(role_label_, who);
    lv_obj_align(role_label_, LV_ALIGN_CENTER, 0, -1);
    lv_label_set_text(message_, shown);
    lv_obj_set_style_bg_color(assistant_card_, ThemeFill(color), 0);
    lv_obj_set_style_bg_grad_color(assistant_card_, ThemeFill(gradient), 0);
    lv_obj_set_style_bg_color(role_box_, ThemeFill(role_color), 0);
    lv_obj_set_style_bg_grad_color(role_box_, ThemeFill(role_gradient), 0);

    const bool has_text = text && text[0];
    if (has_text && role && (strcmp(role, "user") == 0 || strcmp(role, "assistant") == 0)) {
        AppendAssistantHistory(role, text);
    }

    if (page_ == Page::Home || !assistant_dialog_)
        return;
    if (role && strcmp(role, "user") == 0 && has_text) {
        ShowAssistantDialog();
        lv_label_set_text(assistant_dialog_status_, "识别成功");
        lv_obj_set_style_bg_color(assistant_dialog_status_box_, ThemeFill(0xccefe0), 0);
        lv_obj_set_style_bg_color(assistant_dialog_mic_badge_, ThemeFill(0x237b5c), 0);
        lv_label_set_text(assistant_dialog_hint_, "我听见啦，正在想…");
    } else if (role && strcmp(role, "assistant") == 0 && has_text) {
        ShowAssistantDialog();
        lv_label_set_text(assistant_dialog_status_, "正在回答");
        lv_obj_set_style_bg_color(assistant_dialog_status_box_, ThemeFill(0xffdfcf), 0);
        lv_obj_set_style_bg_color(assistant_dialog_mic_badge_, ThemeFill(0xb95635), 0);
        lv_label_set_text(assistant_dialog_hint_, "小智正在回答，请稍候…");
    } else if (role && strcmp(role, "system") == 0) {
        ShowAssistantDialog();
        lv_label_set_text(assistant_dialog_hint_, "正在连接小智…");
    } else if (!has_text && !assistant_navigation_pending_) {
        HideAssistantDialog();
    }
}
void HanDisplay::ClearChatMessages() { SetChatMessage("", ""); }
void HanDisplay::Toast(const char* text) { ShowNotification(text, 4500); }

void HanDisplay::ShowAssistantDialog() {
    if (!assistant_dialog_scrim_ || page_ == Page::Home)
        return;
    if (assistant_dialog_history_ && lv_obj_get_child_count(assistant_dialog_history_) == 0)
        RebuildAssistantHistory();
    assistant_dialog_active_ = true;
    lv_obj_remove_flag(assistant_dialog_scrim_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(assistant_dialog_scrim_);
    lv_obj_invalidate(assistant_dialog_scrim_);
}

void HanDisplay::HideAssistantDialog() {
    assistant_dialog_active_ = false;
    if (assistant_dialog_scrim_)
        lv_obj_add_flag(assistant_dialog_scrim_, LV_OBJ_FLAG_HIDDEN);
}

void HanDisplay::AppendAssistantHistory(const char* role, const char* text) {
    if (!role || !text || !text[0])
        return;
    const bool user = strcmp(role, "user") == 0;
    if (!user && strcmp(role, "assistant") != 0)
        return;

    constexpr size_t kMaxMessageBytes = 768;
    std::string bounded(text);
    if (bounded.size() > kMaxMessageBytes) {
        size_t cut = kMaxMessageBytes;
        while (cut > 0 && (static_cast<unsigned char>(bounded[cut]) & 0xc0) == 0x80)
            --cut;
        bounded.resize(cut);
    }

    // TTS sends one callback per sentence. Join adjacent assistant sentences into a single chat
    // bubble, while every recognized user utterance starts a new turn.
    if (!user && !assistant_history_.empty() && !assistant_history_.back().user) {
        auto& previous = assistant_history_.back().text;
        if (previous != bounded && previous.size() + bounded.size() <= 1024)
            previous += bounded;
    } else {
        assistant_history_.push_back({user, std::move(bounded)});
    }
    constexpr size_t kMaxHistoryItems = 14;
    if (assistant_history_.size() > kMaxHistoryItems) {
        assistant_history_.erase(
            assistant_history_.begin(),
            assistant_history_.begin() + (assistant_history_.size() - kMaxHistoryItems));
    }
    RebuildAssistantHistory();
}

void HanDisplay::RebuildAssistantHistory() {
    if (!assistant_dialog_history_)
        return;
    lv_anim_delete(assistant_dialog_history_, nullptr);
    lv_obj_scroll_to_y(assistant_dialog_history_, 0, LV_ANIM_OFF);
    lv_obj_clean(assistant_dialog_history_);

    int y = 18;
    auto append_row = [this, &y](bool user, const std::string& text) {
        constexpr int kAssistantX = 82;
        constexpr int kAssistantWidth = 780;
        constexpr int kUserX = 318;
        constexpr int kUserWidth = 680;
        const int x = user ? kUserX : kAssistantX;
        const int width = user ? kUserWidth : kAssistantWidth;
        auto bubble = Box(assistant_dialog_history_, x, y, width, 88, user ? 0xe4f8ef : 0xeaf4ff);
        lv_obj_set_style_radius(bubble, 24, 0);
        lv_obj_set_style_border_width(bubble, 2, 0);
        lv_obj_set_style_border_color(bubble, ThemeBorder(user ? 0xb9e5d2 : 0xc4ddf5), 0);

        auto role_label = Label(bubble, user ? "我说" : "小智", 20, 9, width - 40);
        lv_obj_set_height(role_label, 34);
        lv_obj_set_style_text_color(role_label, ThemeText(user ? 0x177052 : 0x25689c), 0);
        lv_obj_set_style_text_align(role_label, user ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
        auto message_label = Label(bubble, text.c_str(), 20, 42, width - 40);
        ApplyDynamicTextFont(message_label);
        lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_height(message_label, LV_SIZE_CONTENT);
        lv_obj_set_style_text_line_space(message_label, 6, 0);
        lv_obj_update_layout(message_label);
        const int bubble_height =
            std::max(88, static_cast<int>(lv_obj_get_height(message_label)) + 58);
        lv_obj_set_height(bubble, bubble_height);

        if (!user) {
            auto avatar = Box(assistant_dialog_history_, 18, y + 8, 52, 52, 0xdaf5ec);
            lv_obj_set_style_radius(avatar, 18, 0);
            lv_obj_set_style_border_width(avatar, 2, 0);
            lv_obj_set_style_border_color(avatar, ThemeBorder(0xffffff), 0);
            auto avatar_image = Image(avatar, &han_art_book, 0, 0);
            lv_image_set_scale(avatar_image, 62);
            lv_image_set_pivot(avatar_image, 0, 0);
            lv_obj_align(avatar_image, LV_ALIGN_CENTER, 0, 1);
        }
        y += bubble_height + 18;
    };

    if (assistant_history_.empty()) {
        append_row(false, "你好呀，我在呢。叫我一声就可以开始聊天。");
    } else {
        for (const auto& item : assistant_history_)
            append_row(item.user, item.text);
    }
    lv_obj_update_layout(assistant_dialog_history_);
    constexpr int kHistoryViewportHeight = 354;
    const int scroll_y = std::max(0, y + 18 - kHistoryViewportHeight);
    lv_obj_scroll_to_y(assistant_dialog_history_, scroll_y, LV_ANIM_OFF);
}

void HanDisplay::StartPendingStrokePlayback() {
    if (!auto_play_stroke_pending_ || stroke_glyph_.character != entry_.character ||
        stroke_glyph_.strokes.empty())
        return;
    stroke_ = -1;
    stroke_playing_ = true;
    auto_play_stroke_pending_ = false;
}

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
    if (code == LV_EVENT_REFR_READY && self->lock_screen_backlight_pending_.exchange(false)) {
        // The lock screen has reached the panel. Restore the backlight only now so neither the
        // underlying page nor an intermediate black frame can become visible. In particular, do
        // not call lv_refr_now() from a UI callback: that can nest a refresh while a flip-clock
        // frame is still rendering.
        Board::GetInstance().GetBacklight()->SetBrightness(self->brightness_setting_);
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
    theme_render_pending_ = false;
    const bool entering_dictionary = page == Page::Dictionary && page_ != Page::Dictionary;
    for (auto& digit : flip_digits_)
        ResetFlipAnimation(digit);
    flip_date_ = flip_lunar_ = nullptr;
    flip_date_key_ = -1;
    flip_clock_initialized_ = false;
    page_ = page;
    if (page == Page::Home) {
        assistant_navigation_pending_ = false;
        assistant_dialog_hide_at_ms_ = 0;
        HideAssistantDialog();
    }
    if (page != Page::Dictionary)
        auto_play_stroke_pending_ = false;
    if (entering_dictionary)
        stroke_ = -1;
    CloseBatteryPopup();
    CloseWeatherIndexPopup();
    stroke_playing_ = false;
    timer_value_ = timer_progress_ = timer_today_value_ = nullptr;
    timer_plan_popup_ = nullptr;
    timer_plan_arcs_.fill(nullptr);
    timer_plan_values_.fill(nullptr);
    timer_last_rendered_second_ = -1;
    timer_week_bars_.fill(nullptr);
    stroke_value_ = stroke_image_ = network_info_ = network_detail_ = search_ = nullptr;
    glyph_title_image_ = glyph_title_placeholder_ = nullptr;
    search_overlay_ = search_input_ = search_results_ = search_status_ = nullptr;
    pinyin_page_label_ = nullptr;
    definition_overlay_ = nullptr;
    pinyin_tone_buttons_.fill(nullptr);
    brightness_value_ = volume_value_ = auto_lock_value_ = nullptr;
    brightness_slider_ = volume_slider_ = auto_lock_slider_ = nullptr;
    mqtt_settings_popup_ = nullptr;
    appearance_popup_ = nullptr;
    appearance_mode_buttons_.fill(nullptr);
    appearance_start_value_ = appearance_end_value_ = nullptr;
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
    const char* titles[] = {"小小助手", "小小字典", "英语音标", "课程表", "作业计时",
                            "闹钟",     "天气",     "设置",     "时钟"};
    const lv_image_dsc_t* page_icons[] = {
        nullptr,         &han_icon_dictionary, &han_icon_phonetics, &han_icon_timetable,
        &han_icon_timer, &han_icon_alarm,      &han_icon_weather,   &han_icon_settings,
        nullptr};
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
    lv_obj_remove_flag(title_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(back_, 24, 20);
    lv_obj_set_size(back_, 72, 72);
    lv_obj_set_style_radius(back_, 24, 0);
    lv_obj_set_pos(back_image_, 12, 12);
    lv_obj_set_pos(date_, 747, 37);
    lv_obj_set_width(date_, 208);
    lv_obj_set_style_text_font(date_, &han_font_28, 0);
    lv_obj_set_style_text_align(date_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_bg_opa(body_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_dir(body_, LV_GRAD_DIR_NONE, 0);
    if (page == Page::Network)
        lv_obj_add_flag(body_, LV_OBJ_FLAG_CLICKABLE);
    else
        lv_obj_remove_flag(body_, LV_OBJ_FLAG_CLICKABLE);

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
    } else if (page == Page::Clock) {
        lv_obj_remove_flag(back_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(title_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(page_icon_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mascot_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(date_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(clock_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(top_divider_left_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(top_divider_right_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(wifi_button_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_button_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(footer_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(assistant_card_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(back_, 28, 24);
        lv_obj_set_size(back_, 78, 78);
        lv_obj_set_style_radius(back_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(back_image_, 15, 15);
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
        lv_obj_set_pos(assistant_badge_, 14, page == Page::Home ? 16 : 7);
        lv_obj_set_size(assistant_badge_, page == Page::Home ? 80 : 76,
                        page == Page::Home ? 80 : 76);
        auto icon = lv_obj_get_child(assistant_badge_, 0);
        lv_obj_set_pos(icon, page == Page::Home ? 3 : 2, page == Page::Home ? 12 : 11);
        lv_image_set_scale(icon, page == Page::Home ? 100 : 96);
        lv_obj_set_pos(role_box_, 112, page == Page::Home ? 17 : 9);
        lv_obj_set_size(role_box_, 128, 34);
        lv_obj_set_pos(message_, 112, page == Page::Home ? 57 : 46);
        lv_obj_set_size(message_, 820, page == Page::Home ? 42 : 34);
        lv_label_set_long_mode(message_, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(status_box_, 958, page == Page::Home ? 22 : 12);
        lv_obj_set_size(status_box_, 256, 68);
        lv_obj_set_pos(status_label_, 12, 15);
        lv_obj_set_pos(notification_label_, 12, 15);
        lv_obj_set_width(status_label_, 178);
        lv_obj_set_width(notification_label_, 178);
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
        case Page::Clock:
            FlipClock();
            break;
    }
    if (page == Page::Timetable) {
        // body_ covers the full screen for this design, so keep the live root labels/buttons above
        // its decorative layers.
        lv_obj_move_foreground(back_);
        lv_obj_move_foreground(title_);
        lv_obj_move_foreground(date_);
    } else if (page == Page::Clock) {
        lv_obj_move_foreground(back_);
    }
    if (assistant_dialog_active_ || assistant_navigation_pending_)
        ShowAssistantDialog();
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
    const uint32_t dark_cards[] = {0x17372f, 0x2c284c, 0x17334c, 0x473225, 0x482a36, 0x183850};
    const uint32_t dark_gradients[] = {0x214d40, 0x3a3562, 0x234764,
                                       0x60422d, 0x603545, 0x24506e};
    for (int i = 0; i < 6; ++i) {
        auto card = Button(body_, "", (i % 3) * 405, (i / 3) * 236, 394, i < 3 ? 224 : 212, kBg, i);
        lv_obj_set_style_radius(card, 28, 0);
        lv_obj_set_style_clip_corner(card, true, 0);
        lv_obj_set_style_shadow_color(card, ThemeShadow(0xdacc9d), 0);
        lv_obj_set_style_shadow_width(card, 14, 0);
        lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
        lv_obj_set_style_shadow_ofs_y(card, 5, 0);
        if (dark_theme_) {
            lv_obj_set_style_bg_color(card, lv_color_hex(dark_cards[i]), 0);
            lv_obj_set_style_bg_grad_color(card, lv_color_hex(dark_gradients[i]), 0);
            lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_HOR, 0);
            lv_obj_set_style_border_width(card, 1, 0);
            lv_obj_set_style_border_color(card, lv_color_hex(kDarkBorder), 0);
            auto glow = Box(card, 246, 116, 188, 128, dark_gradients[i]);
            lv_obj_set_style_radius(glow, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(glow, LV_OPA_30, 0);
        } else {
            Image(card, panels[i], 0, 0);
        }
        auto img = Image(card, icons[i], xs[i], ys[i]);
        if (i == 0) {
            lv_image_set_scale(img, 230);
            lv_image_set_pivot(img, 0, 0);
            auto grid = Box(card, 237, 77, 122, 128, 0xfffefa);
            lv_obj_set_style_radius(grid, 12, 0);
            lv_obj_set_style_border_width(grid, 4, 0);
            lv_obj_set_style_border_color(grid, ThemeBorder(0xffffff), 0);
            Box(grid, 60, 5, 1, 116, 0x9ed7aa);
            Box(grid, 4, 63, 113, 1, 0x9ed7aa);
            auto character = Image(grid, &han_home_gui, 0, 3);
            if (dark_theme_) {
                // This glyph asset is black. Tint it explicitly so it remains legible on the
                // dark-blue practice grid used by the night theme.
                lv_obj_set_style_image_recolor(character, lv_color_hex(0xb9e3ff), 0);
                lv_obj_set_style_image_recolor_opa(character, LV_OPA_COVER, 0);
            }
        }
        if (i == 1) {
            auto ipa = Label(card, "/iː/", 155, 146, 110, &han_font_40);
            // The heading font does not contain IPA: use the existing verified phonetic font.
            lv_obj_set_style_text_font(ipa, &han_font_40, 0);
            lv_obj_set_style_text_align(ipa, LV_TEXT_ALIGN_CENTER, 0);
            // The white phonetic tile is baked into the headphone artwork, so its label must
            // stay dark even while the surrounding card uses the night palette.
            lv_obj_set_style_text_color(ipa, lv_color_hex(0x29456f), 0);
        }
        auto label = Label(card, titles[i], 34, 17, 350, &han_font_home);
        lv_obj_set_style_text_color(label, dark_theme_ ? lv_color_hex(kDarkText)
                                                       : lv_color_hex(colors[i]),
                                    0);
    }
}

void HanDisplay::ResetFlipAnimation(FlipDigit& digit) {
    lv_anim_delete(&digit, nullptr);
    for (auto** overlay : {&digit.old_top, &digit.old_bottom, &digit.new_bottom}) {
        if (*overlay) {
            lv_obj_delete(*overlay);
            *overlay = nullptr;
        }
    }
    digit.card = nullptr;
    digit.steady_label = nullptr;
    digit.value = -2;
}

void HanDisplay::FlipTopExec(void* value, int32_t scale) {
    auto digit = static_cast<FlipDigit*>(value);
    if (digit->old_top)
        lv_obj_set_style_transform_scale_y(digit->old_top, scale, 0);
}

void HanDisplay::FlipBottomExec(void* value, int32_t scale) {
    auto digit = static_cast<FlipDigit*>(value);
    if (digit->new_bottom)
        lv_obj_set_style_transform_scale_y(digit->new_bottom, scale, 0);
}

void HanDisplay::FlipTopCompleted(lv_anim_t* animation) {
    auto digit = static_cast<FlipDigit*>(lv_anim_get_user_data(animation));
    if (!digit || !digit->card || !digit->new_bottom)
        return;
    if (digit->old_top) {
        lv_obj_delete(digit->old_top);
        digit->old_top = nullptr;
    }
    lv_obj_remove_flag(digit->new_bottom, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t lower;
    lv_anim_init(&lower);
    lv_anim_set_var(&lower, digit);
    lv_anim_set_user_data(&lower, digit);
    lv_anim_set_exec_cb(&lower, FlipBottomExec);
    lv_anim_set_values(&lower, 20, 256);
    lv_anim_set_duration(&lower, 150);
    lv_anim_set_path_cb(&lower, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&lower, FlipBottomCompleted);
    lv_anim_start(&lower);
}

void HanDisplay::FlipBottomCompleted(lv_anim_t* animation) {
    auto digit = static_cast<FlipDigit*>(lv_anim_get_user_data(animation));
    if (!digit)
        return;
    if (digit->old_bottom) {
        lv_obj_delete(digit->old_bottom);
        digit->old_bottom = nullptr;
    }
    if (digit->new_bottom) {
        lv_obj_delete(digit->new_bottom);
        digit->new_bottom = nullptr;
    }
}

void HanDisplay::AnimateFlipDigit(FlipDigit& digit, int value) {
    if (!digit.card || !digit.steady_label)
        return;
    const int old_value = digit.value;
    lv_anim_delete(&digit, nullptr);
    for (auto** overlay : {&digit.old_top, &digit.old_bottom, &digit.new_bottom}) {
        if (*overlay) {
            lv_obj_delete(*overlay);
            *overlay = nullptr;
        }
    }
    auto text_for = [](int number, char (&text)[4]) -> const char* {
        if (number >= 0 && number <= 9) {
            text[0] = static_cast<char>('0' + number);
            text[1] = '\0';
            return text;
        }
        return "—";
    };
    char old_text[4]{};
    char new_text[4]{};
    SetTextIfChanged(digit.steady_label, text_for(value, new_text));
    digit.value = value;
    if (old_value < -1) {
        return;
    }

    constexpr int kCardWidth = 156;
    constexpr int kCardHeight = 280;
    constexpr int kHalfHeight = kCardHeight / 2;
    const int label_y = (kCardHeight - han_font_flip_digits.line_height) / 2;
    auto half = [&](bool bottom, const char* text, uint32_t color) {
        auto clip = Box(digit.card, 0, bottom ? kHalfHeight : 0, kCardWidth, kHalfHeight, color);
        lv_obj_set_style_radius(clip, 0, 0);
        auto label = Label(clip, text, 0, label_y - (bottom ? kHalfHeight : 0), kCardWidth,
                           &han_font_flip_digits);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, ThemeText(0xfff8e8), 0);
        return clip;
    };
    digit.old_top = half(false, text_for(old_value, old_text), 0x244a7e);
    digit.old_bottom = half(true, text_for(old_value, old_text), 0x193964);
    digit.new_bottom = half(true, text_for(value, new_text), 0x1d416f);
    lv_obj_add_flag(digit.new_bottom, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_transform_pivot_x(digit.old_top, kCardWidth / 2, 0);
    lv_obj_set_style_transform_pivot_y(digit.old_top, kHalfHeight, 0);
    lv_obj_set_style_transform_pivot_x(digit.new_bottom, kCardWidth / 2, 0);
    lv_obj_set_style_transform_pivot_y(digit.new_bottom, 0, 0);

    lv_anim_t upper;
    lv_anim_init(&upper);
    lv_anim_set_var(&upper, &digit);
    lv_anim_set_user_data(&upper, &digit);
    lv_anim_set_exec_cb(&upper, FlipTopExec);
    lv_anim_set_values(&upper, 256, 20);
    lv_anim_set_duration(&upper, 150);
    lv_anim_set_path_cb(&upper, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&upper, FlipTopCompleted);
    lv_anim_start(&upper);
}

void HanDisplay::UpdateFlipClock(const struct tm& local, bool valid_time, bool animate) {
    if (page_ != Page::Clock || screen_off_ || lock_screen_visible_ || !flip_date_ || !flip_lunar_)
        return;
    int values[6] = {-1, -1, -1, -1, -1, -1};
    if (valid_time) {
        values[0] = local.tm_hour / 10;
        values[1] = local.tm_hour % 10;
        values[2] = local.tm_min / 10;
        values[3] = local.tm_min % 10;
        values[4] = local.tm_sec / 10;
        values[5] = local.tm_sec % 10;
    }
    for (int index = 0; index < 6; ++index) {
        if (flip_digits_[index].value == values[index])
            continue;
        if (animate && flip_clock_initialized_)
            AnimateFlipDigit(flip_digits_[index], values[index]);
        else {
            char text[2] = {static_cast<char>('0' + std::max(values[index], 0)), '\0'};
            SetTextIfChanged(flip_digits_[index].steady_label, values[index] < 0 ? "—" : text);
            flip_digits_[index].value = values[index];
        }
    }
    flip_clock_initialized_ = true;

    const int64_t date_key = valid_time ? static_cast<int64_t>(local.tm_year + 1900) * 10000 +
                                              (local.tm_mon + 1) * 100 + local.tm_mday
                                        : -1;
    if (date_key == flip_date_key_)
        return;
    flip_date_key_ = date_key;
    if (!valid_time) {
        SetTextIfChanged(flip_date_, "日期待同步");
        SetTextIfChanged(flip_lunar_, "时间同步后显示农历");
        return;
    }
    constexpr const char* kFullWeekdays[] = {"星期日", "星期一", "星期二", "星期三",
                                             "星期四", "星期五", "星期六"};
    char date[64];
    snprintf(date, sizeof(date), "%d年%d月%d日    %s", local.tm_year + 1900, local.tm_mon + 1,
             local.tm_mday, kFullWeekdays[local.tm_wday]);
    SetTextIfChanged(flip_date_, date);
    han::LunarDate lunar;
    if (han::LunarFromGregorian(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, lunar)) {
        const auto text = han::FormatLunarDate(lunar);
        SetTextIfChanged(flip_lunar_, text.c_str());
    } else {
        SetTextIfChanged(flip_lunar_, "农历日期暂不可用");
    }
}

void HanDisplay::FlipClock() {
    lv_obj_set_style_bg_color(body_, ThemeFill(0xfff8eb), 0);
    lv_obj_set_style_bg_opa(body_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(body_, ThemeFill(0xdff4ff), 0);
    lv_obj_set_style_bg_grad_dir(body_, LV_GRAD_DIR_HOR, 0);

    // Soft, code-native cloud shapes keep the page light without adding another bitmap payload.
    for (const auto& cloud :
         {std::array<int, 4>{150, 92, 250, 64}, std::array<int, 4>{916, 62, 286, 72},
          std::array<int, 4>{-80, 642, 300, 86}, std::array<int, 4>{1060, 640, 300, 92}}) {
        auto shape = Box(body_, cloud[0], cloud[1], cloud[2], cloud[3], 0xffffff);
        lv_obj_set_style_radius(shape, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(shape, LV_OPA_70, 0);
        lv_obj_set_style_shadow_color(shape, ThemeShadow(0xffffff), 0);
        lv_obj_set_style_shadow_width(shape, 34, 0);
        lv_obj_set_style_shadow_opa(shape, LV_OPA_50, 0);
    }

    constexpr int kCardWidth = 156;
    constexpr int kCardHeight = 280;
    constexpr int kCardY = 170;
    constexpr int kPositions[] = {98, 262, 510, 674, 922, 1086};
    for (int index = 0; index < 6; ++index) {
        auto& digit = flip_digits_[index];
        digit.card = Card(body_, kPositions[index], kCardY, kCardWidth, kCardHeight, 0x193964);
        lv_obj_set_style_radius(digit.card, 27, 0);
        lv_obj_set_style_clip_corner(digit.card, true, 0);
        lv_obj_set_style_border_width(digit.card, 2, 0);
        lv_obj_set_style_border_color(digit.card, ThemeBorder(0x486d9e), 0);
        lv_obj_set_style_bg_grad_color(digit.card, ThemeFill(0x2c5488), 0);
        lv_obj_set_style_bg_grad_dir(digit.card, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_shadow_color(digit.card, ThemeShadow(0x62738d), 0);
        lv_obj_set_style_shadow_width(digit.card, 18, 0);
        lv_obj_set_style_shadow_opa(digit.card, LV_OPA_30, 0);
        const int label_y = (kCardHeight - han_font_flip_digits.line_height) / 2;
        digit.steady_label = Label(digit.card, "—", 0, label_y, kCardWidth, &han_font_flip_digits);
        lv_obj_set_style_text_align(digit.steady_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(digit.steady_label, ThemeText(0xfff8e8), 0);
        digit.value = -2;
    }
    for (int x : {442, 854}) {
        auto top = Box(body_, x + 11, 253, 22, 22, 0x7195f5);
        auto bottom = Box(body_, x + 11, 341, 22, 22, 0x7195f5);
        lv_obj_set_style_radius(top, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_radius(bottom, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_grad_color(top, ThemeFill(0xa9c0ff), 0);
        lv_obj_set_style_bg_grad_color(bottom, ThemeFill(0xa9c0ff), 0);
        lv_obj_set_style_bg_grad_dir(top, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_grad_dir(bottom, LV_GRAD_DIR_VER, 0);
    }

    flip_date_ = Label(body_, "日期待同步", 240, 493, 800, &han_font_flip_date);
    lv_obj_set_style_text_align(flip_date_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(flip_date_, ThemeText(0x173d72), 0);
    flip_lunar_ = Label(body_, "时间同步后显示农历", 240, 558, 800, &han_font_flip_date);
    lv_obj_set_style_text_align(flip_lunar_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(flip_lunar_, ThemeText(0x6684a9), 0);

    const auto now = time(nullptr);
    struct tm local{};
    localtime_r(&now, &local);
    UpdateFlipClock(local, local.tm_year >= 125, false);
}

void HanDisplay::Dictionary() {
    auto grid = Card(body_, 0, 0, 465, 440, 0xfffbf7);
    lv_obj_set_style_border_width(grid, 3, 0);
    lv_obj_set_style_border_color(grid, ThemeBorder(0xf6c2bd), 0);
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
        lv_obj_set_style_line_color(line, ThemeFill(0xf5c9c3), 0);
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
        lv_obj_set_style_shadow_color(control, ThemeShadow(control_colors[index]), 0);
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
    lv_obj_set_style_text_color(pinyin, ThemeText(0x182b50), 0);
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
        lv_obj_set_style_border_color(button, ThemeBorder(action_borders[index]), 0);
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
    lv_obj_set_style_border_color(search_overlay_, ThemeBorder(0xd7eee0), 0);
    auto search_title = Label(search_overlay_, "拼音查字", 24, 18, 190, &han_font_40);
    ApplyDictionaryTextFont(search_title);
    auto input_box = Box(search_overlay_, 220, 14, 450, 64, 0xf2f7fb);
    lv_obj_set_style_border_width(input_box, 2, 0);
    lv_obj_set_style_border_color(input_box, ThemeBorder(0xc9dfea), 0);
    search_input_ = Label(input_box, "输入拼音，例如 han", 20, 10, 410);
    ApplyDictionaryTextFont(search_input_);
    lv_obj_set_style_text_color(search_input_, ThemeText(kMuted), 0);
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
    lv_obj_set_style_border_color(definition_overlay_, ThemeBorder(0xf3d7bc), 0);

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
    lv_obj_set_style_bg_color(content, ThemeFill(0x79b9e8), LV_PART_SCROLLBAR);
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
                                  ThemeFill(selected ? kGreen : 0xf1f4f6), 0);
        lv_obj_set_style_border_width(pinyin_tone_buttons_[index], selected ? 2 : 0, 0);
        lv_obj_set_style_border_color(pinyin_tone_buttons_[index], ThemeBorder(0x8ad5a0), 0);
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
    lv_obj_set_style_text_color(search_status_, ThemeText(kMuted), 0);
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
        auto_play_stroke_pending_ = false;
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
    auto_play_stroke_pending_ = false;
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
    if (auto_play_stroke_pending_ && assistant_dialog_hide_at_ms_ == 0)
        StartPendingStrokePlayback();
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
            lv_obj_set_style_border_color(tab, ThemeBorder(0x8d6dd2), 0);
            lv_obj_set_style_text_color(tab_text, ThemeText(0xffffff), 0);
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
            lv_obj_set_style_border_color(btn, ThemeBorder(0x8e6ce4), 0);
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
    lv_obj_set_style_text_color(lv_obj_get_child(play, 0), ThemeText(0xffffff), 0);

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
        lv_obj_set_style_text_color(play_icon, ThemeText(0x8062df), 0);
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
            lv_obj_set_style_bg_grad_color(cell, ThemeFill(0xfffbf4), 0);
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

void HanDisplay::SetClockTimeForTest(int year, int month, int day, int hour, int minute,
                                     int second) {
    struct tm local{};
    local.tm_year = year - 1900;
    local.tm_mon = month - 1;
    local.tm_mday = day;
    local.tm_hour = hour;
    local.tm_min = minute;
    local.tm_sec = second;
    local.tm_wday = 0;
    mktime(&local);
    UpdateFlipClock(local, year >= 1900, false);
}

void HanDisplay::SetUsbStorageActiveForTest(bool active) {
    usb_storage_active_ = active;
    if (setup_ui_called_)
        Render(Page::Network);
}
#endif

void HanDisplay::Timer() {
    SyncTimerWeek();
    auto left = Card(body_, 0, 0, 694, 592, 0xffffff);
    lv_obj_set_style_radius(left, 30, 0);
    const uint32_t subject_colors[] = {0xe3f7ea, 0xddeeff, 0xeee5ff};
    const uint32_t subject_accents[] = {0x3bc476, 0x3389ed, 0x9254e8};
    const lv_image_dsc_t* subject_icons[] = {&han_subject_book, &han_subject_calculator,
                                             &han_subject_english};
    for (int i = 0; i < 3; ++i) {
        const bool selected = i == study_.subject();
        auto subject = Button(left, kSubjects[i], 18 + i * 226, 18, 206, 78,
                              selected ? 0x4a9df7 : subject_colors[i], 300 + i);
        lv_obj_set_style_radius(subject, 25, 0);
        lv_obj_set_style_border_width(subject, selected ? 4 : 0, 0);
        lv_obj_set_style_border_color(subject, ThemeBorder(0xffffff), 0);
        lv_obj_set_style_outline_width(subject, selected ? 3 : 0, 0);
        lv_obj_set_style_outline_color(subject, ThemeBorder(0xb9ddff), 0);
        if (selected) {
            lv_obj_set_style_bg_grad_color(subject, ThemeFill(0x3389ed), 0);
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
        lv_obj_set_style_text_color(subject_label, ThemeText(selected ? 0xffffff : kInk), 0);
        lv_image_set_scale(icon, 220);
        lv_image_set_pivot(icon, 0, 0);
    }

    auto plan = Button(left, "计划时间", 522, 110, 154, 54, 0xe9f1ff, 330);
    lv_obj_set_style_radius(plan, 22, 0);
    lv_obj_set_style_border_width(plan, 1, 0);
    lv_obj_set_style_border_color(plan, ThemeBorder(0xcbdcf2), 0);
    auto plan_icon = Image(plan, &han_icon_settings, 10, 14);
    lv_image_set_scale(plan_icon, 52);
    lv_image_set_pivot(plan_icon, 0, 0);
    auto plan_label = lv_obj_get_child(plan, 0);
    lv_obj_set_align(plan_label, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(plan_label, 36, 11);
    lv_obj_set_size(plan_label, 112, 34);
    lv_obj_set_style_text_font(plan_label, &han_font_timer, 0);
    lv_obj_set_style_text_color(plan_label, ThemeText(0x315f9e), 0);

    timer_progress_ = lv_arc_create(left);
    lv_obj_set_pos(timer_progress_, 179, 112);
    lv_obj_set_size(timer_progress_, 336, 336);
    lv_arc_set_rotation(timer_progress_, 270);
    lv_arc_set_bg_angles(timer_progress_, 0, 360);
    lv_arc_set_range(timer_progress_, 0, timer_plan_minutes_[study_.subject()] * 60);
    lv_obj_set_style_arc_width(timer_progress_, 24, LV_PART_MAIN);
    lv_obj_set_style_arc_color(timer_progress_, ThemeFill(0xe9edf2), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(timer_progress_, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(timer_progress_, 24, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(timer_progress_, ThemeFill(subject_accents[study_.subject()]),
                               LV_PART_INDICATOR);
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
    lv_obj_set_style_bg_grad_color(start, ThemeFill(0xff9b87), 0);
    lv_obj_set_style_bg_grad_dir(start, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(start, 0), &han_font_timer_title, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(start, 0), ThemeText(0xffffff), 0);
    auto complete = Button(left, "✓  完成本科", 498, 484, 178, 82, 0xffffff, 311);
    lv_obj_set_style_radius(complete, 41, 0);
    lv_obj_set_style_border_width(complete, 2, 0);
    lv_obj_set_style_border_color(complete, ThemeBorder(0xe3e0d9), 0);
    lv_obj_set_style_text_font(lv_obj_get_child(complete, 0), &han_font_timer, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(complete, 0), ThemeText(0x167b50), 0);

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
        lv_obj_set_style_border_color(row, ThemeBorder(0xf1eadf), 0);
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
                                    ThemeText(viewing_today && study_.completed(i) ? 0x27865c
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
            day, ThemeText(selected_day || i == timer_today_index_ ? 0x247a50 : kInk), 0);
    }
    UpdateTimer();
}

void HanDisplay::ShowTimerPlanPopup() {
    if (timer_plan_popup_ || !body_ || page_ != Page::Timer)
        return;
    timer_plan_draft_ = timer_plan_minutes_;
    timer_plan_popup_ = Box(body_, 0, 0, 1232, 592, 0x07111f);
    lv_obj_set_style_radius(timer_plan_popup_, 0, 0);
    lv_obj_set_style_bg_opa(timer_plan_popup_, LV_OPA_70, 0);
    lv_obj_add_flag(timer_plan_popup_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(timer_plan_popup_);

    auto panel = Card(timer_plan_popup_, 112, 22, 1008, 548, 0xffffff);
    lv_obj_set_style_radius(panel, 34, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, ThemeBorder(0xe4e9f0), 0);
    lv_obj_set_style_shadow_color(panel, ThemeShadow(0x52637a), 0);
    lv_obj_set_style_shadow_width(panel, 24, 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_20, 0);

    auto title = Label(panel, "计划完成时间", 0, 20, 1008, &han_font_timer_title);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    const uint32_t chip_colors[] = {0xe0f7e8, 0xe0eeff, 0xeee3ff};
    const uint32_t accents[] = {0x38c573, 0x2f8df4, 0x9653e8};
    const lv_image_dsc_t* icons[] = {&han_subject_book, &han_subject_calculator,
                                     &han_subject_english};
    for (int subject = 0; subject < 3; ++subject) {
        const int x = 70 + subject * 300;
        auto chip = Box(panel, x, 78, 220, 58, chip_colors[subject]);
        lv_obj_set_style_radius(chip, 29, 0);
        auto icon = Image(chip, icons[subject], 40, 8);
        lv_image_set_scale(icon, 205);
        lv_image_set_pivot(icon, 0, 0);
        auto subject_label = Label(chip, kSubjects[subject], 91, 11, 92, &han_font_timer);
        lv_obj_set_style_text_align(subject_label, LV_TEXT_ALIGN_CENTER, 0);

        auto arc = lv_arc_create(panel);
        timer_plan_arcs_[subject] = arc;
        // Vertically centre the 220 px ring in the 302 px space between the subject chip
        // (ending at y=136) and the action buttons (starting at y=438).
        lv_obj_set_pos(arc, x, 177);
        lv_obj_set_size(arc, 220, 220);
        lv_arc_set_rotation(arc, 135);
        lv_arc_set_bg_angles(arc, 0, 270);
        lv_arc_set_range(arc, 30, 99);
        lv_arc_set_value(arc, timer_plan_draft_[subject]);
        lv_obj_set_style_arc_width(arc, 18, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, ThemeFill(chip_colors[subject]), LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 20, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, ThemeFill(accents[subject]), LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(arc, ThemeFill(accents[subject]), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(arc, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_border_width(arc, 5, LV_PART_KNOB);
        lv_obj_set_style_border_color(arc, ThemeBorder(0xffffff), LV_PART_KNOB);
        lv_obj_set_style_pad_all(arc, 8, LV_PART_KNOB);
        lv_obj_set_style_shadow_color(arc, ThemeShadow(accents[subject]), LV_PART_KNOB);
        lv_obj_set_style_shadow_width(arc, 10, LV_PART_KNOB);
        lv_obj_set_style_shadow_opa(arc, LV_OPA_30, LV_PART_KNOB);
        lv_obj_add_event_cb(arc, OnTimerPlanChanged, LV_EVENT_VALUE_CHANGED, this);

        timer_plan_values_[subject] =
            Label(panel, "", x, 238, 220, &han_font_weather_hero);
        lv_obj_set_height(timer_plan_values_[subject], 70);
        lv_obj_set_style_text_align(timer_plan_values_[subject], LV_TEXT_ALIGN_CENTER, 0);
        auto unit = Label(panel, "分钟", x, 306, 220, &han_font_timer);
        lv_obj_set_style_text_align(unit, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_clear_flag(timer_plan_values_[subject], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(unit, LV_OBJ_FLAG_CLICKABLE);
    }

    auto cancel = Button(panel, "取消", 168, 438, 318, 78, 0xe9eef6, 331);
    lv_obj_set_style_radius(cancel, 28, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(cancel, 0), &han_font_timer_title, 0);
    auto save = Button(panel, "保存", 522, 438, 318, 78, 0x258cf4, 332);
    lv_obj_set_style_radius(save, 28, 0);
    lv_obj_set_style_bg_grad_color(save, ThemeFill(0x197be8), 0);
    lv_obj_set_style_bg_grad_dir(save, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(save, 0), &han_font_timer_title, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(save, 0), ThemeText(0xffffff), 0);
    UpdateTimerPlanPopup();
}

void HanDisplay::UpdateTimerPlanPopup() {
    for (int subject = 0; subject < 3; ++subject) {
        if (timer_plan_arcs_[subject] &&
            lv_arc_get_value(timer_plan_arcs_[subject]) != timer_plan_draft_[subject])
            lv_arc_set_value(timer_plan_arcs_[subject], timer_plan_draft_[subject]);
        if (timer_plan_values_[subject]) {
            const auto value = std::to_string(timer_plan_draft_[subject]);
            SetTextIfChanged(timer_plan_values_[subject], value.c_str());
        }
    }
}

void HanDisplay::CloseTimerPlanPopup() {
    if (timer_plan_popup_)
        lv_obj_delete(timer_plan_popup_);
    timer_plan_popup_ = nullptr;
    timer_plan_arcs_.fill(nullptr);
    timer_plan_values_.fill(nullptr);
}

void HanDisplay::SaveTimerPlan() {
#ifndef HAN_UI_HOST_SIM
    const auto minutes = timer_plan_minutes_;
    Application::GetInstance().Schedule([minutes] {
        Settings settings("han_study", true);
        for (int subject = 0; subject < 3; ++subject)
            settings.SetInt("p" + std::to_string(subject), minutes[subject]);
    });
#endif
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
    if (timer_progress_) {
        const int64_t target_ms =
            static_cast<int64_t>(timer_plan_minutes_[study_.subject()]) * 60 * 1000;
        lv_arc_set_value(timer_progress_,
                         static_cast<int>(std::min(current_elapsed, target_ms) / 1000));
    }
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
    lv_obj_set_style_border_color(status, ThemeBorder(alarm_enabled_ ? 0xb6e9c9 : 0xdce3ea), 0);
    lv_obj_set_style_shadow_color(status, ThemeShadow(alarm_enabled_ ? 0xa2dfb7 : 0xcbd3dc), 0);
    lv_obj_set_style_shadow_width(status, 10, 0);
    lv_obj_set_style_shadow_opa(status, LV_OPA_30, 0);
    auto status_icon = Box(status, 15, 10, 38, 38, alarm_enabled_ ? 0x28c978 : 0xb3bdca);
    lv_obj_set_style_radius(status_icon, LV_RADIUS_CIRCLE, 0);
    auto check = Label(status_icon, alarm_enabled_ ? "✓" : "—", 2, 0, 34, &han_font_weather);
    lv_obj_set_style_text_align(check, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(check, ThemeText(0xffffff), 0);
    auto status_text = Label(status,
                             alarm_ringing_.load() ? "正在响铃"
                             : alarm_enabled_      ? "闹钟已开启"
                                                   : "闹钟未开启",
                             61, 9, 240, &han_font_weather);
    lv_obj_set_style_text_align(status_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_text, ThemeText(alarm_enabled_ ? 0x128349 : 0x77849a), 0);

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
        lv_obj_set_style_text_color(roller, ThemeText(0x8c9ab2), 0);
        lv_obj_set_style_radius(roller, 26, 0);
        lv_obj_set_style_border_width(roller, 2, 0);
        lv_obj_set_style_border_color(roller, ThemeBorder(0xc9def1), 0);
        lv_obj_set_style_bg_color(roller, ThemeFill(0xf2f8fd), 0);
        lv_obj_set_style_bg_color(roller, ThemeFill(0xccecff), LV_PART_SELECTED);
        lv_obj_set_style_bg_grad_color(roller, ThemeFill(0xaedbff), LV_PART_SELECTED);
        lv_obj_set_style_bg_grad_dir(roller, LV_GRAD_DIR_VER, LV_PART_SELECTED);
        lv_obj_set_style_text_color(roller, ThemeText(kInk), LV_PART_SELECTED);
        lv_obj_set_style_radius(roller, 18, LV_PART_SELECTED);
        lv_obj_set_style_border_width(roller, 1, LV_PART_SELECTED);
        lv_obj_set_style_border_color(roller, ThemeBorder(0x94cdf6), LV_PART_SELECTED);
        lv_obj_add_flag(roller, LV_OBJ_FLAG_SCROLL_MOMENTUM);
        lv_roller_set_visible_row_count(roller, 3);
        x += 350;
    }
    auto separator = Label(panel, ":", 778, 114, 48, &han_font_weather_hero);
    lv_obj_set_style_text_align(separator, LV_TEXT_ALIGN_CENTER, 0);

    // Keep the time selector, repeat days and ringtone selector as three clearly separated rows.
    // The larger gaps are intentional: weekday chips used to sit too close to the ringtone chips
    // and were easy to hit by mistake on the physical display.
    for (int day = 0; day < 7; ++day) {
        const bool selected = (alarm_days_ & (1U << day)) != 0;
        auto button = Button(panel, kWeekdays[day], 488 + day * 101, 309, 90, 55,
                             selected ? 0xd2f8df : 0xf1f3f6, 410 + day);
        lv_obj_set_style_border_width(button, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(button, ThemeBorder(selected ? 0x8bdfaa : 0xdce3ea), 0);
        auto day_label = lv_obj_get_child(button, 0);
        lv_obj_set_style_text_font(day_label, &han_font_weather, 0);
        lv_obj_set_style_text_color(day_label, ThemeText(selected ? 0x078447 : 0x7b879d), 0);
    }

    auto ringtone_label = Label(panel, "铃声", 488, 401, 64, &han_font_weather);
    lv_obj_set_style_text_color(ringtone_label, ThemeText(0x536782), 0);
    auto ringtone_dropdown = lv_dropdown_create(panel);
    lv_obj_set_pos(ringtone_dropdown, 554, 390);
    lv_obj_set_size(ringtone_dropdown, 420, 60);
    lv_dropdown_set_options(ringtone_dropdown, "晨光\n轻步\n小铃\n启程");
    lv_dropdown_set_selected(ringtone_dropdown, alarm_ringtone_);
    lv_dropdown_set_dir(ringtone_dropdown, LV_DIR_TOP);
    lv_dropdown_set_symbol(ringtone_dropdown, "▽");
    lv_obj_set_style_text_font(ringtone_dropdown, DynamicTextFont(), 0);
    lv_obj_set_style_text_color(ringtone_dropdown, ThemeText(0x5532b7), 0);
    lv_obj_set_style_text_align(ringtone_dropdown, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_bg_color(ringtone_dropdown, ThemeFill(0xeee8ff), 0);
    lv_obj_set_style_bg_opa(ringtone_dropdown, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ringtone_dropdown, 22, 0);
    lv_obj_set_style_border_width(ringtone_dropdown, 2, 0);
    lv_obj_set_style_border_color(ringtone_dropdown, ThemeBorder(0xb9a5ef), 0);
    lv_obj_set_style_pad_left(ringtone_dropdown, 24, 0);
    lv_obj_set_style_pad_right(ringtone_dropdown, 20, 0);
    auto ringtone_list = lv_dropdown_get_list(ringtone_dropdown);
    lv_obj_set_style_text_font(ringtone_list, DynamicTextFont(), 0);
    lv_obj_set_style_text_color(ringtone_list, ThemeText(0x455472), 0);
    lv_obj_set_style_text_line_space(ringtone_list, 12, 0);
    lv_obj_set_style_bg_color(ringtone_list, ThemeFill(0xfffcff), 0);
    lv_obj_set_style_bg_opa(ringtone_list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ringtone_list, 20, 0);
    lv_obj_set_style_border_width(ringtone_list, 2, 0);
    lv_obj_set_style_border_color(ringtone_list, ThemeBorder(0xcbbcf3), 0);
    lv_obj_set_style_bg_color(ringtone_list, ThemeFill(0xe6dcff), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(ringtone_list, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_color(ringtone_list, ThemeText(0x5532b7), LV_PART_SELECTED);
    lv_obj_add_event_cb(ringtone_dropdown, OnAlarmRingtoneChanged, LV_EVENT_VALUE_CHANGED, this);

    auto preview = Button(panel, alarm_previewing_.load() ? "停止试听" : "试听", 992, 390, 188, 60,
                          alarm_previewing_.load() ? 0xffdce6 : 0xdceeff, 430);
    lv_obj_set_style_radius(preview, 20, 0);
    lv_obj_set_style_border_width(preview, 1, 0);
    lv_obj_set_style_border_color(preview,
                                  ThemeBorder(alarm_previewing_.load() ? 0xffaac2 : 0xadd8f6), 0);
    lv_obj_set_style_text_font(lv_obj_get_child(preview, 0), &han_font_28, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(preview, 0),
                                ThemeText(alarm_previewing_.load() ? 0xb8003b : 0x286da8), 0);

    auto save = Button(panel, "保存并开启", 488, 480, 330, 72, 0x28cf7d, 400);
    auto disable = Button(panel, alarm_ringing_.load() ? "停止铃声" : "关闭闹钟", 850, 480, 330, 72,
                          0xffabc5, 401);
    lv_obj_set_style_bg_grad_color(save, ThemeFill(0x08b966), 0);
    lv_obj_set_style_bg_grad_dir(save, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(save, 2, 0);
    lv_obj_set_style_border_color(save, ThemeBorder(0x70e4a8), 0);
    lv_obj_set_style_shadow_color(save, ThemeShadow(0x28c978), 0);
    lv_obj_set_style_shadow_width(save, 14, 0);
    lv_obj_set_style_shadow_opa(save, LV_OPA_30, 0);
    lv_obj_set_style_bg_grad_color(disable, ThemeFill(0xff8fb3), 0);
    lv_obj_set_style_bg_grad_dir(disable, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(disable, 2, 0);
    lv_obj_set_style_border_color(disable, ThemeBorder(0xffc7d8), 0);
    lv_obj_set_style_shadow_color(disable, ThemeShadow(0xff99b9), 0);
    lv_obj_set_style_shadow_width(disable, 14, 0);
    lv_obj_set_style_shadow_opa(disable, LV_OPA_30, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(save, 0), &han_font_weather, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(disable, 0), &han_font_weather, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(save, 0), ThemeText(0xffffff), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(disable, 0), ThemeText(0xb8003b), 0);
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
        lv_obj_set_style_text_color(path, ThemeText(0x3976a8), 0);
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
            lv_obj_set_style_text_color(update, ThemeText(kMuted), 0);
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

void HanDisplay::CacheUsbStorageArtwork() {
#ifndef HAN_UI_HOST_SIM
    if (!usb_storage_art_data_.empty())
        return;
    struct Candidate {
        const char* path;
        int width;
        int height;
    };
    constexpr Candidate candidates[] = {
        {"/sdcard/handict/ui/graphics/settings-page/usb-storage-mode.png", 220, 184},
        {"/sdcard/handict/ui/graphics/settings-page/storage-usb.png", 160, 160},
    };
    for (const auto& candidate : candidates) {
        auto file = fopen(candidate.path, "rb");
        if (!file)
            continue;
        if (fseek(file, 0, SEEK_END) != 0) {
            fclose(file);
            continue;
        }
        const long size = ftell(file);
        rewind(file);
        if (size <= 0 || size > 256 * 1024) {
            fclose(file);
            continue;
        }
        std::vector<uint8_t> data(static_cast<size_t>(size));
        const bool read = fread(data.data(), 1, data.size(), file) == data.size();
        fclose(file);
        if (!read)
            continue;
        usb_storage_art_data_ = std::move(data);
        usb_storage_art_ = {};
        usb_storage_art_.header.magic = LV_IMAGE_HEADER_MAGIC;
        usb_storage_art_.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;
        usb_storage_art_.header.w = candidate.width;
        usb_storage_art_.header.h = candidate.height;
        usb_storage_art_.data_size = static_cast<uint32_t>(usb_storage_art_data_.size());
        usb_storage_art_.data = usb_storage_art_data_.data();
        break;
    }
#endif
}

void HanDisplay::Network() {
    if (!usb_storage_active_)
        CacheUsbStorageArtwork();
    if (usb_storage_active_) {
        auto card = Card(body_, 0, 0, 1232, 592, 0xfdfefe);
        auto header = Box(card, 22, 20, 1188, 96, 0xe4f4ff);
        lv_obj_set_style_radius(header, 30, 0);
        Label(header, "USB 读卡器已开启", 30, 20, 600, &han_font_40);
        auto connected = Box(header, 750, 17, 406, 62, 0xcff2df);
        lv_obj_set_style_radius(connected, 28, 0);
        auto connected_label = Label(connected, "电脑可以访问 microSD 卡", 12, 11, 382);
        lv_obj_set_style_text_align(connected_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(connected_label, ThemeText(0x137249), 0);

        auto visual = Box(card, 22, 134, 414, 326, 0xeaf8f5);
        lv_obj_set_style_radius(visual, 30, 0);
#ifndef HAN_UI_HOST_SIM
        if (!usb_storage_art_data_.empty()) {
            auto art = Image(visual, &usb_storage_art_, 0, 0);
            lv_image_set_scale(art, usb_storage_art_.header.w > 180 ? 300 : 360);
            lv_image_set_pivot(art, 0, 0);
            lv_obj_align(art, LV_ALIGN_TOP_MID, 0, 24);
        } else {
            auto fallback_art = Image(visual, &han_icon_settings, 0, 0);
            lv_image_set_scale(fallback_art, 520);
            lv_image_set_pivot(fallback_art, 0, 0);
            lv_obj_align(fallback_art, LV_ALIGN_TOP_MID, 0, 36);
        }
#else
        auto fallback_art = Image(visual, &han_icon_settings, 0, 0);
        lv_image_set_scale(fallback_art, 520);
        lv_image_set_pivot(fallback_art, 0, 0);
        lv_obj_align(fallback_art, LV_ALIGN_TOP_MID, 0, 36);
#endif
        auto visual_title = Label(visual, "microSD 卡已连接", 12, 262, 390);
        lv_obj_set_style_text_align(visual_title, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(visual_title, ThemeText(0x174e73), 0);

        auto steps = Box(card, 456, 134, 754, 326, 0xfff8e9);
        lv_obj_set_style_radius(steps, 30, 0);
        Label(steps, "使用完成后", 30, 18, 680, &han_font_40);
        const char* step_text[] = {"完成复制或格式化", "在电脑上安全弹出", "点“重启并恢复”"};
        for (int index = 0; index < 3; ++index) {
            auto badge = Box(steps, 32, 84 + index * 72, 50, 50, 0x52b9ee);
            lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
            char number[2] = {static_cast<char>('1' + index), '\0'};
            auto number_label = Label(badge, number, 0, 5, 50);
            lv_obj_set_style_text_align(number_label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(number_label, ThemeText(0xffffff), 0);
            Label(steps, step_text[index], 104, 88 + index * 72, 600);
        }

        auto note = Box(card, 22, 478, 706, 90, 0xf0f5fa);
        lv_obj_set_style_radius(note, 28, 0);
        auto note_label = Label(note, "此模式下字典内容和语音唤醒暂停", 20, 25, 666);
        lv_obj_set_style_text_align(note_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(note_label, ThemeText(kMuted), 0);
        auto restore = Button(card, usb_storage_restore_requested_ ? "正在安全恢复…" : "重启并恢复",
                              750, 478, 460, 90, 0x36c779, 919);
        lv_obj_set_style_radius(restore, 32, 0);
        lv_obj_set_style_shadow_color(restore, ThemeShadow(0x23995a), 0);
        lv_obj_set_style_shadow_width(restore, 12, 0);
        lv_obj_set_style_shadow_opa(restore, LV_OPA_20, 0);
        auto restore_label = lv_obj_get_child(restore, 0);
        lv_obj_set_style_text_color(restore_label, ThemeText(0xffffff), 0);
        lv_obj_set_style_text_font(restore_label, &han_font_40, 0);
        network_info_ = network_detail_ = nullptr;
        return;
    }
    if (settings_page_ == 1) {
        MqttMessageBoardPage();
        return;
    }
    NetworkSettingsPage();
}

void HanDisplay::NetworkSettingsPage() {
    auto left = Card(body_, 0, 0, 650, 592, 0xffffff);
    Label(left, "网络与存储", 24, 16, 580, &han_font_timer_title);

    auto style_action_button = [](lv_obj_t* button) {
        auto label = lv_obj_get_child(button, 0);
        lv_obj_set_style_text_color(label, ThemeText(0xffffff), 0);
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
    lv_obj_set_style_text_font(network_info_, &han_font_timer, 0);
    lv_obj_set_style_text_color(network_info_, ThemeText(0x102347), 0);
    network_detail_ = Label(connection, "", 184, 88, 210, &han_font_28);
    lv_obj_set_style_text_color(network_detail_, ThemeText(0x304664), 0);
    auto network_button = Button(connection, "手机配网", 410, 63, 184, 76, 0x349fe8, 10);
    lv_obj_set_style_radius(network_button, 30, 0);
    style_action_button(network_button);

    auto storage = Box(left, 18, 292, 614, 202, 0xedfbf3);
#ifndef HAN_UI_HOST_SIM
    if (!usb_storage_art_data_.empty()) {
        auto storage_art = Image(storage, &usb_storage_art_, 10, 21);
        lv_image_set_scale(storage_art, usb_storage_art_.header.w > 180 ? 220 : 256);
        lv_image_set_pivot(storage_art, 0, 0);
    } else
#endif
    {
        auto storage_art = Image(storage, &han_icon_settings, 44, 50);
        lv_image_set_scale(storage_art, 270);
        lv_image_set_pivot(storage_art, 0, 0);
    }
    auto storage_title = Label(storage, "microSD卡", 210, 37, 190, &han_font_timer);
    lv_obj_set_style_text_color(storage_title, ThemeText(0x102347), 0);
    auto storage_hint = Label(storage, "字典、语音和插画资源", 210, 86, 190);
    lv_obj_set_style_text_color(storage_hint, ThemeText(kMuted), 0);
    auto storage_button = Button(storage, usb_storage_requested_ ? "正在切换…" : "USB 读卡器", 410,
                                 63, 184, 76, 0x28cf7d, 11);
    lv_obj_set_style_radius(storage_button, 30, 0);
    lv_obj_set_style_bg_grad_color(storage_button, ThemeFill(0x08b966), 0);
    lv_obj_set_style_bg_grad_dir(storage_button, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(storage_button, 2, 0);
    lv_obj_set_style_border_color(storage_button, ThemeBorder(0x70e4a8), 0);
    lv_obj_set_style_shadow_color(storage_button, ThemeShadow(0x28c978), 0);
    lv_obj_set_style_shadow_width(storage_button, 14, 0);
    lv_obj_set_style_shadow_opa(storage_button, LV_OPA_30, 0);
    style_action_button(storage_button);
    auto usb_hint = Label(left, "USB 模式仅用于安全复制 SD 卡内容", 24, 526, 590);
    ApplyDynamicTextFont(usb_hint);
    lv_obj_set_style_text_color(usb_hint, ThemeText(kMuted), 0);

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
        auto slider = lv_slider_create(row);
        lv_obj_set_pos(slider, 124, 69);
        lv_obj_set_size(slider, 378, 14);
        lv_slider_set_range(slider, minimum, maximum);
        lv_slider_set_value(slider, value, LV_ANIM_OFF);
        lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, ThemeFill(0xdde5ec), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, ThemeFill(accent), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, ThemeFill(accent), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_pad_all(slider, 8, LV_PART_KNOB);
        lv_obj_set_style_border_width(slider, 3, LV_PART_KNOB);
        lv_obj_set_style_border_color(slider, ThemeBorder(0xffffff), LV_PART_KNOB);
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

    auto appearance_button = Button(right, dark_theme_ ? "深色模式" : "外观模式", 18, 456, 252,
                                    76, 0x7086e8, 940);
    lv_obj_set_style_radius(appearance_button, 24, 0);
    lv_obj_set_style_bg_grad_color(appearance_button, ThemeFill(0x546dcc), 0);
    lv_obj_set_style_bg_grad_dir(appearance_button, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(appearance_button, 2, 0);
    lv_obj_set_style_border_color(appearance_button, ThemeBorder(0xaebcf5), 0);
    lv_obj_set_style_text_font(lv_obj_get_child(appearance_button, 0), &han_font_timer, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(appearance_button, 0), ThemeText(0xffffff), 0);

    auto screen_button = Button(right, "立即关屏", 292, 456, 252, 76, 0xffabc5, 914);
    lv_obj_set_style_radius(screen_button, 24, 0);
    lv_obj_set_style_bg_grad_color(screen_button, ThemeFill(0xff8fb3), 0);
    lv_obj_set_style_bg_grad_dir(screen_button, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(screen_button, 2, 0);
    lv_obj_set_style_border_color(screen_button, ThemeBorder(0xffc7d8), 0);
    lv_obj_set_style_shadow_color(screen_button, ThemeShadow(0xff99b9), 0);
    lv_obj_set_style_shadow_width(screen_button, 14, 0);
    lv_obj_set_style_shadow_opa(screen_button, LV_OPA_30, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(screen_button, 0), DynamicTextFont(), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(screen_button, 0), ThemeText(0xb8003b), 0);
    auto wake_hint = Label(right, "关屏后点一下屏幕，再向上滑动解锁", 24, 547, 514);
    ApplyDynamicTextFont(wake_hint);
    lv_obj_set_style_text_color(wake_hint, ThemeText(kMuted), 0);
    lv_obj_set_style_text_align(wake_hint, LV_TEXT_ALIGN_CENTER, 0);
    UpdateSettingLabels();
    DrawSettingsPageNavigation(0);
}
void HanDisplay::ShowAppearancePopup() {
    if (appearance_popup_ || !body_)
        return;
    appearance_draft_mode_ = theme_mode_;
    appearance_draft_start_ = dark_start_minutes_;
    appearance_draft_end_ = dark_end_minutes_;

    appearance_popup_ = Box(body_, 0, 0, 1232, 592, 0x09111f);
    lv_obj_set_style_radius(appearance_popup_, 0, 0);
    lv_obj_set_style_bg_opa(appearance_popup_, LV_OPA_70, 0);
    lv_obj_add_flag(appearance_popup_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(appearance_popup_);

    auto panel = Card(appearance_popup_, 222, 24, 788, 544, 0xffffff);
    lv_obj_set_style_radius(panel, 34, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    Label(panel, "外观模式", 34, 22, 500, &han_font_timer_title);
    auto subtitle = Label(panel, "设置显示模式与自动时间", 36, 74, 650);
    lv_obj_set_style_text_font(subtitle, &han_font_timer, 0);
    lv_obj_set_style_text_color(subtitle, ThemeText(kMuted), 0);

    constexpr const char* names[] = {"浅色", "深色", "自动"};
    for (int index = 0; index < 3; ++index) {
        appearance_mode_buttons_[index] =
            Button(panel, names[index], 34 + index * 242, 120, 220, 72, 0xeaf1f8, 941 + index);
        lv_obj_set_style_radius(appearance_mode_buttons_[index], 24, 0);
        lv_obj_set_style_text_font(lv_obj_get_child(appearance_mode_buttons_[index], 0),
                                   &han_font_timer, 0);
    }

    auto schedule = Box(panel, 34, 216, 720, 184, 0xf2f6fb);
    lv_obj_set_style_radius(schedule, 28, 0);
    Label(schedule, "自动时段", 24, 18, 300, &han_font_timer);
    auto add_time_row = [this, schedule](int y, const char* title, lv_obj_t** value, int minus_action,
                                         int plus_action) {
        Label(schedule, title, 28, y + 10, 190, &han_font_timer);
        auto minus = Button(schedule, "-", 300, y, 62, 58, 0xdce8f3, minus_action);
        lv_obj_set_style_radius(minus, 20, 0);
        *value = Label(schedule, "", 374, y + 8, 164, &han_font_timer);
        lv_obj_set_style_text_align(*value, LV_TEXT_ALIGN_CENTER, 0);
        auto plus = Button(schedule, "+", 550, y, 62, 58, 0xdce8f3, plus_action);
        lv_obj_set_style_radius(plus, 20, 0);
    };
    add_time_row(62, "夜间开启", &appearance_start_value_, 944, 945);
    add_time_row(122, "早晨关闭", &appearance_end_value_, 946, 947);

    auto cancel = Button(panel, "取消", 34, 430, 338, 78, 0xe9eef4, 949);
    lv_obj_set_style_radius(cancel, 26, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(cancel, 0), &han_font_timer, 0);
    auto save = Button(panel, "保存并应用", 382, 430, 372, 78, 0x37c979, 948);
    lv_obj_set_style_radius(save, 26, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(save, 0), &han_font_timer, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(save, 0), ThemeText(0xffffff), 0);
    UpdateAppearancePopup();
}

void HanDisplay::UpdateAppearancePopup() {
    for (int index = 0; index < 3; ++index) {
        auto button = appearance_mode_buttons_[index];
        if (!button)
            continue;
        const bool selected = index == static_cast<int>(appearance_draft_mode_);
        lv_obj_set_style_bg_color(button, ThemeFill(selected ? 0x546dcc : 0xeaf1f8), 0);
        lv_obj_set_style_border_width(button, selected ? 3 : 1, 0);
        lv_obj_set_style_border_color(button, ThemeBorder(selected ? 0xaebcf5 : 0xd5e0ea), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(button, 0),
                                    ThemeText(selected ? 0xffffff : kInk), 0);
    }
    auto format_time = [](int minutes, char (&text)[8]) {
        const int bounded = std::clamp(minutes, 0, 1439);
        const int hour = bounded / 60;
        const int minute = bounded % 60;
        text[0] = static_cast<char>('0' + hour / 10);
        text[1] = static_cast<char>('0' + hour % 10);
        text[2] = ':';
        text[3] = static_cast<char>('0' + minute / 10);
        text[4] = static_cast<char>('0' + minute % 10);
        text[5] = '\0';
    };
    char start[8], end[8];
    format_time(appearance_draft_start_, start);
    format_time(appearance_draft_end_, end);
    SetTextIfChanged(appearance_start_value_, start);
    SetTextIfChanged(appearance_end_value_, end);
}

void HanDisplay::CloseAppearancePopup() {
    if (appearance_popup_) {
        lv_obj_delete(appearance_popup_);
        appearance_popup_ = nullptr;
    }
    appearance_mode_buttons_.fill(nullptr);
    appearance_start_value_ = appearance_end_value_ = nullptr;
}

void HanDisplay::DrawSettingsPageNavigation(int selected_page) {
    auto navigation = Box(body_, 568, 555, 96, 34, 0xf5f8fb);
    lv_obj_set_style_radius(navigation, 17, 0);
    for (int index = 0; index < 2; ++index) {
        auto dot = Box(navigation, 19 + index * 34, 9, 16, 16,
                       index == selected_page ? 0x339cf0 : 0xc8d5df);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    }
    lv_obj_move_foreground(navigation);
}

void HanDisplay::MqttMessageBoardPage() {
    struct UiMessage {
        std::string sender;
        std::string text;
        std::string time;
        std::string avatar;
    };

    std::string server = "尚未配置";
    std::string port = "—";
    std::string topic = "handict/message";
    std::string state_text = "等待配置";
    std::string detail = "请从 microSD 读取 MQTT 配置";
    uint32_t state_background = 0xfff1d5;
    uint32_t state_color = 0x8a5a13;
    std::vector<UiMessage> messages;
#ifndef HAN_UI_HOST_SIM
    auto& service = han::MqttMessageBoard::GetInstance();
    const auto config = service.config();
    if (!config.server.empty())
        server = config.server;
    if (config.port > 0)
        port = std::to_string(config.port);
    if (!config.topic.empty())
        topic = config.topic;
    switch (service.state()) {
        case han::MqttBoardState::Connected:
            state_text = "已连接";
            detail = "正在接收家庭留言";
            state_background = 0xdaf7e5;
            state_color = 0x167145;
            break;
        case han::MqttBoardState::Connecting:
            state_text = "连接中";
            detail = "正在连接 MQTT 服务器";
            state_background = 0xe1f2ff;
            state_color = 0x246eaa;
            break;
        case han::MqttBoardState::Disabled:
            state_text = "未启用";
            detail.clear();
            break;
        case han::MqttBoardState::Disconnected:
            state_text = "已断开";
            detail = "点刷新重新连接";
            state_background = 0xffe5e0;
            state_color = 0xa44135;
            break;
        case han::MqttBoardState::Error:
            state_text = "连接失败";
            detail = service.error();
            state_background = 0xffe5e0;
            state_color = 0xa44135;
            break;
        case han::MqttBoardState::Unconfigured:
            detail = service.error();
            break;
    }
    for (const auto& message : service.messages())
        messages.push_back({message.sender, message.text, message.time, message.avatar});
#else
    server = "mqtt.example.com";
    port = "1883";
    topic = "xiaozhi/message";
    state_text = "已连接";
    detail = "正在接收家庭留言";
    state_background = 0xdaf7e5;
    state_color = 0x167145;
    messages = {{"妈妈", "放学后记得带雨伞，回家路上注意安全。", "09:20", "mom"},
                {"王老师", "明天带好数学练习册，第一节课会讲评。", "08:45", "teacher"},
                {"爸爸", "今晚七点到家，我们一起整理书包。", "昨天", "dad"}};
#endif
    const bool compact_status = state_text == "未启用";
    if (detail.empty() && !compact_status)
        detail = "等待 MQTT 服务器响应";

    auto add_asset = [](lv_obj_t* parent, const char* disk_path, const char* lvgl_path,
                        const lv_image_dsc_t* fallback, int x, int y, int scale) {
#ifndef HAN_UI_HOST_SIM
        if (SdFileAvailable(disk_path)) {
            auto image = Image(parent, lvgl_path, x, y);
            lv_image_set_scale(image, scale);
            lv_image_set_pivot(image, 0, 0);
            return image;
        }
#endif
        auto image = Image(parent, fallback, x, y);
        lv_image_set_scale(image, scale);
        lv_image_set_pivot(image, 0, 0);
        return image;
    };

    auto left = Card(body_, 0, 0, 594, 552, 0xffffff);
    Label(left, "MQTT 连接", 24, 14, 300, &han_font_timer_title);
    auto hero = Box(left, 18, 68, 558, 180, 0xeaf7ff);
    lv_obj_set_style_radius(hero, 28, 0);
    add_asset(hero, "/sdcard/handict/ui/graphics/mqtt-message-board/mqtt-hero.png",
              "S:/sdcard/handict/ui/graphics/mqtt-message-board/mqtt-hero.png", &han_icon_settings,
              12, 5, 180);
    auto status = Box(hero, 316, 23, 224, 134, state_background);
    lv_obj_set_style_radius(status, 26, 0);
    auto status_title =
        Label(status, state_text.c_str(), 12, compact_status ? 0 : 16, 200, &han_font_timer);
    lv_obj_set_style_text_align(status_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_title, ThemeText(state_color), 0);
    if (compact_status)
        lv_obj_align(status_title, LV_ALIGN_CENTER, 0, 0);
    if (!compact_status) {
        auto status_detail = Label(status, detail.c_str(), 16, 62, 192, DynamicTextFont());
        lv_obj_set_height(status_detail, 66);
        lv_label_set_long_mode(status_detail, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(status_detail, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(status_detail, ThemeText(kMuted), 0);
    }

    struct ConfigRow {
        const char* title;
        const std::string* value;
        const char* disk_path;
        const char* lvgl_path;
        uint32_t background;
    };
    const ConfigRow rows[] = {
        {"服务器", &server, "/sdcard/handict/ui/graphics/mqtt-message-board/server.png",
         "S:/sdcard/handict/ui/graphics/mqtt-message-board/server.png", 0xf0f8ff},
        {"端口", &port, "/sdcard/handict/ui/graphics/mqtt-message-board/port.png",
         "S:/sdcard/handict/ui/graphics/mqtt-message-board/port.png", 0xf4f1ff},
        {"订阅主题", &topic, "/sdcard/handict/ui/graphics/mqtt-message-board/topic.png",
         "S:/sdcard/handict/ui/graphics/mqtt-message-board/topic.png", 0xedfbf3},
    };
    for (int index = 0; index < 3; ++index) {
        auto row = Box(left, 18, 260 + index * 70, 558, 60, rows[index].background);
        lv_obj_set_style_radius(row, 20, 0);
        add_asset(row, rows[index].disk_path, rows[index].lvgl_path, &han_icon_settings, 10, 7,
                  164);
        auto label = Label(row, rows[index].title, 74, 12, 144, &han_font_timer);
        lv_obj_set_style_text_color(label, ThemeText(0x173258), 0);
        auto value = Label(row, rows[index].value->c_str(), 210, 13, 328, DynamicTextFont());
        lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(value, ThemeText(0x445a76), 0);
    }
    auto settings = Button(left, "连接设置", 18, 474, 558, 62, 0x56aef1, 920);
    lv_obj_set_style_radius(settings, 24, 0);
    lv_obj_set_style_bg_grad_color(settings, ThemeFill(0x318ee6), 0);
    lv_obj_set_style_bg_grad_dir(settings, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(settings, 0), ThemeText(0xffffff), 0);
    lv_obj_set_style_text_font(lv_obj_get_child(settings, 0), &han_font_timer, 0);

    auto right = Card(body_, 614, 0, 618, 552, 0xffffff);
    Label(right, "最新留言", 24, 14, 310, &han_font_timer_title);
    char count_text[32];
    snprintf(count_text, sizeof(count_text), "最近 %u 条", static_cast<unsigned>(messages.size()));
    auto count = Box(right, 426, 14, 166, 46, 0xe8f6ff);
    lv_obj_set_style_radius(count, 23, 0);
    auto count_label = Label(count, count_text, 8, 7, 150, DynamicTextFont());
    lv_obj_set_style_text_align(count_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(count_label, ThemeText(0x276d9f), 0);

    auto list = Box(right, 18, 68, 582, 394, 0xf8fbfd);
    lv_obj_set_style_radius(list, 26, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(list, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(list, 9, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list, 5, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(list, ThemeFill(0x4da9ed), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_70, LV_PART_SCROLLBAR);
    if (messages.empty()) {
        add_asset(list, "/sdcard/handict/ui/graphics/mqtt-message-board/topic.png",
                  "S:/sdcard/handict/ui/graphics/mqtt-message-board/topic.png", &han_icon_settings,
                  255, 62, 256);
        auto empty_title = Label(list, "还没有收到留言", 40, 166, 502, &han_font_timer);
        lv_obj_set_style_text_align(empty_title, LV_TEXT_ALIGN_CENTER, 0);
        auto empty_detail = Label(list, detail.c_str(), 56, 218, 470, DynamicTextFont());
        lv_obj_set_height(empty_detail, 76);
        lv_label_set_long_mode(empty_detail, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(empty_detail, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(empty_detail, ThemeText(kMuted), 0);
    } else {
        const uint32_t backgrounds[] = {0xfff4e5, 0xebf7ff, 0xedfbf3};
        for (size_t index = 0; index < messages.size(); ++index) {
            const auto& message = messages[index];
            auto card = Box(list, 10, 10 + static_cast<int>(index) * 112, 552, 102,
                            backgrounds[index % std::size(backgrounds)]);
            lv_obj_set_style_radius(card, 23, 0);
            const char* avatar_name = "avatar-mom.png";
            if (message.avatar == "dad" || message.sender.find("爸") != std::string::npos)
                avatar_name = "avatar-dad.png";
            else if (message.avatar == "teacher" ||
                     message.sender.find("老师") != std::string::npos)
                avatar_name = "avatar-teacher.png";
            std::string disk =
                std::string("/sdcard/handict/ui/graphics/mqtt-message-board/") + avatar_name;
            std::string lvgl =
                std::string("S:/sdcard/handict/ui/graphics/mqtt-message-board/") + avatar_name;
            add_asset(card, disk.c_str(), lvgl.c_str(), &han_art_book, 10, 11, 176);
            auto sender = Label(card, message.sender.c_str(), 96, 9, 260, &han_font_timer);
            lv_label_set_long_mode(sender, LV_LABEL_LONG_DOT);
            auto time = Label(card, message.time.c_str(), 390, 11, 140, DynamicTextFont());
            lv_obj_set_style_text_align(time, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_style_text_color(time, ThemeText(kMuted), 0);
            auto text = Label(card, message.text.c_str(), 96, 53, 434, DynamicTextFont());
            lv_obj_set_height(text, 42);
            lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_color(text, ThemeText(0x354b68), 0);
        }
    }
    auto refresh = Button(right, "刷新留言", 18, 474, 582, 62, 0x48c887, 923);
    lv_obj_set_style_radius(refresh, 24, 0);
    lv_obj_set_style_bg_grad_color(refresh, ThemeFill(0x23af6c), 0);
    lv_obj_set_style_bg_grad_dir(refresh, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(refresh, 0), ThemeText(0xffffff), 0);
    lv_obj_set_style_text_font(lv_obj_get_child(refresh, 0), &han_font_timer, 0);
    DrawSettingsPageNavigation(1);
}

void HanDisplay::ShowMqttSettingsPopup() {
    if (mqtt_settings_popup_ != nullptr)
        return;
    std::string server = "尚未配置";
    std::string port = "—";
    std::string topic = "尚未配置";
    std::string client_id = "xiaozhi-tab5-board";
    std::string account = "未设置（匿名连接）";
#ifndef HAN_UI_HOST_SIM
    const auto config = han::MqttMessageBoard::GetInstance().config();
    if (!config.server.empty())
        server = config.server;
    if (config.port > 0)
        port = std::to_string(config.port);
    if (!config.topic.empty())
        topic = config.topic;
    if (!config.client_id.empty())
        client_id = config.client_id;
    if (!config.username.empty())
        account = config.username + (config.password.empty() ? "" : "  ·  密码已保存");
#endif
    mqtt_settings_popup_ = Box(body_, 0, 0, 1232, 592, 0x17314b);
    lv_obj_set_style_bg_opa(mqtt_settings_popup_, LV_OPA_50, 0);
    lv_obj_add_flag(mqtt_settings_popup_, LV_OBJ_FLAG_CLICKABLE);
    auto panel = Card(mqtt_settings_popup_, 126, 30, 980, 526, 0xffffff);
    lv_obj_set_style_radius(panel, 32, 0);
    Label(panel, "MQTT 连接设置", 32, 22, 500, &han_font_timer_title);
    auto close = Button(panel, "×", 890, 18, 62, 58, 0xf0f4f7, 922);
    lv_obj_set_style_radius(close, 20, 0);

    const char* labels[] = {"服务器", "端口", "订阅主题", "客户端", "登录账号"};
    const std::string* values[] = {&server, &port, &topic, &client_id, &account};
    const uint32_t colors[] = {0xf0f8ff, 0xf4f1ff, 0xedfbf3, 0xfff6e6, 0xffedf1};
    for (int index = 0; index < 5; ++index) {
        const int column = index % 2;
        const int row = index / 2;
        const int width = index == 4 ? 916 : 448;
        const int x = index == 4 ? 32 : 32 + column * 468;
        auto item = Box(panel, x, 94 + row * 94, width, 76, colors[index]);
        lv_obj_set_style_radius(item, 22, 0);
        Label(item, labels[index], 18, 19, 126, &han_font_timer);
        auto value = Label(item, values[index]->c_str(), 144, 20, width - 166, DynamicTextFont());
        lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(value, ThemeText(0x40546e), 0);
    }
    auto hint = Label(panel, "通过 USB 读卡器编辑 /handict/mqtt.json，然后重新读取配置", 32, 390,
                      916, DynamicTextFont());
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(hint, ThemeText(kMuted), 0);
    auto reload = Button(panel, "从 microSD 重新读取并连接", 198, 440, 584, 64, 0x42c985, 921);
    lv_obj_set_style_radius(reload, 25, 0);
    lv_obj_set_style_bg_grad_color(reload, ThemeFill(0x20ad69), 0);
    lv_obj_set_style_bg_grad_dir(reload, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(reload, 0), ThemeText(0xffffff), 0);
    lv_obj_set_style_text_font(lv_obj_get_child(reload, 0), &han_font_timer, 0);
    lv_obj_move_foreground(mqtt_settings_popup_);
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
    SetScreenOffLocked();
}

void HanDisplay::SetScreenOffLocked() {
    if (!setup_ui_called_ || root_ == nullptr)
        return;

    lock_screen_backlight_pending_ = false;
    // Stop only the temporary flip overlays. The six steady digit cards stay alive so the clock
    // can resume from the current wall time after unlocking without rebuilding the page.
    if (page_ == Page::Clock) {
        for (auto& digit : flip_digits_) {
            lv_anim_delete(&digit, nullptr);
            for (auto** overlay : {&digit.old_top, &digit.old_bottom, &digit.new_bottom}) {
                if (*overlay != nullptr) {
                    lv_obj_delete(*overlay);
                    *overlay = nullptr;
                }
            }
        }
    }
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
    // Wait for release before replacing this object. Deleting an LVGL object while it is still the
    // active pointer target can leave the input device holding a stale target until the next read.
    lv_obj_add_event_cb(screen_wake_overlay_, OnScreenWake, LV_EVENT_RELEASED, this);
    lv_obj_move_foreground(screen_wake_overlay_);
    screen_off_ = true;
    lock_screen_visible_ = false;
    lock_screen_transition_pending_ = false;
    lock_unlock_gesture_ = false;
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
    lv_obj_add_event_cb(screen_wake_overlay_, OnLockReleased, LV_EVENT_RELEASED, this);

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
    lv_obj_set_style_text_color(arrow, ThemeText(0x45b987), 0);
    auto hint = Label(panel, "向上滑动解锁", 410, 350, 520, &han_font_40);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    auto note = Label(panel, "10 秒内未操作将自动关屏", 410, 430, 520, &han_font_28);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(note, ThemeText(kMuted), 0);

    lv_obj_move_foreground(screen_wake_overlay_);
    screen_off_ = false;
    lock_screen_visible_ = true;
    lock_screen_transition_pending_ = false;
    lock_unlock_gesture_ = false;
    lock_screen_shown_ms_ = NowMs();
#ifndef HAN_UI_HOST_SIM
    // Keep the backlight dark until OnRefresh observes the first completed lock-screen frame.
    lock_screen_backlight_pending_ = true;
    Board::GetInstance().SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
#endif
}

void HanDisplay::UnlockScreen() {
    if (!screen_off_ && !lock_screen_visible_)
        return;
    DisplayLockGuard guard(this);
    UnlockScreenLocked();
}

void HanDisplay::UnlockScreenLocked() {
    if (!setup_ui_called_ || root_ == nullptr)
        return;

    screen_off_ = false;
    lock_screen_visible_ = false;
    lock_screen_transition_pending_ = false;
    lock_screen_backlight_pending_ = false;
    lock_unlock_gesture_ = false;
    if (screen_wake_overlay_ != nullptr) {
        lv_obj_delete(screen_wake_overlay_);
        screen_wake_overlay_ = nullptr;
    }
    if (theme_render_pending_)
        Render(page_);
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

bool HanDisplay::ResolveDarkTheme(const struct tm* supplied_local) const {
    if (theme_mode_ == ThemeMode::Light)
        return false;
    if (theme_mode_ == ThemeMode::Dark)
        return true;
    struct tm local{};
    if (supplied_local) {
        local = *supplied_local;
    } else {
        const auto now = time(nullptr);
        localtime_r(&now, &local);
    }
    // NTP may not be ready during boot. Retaining the last resolved state avoids a bright flash
    // before a valid local clock arrives.
    if (local.tm_year < 125)
        return dark_theme_;
    const int minute = local.tm_hour * 60 + local.tm_min;
    if (dark_start_minutes_ == dark_end_minutes_)
        return true;
    if (dark_start_minutes_ < dark_end_minutes_)
        return minute >= dark_start_minutes_ && minute < dark_end_minutes_;
    return minute >= dark_start_minutes_ || minute < dark_end_minutes_;
}

void HanDisplay::ApplyTheme(bool dark, bool rerender) {
    const bool changed = dark_theme_ != dark;
    dark_theme_ = dark;
    g_dark_theme_enabled = dark;
    if (!root_)
        return;

    lv_obj_set_style_bg_color(root_, ThemeFill(kBg), 0);
    lv_obj_set_style_bg_color(back_, ThemeFill(kGreen), 0);
    lv_obj_set_style_text_color(title_, ThemeText(kInk), 0);
    lv_obj_set_style_text_color(date_, ThemeText(kInk), 0);
    lv_obj_set_style_text_color(clock_, ThemeText(kInk), 0);
    lv_obj_set_style_bg_color(top_divider_left_, ThemeFill(0xd7d5d0), 0);
    lv_obj_set_style_bg_color(top_divider_right_, ThemeFill(0xd7d5d0), 0);
    lv_obj_set_style_image_recolor(back_image_, ThemeText(kInk), 0);
    lv_obj_set_style_image_recolor_opa(back_image_, dark ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_image_recolor(wifi_image_, ThemeText(kInk), 0);
    lv_obj_set_style_image_recolor_opa(wifi_image_, dark ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_image_recolor(battery_image_, ThemeText(kInk), 0);
    lv_obj_set_style_image_recolor_opa(battery_image_, dark ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_image_opa(footer_, dark ? LV_OPA_TRANSP : LV_OPA_COVER, 0);

    lv_obj_set_style_bg_color(assistant_card_, ThemeFill(0xf4fbff), 0);
    lv_obj_set_style_bg_grad_color(assistant_card_, ThemeFill(0xfffbef), 0);
    lv_obj_set_style_border_color(assistant_card_, ThemeBorder(0xc9e8df), 0);
    lv_obj_set_style_shadow_color(assistant_card_, ThemeShadow(0x8fbcb2), 0);
    lv_obj_set_style_bg_color(assistant_badge_, ThemeFill(0xdff6ee), 0);
    lv_obj_set_style_bg_grad_color(assistant_badge_, ThemeFill(0xddeeff), 0);
    lv_obj_set_style_border_color(assistant_badge_, ThemeBorder(0xffffff), 0);
    lv_obj_set_style_bg_color(role_box_, ThemeFill(0xc8e9ff), 0);
    lv_obj_set_style_bg_grad_color(role_box_, ThemeFill(0xe3d8ff), 0);
    lv_obj_set_style_bg_color(status_box_, ThemeFill(0xe9f4ff), 0);
    lv_obj_set_style_bg_grad_color(status_box_, ThemeFill(0xe4f8ef), 0);
    lv_obj_set_style_border_color(status_box_, ThemeBorder(0xc9ddf3), 0);
    for (auto* label : {role_label_, message_, status_label_, notification_label_})
        if (label)
            lv_obj_set_style_text_color(label, ThemeText(kInk), 0);

    if (changed) {
        if (rerender && !screen_off_ && !lock_screen_visible_ && !usb_storage_active_)
            Render(page_);
        else
            theme_render_pending_ = true;
    }
}

void HanDisplay::SaveThemeSetting() {
#ifndef HAN_UI_HOST_SIM
    const int mode = static_cast<int>(theme_mode_);
    const int start = dark_start_minutes_;
    const int end = dark_end_minutes_;
    const bool active = dark_theme_;
    Application::GetInstance().Schedule([mode, start, end, active] {
        Settings display("display", true);
        display.SetInt("theme_mode", mode);
        display.SetInt("dark_start", start);
        display.SetInt("dark_end", end);
        display.SetBool("dark_active", active);
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
    lv_obj_set_style_bg_color(content, ThemeFill(0xf09aa6), LV_PART_SCROLLBAR);
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
    if (!self || self->lock_screen_transition_pending_.exchange(true))
        return;
    // Run after LVGL has completed the RELEASED event. This guarantees the black wake layer is no
    // longer referenced by the input device when ShowLockScreenLocked() replaces it.
    if (lv_async_call(ShowLockScreenAsync, self) != LV_RESULT_OK)
        self->lock_screen_transition_pending_ = false;
}

void HanDisplay::ShowLockScreenAsync(void* user_data) {
    auto self = static_cast<HanDisplay*>(user_data);
    if (!self)
        return;
    self->lock_screen_transition_pending_ = false;
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

void HanDisplay::OnSettingsGesture(lv_event_t* event) {
    auto self = static_cast<HanDisplay*>(lv_event_get_user_data(event));
    auto indev = lv_event_get_indev(event);
    if (!self || !indev || self->page_ != Page::Network || self->usb_storage_active_ ||
        self->mqtt_settings_popup_ != nullptr)
        return;
    auto active = lv_indev_get_active_obj();
    if (active == self->brightness_slider_ || active == self->volume_slider_ ||
        active == self->auto_lock_slider_)
        return;
    const auto direction = lv_indev_get_gesture_dir(indev);
#ifndef HAN_UI_HOST_SIM
    ESP_LOGI("HanDisplay", "Settings gesture received: dir=%d page=%d", static_cast<int>(direction),
             self->settings_page_);
#endif
    if (direction == LV_DIR_LEFT && self->settings_page_ == 0) {
        self->settings_page_ = 1;
        self->Render(Page::Network);
    } else if (direction == LV_DIR_RIGHT && self->settings_page_ == 1) {
        self->settings_page_ = 0;
        self->Render(Page::Network);
    }
}

void HanDisplay::OnLockGesture(lv_event_t* event) {
    auto self = static_cast<HanDisplay*>(lv_event_get_user_data(event));
    auto indev = lv_event_get_indev(event);
    if (!self || !indev)
        return;
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_TOP)
        self->lock_unlock_gesture_ = true;
}

void HanDisplay::OnLockReleased(lv_event_t* event) {
    auto self = static_cast<HanDisplay*>(lv_event_get_user_data(event));
    if (!self || !self->lock_unlock_gesture_)
        return;
    self->lock_unlock_gesture_ = false;
    if (self->lock_screen_transition_pending_.exchange(true))
        return;
    // Do not delete the lock-screen object from its gesture/release callback. Deferring one LVGL
    // cycle lets the touch driver clear its active target first and prevents intermittent
    // use-after- free resets during an upward swipe.
    if (lv_async_call(UnlockScreenAsync, self) != LV_RESULT_OK)
        self->lock_screen_transition_pending_ = false;
}

void HanDisplay::UnlockScreenAsync(void* user_data) {
    auto self = static_cast<HanDisplay*>(user_data);
    if (!self)
        return;
    self->lock_screen_transition_pending_ = false;
    self->UnlockScreenLocked();
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

void HanDisplay::OnTimerPlanChanged(lv_event_t* event) {
    auto self = static_cast<HanDisplay*>(lv_event_get_user_data(event));
    auto arc = lv_event_get_target_obj(event);
    if (!self || !arc || !self->timer_plan_popup_)
        return;
    for (int subject = 0; subject < 3; ++subject) {
        if (arc != self->timer_plan_arcs_[subject])
            continue;
        self->timer_plan_draft_[subject] =
            std::clamp(static_cast<int>(lv_arc_get_value(arc)), 30, 99);
        if (self->timer_plan_values_[subject]) {
            const auto value = std::to_string(self->timer_plan_draft_[subject]);
            SetTextIfChanged(self->timer_plan_values_[subject], value.c_str());
        }
        break;
    }
}

void HanDisplay::OnAlarmRingtoneChanged(lv_event_t* event) {
    auto self = static_cast<HanDisplay*>(lv_event_get_user_data(event));
    auto dropdown = lv_event_get_target_obj(event);
    if (!self || !dropdown)
        return;
    self->alarm_ringtone_ = std::clamp(static_cast<int>(lv_dropdown_get_selected(dropdown)), 0,
                                       static_cast<int>(std::size(kAlarmRingtones)) - 1);
    // A newly selected ringtone takes effect on the next preview. Stop an active preview first so
    // the selection and the sound heard by the user can never disagree.
    if (self->alarm_previewing_.exchange(false)) {
        Application::GetInstance().Schedule(
            [] { Application::GetInstance().GetAudioService().ResetDecoder(); });
    }
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
    if (usb_storage_active_) {
        if (a == 919) {
#ifdef HAN_UI_HOST_SIM
            usb_storage_active_ = false;
            Render(Page::Network);
#else
            if (usb_storage_restore_requested_.exchange(true))
                return;
            Toast("正在安全退出 USB 模式…");
            Render(Page::Network);
            if (!Queue(8, "")) {
                usb_storage_restore_requested_ = false;
                Render(Page::Network);
            }
#endif
        }
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
    if (a == 13) {
        Render(Page::Clock);
        return;
    }
    if (a == 14) {
        assistant_dialog_hide_at_ms_ = 0;
        assistant_navigation_pending_ = false;
        HideAssistantDialog();
        StartPendingStrokePlayback();
        return;
    }
    if (a == 15) {
        assistant_dialog_hide_at_ms_ = 0;
        assistant_navigation_pending_ = false;
        if (assistant_dialog_navigation_)
            lv_obj_add_flag(assistant_dialog_navigation_, LV_OBJ_FLAG_HIDDEN);
        HideAssistantDialog();
        Application::GetInstance().Schedule([] {
            auto& app = Application::GetInstance();
            const auto state = app.GetDeviceState();
            if (state == kDeviceStateListening || state == kDeviceStateSpeaking) {
                app.ToggleChatState();
            } else if (state == kDeviceStateConnecting) {
                app.SetDeviceState(kDeviceStateIdle);
            }
        });
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
        // Action() runs in the LVGL event task. Keep the complete transition there instead of
        // handing object creation to the application task during a display refresh.
        SetScreenOffLocked();
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
    if (a == 920) {
        ShowMqttSettingsPopup();
        return;
    }
    if (a == 921) {
        mqtt_settings_popup_ = nullptr;
        Render(Page::Network);
        Toast("正在重新读取 MQTT 配置…");
#ifndef HAN_UI_HOST_SIM
        Queue(11, "");
#endif
        return;
    }
    if (a == 922) {
        mqtt_settings_popup_ = nullptr;
        Render(Page::Network);
        return;
    }
    if (a == 923) {
        Toast("正在刷新 MQTT 留言…");
#ifndef HAN_UI_HOST_SIM
        Queue(12, "");
#endif
        return;
    }
    if (a == 924 || a == 925) {
        settings_page_ = a == 924 ? 0 : 1;
        Render(Page::Network);
        return;
    }
    if (a == 940) {
        ShowAppearancePopup();
        return;
    }
    if (a >= 941 && a <= 943) {
        appearance_draft_mode_ = static_cast<ThemeMode>(a - 941);
        UpdateAppearancePopup();
        return;
    }
    if (a >= 944 && a <= 947) {
        int& value = a <= 945 ? appearance_draft_start_ : appearance_draft_end_;
        value = (value + ((a == 944 || a == 946) ? -30 : 30) + 1440) % 1440;
        UpdateAppearancePopup();
        return;
    }
    if (a == 948) {
        theme_mode_ = appearance_draft_mode_;
        dark_start_minutes_ = appearance_draft_start_;
        dark_end_minutes_ = appearance_draft_end_;
        const bool dark = ResolveDarkTheme();
        CloseAppearancePopup();
        ApplyTheme(dark, false);
        SaveThemeSetting();
        Render(Page::Network);
        return;
    }
    if (a == 949) {
        CloseAppearancePopup();
        return;
    }
    if (a >= 1100 && a < 1126) {
        if (pinyin_query_.size() < 7) {
            pinyin_query_.push_back(static_cast<char>('a' + a - 1100));
            pinyin_search_key_.clear();
            pinyin_results_.clear();
            pinyin_page_ = 0;
            lv_label_set_text(search_input_, pinyin_query_.c_str());
            lv_obj_set_style_text_color(search_input_, ThemeText(kInk), 0);
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
                                    ThemeText(pinyin_query_.empty() ? kMuted : kInk), 0);
        RenderPinyinResults("输入完成后点查找，或直接选择音调");
        return;
    }
    if (a == 1129) {
        pinyin_query_.clear();
        pinyin_search_key_.clear();
        pinyin_results_.clear();
        pinyin_page_ = 0;
        lv_label_set_text(search_input_, "输入拼音，例如 han");
        lv_obj_set_style_text_color(search_input_, ThemeText(kMuted), 0);
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
        auto_play_stroke_pending_ = false;
        stroke_playing_ = false;
        const int stroke_count = static_cast<int>(entry_.strokes.size());
        stroke_ = std::clamp(stroke_ + (a == 20 ? -1 : 1), -1, stroke_count);
        UpdateStroke();
        return;
    }
    if (a == 21) {
        auto_play_stroke_pending_ = false;
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
    if (a == 330) {
        ShowTimerPlanPopup();
        return;
    }
    if (a == 331) {
        CloseTimerPlanPopup();
        return;
    }
    if (a == 332) {
        for (int subject = 0; subject < 3; ++subject)
            timer_plan_minutes_[subject] = std::clamp(timer_plan_draft_[subject], 30, 99);
        SaveTimerPlan();
        CloseTimerPlanPopup();
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
    if (a == 430) {
        if (alarm_previewing_.exchange(false)) {
            Application::GetInstance().Schedule(
                [] { Application::GetInstance().GetAudioService().ResetDecoder(); });
            Render(Page::Alarm);
            return;
        }
        if (local_audio_.load() || alarm_ringing_.load()) {
            Toast(alarm_ringing_.load() ? "请先停止当前铃声" : "请听完当前音频");
            return;
        }
        alarm_previewing_ = true;
        Render(Page::Alarm);
        if (!Queue(10, kAlarmRingtones[alarm_ringtone_].path)) {
            alarm_previewing_ = false;
            Render(Page::Alarm);
        }
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
        if (a == 401 && alarm_ringing_.exchange(false)) {
            Application::GetInstance().Schedule(
                [] { Application::GetInstance().GetAudioService().ResetDecoder(); });
            Render(Page::Alarm);
            Toast("铃声已停止，闹钟仍保持开启");
            return;
        }
        if (a == 400 && alarm_days_ == 0) {
            Toast("请至少选择一天");
            return;
        }
        const bool audio_was_active = alarm_previewing_.exchange(false);
        alarm_enabled_ = a == 400;
        if (a == 400)
            alarm_minutes_ =
                lv_roller_get_selected(alarm_hour_) * 60 + lv_roller_get_selected(alarm_minute_);
        alarm_ringing_ = false;
        const auto minutes = alarm_minutes_;
        const auto days = alarm_days_;
        const auto enabled = alarm_enabled_;
        const auto ringtone = alarm_ringtone_;
        Application::GetInstance().Schedule([minutes, days, enabled, ringtone, audio_was_active] {
            if (audio_was_active)
                Application::GetInstance().GetAudioService().ResetDecoder();
            Settings s("han_alarm", true);
            s.SetInt("minutes", minutes);
            s.SetInt("days", days);
            s.SetBool("enabled", enabled);
            s.SetInt("ringtone", ringtone);
        });
        Render(Page::Alarm);
        Toast(a == 400 ? "闹钟已保存" : "闹钟已关闭");
        return;
    }
}

void HanDisplay::Tick(lv_timer_t* timer) {
    auto self = static_cast<HanDisplay*>(lv_timer_get_user_data(timer));
    if (self->theme_mode_ == ThemeMode::Auto) {
        const auto now = time(nullptr);
        struct tm local{};
        localtime_r(&now, &local);
        if (local.tm_year >= 125) {
            const int64_t minute_key =
                static_cast<int64_t>(local.tm_year) * 527040 + local.tm_yday * 1440 +
                local.tm_hour * 60 + local.tm_min;
            if (minute_key != self->theme_minute_key_) {
                self->theme_minute_key_ = minute_key;
                const bool dark = self->ResolveDarkTheme(&local);
                if (dark != self->dark_theme_) {
                    const bool can_render = !self->screen_off_ && !self->lock_screen_visible_ &&
                                            !self->usb_storage_active_;
                    self->ApplyTheme(dark, can_render);
                    self->SaveThemeSetting();
                    if (can_render)
                        return;
                }
            }
        }
    }
    if (self->mqtt_board_dirty_.exchange(false) && self->page_ == Page::Network &&
        self->settings_page_ == 1 && self->mqtt_settings_popup_ == nullptr &&
        !self->usb_storage_active_) {
        self->Render(Page::Network);
        return;
    }
    if (self->assistant_dialog_hide_at_ms_ > 0 && NowMs() >= self->assistant_dialog_hide_at_ms_) {
        self->assistant_dialog_hide_at_ms_ = 0;
        self->assistant_navigation_pending_ = false;
        self->HideAssistantDialog();
        self->StartPendingStrokePlayback();
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
    if (self->lock_screen_visible_ && NowMs() - self->lock_screen_shown_ms_ >= 10000) {
        self->SetScreenOffLocked();
        return;
    }
    if (!self->screen_off_ && !self->lock_screen_visible_ && !self->usb_storage_active_ &&
        !self->alarm_ringing_ && self->auto_lock_minutes_ > 0 &&
        lv_display_get_inactive_time(self->display_) >=
            static_cast<uint32_t>(self->auto_lock_minutes_) * 60U * 1000U) {
        // Tick() is an LVGL timer callback. Mutating the UI here keeps all invalidation and flip
        // animation state on the single LVGL task and avoids a rendering race with main.
        self->SetScreenOffLocked();
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

void HanDisplay::ClockTick(lv_timer_t* timer) {
    auto self = static_cast<HanDisplay*>(lv_timer_get_user_data(timer));
    if (self->page_ != Page::Clock || self->screen_off_ || self->lock_screen_visible_)
        return;
    const auto now = time(nullptr);
    struct tm local{};
    localtime_r(&now, &local);
    self->UpdateFlipClock(local, local.tm_year >= 125, true);
}

void HanDisplay::ShowEntry(const han::Entry& entry, bool auto_play_strokes) {
    DisplayLockGuard guard(this);
    entry_ = entry;
    stroke_ = -1;
    stroke_playing_ = false;
    auto_play_stroke_pending_ = auto_play_strokes && !entry.strokes.empty();
    if (auto_play_strokes) {
        assistant_navigation_pending_ = true;
        assistant_dialog_hide_at_ms_ = NowMs() + 2400;
    }
    if (setup_ui_called_) {
        Render(Page::Dictionary);
        if (auto_play_strokes) {
            auto detail = std::string("打开“") + entry.character + "”字并自动播放笔顺";
            ApplyDynamicTextFont(assistant_dialog_navigation_title_);
            ApplyDynamicTextFont(assistant_dialog_navigation_detail_);
            lv_label_set_text(assistant_dialog_navigation_title_, "正在打开“小小字典”");
            lv_label_set_text(assistant_dialog_navigation_detail_, detail.c_str());
            lv_obj_remove_flag(assistant_dialog_navigation_, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(assistant_dialog_status_, "识别成功");
            ShowAssistantDialog();
        }
    }
}

bool HanDisplay::OpenPage(const std::string& page) {
    if (usb_storage_active_ || usb_storage_requested_)
        return false;
    const char* names[] = {"home",  "dictionary", "phonetics", "timetable", "timer",
                           "alarm", "weather",    "network",   "clock"};
    for (int i = 0; i < 9; ++i)
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
    std::string network_status;
    std::string network_detail;
    const bool config = wifi.IsConfigMode();
    if (config) {
        network_status = "手机配网模式";
        network_detail = wifi.GetApSsid();
    } else if (wifi.IsConnected()) {
        network_status = "Wi-Fi 已连接";
        network_detail = wifi.GetSsid();
    } else {
        network_status = "Wi-Fi 未连接";
        network_detail = "可使用本地功能";
    }
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
    UpdateFlipClock(tm, valid_time, true);
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
        SetTextIfChanged(network_info_, network_status.c_str());
    if (network_detail_)
        SetTextIfChanged(network_detail_, network_detail.c_str());
    // The alarm is based on synchronized system time; RTC wake-up is not implied. tm_wday starts
    // on Sunday, while the persisted weekday mask starts on Monday.
    const int64_t day = static_cast<int64_t>(tm.tm_year) * 366 + tm.tm_yday;
    const int weekday = (tm.tm_wday + 6) % 7;
    if (valid_time && alarm_enabled_ && alarm_last_day_ != day &&
        (alarm_days_ & (1U << weekday)) != 0 && tm.tm_hour * 60 + tm.tm_min == alarm_minutes_) {
        alarm_last_day_ = day;
        alarm_ringing_ = true;
        alarm_previewing_ = false;
        Render(Page::Alarm);
        Toast("时间到了！");
        if (local_audio_.load()) {
            Application::GetInstance().Schedule(
                [] { Application::GetInstance().GetAudioService().ResetDecoder(); });
        }
        if (!Queue(9, kAlarmRingtones[alarm_ringtone_].path)) {
            alarm_ringing_ = false;
            Toast("铃声播放排队失败");
        }
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
    constexpr int default_plan_minutes[] = {45, 60, 40};
    for (int i = 0; i < 3; ++i) {
        const auto key = std::to_string(i);
        study_.Restore(i, static_cast<int64_t>(s.GetInt("s" + key, 0)) * 1000,
                       s.GetBool("c" + key, false));
        timer_plan_minutes_[i] = std::clamp(
            static_cast<int>(s.GetInt("p" + key, default_plan_minutes[i])), 30, 99);
    }
    timer_plan_draft_ = timer_plan_minutes_;
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
    alarm_ringtone_ = std::clamp(static_cast<int>(a.GetInt("ringtone", 0)), 0,
                                 static_cast<int>(std::size(kAlarmRingtones)) - 1);
    Settings display("display");
    brightness_setting_ = std::clamp(static_cast<int>(display.GetInt("brightness", 75)), 10, 100);
    auto_lock_minutes_ = std::clamp(static_cast<int>(display.GetInt("lock_min", 10)), 1, 30);
    theme_mode_ = static_cast<ThemeMode>(
        std::clamp(static_cast<int>(display.GetInt("theme_mode", 0)), 0, 2));
    dark_start_minutes_ =
        std::clamp(static_cast<int>(display.GetInt("dark_start", 19 * 60)), 0, 1439);
    dark_end_minutes_ =
        std::clamp(static_cast<int>(display.GetInt("dark_end", 7 * 60)), 0, 1439);
    dark_theme_ = display.GetBool("dark_active", false);
    dark_theme_ = ResolveDarkTheme();
    g_dark_theme_enabled = dark_theme_;
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
        } else if (job.type == 9 || job.type == 10) {
            const bool preview = job.type == 10;
            auto& app = Application::GetInstance();
            if (preview && app.GetDeviceState() != kDeviceStateIdle &&
                app.GetDeviceState() != kDeviceStateWifiConfiguring) {
                self->alarm_previewing_ = false;
                self->Toast("请先结束当前语音对话");
                DisplayLockGuard guard(self);
                if (self->page_ == Page::Alarm)
                    self->Render(Page::Alarm);
                continue;
            }
            std::string data;
            if (!store.ready() || !store.Read(job.value, data, kAlarmAudioLimit) ||
                data.compare(0, 4, "OggS") != 0 ||
                data.substr(0, 128).find("OpusHead") == std::string::npos) {
                if (preview)
                    self->alarm_previewing_ = false;
                else
                    self->alarm_ringing_ = false;
                self->Toast("SD 卡铃声缺失，请重新复制铃声包");
                DisplayLockGuard guard(self);
                if (self->page_ == Page::Alarm)
                    self->Render(Page::Alarm);
                continue;
            }

            self->local_audio_ = true;
            auto& audio = app.GetAudioService();
            const bool restore_wake = audio.IsWakeWordRunning();
            audio.EnableWakeWordDetection(false);
            audio.ResetDecoder();
            const auto deadline = NowMs() + (preview ? 20000 : kAlarmMaximumRingMs);
            auto active = [&] {
                return preview ? self->alarm_previewing_.load() : self->alarm_ringing_.load();
            };
            while (active() && NowMs() < deadline) {
                audio.PlaySound(data);
                while (active() && !audio.IsPlaybackIdle() && NowMs() < deadline)
                    vTaskDelay(pdMS_TO_TICKS(50));
                if (preview)
                    break;
                if (active())
                    vTaskDelay(pdMS_TO_TICKS(300));
            }
            if (!audio.IsPlaybackIdle())
                audio.ResetDecoder();
            const bool timed_out = !preview && self->alarm_ringing_.load() && NowMs() >= deadline;
            if (preview)
                self->alarm_previewing_ = false;
            else if (timed_out)
                self->alarm_ringing_ = false;
            if (restore_wake && app.GetDeviceState() == kDeviceStateIdle)
                audio.EnableWakeWordDetection(true);
            self->local_audio_ = false;
            if (timed_out)
                self->Toast("闹钟已自动停止");
            DisplayLockGuard guard(self);
            if (self->page_ == Page::Alarm)
                self->Render(Page::Alarm);
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
        } else if (job.type == 8) {
            const auto error = self->usb_storage_restore_action_
                                   ? self->usb_storage_restore_action_()
                                   : "当前设备不支持恢复 USB 读卡器";
            if (!error.empty()) {
                self->usb_storage_restore_requested_ = false;
                self->Toast(error.c_str());
                DisplayLockGuard guard(self);
                self->Render(Page::Network);
            } else {
#ifndef HAN_UI_HOST_SIM
                Application::GetInstance().Schedule([] { Application::GetInstance().Reboot(); });
#endif
            }
#ifndef HAN_UI_HOST_SIM
        } else if (job.type == 11) {
            han::MqttMessageBoard::GetInstance().ReloadAndConnect();
        } else if (job.type == 12) {
            han::MqttMessageBoard::GetInstance().Refresh();
#endif
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
