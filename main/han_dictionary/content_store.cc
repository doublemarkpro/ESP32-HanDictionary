#include "content_store.h"

#include <cJSON.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>

namespace han {
namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
constexpr uint32_t kIndexVersion = 1;
constexpr uint32_t kFirstIndexedCodepoint = 0x4e00;
constexpr uint32_t kIndexedSlots = 0x9fff - kFirstIndexedCodepoint + 1;
constexpr size_t kIndexHeaderSize = 32;
constexpr size_t kIndexSlotSize = 8;
constexpr size_t kIndexedEntryLimit = 4096;
constexpr uint32_t kStrokeIndexVersion = 1;
constexpr size_t kStrokeRecordLimit = 64 * 1024;
constexpr size_t kStrokeHeaderSize = 8;
constexpr size_t kStrokeDirectorySize = 16;
constexpr uint32_t kPinyinIndexVersion = 1;
constexpr size_t kPinyinHeaderSize = 24;
constexpr size_t kPinyinDirectorySize = 16;
constexpr uint32_t kPinyinMaxRecords = 2048;
constexpr uint32_t kPinyinDataLimit = 256 * 1024;

uint32_t ReadLe32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

uint16_t ReadLe16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

int16_t ReadLeS16(const uint8_t* data) { return static_cast<int16_t>(ReadLe16(data)); }

std::string StrokeName(uint8_t code, size_t index) {
    switch (code) {
        case 'a':
            return "横折折撇";
        case 'b':
            return "竖弯";
        case 'c':
            return "横折";
        case 'd':
            return "点";
        case 'e':
            return "横撇";
        case 'f':
            return "竖";
        case 'g':
            return "竖钩";
        case 'h':
            return "竖提";
        case 'i':
            return "提";
        case 'j':
            return "横";
        case 'k':
            return "点";
        case 'l':
            return "捺";
        case 'm':
            return "撇点";
        case 'n':
            return "撇折";
        case 'o':
            return "横斜钩";
        case 'p':
            return "横折提";
        case 'q':
            return "横折折折";
        case 'r':
            return "横折钩";
        case 's':
            return "撇";
        case 't':
            return "弯钩";
        case 'u':
            return "竖弯钩";
        case 'v':
            return "横折弯";
        case 'w':
            return "横撇弯钩";
        case 'x':
            return "竖折撇";
        case 'y':
            return "斜钩";
        case 'z':
            return "竖折折钩";
        default:
            return "第" + std::to_string(index + 1) + "笔";
    }
}

long FileSize(FILE* file) {
    if (!file || fseek(file, 0, SEEK_END) != 0)
        return -1;
    const auto size = ftell(file);
    rewind(file);
    return size;
}

uint32_t Crc32(const std::string& data) {
    uint32_t crc = 0xffffffff;
    for (const uint8_t value : data) {
        crc ^= value;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320 & (0 - (crc & 1)));
    }
    return crc ^ 0xffffffff;
}

std::string Text(cJSON* object, const char* key, size_t max = 2048) {
    if (!object)
        return {};
    auto value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(value) || !value->valuestring)
        return {};
    std::string result(value->valuestring);
    return result.size() <= max ? result : std::string();
}
bool Strings(cJSON* object, const char* key, std::vector<std::string>& out, int max) {
    auto array = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) > max)
        return false;
    cJSON* item;
    cJSON_ArrayForEach (item, array) {
        if (!cJSON_IsString(item) || !item->valuestring || strlen(item->valuestring) > 96)
            return false;
        out.emplace_back(item->valuestring);
    }
    return true;
}
uint32_t Codepoint(const std::string& s) {
    if (s.size() != 3)
        return 0;
    const auto* p = reinterpret_cast<const uint8_t*>(s.data());
    if (p[0] < 0xe4 || p[0] > 0xe9 || (p[1] & 0xc0) != 0x80 || (p[2] & 0xc0) != 0x80)
        return 0;
    auto cp = ((p[0] & 15) << 12) | ((p[1] & 63) << 6) | (p[2] & 63);
    return cp >= 0x4e00 && cp <= 0x9fff ? cp : 0;
}
}  // namespace

