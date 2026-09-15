#include "qweather_service.h"

#include <cJSON.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include <esp_log.h>
#include <miniz.h>

#include "board.h"
#include "http.h"
#include "wifi_manager.h"

namespace han {
namespace {

constexpr char kTag[] = "QWeather";
constexpr size_t kMaxResponseBytes = 48 * 1024;
constexpr char kConfigFile[] = "qweather.json";
constexpr char kCacheFile[] = "weather.json";
constexpr char kCachePath[] = "/sdcard/handict/weather.json";
constexpr char kCacheTempPath[] = "/sdcard/handict/weather.json.tmp";

using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

struct Config {
    std::string api_host;
    std::string api_key;
    std::string city;
    double latitude = NAN;
    double longitude = NAN;
};

struct ForecastDay {
    std::string date;
    std::string condition;
    std::string condition_code;
    int temperature_min = 0;
    int temperature_max = 0;
    int precipitation_probability = -1;
    std::string sunrise;
    std::string sunset;
};

struct Snapshot {
    std::string city;
    std::string condition;
    std::string condition_code;
    int temperature = 0;
    int feels_like = 0;
    int humidity = -1;
    std::string wind;
    int wind_scale = -1;
    std::vector<ForecastDay> days;
    std::string air_category;
    std::string air_aqi;
    std::string index_name;
    std::string index_category;
    std::string index_text;
    std::string updated;
};

const char* StringValue(cJSON* object, const char* key) {
    auto value = object ? cJSON_GetObjectItemCaseSensitive(object, key) : nullptr;
    return cJSON_IsString(value) && value->valuestring ? value->valuestring : "";
}

cJSON* ObjectValue(cJSON* object, const char* key) {
    auto value = object ? cJSON_GetObjectItemCaseSensitive(object, key) : nullptr;
    return cJSON_IsObject(value) ? value : nullptr;
}

bool NumberValue(cJSON* object, const char* key, double& out) {
    auto value = object ? cJSON_GetObjectItemCaseSensitive(object, key) : nullptr;
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble))
        return false;
    out = value->valuedouble;
    return true;
}

bool FlexibleNumberValue(cJSON* object, const char* key, double& out) {
    auto value = object ? cJSON_GetObjectItemCaseSensitive(object, key) : nullptr;
    if (cJSON_IsNumber(value) && std::isfinite(value->valuedouble)) {
        out = value->valuedouble;
        return true;
    }
    if (!cJSON_IsString(value) || !value->valuestring || !value->valuestring[0])
        return false;
    char* end = nullptr;
    const double parsed = strtod(value->valuestring, &end);
    if (end == value->valuestring || *end != '\0' || !std::isfinite(parsed))
        return false;
    out = parsed;
    return true;
}

bool ValidHost(const std::string& host) {
    if (host.empty() || host.size() > 128 || host.front() == '.' || host.back() == '.')
        return false;
    for (unsigned char ch : host)
        if (!(std::isalnum(ch) || ch == '.' || ch == '-'))
            return false;
    return true;
}

bool LoadConfig(const ContentStore& store, Config& config, std::string& error) {
    std::string data;
    if (!store.Read(kConfigFile, data, 2048)) {
        error = "请将 qweather.json 放入 SD 卡 handict 目录";
        return false;
    }
    Json root(cJSON_ParseWithLength(data.data(), data.size()), cJSON_Delete);
    if (!root) {
        error = "和风天气配置格式错误";
        return false;
    }
    config.api_host = StringValue(root.get(), "api_host");
    config.api_key = StringValue(root.get(), "api_key");
    config.city = StringValue(root.get(), "city");
    if (!ValidHost(config.api_host) || config.api_key.empty() || config.api_key.size() > 160 ||
        config.city.empty() || config.city.size() > 48 ||
        !NumberValue(root.get(), "latitude", config.latitude) ||
        !NumberValue(root.get(), "longitude", config.longitude) || config.latitude < -90.0 ||
        config.latitude > 90.0 || config.longitude < -180.0 || config.longitude > 180.0) {
        error = "和风天气配置字段无效";
        return false;
    }
    for (unsigned char ch : config.api_key)
        if (ch <= 0x20 || ch >= 0x7f) {
            error = "和风天气 API KEY 无效";
            return false;
        }
    return true;
}

