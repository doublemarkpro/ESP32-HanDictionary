#pragma once
#include <functional>
#include "content_store.h"
#include "timetable.h"

class DictionaryService {
public:
    static DictionaryService& GetInstance();
    void RegisterMcpTools();
    han::ContentStore& store() { return store_; }
    static bool IsStrokePlaybackQuery(const std::string& query) {
        for (const char* phrase : {"怎么写", "怎样写", "如何写", "笔顺", "笔画顺序", "书写顺序"}) {
            if (query.find(phrase) != std::string::npos)
                return true;
        }
        return false;
    }
    // Set once during board initialization, before MCP calls can arrive.
    void SetResultCallback(std::function<void(const han::Entry&, bool)> callback) {
        result_callback_ = std::move(callback);
    }

private:
    DictionaryService() = default;
    han::ContentStore store_;
    han::TimetableData timetable_;
    bool tools_registered_ = false;
    std::function<void(const han::Entry&, bool)> result_callback_;
};
