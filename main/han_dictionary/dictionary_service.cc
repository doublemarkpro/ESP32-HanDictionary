#include "dictionary_service.h"

#include "mcp_server.h"

#include <cJSON.h>

namespace {

constexpr const char *kGuiWords[] = {"规矩", "规则", "规定", "规划"};
constexpr const char *kGuiStrokeOrder[] = {
    "横", "横", "撇", "点", "竖", "横折", "撇", "竖弯钩",
};

constexpr DictionaryEntry kDemoEntries[] = {
    {
        .character = "规",
        .traditional = "規",
        .pinyin = "guī",
        .radical = "见",
        .stroke_count = 8,
        .structure = "左右结构",
        .definition = "规则、章程或一定的标准；也表示劝告、谋划。",
        .words = kGuiWords,
        .word_count = sizeof(kGuiWords) / sizeof(kGuiWords[0]),
        .stroke_order = kGuiStrokeOrder,
        .stroke_order_count =
            sizeof(kGuiStrokeOrder) / sizeof(kGuiStrokeOrder[0]),
    },
};

cJSON *EntryToJson(const DictionaryEntry &entry, const std::string &query) {
  cJSON *result = cJSON_CreateObject();
  cJSON_AddBoolToObject(result, "found", true);
  cJSON_AddStringToObject(result, "query", query.c_str());
  cJSON_AddStringToObject(result, "character", entry.character);
  cJSON_AddStringToObject(result, "traditional", entry.traditional);
  cJSON_AddStringToObject(result, "pinyin", entry.pinyin);
  cJSON_AddStringToObject(result, "radical", entry.radical);
  cJSON_AddNumberToObject(result, "stroke_count", entry.stroke_count);
  cJSON_AddStringToObject(result, "structure", entry.structure);
  cJSON_AddStringToObject(result, "definition", entry.definition);

  cJSON *words = cJSON_AddArrayToObject(result, "words");
  for (std::size_t i = 0; i < entry.word_count; ++i) {
    cJSON_AddItemToArray(words, cJSON_CreateString(entry.words[i]));
  }

  cJSON *stroke_order = cJSON_AddArrayToObject(result, "stroke_order");
  for (std::size_t i = 0; i < entry.stroke_order_count; ++i) {
    cJSON_AddItemToArray(stroke_order,
                         cJSON_CreateString(entry.stroke_order[i]));
  }

  cJSON_AddNullToObject(result, "dictionary_page");
  cJSON_AddStringToObject(result, "dictionary_edition", "");
  cJSON_AddStringToObject(result, "data_source", "embedded-demo");
  cJSON_AddStringToObject(
      result, "data_notice",
      "当前是开发用演示数据；正式版将从TF卡读取经授权或开源的数据集。");
  return result;
}

cJSON *NotFoundToJson(const std::string &query) {
  cJSON *result = cJSON_CreateObject();
  cJSON_AddBoolToObject(result, "found", false);
  cJSON_AddStringToObject(result, "query", query.c_str());
  cJSON_AddStringToObject(result, "message",
                          "演示字库中暂时没有这个字。当前里程碑只内置“规”，后续"
                          "将接入TF卡完整字库。");
  cJSON_AddStringToObject(result, "data_source", "embedded-demo");
  return result;
}

} // namespace

DictionaryService &DictionaryService::GetInstance() {
  static DictionaryService instance;
  return instance;
}

const DictionaryEntry *
DictionaryService::FindEntry(const std::string &query) const {
  for (const auto &entry : kDemoEntries) {
    if (query == entry.character ||
        query.find(entry.character) != std::string::npos) {
      return &entry;
    }
  }
  return nullptr;
}

void DictionaryService::RegisterMcpTools() {
  if (tools_registered_) {
    return;
  }
  tools_registered_ = true;

  McpServer::GetInstance().AddTool(
      "self.dictionary.lookup",
      "查询本机离线汉字字典。用户问某个字怎么写、怎么读、多少笔、笔顺、部首、组"
      "词或字典页码时，"
      "必须调用本工具。query可以是单字，也可以保留用户的表达，例如“规矩的规”。"
      "不要自行编造笔顺或页码，以工具返回的数据为准。",
      PropertyList({Property("query", kPropertyTypeString)}),
      [this](const PropertyList &properties) -> ReturnValue {
        const std::string query = properties["query"].value<std::string>();
        if (query.empty() || query.size() > 128) {
          return NotFoundToJson(query.substr(0, 128));
        }
        const DictionaryEntry *entry = FindEntry(query);
        return entry == nullptr ? NotFoundToJson(query)
                                : EntryToJson(*entry, query);
      });
}
