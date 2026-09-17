#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace han {
struct Entry {
    std::string character, pinyin, radical, structure, definition;
    std::string source = "embedded-demo";
    int stroke_count = 0;
    std::vector<std::string> words, strokes;
};

struct StrokePoint {
    int16_t x = 0;
    int16_t y = 0;
};

struct StrokeCommand {
    uint8_t op = 0;
    uint8_t point_count = 0;
    StrokePoint points[3]{};
};

struct StrokeShape {
    std::string name;
    bool radical = false;
    std::vector<StrokeCommand> commands;
    std::vector<StrokePoint> medians;
};

struct StrokeGlyph {
    std::string character;
    std::vector<StrokeShape> strokes;
};

// All file reads are bounded; callers perform SD I/O on a worker task.
class ContentStore {
public:
    explicit ContentStore(std::string root = "/sdcard/handict") : root_(std::move(root)) {}
    bool Initialize();
    void Detach(const std::string& notice = "SD 卡已交给 USB，重启后恢复内容读取");
    bool Lookup(const std::string& query, Entry& entry) const;
    bool SearchPinyin(const std::string& query, std::vector<std::string>& characters,
                      size_t limit = 20) const;
    bool Read(const std::string& relative, std::string& data, size_t limit) const;
    bool ReadDictionaryFont(std::string& data) const;
    bool ReadCandidateDictionaryFont(std::string& data) const;
    std::string DictionaryScalableFontPath() const;
    std::string DictionaryCandidateScalableFontPath() const;
    bool ReadStrokeGlyph(const std::string& character, StrokeGlyph& glyph) const;
    bool ready() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return ready_;
    }
    bool pinyin_ready() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return pinyin_index_ready_;
    }
    std::string notice() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return notice_;
    }
    static Entry Demo();
    static std::string TargetCharacter(const std::string& query);
    static std::string NormalizePinyin(const std::string& query);
    static bool IsCommonCharacter(const std::string& character);
    static bool ParseEntry(const std::string& json, const std::string& character, Entry& entry);

private:
    bool ReadIndexed(uint32_t codepoint, std::string& data) const;
    std::string root_;
    std::string notice_ = "未检测到内容包，使用内置示例";
    mutable std::recursive_mutex mutex_;
    bool available_ = true;
    bool ready_ = false;
    bool indexed_ready_ = false;
    uint32_t indexed_records_ = 0;
    uint32_t indexed_data_size_ = 0;
    uint32_t dictionary_font_size_ = 0;
    uint32_t dictionary_font_crc_ = 0;
    std::string dictionary_font_path_;
    uint32_t dictionary_candidate_font_size_ = 0;
    uint32_t dictionary_candidate_font_crc_ = 0;
    std::string dictionary_candidate_font_path_;
    std::string dictionary_scalable_font_path_;
    std::string dictionary_candidate_scalable_font_path_;
    bool stroke_index_ready_ = false;
    uint32_t stroke_records_ = 0;
    uint32_t stroke_data_size_ = 0;
    bool pinyin_index_ready_ = false;
    uint32_t pinyin_records_ = 0;
    uint32_t pinyin_directory_size_ = 0;
    uint32_t pinyin_data_size_ = 0;
};
}  // namespace han
