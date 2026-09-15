#include "lunar_calendar.h"

#include <array>
#include <cstdint>

namespace han {
namespace {

// Month sizes and leap months for 1900-2100. The compact representation follows the
// Gregorian-Lunar conversion tables published by the Hong Kong Observatory. Bit 16 stores the
// leap-month size, bits 15..4 store months 1..12, and bits 3..0 store the leap-month number.
constexpr std::array<uint32_t, 201> kLunarYears = {
    0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0, 0x09ad0,
    0x055d2, 0x04ae0, 0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540, 0x0d6a0, 0x0ada2,
    0x095b0, 0x14977, 0x04970, 0x0a4b0, 0x0b4b5, 0x06a50, 0x06d40, 0x1ab54, 0x02b60,
    0x09570, 0x052f2, 0x04970, 0x06566, 0x0d4a0, 0x0ea50, 0x16a95, 0x05ad0, 0x02b60,
    0x186e3, 0x092e0, 0x1c8d7, 0x0c950, 0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0, 0x1a5b4,
    0x025d0, 0x092d0, 0x0d2b2, 0x0a950, 0x0b557, 0x06ca0, 0x0b550, 0x15355, 0x04da0,
    0x0a5b0, 0x14573, 0x052b0, 0x0a9a8, 0x0e950, 0x06aa0, 0x0aea6, 0x0ab50, 0x04b60,
    0x0aae4, 0x0a570, 0x05260, 0x0f263, 0x0d950, 0x05b57, 0x056a0, 0x096d0, 0x04dd5,
    0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558, 0x0b540, 0x0b6a0, 0x195a6, 0x095b0,
    0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46, 0x0ab60, 0x09570,
    0x04af5, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58, 0x05ac0, 0x0ab60, 0x096d5,
    0x092e0, 0x0c960, 0x0d954, 0x0d4a0, 0x0da50, 0x07552, 0x056a0, 0x0abb7, 0x025d0,
    0x092d0, 0x0cab5, 0x0a950, 0x0b4a0, 0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0,
    0x15176, 0x052b0, 0x0a930, 0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6,
    0x0a4e0, 0x0d260, 0x0ea65, 0x0d530, 0x05aa0, 0x076a3, 0x096d0, 0x04afb, 0x04ad0,
    0x0a4d0, 0x1d0b6, 0x0d250, 0x0d520, 0x0dd45, 0x0b5a0, 0x056d0, 0x055b2, 0x049b0,
    0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0, 0x14b63, 0x09370, 0x049f8,
    0x04970, 0x064b0, 0x168a6, 0x0ea50, 0x06aa0, 0x1a6c4, 0x0aae0, 0x092e0, 0x0d2e3,
    0x0c960, 0x0d557, 0x0d4a0, 0x0da50, 0x05d55, 0x056a0, 0x0a6d0, 0x055d4, 0x052d0,
    0x0a9b8, 0x0a950, 0x0b4a0, 0x0b6a6, 0x0ad50, 0x055a0, 0x0aba4, 0x0a5b0, 0x052b0,
    0x0b273, 0x06930, 0x07337, 0x06aa0, 0x0ad50, 0x14b55, 0x04b60, 0x0a570, 0x054e4,
    0x0d160, 0x0e968, 0x0d520, 0x0daa0, 0x16aa6, 0x056d0, 0x04ae0, 0x0a9d4, 0x0a2d0,
    0x0d150, 0x0f252, 0x0d520,
};

constexpr int LeapMonth(int year) { return kLunarYears[year - 1900] & 0x0f; }

constexpr int LeapDays(int year) {
    return LeapMonth(year) == 0 ? 0 : ((kLunarYears[year - 1900] & 0x10000) ? 30 : 29);
}

constexpr int MonthDays(int year, int month) {
    return (kLunarYears[year - 1900] & (0x10000 >> month)) ? 30 : 29;
}

constexpr int YearDays(int year) {
    int days = 348;
    for (uint32_t bit = 0x8000; bit > 0x8; bit >>= 1)
        days += (kLunarYears[year - 1900] & bit) ? 1 : 0;
    return days + LeapDays(year);
}

constexpr int64_t DaysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const int adjusted_month = static_cast<int>(month) + (month > 2 ? -3 : 9);
    const unsigned day_of_year =
        static_cast<unsigned>((153 * adjusted_month + 2) / 5) + day - 1;
    const unsigned day_of_era =
        year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097LL + static_cast<int>(day_of_era) - 719468;
}

bool ValidGregorianDate(int year, int month, int day) {
    if (year < 1900 || year > 2100 || month < 1 || month > 12 || day < 1)
        return false;
    constexpr int kMonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int maximum = kMonthDays[month - 1];
    if (month == 2 && (year % 400 == 0 || (year % 4 == 0 && year % 100 != 0)))
        ++maximum;
    return day <= maximum;
}

std::string LunarDayName(int day) {
    constexpr const char* kDigits[] = {"", "一", "二", "三", "四", "五", "六", "七", "八", "九", "十"};
    if (day <= 0 || day > 30)
        return "";
    if (day <= 10)
        return std::string("初") + kDigits[day];
    if (day < 20)
        return std::string("十") + kDigits[day - 10];
    if (day == 20)
        return "二十";
    if (day < 30)
        return std::string("廿") + kDigits[day - 20];
    return "三十";
}

}  // namespace

bool LunarFromGregorian(int year, int month, int day, LunarDate& result) {
    if (!ValidGregorianDate(year, month, day))
        return false;
    int64_t offset = DaysFromCivil(year, month, day) - DaysFromCivil(1900, 1, 31);
    if (offset < 0)
        return false;

    int lunar_year = 1900;
    while (lunar_year <= 2100) {
        const int days = YearDays(lunar_year);
        if (offset < days)
            break;
        offset -= days;
        ++lunar_year;
    }
    if (lunar_year > 2100)
        return false;

    const int leap_month = LeapMonth(lunar_year);
    int lunar_month = 1;
    bool is_leap = false;
    while (lunar_month <= 12) {
        const int days = is_leap ? LeapDays(lunar_year) : MonthDays(lunar_year, lunar_month);
        if (offset < days)
            break;
        offset -= days;
        if (leap_month == lunar_month && !is_leap) {
            is_leap = true;
        } else {
            is_leap = false;
            ++lunar_month;
        }
    }
    if (lunar_month > 12)
        return false;
    result = {lunar_year, lunar_month, static_cast<int>(offset) + 1, is_leap};
    return true;
}

std::string FormatLunarDate(const LunarDate& date) {
    constexpr const char* kStems[] = {"甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸"};
    constexpr const char* kBranches[] = {"子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥"};
    constexpr const char* kMonths[] = {"",  "正", "二", "三", "四", "五", "六",
                                       "七", "八", "九", "十", "冬", "腊"};
    if (date.year < 1900 || date.year > 2100 || date.month < 1 || date.month > 12 ||
        date.day < 1 || date.day > 30)
        return "";
    const int stem = (date.year - 4) % 10;
    const int branch = (date.year - 4) % 12;
    return std::string("农历") + kStems[stem] + kBranches[branch] + "年 · " +
           (date.leap_month ? "闰" : "") + kMonths[date.month] + "月" +
           LunarDayName(date.day);
}

}  // namespace han
