#include "content_store.h"

#include <cJSON.h>
#include <cstdio>
#include <cstring>
#include <memory>

namespace han {
namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
std::string Text(cJSON* object, const char* key, size_t max = 2048) {
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
    return true;
}

void ContentStore::Detach(const std::string& notice) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    available_ = false;
    ready_ = false;
    notice_ = notice;
}

std::string ContentStore::StrokePath(const std::string& character, int frame) {
    if (!Codepoint(character) || frame < 0 || frame >= 64)
        return {};
    char path[64];
    snprintf(path, sizeof(path), "dictionary/strokes/%04X/%02d.png",
             static_cast<unsigned>(Codepoint(character)), frame + 1);
    return path;
}

bool ContentStore::IsStrokePng(const std::string& data) {
    // Bound decoded dimensions as well as compressed size before passing to the PNG decoder.
    static constexpr unsigned char header[] = {137, 80, 78,  71,  13,  10,  26, 10, 0, 0,
                                               0,   13, 'I', 'H', 'D', 'R', 0,  0,  1, 44,
                                               0,   0,  1,   44,  8,   6,   0,  0,  0};
    static constexpr unsigned char end[] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 174, 66, 96, 130};
    return data.size() >= 45 && data.size() <= 128 * 1024 &&
           memcmp(data.data(), header, sizeof(header)) == 0 &&
           memcmp(data.data() + data.size() - sizeof(end), end, sizeof(end)) == 0;
}

bool ContentStore::ReadStroke(const std::string& path, std::string& data) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!ready_ || path.compare(0, 19, "dictionary/strokes/") != 0 ||
        !Read(path, data, 128 * 1024) || !IsStrokePng(data)) {
        data.clear();
        return false;
    }
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
        count->valuedouble != count->valueint || count->valueint < 1 || count->valueint > 64 ||
        !Strings(root.get(), "words", candidate.words, 12) ||
        !Strings(root.get(), "stroke_order", candidate.strokes, 64))
        return false;
    candidate.stroke_count = count->valueint;
    if (candidate.strokes.size() != static_cast<size_t>(candidate.stroke_count))
        return false;
    auto ref = cJSON_GetObjectItemCaseSensitive(root.get(), "reference");
    candidate.isbn = Text(ref, "isbn", 32);
    auto page = cJSON_GetObjectItemCaseSensitive(ref, "page");
    auto verified = cJSON_GetObjectItemCaseSensitive(ref, "verified");
    if (Text(ref, "edition", 80) == candidate.edition && candidate.isbn == kDictionaryIsbn &&
        cJSON_IsTrue(verified) && cJSON_IsNumber(page) && page->valuedouble == page->valueint &&
        page->valueint > 0 && page->valueint < 10000)
        candidate.page = page->valueint;
    entry = std::move(candidate);
    return true;
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
        if (Read(relative, data, 16384) && ParseEntry(data, character, entry))
            return true;
    }
    if (character == "规") {
        entry = Demo();
        return true;
    }
    return false;
}
}  // namespace han