std::string CompassName(const char* code) {
    struct Compass {
        const char* code;
        const char* name;
    };
    static constexpr Compass names[] = {
        {"n", "北风"},       {"nne", "东北偏北风"},  {"ne", "东北风"}, {"ene", "东北偏东风"},
        {"e", "东风"},       {"ese", "东南偏东风"},  {"se", "东南风"}, {"sse", "东南偏南风"},
        {"s", "南风"},       {"ssw", "西南偏南风"},  {"sw", "西南风"}, {"wsw", "西南偏西风"},
        {"w", "西风"},       {"wnw", "西北偏西风"},  {"nw", "西北风"}, {"nnw", "西北偏北风"},
        {"vrb", "风向不定"}, {"none", "无持续风向"},
    };
    for (const auto& item : names)
        if (strcmp(item.code, code) == 0)
            return item.name;
    return "风向不明";
}

std::string Timestamp() {
    const auto now = time(nullptr);
    struct tm local{};
    localtime_r(&now, &local);
    if (local.tm_year < 125)
        return "刚刚";
    char text[32];
    strftime(text, sizeof(text), "%m-%d %H:%M", &local);
    return text;
}

std::string DatePart(const char* value) {
    if (!value)
        return {};
    const std::string text(value);
    return text.size() >= 10 ? text.substr(0, 10) : text;
}

std::string TimePart(const char* value) {
    if (!value || !value[0])
        return {};
    const std::string text(value);
    const auto separator = text.find('T');
    if (separator != std::string::npos && separator + 6 <= text.size())
        return text.substr(separator + 1, 5);
    return text.size() >= 5 ? text.substr(0, 5) : text;
}

bool ReadResponse(Http& http, std::string& body) {
    body.clear();
    const auto content_length = http.GetBodyLength();
    if (content_length > kMaxResponseBytes)
        return false;
    char buffer[1024];
    for (;;) {
        const int count = http.Read(buffer, sizeof(buffer));
        if (count < 0 || body.size() + count > kMaxResponseBytes) {
            body.clear();
            return false;
        }
        if (count == 0)
            return !body.empty();
        body.append(buffer, count);
    }
}

