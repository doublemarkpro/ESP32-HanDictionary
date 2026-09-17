#include "content_store.h"

#include <cJSON.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

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
// Put familiar school-age characters ahead of rare homophones without dropping any candidate.
// Keep the importer's list in sync so newly generated pinyin indexes have the same ordering.
constexpr char kCommonCharacters[] =
    "的一是不了人我在有他这为之大来以个中上们到说国和地也子时道出而要于就下得可你"
    "年生自会那后能对着事其里所去趣行过家十用发天如然作方成者多日都三小军二无同么"
    "经法当起与好看学进种将还分此心前面又定见只主没公从知全工己使情明性汉规矩"
    "外想实把做本点现因些正更美次动话合回加向间问很最头新样体别她老名长比内路化"
    "任给第门相应开手但重身放常西气五直总四场由书它高意真才度海安口连难望风教"
    "受车空带今满变数东声该记少保报结反处目太快关原认志几何光社非德强平形利清"
    "等部月象物世文感表战果被解许写信爱至神量级近江期识造取根论运农指区白条系乐"
    "每林住队南色打收告先王亲边怕服早院吃房音火际则完治导器确容必整置百须改周况"
    "查找字典拼读习校师友父母哥姐弟妹春夏秋冬山水花草木石土金雨雪云电星日月早晚"
    "午夜红黄蓝绿黑白大小多少上下左右前后里外东西南北一二三四五六七八九零语数学"
    "英语体育音乐美术科学劳动信息班会社团课本笔画偏旁结构组词意思天气闹钟课程作业";

size_t CommonCharacterRank(const std::string& character) {
    if (character.size() != 3)
        return std::numeric_limits<size_t>::max();
    const auto match = strstr(kCommonCharacters, character.c_str());
    return match ? static_cast<size_t>(match - kCommonCharacters) / 3
                 : std::numeric_limits<size_t>::max();
}

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

bool DecodeUtf8(const std::string& text, size_t& offset, uint32_t& codepoint) {
    if (offset >= text.size())
        return false;
    const auto first = static_cast<uint8_t>(text[offset++]);
    if (first < 0x80) {
        codepoint = first;
        return true;
    }
    const int tails = (first & 0xe0) == 0xc0 ? 1 : (first & 0xf0) == 0xe0 ? 2 : 0;
    if (!tails || offset + tails > text.size())
        return false;
    codepoint = first & (tails == 1 ? 0x1f : 0x0f);
    for (int index = 0; index < tails; ++index) {
        const auto next = static_cast<uint8_t>(text[offset++]);
        if ((next & 0xc0) != 0x80)
            return false;
        codepoint = (codepoint << 6) | (next & 0x3f);
    }
    return true;
}