Entry ContentStore::Demo() {
    Entry e;
    e.character = "规";
    e.pinyin = "guī";
    e.radical = "见";
    e.structure = "左右结构";
    e.definition = "规则、章程或一定的标准；也表示劝告、谋划。";
    e.stroke_count = 8;
    e.words = {"规矩", "规则", "规定", "规划"};
    e.strokes = {"横", "横", "撇", "点", "竖", "横折", "撇", "竖弯钩"};
    return e;
}

std::string ContentStore::TargetCharacter(const std::string& query) {
    if (query.size() > 128)
        return {};
    if (Codepoint(query))
        return query;
    // Accept an explicit "…的规" target; do not choose any character merely found in a sentence.
    const auto pos = query.rfind("的");
    if (pos != std::string::npos && query.size() >= pos + 6) {
        auto result = query.substr(pos + 3, 3);
        if (Codepoint(result))
            return result;
    }
    return {};
}

std::string ContentStore::NormalizePinyin(const std::string& query) {
    std::string value;
    value.reserve(query.size());
    for (size_t index = 0; index < query.size(); ++index) {
        const auto character = static_cast<unsigned char>(query[index]);
        if (std::isspace(character))
            continue;
        if ((character == 'u' || character == 'U') && index + 1 < query.size() &&
            query[index + 1] == ':') {
            value.push_back('v');
            ++index;
        } else if (character >= 'A' && character <= 'Z') {
            value.push_back(static_cast<char>(character - 'A' + 'a'));
        } else if (character >= 'a' && character <= 'z') {
            value.push_back(static_cast<char>(character));
        } else if (character >= '1' && character <= '5') {
            bool only_spaces_follow = true;
            for (size_t tail = index + 1; tail < query.size(); ++tail)
                only_spaces_follow =
                    only_spaces_follow && std::isspace(static_cast<unsigned char>(query[tail]));
            if (!only_spaces_follow)
                return {};
        } else {
            return {};
        }
    }
    return value.empty() || value.size() > 7 ? std::string() : value;
}

bool ContentStore::Read(const std::string& relative, std::string& data, size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    data.clear();
    if (!available_)
        return false;
    if (relative.empty() || relative.front() == '/' || relative.find("..") != std::string::npos ||
        relative.find_first_of("\\:\0", 0, 3) != std::string::npos)
        return false;
    FILE* file = fopen((root_ + "/" + relative).c_str(), "rb");
    if (!file)
        return false;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    const auto size = ftell(file);
    if (size <= 0 || static_cast<size_t>(size) > limit) {
        fclose(file);
        return false;
    }
    rewind(file);
    data.resize(size);
    const bool ok = fread(data.data(), 1, size, file) == static_cast<size_t>(size);
    fclose(file);
    if (!ok)
        data.clear();
    return ok;
}