bool InflateGzip(const std::string& compressed, std::string& plain) {
    const auto* data = reinterpret_cast<const uint8_t*>(compressed.data());
    const size_t size = compressed.size();
    if (size < 18 || data[0] != 0x1f || data[1] != 0x8b || data[2] != 8 || (data[3] & 0xe0))
        return false;
    size_t offset = 10;
    const uint8_t flags = data[3];
    if (flags & 0x04) {
        if (offset + 2 > size - 8)
            return false;
        const size_t length = data[offset] | (static_cast<size_t>(data[offset + 1]) << 8);
        offset += 2;
        if (length > size - 8 - offset)
            return false;
        offset += length;
    }
    auto skip_text = [&] {
        while (offset < size - 8 && data[offset] != 0)
            ++offset;
        return offset < size - 8 ? (++offset, true) : false;
    };
    if ((flags & 0x08) && !skip_text())
        return false;
    if ((flags & 0x10) && !skip_text())
        return false;
    if (flags & 0x02) {
        if (offset + 2 > size - 8)
            return false;
        offset += 2;
    }
    if (offset >= size - 8)
        return false;
    const size_t output_size =
        static_cast<size_t>(data[size - 4]) | (static_cast<size_t>(data[size - 3]) << 8) |
        (static_cast<size_t>(data[size - 2]) << 16) | (static_cast<size_t>(data[size - 1]) << 24);
    if (!output_size || output_size > kMaxResponseBytes)
        return false;
    auto decompressor = std::unique_ptr<tinfl_decompressor, decltype(&free)>(
        static_cast<tinfl_decompressor*>(calloc(1, sizeof(tinfl_decompressor))), free);
    if (!decompressor)
        return false;
    tinfl_init(decompressor.get());
    plain.assign(output_size, '\0');
    size_t input_bytes = size - 8 - offset;
    size_t output_bytes = plain.size();
    const auto status = tinfl_decompress(decompressor.get(), data + offset, &input_bytes,
                                         reinterpret_cast<uint8_t*>(plain.data()),
                                         reinterpret_cast<uint8_t*>(plain.data()), &output_bytes,
                                         TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    if (status != TINFL_STATUS_DONE || output_bytes != output_size) {
        plain.clear();
        return false;
    }
    return true;
}

bool Request(const Config& config, const std::string& path, std::string& body, int& status) {
    status = 0;
    auto network = Board::GetInstance().GetNetwork();
    auto http = network ? network->CreateHttp(0) : nullptr;
    if (!http)
        return false;
    http->SetTimeout(8000);
    http->SetHeader("X-QW-Api-Key", config.api_key);
    http->SetHeader("Accept", "application/json");
    http->SetHeader("Accept-Encoding", "identity");
    const std::string url = "https://" + config.api_host + path;
    if (!http->Open("GET", url))
        return false;
    status = http->GetStatusCode();
    const auto encoding = http->GetResponseHeader("Content-Encoding");
    const bool read = status == 200 && ReadResponse(*http, body);
    http->Close();
    if (!read)
        return false;
    if (encoding == "gzip" || (body.size() >= 2 && static_cast<uint8_t>(body[0]) == 0x1f &&
                               static_cast<uint8_t>(body[1]) == 0x8b)) {
        std::string plain;
        if (!InflateGzip(body, plain))
            return false;
        body = std::move(plain);
    }
    return true;
}

bool ParseCurrent(const std::string& body, Snapshot& out, std::string& error) {
    Json root(cJSON_ParseWithLength(body.data(), body.size()), cJSON_Delete);
    if (!root) {
        error = "和风天气返回数据不完整";
        return false;
    }
    auto legacy = ObjectValue(root.get(), "now");
    auto condition = legacy ? legacy : ObjectValue(root.get(), "condition");
    auto temperature = legacy ? legacy : ObjectValue(root.get(), "temperature");
    auto feels_like = legacy ? legacy : ObjectValue(root.get(), "feelsLike");
    auto wind = legacy ? legacy : ObjectValue(root.get(), "wind");
    auto direction = legacy ? nullptr : ObjectValue(wind, "direction");
    double temp = 0, feels = 0, humidity = 0, scale = 0;
    const char* condition_text = StringValue(condition, "text");
    if (!condition_text[0] || !FlexibleNumberValue(temperature, legacy ? "temp" : "value", temp) ||
        temp < -100 || temp > 100) {
        error = "和风天气返回数据不完整";
        return false;
    }
    if (!FlexibleNumberValue(feels_like, legacy ? "feelsLike" : "value", feels) || feels < -100 ||
        feels > 100)
        feels = temp;
    const bool has_humidity =
        FlexibleNumberValue(legacy ? legacy : root.get(), "humidity", humidity);
    if (has_humidity && humidity <= 1.0)
        humidity *= 100.0;
    const bool has_scale = FlexibleNumberValue(wind, legacy ? "windScale" : "scale", scale);
    const char* compass =
        legacy ? StringValue(legacy, "windDir") : StringValue(direction, "compass");
    out.condition = condition_text;
    out.condition_code = StringValue(condition, legacy ? "icon" : "code");
    out.temperature = static_cast<int>(lround(temp));
    out.feels_like = static_cast<int>(lround(feels));
    out.humidity =
        has_humidity && humidity >= 0 && humidity <= 100 ? static_cast<int>(lround(humidity)) : -1;
    out.wind = legacy ? (compass[0] ? compass : "风向不明") : CompassName(compass);
    out.wind_scale = has_scale && scale >= 0 && scale <= 17 ? static_cast<int>(lround(scale)) : -1;
    return true;
}

bool ParseDaily(const std::string& body, Snapshot& out) {
    Json root(cJSON_ParseWithLength(body.data(), body.size()), cJSON_Delete);
    if (!root)
        return false;
    auto days = cJSON_GetObjectItemCaseSensitive(root.get(), "days");
    const bool legacy = !cJSON_IsArray(days);
    if (legacy)
        days = cJSON_GetObjectItemCaseSensitive(root.get(), "daily");
    if (!cJSON_IsArray(days))
        return false;
    const int count = std::min(4, cJSON_GetArraySize(days));
    for (int index = 0; index < count; ++index) {
        auto day = cJSON_GetArrayItem(days, index);
        if (!cJSON_IsObject(day))
            continue;
        ForecastDay parsed;
        double minimum = 0, maximum = 0, probability = -1;
        if (legacy) {
            parsed.date = DatePart(StringValue(day, "fxDate"));
            parsed.condition = StringValue(day, "textDay");
            parsed.condition_code = StringValue(day, "iconDay");
            if (!FlexibleNumberValue(day, "tempMin", minimum) ||
                !FlexibleNumberValue(day, "tempMax", maximum))
                continue;
            FlexibleNumberValue(day, "pop", probability);
            parsed.sunrise = TimePart(StringValue(day, "sunrise"));
            parsed.sunset = TimePart(StringValue(day, "sunset"));
        } else {
            parsed.date = DatePart(StringValue(day, "forecastStartTime"));
            auto daytime = ObjectValue(day, "daytime");
            auto condition = ObjectValue(daytime, "condition");
            parsed.condition = StringValue(condition, "text");
            parsed.condition_code = StringValue(condition, "code");
            if (!FlexibleNumberValue(ObjectValue(day, "temperatureMin"), "value", minimum) ||
                !FlexibleNumberValue(ObjectValue(day, "temperatureMax"), "value", maximum))
                continue;
            auto precipitation = ObjectValue(daytime, "precipitation");
            FlexibleNumberValue(precipitation, "probability", probability);
            auto astro = ObjectValue(day, "astro");
            parsed.sunrise = TimePart(StringValue(astro, "sunrise"));
            parsed.sunset = TimePart(StringValue(astro, "sunset"));
        }
        if (probability >= 0 && probability <= 1)
            probability *= 100;
        parsed.temperature_min = static_cast<int>(lround(minimum));
        parsed.temperature_max = static_cast<int>(lround(maximum));
        parsed.precipitation_probability =
            probability >= 0 && probability <= 100 ? static_cast<int>(lround(probability)) : -1;
        out.days.push_back(std::move(parsed));
    }
    return !out.days.empty();
}

bool ParseAir(const std::string& body, Snapshot& out) {
    Json root(cJSON_ParseWithLength(body.data(), body.size()), cJSON_Delete);
    if (!root)
        return false;
    auto indexes = cJSON_GetObjectItemCaseSensitive(root.get(), "indexes");
    if (cJSON_IsArray(indexes)) {
        cJSON* selected = nullptr;
        cJSON* item = nullptr;
        cJSON_ArrayForEach (item, indexes) {
            if (!selected)
                selected = item;
            if (strcmp(StringValue(item, "code"), "cn-mee") == 0) {
                selected = item;
                break;
            }
        }
        if (selected) {
            out.air_category = StringValue(selected, "category");
            out.air_aqi = StringValue(selected, "aqiDisplay");
        }
    } else if (auto now = ObjectValue(root.get(), "now")) {
        out.air_category = StringValue(now, "category");
        out.air_aqi = StringValue(now, "aqi");
    }
    return !out.air_category.empty() || !out.air_aqi.empty();
}

bool ParseIndices(const std::string& body, Snapshot& out) {
    Json root(cJSON_ParseWithLength(body.data(), body.size()), cJSON_Delete);
    auto daily = root ? cJSON_GetObjectItemCaseSensitive(root.get(), "daily") : nullptr;
    auto first = cJSON_IsArray(daily) ? cJSON_GetArrayItem(daily, 0) : nullptr;
    if (!cJSON_IsObject(first))
        return false;
    out.index_name = StringValue(first, "name");
    out.index_category = StringValue(first, "category");
    out.index_text = StringValue(first, "text");
    return !out.index_text.empty();
}

std::string Serialize(const Snapshot& snapshot, bool cached) {
    Json root(cJSON_CreateObject(), cJSON_Delete);
    if (!root)
        return {};
    cJSON_AddNumberToObject(root.get(), "schema_version", 2);
    cJSON_AddStringToObject(root.get(), "city", snapshot.city.c_str());
    cJSON_AddStringToObject(root.get(), "updated_at", snapshot.updated.c_str());
    cJSON_AddBoolToObject(root.get(), "cached", cached);
    auto current = cJSON_AddObjectToObject(root.get(), "current");
    cJSON_AddStringToObject(current, "condition", snapshot.condition.c_str());
    cJSON_AddStringToObject(current, "condition_code", snapshot.condition_code.c_str());
    cJSON_AddNumberToObject(current, "temperature", snapshot.temperature);
    cJSON_AddNumberToObject(current, "feels_like", snapshot.feels_like);
    cJSON_AddNumberToObject(current, "humidity", snapshot.humidity);
    cJSON_AddStringToObject(current, "wind", snapshot.wind.c_str());
    cJSON_AddNumberToObject(current, "wind_scale", snapshot.wind_scale);
    auto days = cJSON_AddArrayToObject(root.get(), "days");
    for (const auto& day : snapshot.days) {
        auto item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "date", day.date.c_str());
        cJSON_AddStringToObject(item, "condition", day.condition.c_str());
        cJSON_AddStringToObject(item, "condition_code", day.condition_code.c_str());
        cJSON_AddNumberToObject(item, "temperature_min", day.temperature_min);
        cJSON_AddNumberToObject(item, "temperature_max", day.temperature_max);
        cJSON_AddNumberToObject(item, "precipitation_probability", day.precipitation_probability);
        cJSON_AddStringToObject(item, "sunrise", day.sunrise.c_str());
        cJSON_AddStringToObject(item, "sunset", day.sunset.c_str());
        cJSON_AddItemToArray(days, item);
    }
    auto air = cJSON_AddObjectToObject(root.get(), "air");
    cJSON_AddStringToObject(air, "category", snapshot.air_category.c_str());
    cJSON_AddStringToObject(air, "aqi", snapshot.air_aqi.c_str());
    auto index = cJSON_AddObjectToObject(root.get(), "index");
    cJSON_AddStringToObject(index, "name", snapshot.index_name.c_str());
    cJSON_AddStringToObject(index, "category", snapshot.index_category.c_str());
    cJSON_AddStringToObject(index, "text", snapshot.index_text.c_str());
    std::unique_ptr<char, decltype(&cJSON_free)> json(cJSON_PrintUnformatted(root.get()),
                                                      cJSON_free);
    return json ? json.get() : std::string();
}

