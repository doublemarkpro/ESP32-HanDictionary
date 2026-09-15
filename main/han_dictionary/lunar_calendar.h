#pragma once

#include <string>

namespace han {

struct LunarDate {
    int year = 0;
    int month = 0;
    int day = 0;
    bool leap_month = false;
};

// Supported Gregorian range: 1900-01-31 through 2100-12-31.
bool LunarFromGregorian(int year, int month, int day, LunarDate& result);
std::string FormatLunarDate(const LunarDate& date);

}  // namespace han
