#include "dictionary_service.h"
#include <cJSON.h>
#include "mcp_server.h"

DictionaryService& DictionaryService::GetInstance() {
    static DictionaryService instance;
    return instance;
}

void DictionaryService::RegisterMcpTools() {
    if (tools_registered_)
        return;
    tools_registered_ = true;
    McpServer::GetInstance().AddTool(
        "self.dictionary.lookup",
        "查本机汉字数据并打开查字页。query优先传目标单字，也可传规矩的规。"
        "释义来源以data_source为准，不把演示释义称为新华字典原文。"
        "页码目标是新华字典第12版；dictionary_page为null时必须说待核对，不猜页码。",
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
            cJSON_AddStringToObject(result, "dictionary_edition", entry.edition.c_str());
            cJSON_AddStringToObject(result, "dictionary_isbn", entry.isbn.c_str());
            if (entry.page)
                cJSON_AddNumberToObject(result, "dictionary_page", entry.page);
            else
                cJSON_AddNullToObject(result, "dictionary_page");
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
