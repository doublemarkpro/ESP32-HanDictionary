#pragma once
#include <cJSON.h>
#include <array>
#include <string>
#include <vector>

namespace han {
// Weekly template, Monday first. Five-day legacy files remain valid.
struct TimetableData {
    std::array<std::vector<std::string>, 7> days;
    std::array<std::vector<std::string>, 7> supplies;
    bool valid = false;
    bool empty() const {
        for (const auto& d : days)
            for (const auto& lesson : d)
                if (!lesson.empty())
                    return false;
        return true;
    }
    static bool Parse(const std::string& json, TimetableData& result) {
        if (json.size() > 8192 || json.find('\0') != std::string::npos ||
            json.find("\\u0000") != std::string::npos)
            return false;
        const char* end = nullptr;
        auto root = cJSON_ParseWithOpts(json.c_str(), &end, true);
        if (!root)
            return false;
        TimetableData data;
        auto read = [](cJSON* array, auto& output, bool optional) {
            if (!array)
                return optional;
            const int count = cJSON_GetArraySize(array);
            if (!cJSON_IsArray(array) || (count != 5 && count != 7))
                return false;
            for (int d = 0; d < count; ++d) {
                const auto day = cJSON_GetArrayItem(array, d);
                if (!cJSON_IsArray(day) || cJSON_GetArraySize(day) > 8)
                    return false;
                for (int i = 0; i < cJSON_GetArraySize(day); ++i) {
                    const auto item = cJSON_GetArrayItem(day, i);
                    if (!cJSON_IsString(item) || !item->valuestring)
                        return false;
                    std::string value = item->valuestring;
                    if (value.size() > 32 || value.find_first_of("\r\n\t") != std::string::npos)
                        return false;
                    output[d].push_back(value);
                }
            }
            return true;
        };
        const bool ok =
            cJSON_IsObject(root) &&
            read(cJSON_GetObjectItemCaseSensitive(root, "days"), data.days, false) &&
            read(cJSON_GetObjectItemCaseSensitive(root, "supplies"), data.supplies, true);
        cJSON_Delete(root);
        if (!ok)
            return false;
        data.valid = true;
        result = std::move(data);
        return true;
    }
};
}  // namespace han