bool ContentStore::Initialize() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string data;
    available_ = true;
    ready_ = false;
    indexed_ready_ = false;
    indexed_records_ = 0;
    indexed_data_size_ = 0;
    dictionary_font_size_ = 0;
    dictionary_font_crc_ = 0;
    stroke_index_ready_ = false;
    stroke_records_ = 0;
    stroke_data_size_ = 0;
    pinyin_index_ready_ = false;
    pinyin_records_ = 0;
    pinyin_directory_size_ = 0;
    pinyin_data_size_ = 0;
    if (!Read("manifest.json", data, 4096))
        return false;
    Json root(cJSON_Parse(data.c_str()), cJSON_Delete);
    if (!root) {
        notice_ = "内容包格式错误，使用内置示例";
        return false;
    }
    auto version = cJSON_GetObjectItemCaseSensitive(root.get(), "schema_version");
    if (!cJSON_IsNumber(version) || version->valuedouble != 1) {
        notice_ = "内容包版本不支持，使用内置示例";
        return false;
    }
    ready_ = true;
    notice_ = "SD 内容包已加载";

    const auto index_path = root_ + "/dictionary/index.bin";
    const auto data_path = root_ + "/dictionary/data.bin";
    FILE* index = fopen(index_path.c_str(), "rb");
    FILE* records = fopen(data_path.c_str(), "rb");
    uint8_t header[kIndexHeaderSize]{};
    if (index && records &&
        FileSize(index) == static_cast<long>(kIndexHeaderSize + kIndexedSlots * kIndexSlotSize) &&
        fread(header, 1, sizeof(header), index) == sizeof(header) &&
        memcmp(header, "HDX1", 4) == 0 && ReadLe32(header + 4) == kIndexVersion &&
        ReadLe32(header + 8) == kFirstIndexedCodepoint && ReadLe32(header + 12) == kIndexedSlots &&
        ReadLe32(header + 20) <= 64 * 1024 * 1024 &&
        FileSize(records) == static_cast<long>(ReadLe32(header + 20))) {
        indexed_ready_ = true;
        indexed_records_ = ReadLe32(header + 16);
        indexed_data_size_ = ReadLe32(header + 20);
        notice_ = "SD 索引字典已加载（" + std::to_string(indexed_records_) + "字）";
    }
    if (index)
        fclose(index);
    if (records)
        fclose(records);

    const auto stroke_index_path = root_ + "/dictionary/strokes.idx";
    const auto stroke_data_path = root_ + "/dictionary/strokes.dat";
    index = fopen(stroke_index_path.c_str(), "rb");
    records = fopen(stroke_data_path.c_str(), "rb");
    memset(header, 0, sizeof(header));
    if (index && records &&
        FileSize(index) == static_cast<long>(kIndexHeaderSize + kIndexedSlots * kIndexSlotSize) &&
        fread(header, 1, sizeof(header), index) == sizeof(header) &&
        memcmp(header, "HST1", 4) == 0 && ReadLe32(header + 4) == kStrokeIndexVersion &&
        ReadLe32(header + 8) == kFirstIndexedCodepoint && ReadLe32(header + 12) == kIndexedSlots &&
        ReadLe32(header + 20) <= 32 * 1024 * 1024 &&
        FileSize(records) == static_cast<long>(ReadLe32(header + 20))) {
        stroke_index_ready_ = true;
        stroke_records_ = ReadLe32(header + 16);
        stroke_data_size_ = ReadLe32(header + 20);
    }
    if (index)
        fclose(index);
    if (records)
        fclose(records);
    const auto pinyin_path = root_ + "/dictionary/pinyin.idx";
    FILE* pinyin = fopen(pinyin_path.c_str(), "rb");
    memset(header, 0, sizeof(header));
    if (pinyin && fread(header, 1, kPinyinHeaderSize, pinyin) == kPinyinHeaderSize &&
        memcmp(header, "HPY1", 4) == 0 && ReadLe32(header + 4) == kPinyinIndexVersion) {
        const uint32_t record_count = ReadLe32(header + 8);
        const uint32_t directory_size = ReadLe32(header + 12);
        const uint32_t data_size = ReadLe32(header + 16);
        const long expected_size =
            static_cast<long>(kPinyinHeaderSize + directory_size + data_size);
        bool valid = record_count > 0 && record_count <= kPinyinMaxRecords &&
                     directory_size == record_count * kPinyinDirectorySize &&
                     data_size <= kPinyinDataLimit && FileSize(pinyin) == expected_size;
        std::string previous;
        uint32_t expected_offset = 0;
        for (uint32_t record = 0; valid && record < record_count; ++record) {
            uint8_t directory[kPinyinDirectorySize]{};
            valid =
                fseek(pinyin, static_cast<long>(kPinyinHeaderSize + record * kPinyinDirectorySize),
                      SEEK_SET) == 0 &&
                fread(directory, 1, sizeof(directory), pinyin) == sizeof(directory);
            size_t key_size = 0;
            while (key_size < 8 && directory[key_size])
                ++key_size;
            std::string key(reinterpret_cast<char*>(directory), key_size);
            const uint32_t offset = ReadLe32(directory + 8);
            const uint16_t candidates = ReadLe16(directory + 12);
            valid = valid && key_size > 0 && key_size <= 7 && NormalizePinyin(key) == key &&
                    key > previous && candidates > 0 && candidates <= 1024 &&
                    ReadLe16(directory + 14) == 0 && offset == expected_offset &&
                    offset <= data_size &&
                    static_cast<uint32_t>(candidates) * 3 <= data_size - offset;
            previous = std::move(key);
            expected_offset += static_cast<uint32_t>(candidates) * 3;
        }
        if (valid && expected_offset == data_size) {
            pinyin_index_ready_ = true;
            pinyin_records_ = record_count;
            pinyin_directory_size_ = directory_size;
            pinyin_data_size_ = data_size;
        }
    }
    if (pinyin)
        fclose(pinyin);
    auto indexed = cJSON_GetObjectItemCaseSensitive(root.get(), "indexed_dictionary");
    auto font = cJSON_GetObjectItemCaseSensitive(indexed, "font");
    auto font_path = cJSON_GetObjectItemCaseSensitive(font, "path");
    auto font_size = cJSON_GetObjectItemCaseSensitive(font, "bytes");
    auto font_crc = cJSON_GetObjectItemCaseSensitive(font, "crc32");
    if (indexed_ready_ && cJSON_IsString(font_path) &&
        strcmp(font_path->valuestring, "dictionary/font-28-1.bin") == 0 &&
        cJSON_IsNumber(font_size) && font_size->valuedouble == font_size->valueint &&
        font_size->valueint >= 128 * 1024 && font_size->valueint <= 4 * 1024 * 1024 &&
        cJSON_IsNumber(font_crc) && font_crc->valuedouble >= 0 &&
        font_crc->valuedouble <= UINT32_MAX) {
        FILE* dictionary_font = fopen((root_ + "/dictionary/font-28-1.bin").c_str(), "rb");
        if (dictionary_font && FileSize(dictionary_font) == font_size->valueint) {
            dictionary_font_size_ = font_size->valueint;
            dictionary_font_crc_ = static_cast<uint32_t>(font_crc->valuedouble);
        }
        if (dictionary_font)
            fclose(dictionary_font);
    }
    return true;
}

