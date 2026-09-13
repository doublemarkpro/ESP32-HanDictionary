#include "qweather_service.h"

#include <cJSON.h>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>

#include <esp_log.h>

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

bool ParseCurrent(const std::string& body, std::string& summary, std::string& error) {
    Json root(cJSON_ParseWithLength(body.data(), body.size()), cJSON_Delete);
    auto condition = ObjectValue(root.get(), "condition");
    auto temperature = ObjectValue(root.get(), "temperature");
    auto feels_like = ObjectValue(root.get(), "feelsLike");
    auto wind = ObjectValue(root.get(), "wind");
    auto direction = ObjectValue(wind, "direction");
    double temp = 0, feels = 0, humidity = 0, scale = 0;
    const char* condition_text = StringValue(condition, "text");
    const char* compass = StringValue(direction, "compass");
    if (!root || !condition_text[0] || !compass[0] || !NumberValue(temperature, "value", temp) ||
        !NumberValue(feels_like, "value", feels) ||
        !NumberValue(root.get(), "humidity", humidity) || !NumberValue(wind, "scale", scale) ||
        temp < -100 || temp > 100 || feels < -100 || feels > 100 || humidity < 0 || humidity > 1 ||
        scale < 0 || scale > 17) {
        error = "和风天气返回数据不完整";
        return false;
    }
    char details[256];
    snprintf(details, sizeof(details), "%s %.0f°C\n体感 %.0f°C · 湿度 %.0f%%\n%s %.0f级",
             condition_text, temp, feels, humidity * 100.0, CompassName(compass).c_str(), scale);
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
             "https://%s/weather/v1/current/%.4f/%.4f?lang=zh&localTime=true",
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
    if (!http->Open("GET", location)) {
        error = "连接和风天气失败";
        return false;
    }
    const int status = http->GetStatusCode();
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
