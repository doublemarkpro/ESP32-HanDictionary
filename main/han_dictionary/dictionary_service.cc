#include "dictionary_service.h"
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <cJSON.h>
#include <ctime>
#include "mcp_server.h"
#include "timetable.h"

DictionaryService& DictionaryService::GetInstance() {
    static DictionaryService instance;
    return instance;
}

void DictionaryService::RegisterMcpTools() {
    if (tools_registered_)
        return;
    tools_registered_ = true;
    // Called during board construction after the card is mounted, before the app loop.
    // Keep tool callbacks free of SD I/O. Card changes take effect after restarting.
    std::string timetable_json;
    // The timetable is useful on its own and must not depend on the optional dictionary
    // manifest. Read() still rejects detached/unmounted cards and bounds the file size.
    if (store_.Read("timetable.json", timetable_json, 8192))
        han::TimetableData::Parse(timetable_json, timetable_);
    McpServer::GetInstance().AddTool(
        "self.study.timetable",
        "读取本机SD周课表和需带物品。day_offset=0今天、1明天，最大7。"
        "必须使用返回数据回答，不猜课程；时间未同步或内容未提供时明确说明。每周重复，不代表调休安排"
        "。",
        PropertyList({Property("day_offset", kPropertyTypeInteger, 1, 0, 7)}),
        [this](const PropertyList& properties) -> ReturnValue {
            auto result = cJSON_CreateObject();
            const auto& schedule = timetable_;
            const bool available = schedule.valid;
            auto now = time(nullptr);
            struct tm local{};
            localtime_r(&now, &local);
            const bool time_valid = local.tm_year >= 125;
            cJSON_AddBoolToObject(result, "available", available && time_valid);
            if (!available || !time_valid) {
                cJSON_AddStringToObject(result, "message",
                                        !time_valid ? "日期未同步" : "课程表未提供或格式错误");
                return result;
            }
            local.tm_hour = 12;
            local.tm_min = 0;
            local.tm_sec = 0;
            local.tm_isdst = -1;
            local.tm_mday += properties["day_offset"].value<int>();
            mktime(&local);
            const int day = (local.tm_wday + 6) % 7;
            char date[32];
            strftime(date, sizeof(date), "%Y-%m-%d", &local);
            cJSON_AddStringToObject(result, "date", date);
            cJSON_AddNumberToObject(result, "weekday_monday_zero", day);
            cJSON_AddBoolToObject(result, "weekly_template", true);
            cJSON_AddBoolToObject(result, "empty_template", schedule.empty());
            auto lessons = cJSON_AddArrayToObject(result, "lessons");
            for (const auto& value : schedule.days[day])
                cJSON_AddItemToArray(lessons, cJSON_CreateString(value.c_str()));
            auto supplies = cJSON_AddArrayToObject(result, "supplies");
            for (const auto& value : schedule.supplies[day])
                cJSON_AddItemToArray(supplies, cJSON_CreateString(value.c_str()));
            return result;
        });
    McpServer::GetInstance().AddTool(
        "self.study.capacity",
        "只读查看设备运行内存和分区容量，数值单位为字节。PSRAM不是Flash；空闲总量不等于最大连续可分"
        "配块。",
        PropertyList(), [](const PropertyList&) -> ReturnValue {
            auto result = cJSON_CreateObject();
            cJSON_AddNumberToObject(result, "internal_free",
                                    heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            cJSON_AddNumberToObject(
                result, "internal_largest_block",
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            cJSON_AddNumberToObject(result, "psram_free",
                                    heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            cJSON_AddNumberToObject(result, "psram_largest_block",
                                    heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
            const auto partition = esp_ota_get_running_partition();
            if (partition)
                cJSON_AddNumberToObject(result, "app_partition_size", partition->size);
            cJSON_AddBoolToObject(result, "sd_content_ready",
                                  DictionaryService::GetInstance().store().ready());
            return result;
        });
    McpServer::GetInstance().AddTool(
        "self.dictionary.lookup",
        "查本机汉字数据并打开查字页。query优先传目标单字，也可传规矩的规。"
        "释义来源以data_source为准；这是离线汉字学习资料，不得称为纸质字典官方原文。",
        PropertyList({Property("query", kPropertyTypeString)}),
        [this](const PropertyList& properties) -> ReturnValue {
            const auto query = properties["query"].value<std::string>();
            han::Entry entry;
            const bool found = store_.Lookup(query, entry);
            auto result = cJSON_CreateObject();
            cJSON_AddBoolToObject(result, "found", found);
            cJSON_AddStringToObject(result, "query", query.substr(0, 128).c_str());
            if (!found) {
                cJSON_AddStringToObject(result, "message",
                                        "未找到，请明确目标单字或导入相应字条。");
                return result;
            }
            cJSON_AddStringToObject(result, "character", entry.character.c_str());
            cJSON_AddStringToObject(result, "pinyin", entry.pinyin.c_str());
            cJSON_AddStringToObject(result, "radical", entry.radical.c_str());
            cJSON_AddStringToObject(result, "structure", entry.structure.c_str());
            cJSON_AddStringToObject(result, "definition", entry.definition.c_str());
            cJSON_AddNumberToObject(result, "stroke_count", entry.stroke_count);
            cJSON_AddStringToObject(result, "data_source", entry.source.c_str());
            auto words = cJSON_AddArrayToObject(result, "words");
            for (auto& word : entry.words)
                cJSON_AddItemToArray(words, cJSON_CreateString(word.c_str()));
            auto strokes = cJSON_AddArrayToObject(result, "stroke_order");
            for (auto& stroke : entry.strokes)
                cJSON_AddItemToArray(strokes, cJSON_CreateString(stroke.c_str()));
            if (result_callback_)
                result_callback_(entry);
            return result;
        });
}