void ContentStore::Detach(const std::string& notice) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    available_ = false;
    ready_ = false;
    indexed_ready_ = false;
    stroke_index_ready_ = false;
    pinyin_index_ready_ = false;
    dictionary_font_size_ = 0;
    dictionary_font_crc_ = 0;
    notice_ = notice;
}

bool ContentStore::ReadDictionaryFont(std::string& data) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!indexed_ready_ || dictionary_font_size_ == 0 ||
        !Read("dictionary/font-28-1.bin", data, 4 * 1024 * 1024) ||
        data.size() != dictionary_font_size_ || Crc32(data) != dictionary_font_crc_) {
        data.clear();
        return false;
    }
    return true;
}

bool ContentStore::ReadStrokeGlyph(const std::string& character, StrokeGlyph& glyph) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    glyph = {};
    const uint32_t codepoint = Codepoint(character);
    if (!ready_ || !stroke_index_ready_ || codepoint < kFirstIndexedCodepoint ||
        codepoint >= kFirstIndexedCodepoint + kIndexedSlots)
        return false;
    FILE* index = fopen((root_ + "/dictionary/strokes.idx").c_str(), "rb");
    if (!index)
        return false;
    const auto position =
        kIndexHeaderSize + static_cast<size_t>(codepoint - kFirstIndexedCodepoint) * kIndexSlotSize;
    uint8_t slot[kIndexSlotSize]{};
    const bool slot_ok = fseek(index, static_cast<long>(position), SEEK_SET) == 0 &&
                         fread(slot, 1, sizeof(slot), index) == sizeof(slot);
    fclose(index);
    const uint32_t offset = ReadLe32(slot);
    const uint32_t length = ReadLe32(slot + 4);
    if (!slot_ok || length < kStrokeHeaderSize || length > kStrokeRecordLimit ||
        offset > stroke_data_size_ || length > stroke_data_size_ - offset)
        return false;
    FILE* records = fopen((root_ + "/dictionary/strokes.dat").c_str(), "rb");
    if (!records)
        return false;
    std::string data(length, '\0');
    const bool read_ok = fseek(records, static_cast<long>(offset), SEEK_SET) == 0 &&
                         fread(data.data(), 1, length, records) == length;
    fclose(records);
    if (!read_ok)
        return false;
    const auto* bytes = reinterpret_cast<const uint8_t*>(data.data());
    const uint8_t count = bytes[5];
    const uint16_t header_size = ReadLe16(bytes + 6);
    if (memcmp(bytes, "HSG1", 4) != 0 || bytes[4] != 1 || count == 0 || count > 64 ||
        header_size != kStrokeHeaderSize + count * kStrokeDirectorySize || header_size > length)
        return false;
    StrokeGlyph candidate;
    candidate.character = character;
    candidate.strokes.reserve(count);
    for (size_t stroke_index = 0; stroke_index < count; ++stroke_index) {
        const auto* directory = bytes + kStrokeHeaderSize + stroke_index * kStrokeDirectorySize;
        const uint32_t path_offset = ReadLe32(directory);
        const uint32_t path_length = ReadLe32(directory + 4);
        const uint32_t median_offset = ReadLe32(directory + 8);
        const uint16_t median_count = ReadLe16(directory + 12);
        if (path_length == 0 || path_offset < header_size || path_offset > length ||
            path_length > length - path_offset || median_offset < path_offset + path_length ||
            median_offset > length ||
            static_cast<uint32_t>(median_count) * 4 > length - median_offset)
            return false;
        StrokeShape shape;
        shape.name = StrokeName(directory[14], stroke_index);
        shape.radical = (directory[15] & 1) != 0;
        size_t cursor = path_offset;
        const size_t path_end = path_offset + path_length;
        while (cursor < path_end) {
            StrokeCommand command;
            command.op = bytes[cursor++];
            static constexpr uint8_t kPointCounts[] = {1, 1, 2, 3, 0};
            if (command.op >= sizeof(kPointCounts))
                return false;
            command.point_count = kPointCounts[command.op];
            const size_t coordinate_bytes = command.point_count * 4;
            if (coordinate_bytes > path_end - cursor)
                return false;
            for (size_t point = 0; point < command.point_count; ++point) {
                command.points[point].x = ReadLeS16(bytes + cursor);
                command.points[point].y = ReadLeS16(bytes + cursor + 2);
                cursor += 4;
            }
            shape.commands.push_back(command);
        }
        shape.medians.reserve(median_count);
        for (size_t point = 0; point < median_count; ++point) {
            const auto* value = bytes + median_offset + point * 4;
            shape.medians.push_back({ReadLeS16(value), ReadLeS16(value + 2)});
        }
        candidate.strokes.push_back(std::move(shape));
    }
    glyph = std::move(candidate);
    return true;
}