bool PinyinMatchesTone(const std::string& value, const std::string& expected, int expected_tone) {
    std::string syllable;
    int tone = 0;
    auto finish = [&] {
        const bool match = syllable == expected && tone == expected_tone;
        syllable.clear();
        tone = 0;
        return match;
    };
    size_t offset = 0;
    while (offset < value.size()) {
        uint32_t cp = 0;
        if (!DecodeUtf8(value, offset, cp))
            return false;
        char letter = 0;
        int marked_tone = 0;
        if (cp >= 'A' && cp <= 'Z')
            letter = static_cast<char>(cp - 'A' + 'a');
        else if (cp >= 'a' && cp <= 'z')
            letter = static_cast<char>(cp);
        else {
            switch (cp) {
                case 0x0101:
                    letter = 'a';
                    marked_tone = 1;
                    break;
                case 0x00e1:
                    letter = 'a';
                    marked_tone = 2;
                    break;
                case 0x01ce:
                    letter = 'a';
                    marked_tone = 3;
                    break;
                case 0x00e0:
                    letter = 'a';
                    marked_tone = 4;
                    break;
                case 0x0113:
                    letter = 'e';
                    marked_tone = 1;
                    break;
                case 0x00e9:
                    letter = 'e';
                    marked_tone = 2;
                    break;
                case 0x011b:
                    letter = 'e';
                    marked_tone = 3;
                    break;
                case 0x00e8:
                    letter = 'e';
                    marked_tone = 4;
                    break;
                case 0x012b:
                    letter = 'i';
                    marked_tone = 1;
                    break;
                case 0x00ed:
                    letter = 'i';
                    marked_tone = 2;
                    break;
                case 0x01d0:
                    letter = 'i';
                    marked_tone = 3;
                    break;
                case 0x00ec:
                    letter = 'i';
                    marked_tone = 4;
                    break;
                case 0x014d:
                    letter = 'o';
                    marked_tone = 1;
                    break;
                case 0x00f3:
                    letter = 'o';
                    marked_tone = 2;
                    break;
                case 0x01d2:
                    letter = 'o';
                    marked_tone = 3;
                    break;
                case 0x00f2:
                    letter = 'o';
                    marked_tone = 4;
                    break;
                case 0x016b:
                    letter = 'u';
                    marked_tone = 1;
                    break;
                case 0x00fa:
                    letter = 'u';
                    marked_tone = 2;
                    break;
                case 0x01d4:
                    letter = 'u';
                    marked_tone = 3;
                    break;
                case 0x00f9:
                    letter = 'u';
                    marked_tone = 4;
                    break;
                case 0x00fc:
                    letter = 'v';
                    break;
                case 0x01d6:
                    letter = 'v';
                    marked_tone = 1;
                    break;
                case 0x01d8:
                    letter = 'v';
                    marked_tone = 2;
                    break;
                case 0x01da:
                    letter = 'v';
                    marked_tone = 3;
                    break;
                case 0x01dc:
                    letter = 'v';
                    marked_tone = 4;
                    break;
                case 0x0144:
                    letter = 'n';
                    marked_tone = 2;
                    break;
                case 0x0148:
                    letter = 'n';
                    marked_tone = 3;
                    break;
                case 0x01f9:
                    letter = 'n';
                    marked_tone = 4;
                    break;
                case 0x1e3f:
                    letter = 'm';
                    marked_tone = 2;
                    break;
                default:
                    break;
            }
        }
        if (letter) {
            syllable.push_back(letter);
            if (marked_tone)
                tone = marked_tone;
        } else if (cp >= '1' && cp <= '5') {
            tone = cp == '5' ? 0 : static_cast<int>(cp - '0');
        } else if (finish()) {
            return true;
        }
    }
    return finish();
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
    char tone = 0;
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
        } else if (character >= '0' && character <= '5') {
            bool only_spaces_follow = true;
            for (size_t tail = index + 1; tail < query.size(); ++tail)
                only_spaces_follow =
                    only_spaces_follow && std::isspace(static_cast<unsigned char>(query[tail]));
            if (!only_spaces_follow || tone)
                return {};
            tone = character == '5' ? '0' : static_cast<char>(character);
        } else {
            return {};
        }
    }
    if (value.empty() || value.size() > 7)
        return {};
    if (tone)
        value.push_back(tone);
    return value;
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
    try {
        data.resize(size);
    } catch (const std::bad_alloc&) {
        fclose(file);
        data.clear();
        return false;
    }
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
    dictionary_font_path_.clear();
    dictionary_candidate_font_size_ = 0;
    dictionary_candidate_font_crc_ = 0;
    dictionary_candidate_font_path_.clear();
    dictionary_scalable_font_path_.clear();
    dictionary_candidate_scalable_font_path_.clear();
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
            valid = valid && key_size > 0 && key_size <= 8 && NormalizePinyin(key) == key &&
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
    auto font_bpp = cJSON_GetObjectItemCaseSensitive(font, "bpp");
    auto font_size = cJSON_GetObjectItemCaseSensitive(font, "bytes");
    auto font_crc = cJSON_GetObjectItemCaseSensitive(font, "crc32");
    const bool legacy_font = cJSON_IsString(font_path) && cJSON_IsNumber(font_bpp) &&
                             strcmp(font_path->valuestring, "dictionary/font-28-1.bin") == 0 &&
                             font_bpp->valueint == 1;
    const bool smooth_font = cJSON_IsString(font_path) && cJSON_IsNumber(font_bpp) &&
                             strcmp(font_path->valuestring, "dictionary/font-28-2.bin") == 0 &&
                             font_bpp->valueint == 2;
    if (indexed_ready_ && (legacy_font || smooth_font) && cJSON_IsNumber(font_size) &&
        font_size->valuedouble == font_size->valueint && font_size->valueint >= 128 * 1024 &&
        font_size->valueint <= 4 * 1024 * 1024 && cJSON_IsNumber(font_crc) &&
        font_crc->valuedouble >= 0 && font_crc->valuedouble <= UINT32_MAX) {
        dictionary_font_path_ = font_path->valuestring;
        FILE* dictionary_font = fopen((root_ + "/" + dictionary_font_path_).c_str(), "rb");
        if (dictionary_font && FileSize(dictionary_font) == font_size->valueint) {
            dictionary_font_size_ = font_size->valueint;
            dictionary_font_crc_ = static_cast<uint32_t>(font_crc->valuedouble);
        }
        if (dictionary_font)
            fclose(dictionary_font);
    }
    auto candidate_font = cJSON_GetObjectItemCaseSensitive(indexed, "candidate_font");
    auto candidate_font_path = cJSON_GetObjectItemCaseSensitive(candidate_font, "path");
    auto candidate_font_size = cJSON_GetObjectItemCaseSensitive(candidate_font, "bytes");
    auto candidate_font_bpp = cJSON_GetObjectItemCaseSensitive(candidate_font, "bpp");
    auto candidate_font_height = cJSON_GetObjectItemCaseSensitive(candidate_font, "size");
    auto candidate_font_crc = cJSON_GetObjectItemCaseSensitive(candidate_font, "crc32");
    const bool legacy_candidate_font =
        cJSON_IsString(candidate_font_path) && cJSON_IsNumber(candidate_font_height) &&
        strcmp(candidate_font_path->valuestring, "dictionary/font-40-1.bin") == 0 &&
        candidate_font_height->valueint == 40;
    const bool large_candidate_font =
        cJSON_IsString(candidate_font_path) && cJSON_IsNumber(candidate_font_height) &&
        (strcmp(candidate_font_path->valuestring, "dictionary/font-56-kai-1.bin") == 0 ||
         strcmp(candidate_font_path->valuestring, "dictionary/font-56-heavy-1.bin") == 0) &&
        candidate_font_height->valueint == 56;
    if (indexed_ready_ && cJSON_IsString(candidate_font_path) &&
        (legacy_candidate_font || large_candidate_font) && cJSON_IsNumber(candidate_font_bpp) &&
        candidate_font_bpp->valueint == 1 && cJSON_IsNumber(candidate_font_size) &&
        candidate_font_size->valuedouble == candidate_font_size->valueint &&
        candidate_font_size->valueint >= 128 * 1024 &&
        candidate_font_size->valueint <= (large_candidate_font ? 8 : 4) * 1024 * 1024 &&
        cJSON_IsNumber(candidate_font_crc) && candidate_font_crc->valuedouble >= 0 &&
        candidate_font_crc->valuedouble <= UINT32_MAX) {
        dictionary_candidate_font_path_ = candidate_font_path->valuestring;
        FILE* file = fopen((root_ + "/" + dictionary_candidate_font_path_).c_str(), "rb");
        if (file && FileSize(file) == candidate_font_size->valueint) {
            dictionary_candidate_font_size_ = candidate_font_size->valueint;
            dictionary_candidate_font_crc_ = static_cast<uint32_t>(candidate_font_crc->valuedouble);
        }
        if (file)
            fclose(file);
    }
    auto scalable_font = cJSON_GetObjectItemCaseSensitive(indexed, "scalable_font");
    auto scalable_font_path = cJSON_GetObjectItemCaseSensitive(scalable_font, "path");
    auto scalable_font_format = cJSON_GetObjectItemCaseSensitive(scalable_font, "format");
    auto scalable_font_size = cJSON_GetObjectItemCaseSensitive(scalable_font, "bytes");
    if (indexed_ready_ && cJSON_IsString(scalable_font_path) &&
        strcmp(scalable_font_path->valuestring, "dictionary/SourceHanSansSC-Normal.otf") == 0 &&
        cJSON_IsString(scalable_font_format) &&
        strcmp(scalable_font_format->valuestring, "opentype") == 0 &&
        cJSON_IsNumber(scalable_font_size) &&
        scalable_font_size->valuedouble == scalable_font_size->valueint &&
        scalable_font_size->valueint >= 1024 * 1024 &&
        scalable_font_size->valueint <= 32 * 1024 * 1024) {
        const std::string relative = scalable_font_path->valuestring;
        FILE* scalable = fopen((root_ + "/" + relative).c_str(), "rb");
        if (scalable && FileSize(scalable) == scalable_font_size->valueint)
            dictionary_scalable_font_path_ = "S:" + root_ + "/" + relative;
        if (scalable)
            fclose(scalable);
    }
    auto candidate_scalable_font =
        cJSON_GetObjectItemCaseSensitive(indexed, "candidate_scalable_font");
    auto candidate_scalable_path =
        cJSON_GetObjectItemCaseSensitive(candidate_scalable_font, "path");
    auto candidate_scalable_format =
        cJSON_GetObjectItemCaseSensitive(candidate_scalable_font, "format");
    auto candidate_scalable_size =
        cJSON_GetObjectItemCaseSensitive(candidate_scalable_font, "bytes");
    auto candidate_scalable_height =
        cJSON_GetObjectItemCaseSensitive(candidate_scalable_font, "size");
    if (indexed_ready_ && cJSON_IsString(candidate_scalable_path) &&
        strcmp(candidate_scalable_path->valuestring, "dictionary/NotoSansSC-Medium.ttf") == 0 &&
        cJSON_IsString(candidate_scalable_format) &&
        strcmp(candidate_scalable_format->valuestring, "truetype") == 0 &&
        cJSON_IsNumber(candidate_scalable_height) && candidate_scalable_height->valueint == 56 &&
        cJSON_IsNumber(candidate_scalable_size) &&
        candidate_scalable_size->valuedouble == candidate_scalable_size->valueint &&
        candidate_scalable_size->valueint >= 1024 * 1024 &&
        candidate_scalable_size->valueint <= 32 * 1024 * 1024) {
        const std::string relative = candidate_scalable_path->valuestring;
        FILE* scalable = fopen((root_ + "/" + relative).c_str(), "rb");
        if (scalable && FileSize(scalable) == candidate_scalable_size->valueint)
            dictionary_candidate_scalable_font_path_ = "S:" + root_ + "/" + relative;
        if (scalable)
            fclose(scalable);
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
    dictionary_font_path_.clear();
    dictionary_candidate_font_size_ = 0;
    dictionary_candidate_font_crc_ = 0;
    dictionary_candidate_font_path_.clear();
    dictionary_scalable_font_path_.clear();
    dictionary_candidate_scalable_font_path_.clear();
    notice_ = notice;
}

