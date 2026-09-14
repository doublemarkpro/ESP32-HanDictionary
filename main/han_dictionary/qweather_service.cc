#include "qweather_service.h"

#include <cJSON.h>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>

#include <esp_log.h>
#include <miniz.h>

#include "board.h"
#include "http.h"
#include "wifi_manager.h"

namespace han {
namespace {

constexpr char kTag[] = "QWeather";
constexpr size_t kMaxResponseBytes = 16 * 1024;
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
    auto now = time(nullptr);
    struct tm local{};
    localtime_r(&now, &local);
    if (local.tm_year < 125)
        return "刚刚";
    char text[32];
    strftime(text, sizeof(text), "%m-%d %H:%M", &local);
    return text;
}

std::string Format(const std::string& city, const std::string& summary, const std::string& updated,
                   bool cached) {
    return city + (cached ? "（缓存）\n" : "\n") + summary + "\n\n数据来源：和风天气\n更新时间：" +
           updated;
}

bool ReadResponse(Http& http, std::string& body) {
    body.clear();
    const auto content_length = http.GetBodyLength();
    if (content_length > kMaxResponseBytes)
        return false;
    char buffer[1024];
    for (;;) {
        int count = http.Read(buffer, sizeof(buffer));
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
        const size_t extra_length = data[offset] | (static_cast<size_t>(data[offset + 1]) << 8);
        offset += 2;
        if (extra_length > size - 8 - offset)
            return false;
        offset += extra_length;
    }
    auto skip_text = [&] {
        while (offset < size - 8 && data[offset] != 0)
            ++offset;
        if (offset >= size - 8)
            return false;
        ++offset;
        return true;
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

    const size_t trailer = size - 8;
    const size_t output_size =
        static_cast<size_t>(data[size - 4]) | (static_cast<size_t>(data[size - 3]) << 8) |
        (static_cast<size_t>(data[size - 2]) << 16) | (static_cast<size_t>(data[size - 1]) << 24);
    if (output_size == 0 || output_size > kMaxResponseBytes)
        return false;
    auto decompressor = std::unique_ptr<tinfl_decompressor, decltype(&free)>(
        static_cast<tinfl_decompressor*>(calloc(1, sizeof(tinfl_decompressor))), free);
    if (!decompressor)
        return false;
    tinfl_init(decompressor.get());
    plain.assign(output_size, '\0');
    size_t input_bytes = trailer - offset;
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

bool ParseCurrent(const std::string& body, std::string& summary, std::string& error) {
    Json root(cJSON_ParseWithLength(body.data(), body.size()), cJSON_Delete);
    if (!root) {
        ESP_LOGW(kTag, "Could not parse current weather JSON (%u bytes)",
                 static_cast<unsigned>(body.size()));
        error = "和风天气返回数据不完整";
        return false;
    }

    // QWeather's current API uses numeric values in nested objects. Accept numeric strings and
    // the retiring city-weather shape too, so an account still routed through the compatibility
    // response does not turn otherwise useful weather into a hard failure.
    auto legacy = ObjectValue(root.get(), "now");
    auto condition = legacy ? legacy : ObjectValue(root.get(), "condition");
    auto temperature = legacy ? legacy : ObjectValue(root.get(), "temperature");
    auto feels_like = legacy ? legacy : ObjectValue(root.get(), "feelsLike");
    auto wind = legacy ? legacy : ObjectValue(root.get(), "wind");
    auto direction = legacy ? nullptr : ObjectValue(wind, "direction");
    double temp = 0, feels = 0, humidity = 0, scale = 0;
    const char* condition_text = StringValue(condition, "text");
    const bool has_temp = FlexibleNumberValue(temperature, legacy ? "temp" : "value", temp);
    const bool has_feels = FlexibleNumberValue(feels_like, legacy ? "feelsLike" : "value", feels);
    const bool has_humidity =
        FlexibleNumberValue(legacy ? legacy : root.get(), "humidity", humidity);
    const bool has_scale = FlexibleNumberValue(wind, legacy ? "windScale" : "scale", scale);
    const char* compass =
        legacy ? StringValue(legacy, "windDir") : StringValue(direction, "compass");
    if (!condition_text[0] || !has_temp || temp < -100 || temp > 100) {
        ESP_LOGW(kTag,
                 "Current weather missing required fields: condition=%d temperature=%d "
                 "legacy=%d bytes=%u",
                 condition_text[0] != '\0', has_temp, legacy != nullptr,
                 static_cast<unsigned>(body.size()));
        error = "和风天气返回数据不完整";
        return false;
    }
    if (!has_feels || feels < -100 || feels > 100)
        feels = temp;
    if (has_humidity && humidity > 1.0 && humidity <= 100.0)
        humidity /= 100.0;

    const std::string wind_name =
        legacy ? (compass[0] ? compass : "风向不明") : CompassName(compass);
    char details[256];
    const int written = snprintf(details, sizeof(details), "%s %.0f°C\n体感 %.0f°C · ",
                                 condition_text, temp, feels);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(details)) {
        error = "和风天气返回数据过长";
        return false;
    }
    size_t used = static_cast<size_t>(written);
    if (has_humidity && humidity >= 0 && humidity <= 1)
        used += snprintf(details + used, sizeof(details) - used, "湿度 %.0f%%", humidity * 100.0);
    else
        used += snprintf(details + used, sizeof(details) - used, "湿度 --");
    if (has_scale && scale >= 0 && scale <= 17)
        snprintf(details + used, sizeof(details) - used, "\n%s %.0f级", wind_name.c_str(), scale);
    else
        snprintf(details + used, sizeof(details) - used, "\n%s", wind_name.c_str());
    summary = details;
    return true;
}

void SaveCache(const Config& config, const std::string& summary, const std::string& updated) {
    Json root(cJSON_CreateObject(), cJSON_Delete);
    if (!root)
        return;
    cJSON_AddStringToObject(root.get(), "city", config.city.c_str());
    cJSON_AddStringToObject(root.get(), "summary", summary.c_str());
    cJSON_AddStringToObject(root.get(), "updated_at", updated.c_str());
    std::unique_ptr<char, decltype(&cJSON_free)> json(cJSON_PrintUnformatted(root.get()),
                                                      cJSON_free);
    if (!json)
        return;
    const auto size = strlen(json.get());
    FILE* file = fopen(kCacheTempPath, "wb");
    if (!file)
        return;
    const bool written = fwrite(json.get(), 1, size, file) == size && fflush(file) == 0;
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

}  // namespace

std::string QWeatherService::LoadCache(const ContentStore& store) {
    std::string data;
    if (!store.Read(kCacheFile, data, 4096))
        return {};
    Json root(cJSON_ParseWithLength(data.data(), data.size()), cJSON_Delete);
    const std::string city = StringValue(root.get(), "city");
    const std::string summary = StringValue(root.get(), "summary");
    const std::string updated = StringValue(root.get(), "updated_at");
    if (city.empty() || summary.empty() || updated.empty())
        return {};
    return Format(city, summary, updated, true);
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

    char location[256];
    snprintf(location, sizeof(location),
             "https://%s/weather/v1/current/%.2f/%.2f?lang=zh&localTime=true",
             config.api_host.c_str(), config.latitude, config.longitude);
    auto network = Board::GetInstance().GetNetwork();
    auto http = network ? network->CreateHttp(0) : nullptr;
    if (!http) {
        error = "天气网络服务不可用";
        return false;
    }
    http->SetTimeout(8000);
    http->SetHeader("X-QW-Api-Key", config.api_key);
    http->SetHeader("Accept", "application/json");
    // The board HTTP abstraction returns the response body verbatim and does not transparently
    // inflate gzip. Ask QWeather for plain JSON so cJSON always receives decodable text.
    http->SetHeader("Accept-Encoding", "identity");
    if (!http->Open("GET", location)) {
        error = "连接和风天气失败";
        return false;
    }
    const int status = http->GetStatusCode();
    const auto content_type = http->GetResponseHeader("Content-Type");
    const auto content_encoding = http->GetResponseHeader("Content-Encoding");
    std::string body;
    const bool read = status == 200 && ReadResponse(*http, body);
    http->Close();
    if (status != 200) {
        ESP_LOGW(kTag, "Current weather request failed with HTTP %d", status);
        error = status == 401 || status == 403 ? "和风天气密钥或权限无效" : "和风天气请求失败";
        return false;
    }
    if (!read) {
        error = "和风天气响应过大或读取失败";
        return false;
    }
    if (content_encoding == "gzip" || (body.size() >= 2 && static_cast<uint8_t>(body[0]) == 0x1f &&
                                       static_cast<uint8_t>(body[1]) == 0x8b)) {
        std::string plain;
        if (!InflateGzip(body, plain)) {
            ESP_LOGW(kTag, "Could not decompress gzip weather response (%u bytes)",
                     static_cast<unsigned>(body.size()));
            error = "和风天气响应解压失败";
            return false;
        }
        ESP_LOGI(kTag, "Decompressed weather response: %u -> %u bytes",
                 static_cast<unsigned>(body.size()), static_cast<unsigned>(plain.size()));
        body = std::move(plain);
    } else {
        ESP_LOGI(kTag, "Weather response: type=%s encoding=%s bytes=%u", content_type.c_str(),
                 content_encoding.empty() ? "identity" : content_encoding.c_str(),
                 static_cast<unsigned>(body.size()));
    }
    std::string summary;
    if (!ParseCurrent(body, summary, error))
        return false;
    const auto updated = Timestamp();
    SaveCache(config, summary, updated);
    display_text = Format(config.city, summary, updated, false);
    ESP_LOGI(kTag, "Updated current weather for %s", config.city.c_str());
    return true;
}

}  // namespace han