bool ContentStore::ParseEntry(const std::string& json, const std::string& character, Entry& entry) {
    if (json.size() > 16384)
        return false;
    Json root(cJSON_Parse(json.c_str()), cJSON_Delete);
    if (!root)
        return false;
    Entry candidate;
    candidate.character = Text(root.get(), "character", 4);
    candidate.pinyin = Text(root.get(), "pinyin", 80);
    candidate.radical = Text(root.get(), "radical", 24);
    candidate.structure = Text(root.get(), "structure", 48);
    candidate.definition = Text(root.get(), "definition");
    candidate.source = Text(root.get(), "source", 128);
    auto count = cJSON_GetObjectItemCaseSensitive(root.get(), "stroke_count");
    if (candidate.character != character || !Codepoint(character) || candidate.pinyin.empty() ||
        candidate.definition.empty() || candidate.source.empty() || !cJSON_IsNumber(count) ||
        count->valuedouble != count->valueint || count->valueint < 0 || count->valueint > 64 ||
        !Strings(root.get(), "words", candidate.words, 12) ||
        !Strings(root.get(), "stroke_order", candidate.strokes, 64))
        return false;
    candidate.stroke_count = count->valueint;
    if (candidate.strokes.size() != static_cast<size_t>(candidate.stroke_count))
        return false;
    entry = std::move(candidate);
    return true;
}

