#pragma once
#include <functional>
#include "content_store.h"

class DictionaryService {
public:
    static DictionaryService& GetInstance();
    void RegisterMcpTools();
    han::ContentStore& store() { return store_; }
    // Set once during board initialization, before MCP calls can arrive.
    void SetResultCallback(std::function<void(const han::Entry&)> callback) {
        result_callback_ = std::move(callback);
    }

private:
    DictionaryService() = default;
    han::ContentStore store_;
    bool tools_registered_ = false;
    std::function<void(const han::Entry&)> result_callback_;
};