void SaveCache(const std::string& data) {
    FILE* file = fopen(kCacheTempPath, "wb");
    if (!file)
        return;
    const bool written =
        fwrite(data.data(), 1, data.size(), file) == data.size() && fflush(file) == 0;
    fclose(file);
    bool saved = written;
    if (saved && rename(kCacheTempPath, kCachePath) != 0) {
        remove(kCachePath);
        saved = rename(kCacheTempPath, kCachePath) == 0;
    }
    if (!saved) {
        remove(kCacheTempPath);
        ESP_LOGW(kTag, "Could not update weather cache");
    }
}

std::string LegacyCache(cJSON* root) {
    const std::string city = StringValue(root, "city");
    const std::string summary = StringValue(root, "summary");
    const std::string updated = StringValue(root, "updated_at");
    if (city.empty() || summary.empty() || updated.empty())
        return {};
    return city + "（缓存）\n" + summary + "\n\n数据来源：和风天气\n更新时间：" + updated;
}

}  // namespace

std::string QWeatherService::LoadCache(const ContentStore& store) {
    std::string data;
    if (!store.Read(kCacheFile, data, kMaxResponseBytes))
        return {};
    Json root(cJSON_ParseWithLength(data.data(), data.size()), cJSON_Delete);
    if (!root)
        return {};
    auto version = cJSON_GetObjectItemCaseSensitive(root.get(), "schema_version");
    auto current = ObjectValue(root.get(), "current");
    if (!cJSON_IsNumber(version) || version->valueint != 2 || !current ||
        !StringValue(root.get(), "city")[0] || !StringValue(current, "condition")[0])
        return LegacyCache(root.get());
    cJSON_ReplaceItemInObjectCaseSensitive(root.get(), "cached", cJSON_CreateBool(true));
    std::unique_ptr<char, decltype(&cJSON_free)> json(cJSON_PrintUnformatted(root.get()),
                                                      cJSON_free);
    return json ? json.get() : std::string();
}