bool ContentStore::ReadIndexed(uint32_t codepoint, std::string& data) const {
    data.clear();
    if (!indexed_ready_ || codepoint < kFirstIndexedCodepoint ||
        codepoint >= kFirstIndexedCodepoint + kIndexedSlots)
        return false;

    FILE* index = fopen((root_ + "/dictionary/index.bin").c_str(), "rb");
    if (!index)
        return false;
    const auto position =
        kIndexHeaderSize + static_cast<size_t>(codepoint - kFirstIndexedCodepoint) * kIndexSlotSize;
    uint8_t slot[kIndexSlotSize]{};
    const bool index_ok = fseek(index, static_cast<long>(position), SEEK_SET) == 0 &&
                          fread(slot, 1, sizeof(slot), index) == sizeof(slot);
    fclose(index);
    if (!index_ok)
        return false;

    const uint32_t offset = ReadLe32(slot);
    const uint32_t length = ReadLe32(slot + 4);
    if (length == 0 || length > kIndexedEntryLimit || offset > indexed_data_size_ ||
        length > indexed_data_size_ - offset)
        return false;

    FILE* records = fopen((root_ + "/dictionary/data.bin").c_str(), "rb");
    if (!records)
        return false;
    data.resize(length);
    const bool ok = fseek(records, static_cast<long>(offset), SEEK_SET) == 0 &&
                    fread(data.data(), 1, length, records) == length;
    fclose(records);
    if (!ok)
        data.clear();
    return ok;
}

bool ContentStore::Lookup(const std::string& query, Entry& entry) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto character = TargetCharacter(query);
    if (character.empty())
        return false;
    if (ready_) {
        char relative[64];
        snprintf(relative, sizeof(relative), "dictionary/entries/%04X.json",
                 static_cast<unsigned>(Codepoint(character)));
        std::string data;
        if (Read(relative, data, 16384) && ParseEntry(data, character, entry)) {
            StrokeGlyph glyph;
            if (ReadStrokeGlyph(character, glyph)) {
                entry.stroke_count = static_cast<int>(glyph.strokes.size());
                entry.strokes.clear();
                for (const auto& stroke : glyph.strokes)
                    entry.strokes.push_back(stroke.name);
            }
            return true;
        }
        if (ReadIndexed(Codepoint(character), data) && ParseEntry(data, character, entry)) {
            StrokeGlyph glyph;
            if (ReadStrokeGlyph(character, glyph)) {
                entry.stroke_count = static_cast<int>(glyph.strokes.size());
                entry.strokes.clear();
                for (const auto& stroke : glyph.strokes)
                    entry.strokes.push_back(stroke.name);
            }
            return true;
        }
    }
    if (character == "规") {
        entry = Demo();
        return true;
    }
    return false;
}

bool ContentStore::SearchPinyin(const std::string& query, std::vector<std::string>& characters,
                                size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    characters.clear();
    const auto key = NormalizePinyin(query);
    if (!pinyin_index_ready_ || key.empty() || limit == 0)
        return false;
    FILE* file = fopen((root_ + "/dictionary/pinyin.idx").c_str(), "rb");
    if (!file)
        return false;
    uint32_t low = 0, high = pinyin_records_;
    uint8_t directory[kPinyinDirectorySize]{};
    bool found = false;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2;
        if (fseek(file, static_cast<long>(kPinyinHeaderSize + middle * kPinyinDirectorySize),
                  SEEK_SET) != 0 ||
            fread(directory, 1, sizeof(directory), file) != sizeof(directory))
            break;
        size_t length = 0;
        while (length < 8 && directory[length])
            ++length;
        const std::string candidate(reinterpret_cast<char*>(directory), length);
        if (candidate < key)
            low = middle + 1;
        else if (candidate > key)
            high = middle;
        else {
            found = true;
            break;
        }
    }
    if (!found) {
        fclose(file);
        return false;
    }
    const uint32_t offset = ReadLe32(directory + 8);
    const size_t count = std::min<size_t>(ReadLe16(directory + 12), std::min<size_t>(limit, 24));
    if (offset > pinyin_data_size_ || count * 3 > pinyin_data_size_ - offset ||
        fseek(file, static_cast<long>(kPinyinHeaderSize + pinyin_directory_size_ + offset),
              SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    std::string data(count * 3, '\0');
    const bool ok = fread(data.data(), 1, data.size(), file) == data.size();
    fclose(file);
    if (!ok)
        return false;
    characters.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        auto character = data.substr(index * 3, 3);
        if (!Codepoint(character)) {
            characters.clear();
            return false;
        }
        characters.push_back(std::move(character));
    }
    return !characters.empty();
}
}  // namespace han