bool ContentStore::ReadDictionaryFont(std::string& data) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!indexed_ready_ || dictionary_font_size_ == 0 || dictionary_font_path_.empty() ||
        !Read(dictionary_font_path_, data, 4 * 1024 * 1024) ||
        data.size() != dictionary_font_size_ || Crc32(data) != dictionary_font_crc_) {
        data.clear();
        return false;
    }
    return true;
}

bool ContentStore::ReadCandidateDictionaryFont(std::string& data) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!indexed_ready_ || dictionary_candidate_font_size_ == 0 ||
        dictionary_candidate_font_path_.empty() ||
        !Read(dictionary_candidate_font_path_, data, 8 * 1024 * 1024) ||
        data.size() != dictionary_candidate_font_size_ ||
        Crc32(data) != dictionary_candidate_font_crc_) {
        data.clear();
        return false;
    }
    return true;
}

std::string ContentStore::DictionaryScalableFontPath() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return indexed_ready_ ? dictionary_scalable_font_path_ : std::string();
}

std::string ContentStore::DictionaryCandidateScalableFontPath() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return indexed_ready_ ? dictionary_candidate_scalable_font_path_ : std::string();
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
    auto key = NormalizePinyin(query);
    if (!pinyin_index_ready_ || key.empty() || limit == 0)
        return false;
    FILE* file = fopen((root_ + "/dictionary/pinyin.idx").c_str(), "rb");
    if (!file)
        return false;
    uint8_t directory[kPinyinDirectorySize]{};
    auto find_key = [&](const std::string& target) {
        uint32_t low = 0, high = pinyin_records_;
        while (low < high) {
            const uint32_t middle = low + (high - low) / 2;
            if (fseek(file, static_cast<long>(kPinyinHeaderSize + middle * kPinyinDirectorySize),
                      SEEK_SET) != 0 ||
                fread(directory, 1, sizeof(directory), file) != sizeof(directory))
                return false;
            size_t length = 0;
            while (length < 8 && directory[length])
                ++length;
            const std::string candidate(reinterpret_cast<char*>(directory), length);
            if (candidate < target)
                low = middle + 1;
            else if (candidate > target)
                high = middle;
            else
                return true;
        }
        return false;
    };
    bool found = find_key(key);
    int fallback_tone = -1;
    if (!found && key.size() > 1 && key.back() >= '0' && key.back() <= '4') {
        fallback_tone = key.back() - '0';
        key.pop_back();
        found = find_key(key);
    }
    if (!found) {
        fclose(file);
        return false;
    }
    const uint32_t offset = ReadLe32(directory + 8);
    const size_t count = std::min<size_t>(ReadLe16(directory + 12), limit);
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
    characters.reserve(std::min(count, limit));
    for (size_t index = 0; index < count; ++index) {
        auto character = data.substr(index * 3, 3);
        if (!Codepoint(character)) {
            characters.clear();
            return false;
        }
        if (fallback_tone >= 0) {
            std::string entry_data;
            Entry entry;
            if (!ReadIndexed(Codepoint(character), entry_data) ||
                !ParseEntry(entry_data, character, entry) ||
                !PinyinMatchesTone(entry.pinyin, key, fallback_tone))
                continue;
        }
        characters.push_back(std::move(character));
        if (characters.size() >= limit)
            break;
    }
    struct RankedCandidate {
        std::string character;
        size_t common_rank = std::numeric_limits<size_t>::max();
        uint8_t stroke_count = UINT8_MAX;
        size_t original_index = 0;
    };
    std::vector<RankedCandidate> ranked;
    ranked.reserve(characters.size());
    FILE* stroke_index =
        stroke_index_ready_ ? fopen((root_ + "/dictionary/strokes.idx").c_str(), "rb") : nullptr;
    FILE* stroke_data =
        stroke_index ? fopen((root_ + "/dictionary/strokes.dat").c_str(), "rb") : nullptr;
    for (size_t index = 0; index < characters.size(); ++index) {
        RankedCandidate candidate{characters[index], CommonCharacterRank(characters[index]),
                                  UINT8_MAX, index};
        const uint32_t codepoint = Codepoint(candidate.character);
        if (stroke_index && stroke_data && codepoint >= kFirstIndexedCodepoint &&
            codepoint < kFirstIndexedCodepoint + kIndexedSlots) {
            const auto position =
                kIndexHeaderSize +
                static_cast<size_t>(codepoint - kFirstIndexedCodepoint) * kIndexSlotSize;
            uint8_t slot[kIndexSlotSize]{};
            const bool slot_ok = fseek(stroke_index, static_cast<long>(position), SEEK_SET) == 0 &&
                                 fread(slot, 1, sizeof(slot), stroke_index) == sizeof(slot);
            const uint32_t offset = ReadLe32(slot);
            const uint32_t length = ReadLe32(slot + 4);
            uint8_t count = 0;
            if (slot_ok && length >= kStrokeHeaderSize && offset <= stroke_data_size_ &&
                length <= stroke_data_size_ - offset &&
                fseek(stroke_data, static_cast<long>(offset + 5), SEEK_SET) == 0 &&
                fread(&count, 1, 1, stroke_data) == 1 && count > 0 && count <= 64)
                candidate.stroke_count = count;
        }
        ranked.push_back(std::move(candidate));
    }
    if (stroke_data)
        fclose(stroke_data);
    if (stroke_index)
        fclose(stroke_index);
    std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
        const auto missing = std::numeric_limits<size_t>::max();
        const bool left_common = left.common_rank != missing;
        const bool right_common = right.common_rank != missing;
        if (left_common != right_common)
            return left_common;
        if (left.stroke_count != right.stroke_count)
            return left.stroke_count < right.stroke_count;
        if (left.common_rank != right.common_rank)
            return left.common_rank < right.common_rank;
        return left.original_index < right.original_index;
    });
    characters.clear();
    characters.reserve(ranked.size());
    for (auto& candidate : ranked)
        characters.push_back(std::move(candidate.character));
    return !characters.empty();
}

bool ContentStore::IsCommonCharacter(const std::string& character) {
    return CommonCharacterRank(character) != std::numeric_limits<size_t>::max();
}
}  // namespace han
