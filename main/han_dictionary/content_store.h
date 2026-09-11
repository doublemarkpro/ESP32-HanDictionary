#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace han {
inline constexpr const char* kDictionaryIsbn = "9787100168076";
struct Entry {
    std::string character, pinyin, radical, structure, definition;
    std::string source = "embedded-demo";
    std::string edition = "新华字典第12版", isbn = kDictionaryIsbn;
    int stroke_count = 0;
    int page = 0;  // 0 means unknown, never a guessed printed page.
    std::vector<std::string> words, strokes;
};

// All file reads are bounded; callers perform SD I/O on a worker task.
class ContentStore {
public:
    explicit ContentStore(std::string root = "/sdcard/handict") : root_(std::move(root)) {}
    bool Initialize();
    bool Lookup(const std::string& query, Entry& entry) const;
    bool Read(const std::string& relative, std::string& data, size_t limit) const;
    bool ready() const { return ready_; }
    const std::string& notice() const { return notice_; }
    static Entry Demo();
    static std::string TargetCharacter(const std::string& query);
    static bool ParseEntry(const std::string& json, const std::string& character, Entry& entry);

private:
    std::string root_;
    std::string notice_ = "未检测到内容包，使用内置示例";
    bool ready_ = false;
};
}  // namespace han
