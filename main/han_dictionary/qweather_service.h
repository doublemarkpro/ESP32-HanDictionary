#pragma once

#include <string>

#include "content_store.h"

namespace han {

class QWeatherService {
public:
    // Reads /sdcard/handict/qweather.json, requests QWeather current-weather v1, and updates the
    // local weather cache. Credentials are never compiled into the firmware.
    static bool Refresh(const ContentStore& store, std::string& display_text, std::string& error);
    static std::string LoadCache(const ContentStore& store);
};

}  // namespace han