bool QWeatherService::Refresh(const ContentStore& store, std::string& display_text,
                              std::string& error) {
    display_text.clear();
    Config config;
    if (!LoadConfig(store, config, error))
        return false;
    if (!WifiManager::GetInstance().IsConnected()) {
        error = "尚未联网，显示上次天气";
        return false;
    }

    char path[320];
    std::string body;
    int status = 0;
    snprintf(path, sizeof(path), "/weather/v1/current/%.2f/%.2f?lang=zh&localTime=true",
             config.latitude, config.longitude);
    if (!Request(config, path, body, status)) {
        ESP_LOGW(kTag, "Current weather request failed with HTTP %d", status);
        error = status == 401 || status == 403 ? "和风天气密钥或权限无效" : "和风天气请求失败";
        return false;
    }

    Snapshot snapshot;
    snapshot.city = config.city;
    if (!ParseCurrent(body, snapshot, error))
        return false;

    snprintf(path, sizeof(path), "/weather/v1/daily/%.2f/%.2f?days=4&lang=zh&localTime=true",
             config.latitude, config.longitude);
    if (Request(config, path, body, status) && ParseDaily(body, snapshot))
        ESP_LOGI(kTag, "Loaded %u daily forecasts", static_cast<unsigned>(snapshot.days.size()));
    else
        ESP_LOGW(kTag, "Daily forecast unavailable (HTTP %d)", status);

    snprintf(path, sizeof(path), "/airquality/v1/current/%.2f/%.2f?lang=zh", config.latitude,
             config.longitude);
    if (!Request(config, path, body, status) || !ParseAir(body, snapshot))
        ESP_LOGW(kTag, "Air quality unavailable (HTTP %d)", status);

    snprintf(path, sizeof(path), "/v7/indices/1d?type=3&location=%.2f%%2C%.2f&lang=zh",
             config.longitude, config.latitude);
    if (!Request(config, path, body, status) || !ParseIndices(body, snapshot))
        ESP_LOGW(kTag, "Lifestyle index unavailable (HTTP %d)", status);

    snapshot.updated = Timestamp();
    display_text = Serialize(snapshot, false);
    if (display_text.empty()) {
        error = "天气数据整理失败";
        return false;
    }
    SaveCache(display_text);
    ESP_LOGI(kTag, "Updated weather dashboard for %s", config.city.c_str());
    return true;
}

}  // namespace han
