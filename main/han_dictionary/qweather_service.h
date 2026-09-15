#pragma once

#include <string>

#include "content_store.h"

namespace han {

class QWeatherService {
public:
    // Reads /sdcard/handict/qweather.json and builds one normalized dashboard snapshot from
    // QWeather current, daily, air-quality and lifestyle-index data. Credentials stay on SD.
#ifdef HAN_UI_HOST_SIM
    static bool Refresh(const ContentStore&, std::string& display_text, std::string& error) {
        display_text.clear();
        error = "和风天气仅在设备端刷新";
        return false;
    }
    static std::string LoadCache(const ContentStore&) { return {}; }
#else
    static bool Refresh(const ContentStore& store, std::string& display_text, std::string& error);
    static std::string LoadCache(const ContentStore& store);
#endif
};

}  // namespace han
