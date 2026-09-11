#ifndef HAN_DICTIONARY_DICTIONARY_SERVICE_H_
#define HAN_DICTIONARY_DICTIONARY_SERVICE_H_

#include <cstddef>
#include <string>

struct DictionaryEntry {
    const char* character;
    const char* traditional;
    const char* pinyin;
    const char* radical;
    int stroke_count;
    const char* structure;
    const char* definition;
    const char* const* words;
    std::size_t word_count;
    const char* const* stroke_order;
    std::size_t stroke_order_count;
};

class DictionaryService {
public:
    static DictionaryService& GetInstance();

    void RegisterMcpTools();

private:
    DictionaryService() = default;

    const DictionaryEntry* FindEntry(const std::string& query) const;

    bool tools_registered_ = false;
};

#endif  // HAN_DICTIONARY_DICTIONARY_SERVICE_H_
