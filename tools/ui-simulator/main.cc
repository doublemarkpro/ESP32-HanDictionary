#include <cJSON.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include "assets/home_skin.h"
#include "assets/ui_assets.h"
#include "dictionary_service.h"
#include "han_display.h"
#include "phonetics.h"

DictionaryService& DictionaryService::GetInstance() {
    static DictionaryService d;
    return d;
}
static std::vector<uint8_t> pixels(1280 * 720 * 3);
void Flush(lv_display_t* d, const lv_area_t* area, uint8_t* data) {
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x) {
            auto i = (y * 1280 + x) * 3;
            pixels[i] = data[2];
            pixels[i + 1] = data[1];
            pixels[i + 2] = data[0];
            data += 4;
        }
    lv_display_flush_ready(d);
}
void Check(bool b, const char* message) {
    if (!b)
        throw std::runtime_error(message);
}
lv_obj_t* FindLabel(lv_obj_t* obj, const char* text) {
    if (lv_obj_check_type(obj, &lv_label_class) && std::string(lv_label_get_text(obj)) == text)
        return obj;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        if (auto v = FindLabel(lv_obj_get_child(obj, i), text))
            return v;
    return nullptr;
}
lv_obj_t* FindImage(lv_obj_t* obj, const lv_image_dsc_t* source) {
    if (lv_obj_check_type(obj, &lv_image_class) && lv_image_get_src(obj) == source)
        return obj;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        if (auto image = FindImage(lv_obj_get_child(obj, i), source))
            return image;
    return nullptr;
}
bool HasCheck(lv_obj_t* obj) {
    if (lv_obj_check_type(obj, &lv_line_class))
        return true;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        if (HasCheck(lv_obj_get_child(obj, i)))
            return true;
    return false;
}
void Click(const char* label) {
    auto obj = FindLabel(lv_screen_active(), label);
    Check(obj != nullptr, label);
    lv_obj_send_event(lv_obj_get_parent(obj), LV_EVENT_CLICKED, nullptr);
}
void Shot(const std::filesystem::path& folder, const char* name) {
    lv_refr_now(nullptr);
    size_t title_ink = 0;
    for (int y = 20; y < 90; ++y)
        for (int x = 110; x < 600; ++x) {
            const auto i = (y * 1280 + x) * 3;
            if (pixels[i] < 60 && pixels[i + 1] < 80 && pixels[i + 2] < 120)
                ++title_ink;
        }
    Check(title_ink > 100, "screenshot must contain visibly rendered title text");
    std::ofstream f(folder / (std::string(name) + ".ppm"), std::ios::binary);
    f << "P6\n1280 720\n255\n";
    f.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
}
int main(int argc, char** argv) {
    try {
        Check(argc == 2 || argc == 3,
              "Pass output directory and optional generated content directory");
        const std::filesystem::path folder(argv[1]);
        std::filesystem::create_directories(folder);
        han::StudyTimer timer;
        Check(timer.Start(0, 1000), "start");
        Check(!timer.Start(1, 2000), "single active subject");
        timer.Pause(61000);
        Check(timer.Elapsed(0, 500000) == 60000, "paused duration");
        timer.Start(1, 500000);
        timer.Complete(530000);
        Check(timer.Elapsed(1, 700000) == 30000, "subject duration");
        Check(han::ContentStore::TargetCharacter("规矩的规怎么写") == "规", "target extraction");
        Check(han::ContentStore::TargetCharacter("规矩的矩怎么写") == "矩", "no false gui match");
        Check(han::ContentStore::TargetCharacter("随便说一段话").empty(), "ambiguous query");
        Check(han::ContentStore::NormalizePinyin(" Han4 ") == "han", "pinyin normalization");
        han::Entry entry;
        Check(han::ContentStore().Lookup("规", entry), "embedded sample");
        han::ContentStore card(std::string(HAN_SOURCE_ROOT) + "/content/sdcard/handict");
        Check(card.Initialize(), "SD manifest");
        Check(card.Lookup("规", entry), "SD entry parsed");
        std::string json;
        Check(!card.Read("../manifest.json", json, 4096), "path traversal rejected");
        Check(card.Read("dictionary/entries/89C4.json", json, 16384), "bounded entry read");
        auto obj = cJSON_Parse(json.c_str());
        auto modified = cJSON_PrintUnformatted(obj);
        Check(han::ContentStore::ParseEntry(modified, "规", entry), "entry JSON parsed");
        cJSON_free(modified);
        cJSON_Delete(obj);
        lv_init();
        Check(LV_USE_FONT_COMPRESSED == 1, "compressed fonts enabled");
        lv_font_glyph_dsc_t probe{};
        Check(lv_font_get_glyph_dsc(&han_font_40, &probe, 'A', 0), "font probe descriptor");
        auto bitmap =
            lv_draw_buf_create(probe.box_w, probe.box_h, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
        Check(bitmap != nullptr, "font probe buffer");
        Check(lv_font_get_glyph_bitmap(&probe, bitmap) != nullptr, "font bitmap decode");
        size_t alpha = 0;
        for (uint32_t i = 0; i < bitmap->data_size; ++i)
            alpha += bitmap->data[i];
        Check(alpha > 1000, "font raster must contain visible pixels");
        lv_draw_buf_destroy(bitmap);
        auto display = lv_display_create(1280, 720);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
        static uint8_t buffer[1280 * 48 * 4];
        lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer),
                               LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(display, Flush);
        HanDisplay ui(nullptr, nullptr, 1280, 720, 0, 0, false, false, false);
        ui.SetupUI();
        const std::string empty_schedule = R"({"days":[[],[],[],[],[]]})";
        han::TimetableData schedule;
        Check(han::TimetableData::Parse(empty_schedule, schedule) && schedule.empty(),
              "legacy empty timetable");
        Check(han::TimetableData::Parse(R"({"days":[[""],[],[],[],[]]})", schedule) &&
                  schedule.empty(),
              "blank lessons are empty");
        Check(han::TimetableData::Parse("\xef\xbb\xbf" + empty_schedule, schedule) &&
                  schedule.empty(),
              "UTF-8 BOM timetable");
        for (const auto& invalid :
             {std::string("{}"), empty_schedule + "garbage",
              std::string(R"({"days":[null,[],[],[],[]]})"),
              std::string(R"({"days":[["1","2","3","4","5","6","7","8","9"],[],[],[],[]]})"),
              std::string(R"({"days":[["\u0000"],[],[],[],[]]})"), std::string(8193, ' ')})
            Check(!han::TimetableData::Parse(invalid, schedule), "malformed timetable rejected");
        Check(ui.ApplyTimetable(empty_schedule), "load genuine empty template");
        ui.UpdateStatusBar();
        Shot(folder, "home");
        Check(!FindLabel(lv_screen_active(), "按住说话"), "press-to-talk control removed");
        ui.SetStatus("正在初始化");
        ui.SetChatMessage("system", "xiaozhi/2.4.2 esp32p4");
        Check(FindLabel(lv_screen_active(), "正在准备屏幕、声音和网络，请稍候…"),
              "technical startup banner becomes child-friendly");
        Shot(folder, "home-starting");
        ui.SetStatus("正在回答");
        ui.SetChatMessage("assistant", "当然可以，我们先从今天的第一个生字开始吧！");
        Check(FindLabel(lv_screen_active(), "小智") &&
                  FindLabel(lv_screen_active(), "当然可以，我们先从今天的第一个生字开始吧！"),
              "assistant response has a dedicated role and message area");
        Shot(folder, "home-assistant-reply");
        ui.ClearChatMessages();
        WifiManager::GetInstance().connected = true;
        Board::GetInstance().battery_known = true;
        Board::GetInstance().battery = 86;
        ui.UpdateStatusBar();
        Check(FindImage(lv_screen_active(), &han_status_wifi_3), "connected Wi-Fi icon");
        Check(FindImage(lv_screen_active(), &han_status_battery_full), "known battery icon");
        Shot(folder, "home-online-fixture");
        WifiManager::GetInstance().rssi = -80;
        Board::GetInstance().battery = 10;
        ui.UpdateStatusBar();
        Check(FindImage(lv_screen_active(), &han_status_wifi_1), "weak Wi-Fi icon");
        Check(FindImage(lv_screen_active(), &han_status_battery_low), "low battery icon");
        Board::GetInstance().charging = true;
        ui.UpdateStatusBar();
        Check(FindImage(lv_screen_active(), &han_status_battery_charging), "charging icon");
        Board::GetInstance().battery_known = false;
        WifiManager::GetInstance().connected = false;
        ui.UpdateStatusBar();
        Check(FindImage(lv_screen_active(), &han_status_battery_unknown),
              "unknown is not full battery");
        Check(FindImage(lv_screen_active(), &han_status_wifi_off), "offline icon");
        // Exercise actual pointer hit testing over the inner character card, not just label
        // callbacks.
        auto pointer = lv_indev_create();
        lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(pointer, display);
        lv_indev_set_mode(pointer, LV_INDEV_MODE_EVENT);
        static bool pressed = false;
        lv_indev_set_read_cb(pointer, [](lv_indev_t*, lv_indev_data_t* data) {
            data->point = {330, 270};
            data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        });
        lv_obj_update_layout(lv_screen_active());
        pressed = true;
        lv_indev_read(pointer);
        pressed = false;
        lv_indev_read(pointer);
        Check(FindLabel(lv_screen_active(), "播放笔顺"),
              "character artwork does not swallow card touch");
        lv_indev_delete(pointer);
        ui.OpenPage("home");
        // Typography is a real LVGL font, not lettering baked into a background.
        Check(lv_obj_get_style_text_font(FindLabel(lv_screen_active(), "英语音标"), LV_PART_MAIN) ==
                  &han_font_home,
              "home uses heavyweight rounded type");
        const char* pages[] = {"查字典", "英语音标", "课程表", "作业计时", "闹钟", "天气"};
        const char* files[] = {"dictionary", "phonetics", "timetable", "timer", "alarm", "weather"};
        for (int i = 0; i < 6; ++i) {
            Click(pages[i]);
            Shot(folder, files[i]);
            if (i == 0) {
                Check(!FindLabel(lv_screen_active(), "语音查字"),
                      "dictionary voice button removed");
                Check(!FindLabel(lv_screen_active(), "离线汉字学习"),
                      "redundant dictionary subtitle removed");
                Check(!FindLabel(lv_screen_active(), "离线字库"),
                      "redundant dictionary status button removed");
                Click("下一步");
                Check(FindLabel(lv_screen_active(), "2/8"), "stroke advance");
                Click("上一步");
                Click("拼音查字");
                Check(FindLabel(lv_screen_active(), "输入拼音，例如 han"), "pinyin search opens");
                Click("h");
                Click("a");
                Click("n");
                Check(FindLabel(lv_screen_active(), "han"), "pinyin keypad entry");
                Shot(folder, "dictionary-search");
                Click("关闭");
                auto long_entry = entry;
                long_entry.stroke_count = 13;
                long_entry.strokes.resize(13, "横");
                long_entry.strokes[12] = "横撇弯钩";
                ui.ShowEntry(long_entry);
                Check(FindLabel(lv_screen_active(), "笔顺 · 共13画"),
                      "stroke total stays on one line");
                Check(FindLabel(lv_screen_active(), "横撇弯钩"),
                      "long stroke names after the eighth remain available");
                Check(!FindLabel(lv_screen_active(), "当前：横"),
                      "redundant current-stroke caption is absent");
                Shot(folder, "dictionary-thirteen-strokes");
            }
            if (i == 1) {
                Click("下一页");
                Check(FindLabel(lv_screen_active(), "ɒ"), "second vowel page");
                Click("上一页");
                Click("双元音");
                Check(FindLabel(lv_screen_active(), "eɪ"), "category");
                Click("下一页");
                Check(FindLabel(lv_screen_active(), "ʊə"), "second diphthong page");
                Click("辅音");
                Click("下一页");
                Check(FindLabel(lv_screen_active(), "θ"), "theta glyph and consonant page");
                Click("θ");
                Shot(folder, "phonetics-theta");
                Click("下一页");
                Click("下一页");
                Check(FindLabel(lv_screen_active(), "j"), "last consonant page");
                Shot(folder, "phonetics-last");
                Click("下一页");
                Check(FindLabel(lv_screen_active(), "p"), "pagination wraps");
                Click("单元音");
            }
            if (i == 3) {
                Click("开始 / 继续");
                lv_tick_inc(65000);
                lv_timer_handler();
                Click("<");
                Click("作业计时");
                Click("暂停");
                Check(FindLabel(lv_screen_active(), "00:01:05"), "timer survives back");
            }
            if (i == 5) {
                ui.SetWeatherTextForTest(
                    "青岛（缓存）\n晴 26°C\n体感 27°C · 湿度 58%\n东南风 2级\n\n"
                    "数据来源：和风天气\n更新时间：09-14 06:38");
                Check(FindLabel(lv_screen_active(), "缓存天气"), "cached weather is explicit");
                Shot(folder, "weather-data");
            }
            if (i == 2) {
                Check(FindLabel(lv_screen_active(), "还没有课程，请导入课表"),
                      "empty template does not invent lessons");
                Click("问问明天上什么课");
                Check(FindLabel(lv_screen_active(), "请先联网，再询问课程"),
                      "offline timetable voice is guarded");
                std::ifstream fixture(std::string(HAN_SOURCE_ROOT) +
                                      "/content/sdcard/handict/timetable.json");
                const std::string example((std::istreambuf_iterator<char>(fixture)), {});
                Check(ui.ApplyTimetable(example), "explicit demo fixture accepted");
                ui.ShowNotification("", 1);
                lv_tick_inc(2);
                lv_timer_handler();
                Shot(folder, "timetable-example");
                Check(FindLabel(lv_screen_active(), "信息") &&
                          FindLabel(lv_screen_active(), "音乐") &&
                          FindLabel(lv_screen_active(), "武术") &&
                          FindLabel(lv_screen_active(), "劳动") &&
                          FindLabel(lv_screen_active(), "竖笛") &&
                          FindLabel(lv_screen_active(), "第7节"),
                      "all seven lessons and custom subjects fit on one screen");
                Click("本周");
                Check(FindLabel(lv_screen_active(), "下周"), "next weekly template");
                Click("下周");
                Check(
                    FindLabel(lv_screen_active(), "周六") && FindLabel(lv_screen_active(), "周日"),
                    "weekend reachable");
                Shot(folder, "timetable-weekend");
                Click("周末");
                Click("美术本");
                Check(HasCheck(lv_obj_get_parent(FindLabel(lv_screen_active(), "美术本"))),
                      "supplies can be checked");
                Shot(folder, "timetable-checked");
                Check(!ui.ApplyTimetable("{}"), "invalid import returns failure");
                Check(FindLabel(lv_screen_active(), "课表未加载或格式错误"),
                      "invalid import clears stale courses");
                Check(ui.ApplyTimetable(empty_schedule), "restore real template");
            }
            Click("<");
            Check(FindLabel(lv_screen_active(), "小小助手"), "back home");
        }
        auto wifi = FindImage(lv_screen_active(), &han_status_wifi_off);
        Check(wifi != nullptr, "Wi-Fi action present");
        lv_obj_send_event(lv_obj_get_parent(wifi), LV_EVENT_CLICKED, nullptr);
        ui.UpdateStatusBar();
        Shot(folder, "network");
        Click("<");
        ui.ShowEntry(han::ContentStore::Demo());
        Check(FindLabel(lv_screen_active(), "小小字典"), "MCP result opens dictionary");
        for (const auto& sound : han::kSounds) {
            for (const unsigned char* p = reinterpret_cast<const unsigned char*>(sound.ipa); *p;) {
                uint32_t cp = *p++;
                if (cp >= 0xc0) {
                    const int tail = cp < 0xe0 ? 1 : 2;
                    cp &= tail == 1 ? 31 : 15;
                    for (int j = 0; j < tail; ++j)
                        cp = (cp << 6) | (*p++ & 63);
                }
                lv_font_glyph_dsc_t glyph{};
                Check(lv_font_get_glyph_dsc(&han_font_40, &glyph, cp, 0), "small IPA glyph exists");
                Check(!glyph.is_placeholder, "small IPA is not a placeholder");
                Check(lv_font_get_glyph_dsc(&han_font_large, &glyph, cp, 0),
                      "large IPA glyph exists");
                Check(!glyph.is_placeholder, "large IPA is not a placeholder");
            }
        }
        if (argc == 3) {
            han::ContentStore generated(argv[2]);
            Check(generated.Initialize(), "generated card");
            std::vector<std::string> pinyin_results;
            Check(generated.SearchPinyin("han", pinyin_results) &&
                      std::find(pinyin_results.begin(), pinyin_results.end(), "汉") !=
                          pinyin_results.end(),
                  "indexed pinyin candidate search");
            Check(generated.Lookup("汉", entry), "indexed dictionary lookup");
            Check(entry.source == "guoxuedashi-xinhua-community" && entry.stroke_count > 0,
                  "indexed dictionary metadata");
            ui.ShowEntry(entry);
            han::StrokeGlyph glyph;
            Check(generated.ReadStrokeGlyph("汉", glyph), "read indexed vector strokes");
            Check(ui.ApplyStrokeGlyph("汉", std::move(glyph)), "apply indexed vector strokes");
            Shot(folder, "dictionary-han-indexed");
            Check(generated.Lookup("嗝", entry) && entry.radical == "口" &&
                      entry.structure == "左右结构" && entry.stroke_count == 13,
                  "cnchar metadata and full stroke list for 嗝");
            ui.ShowEntry(entry);
            Check(generated.ReadStrokeGlyph("嗝", glyph), "read 13-stroke vector glyph");
            Check(ui.ApplyStrokeGlyph("嗝", std::move(glyph)), "apply 13-stroke vector glyph");
            Check(FindLabel(lv_screen_active(), "笔顺 · 共13画"), "13-stroke summary");
            Shot(folder, "dictionary-ge");
            Check(generated.Lookup("规矩的矩", entry), "second dictionary entry");
            ui.ShowEntry(entry);
            Check(generated.ReadStrokeGlyph("矩", glyph), "read real matrix vector paths");
            Check(ui.ApplyStrokeGlyph("矩", std::move(glyph)), "apply current vector glyph");
            Shot(folder, "dictionary-ju");
            Click("下一步");
            Click("<");
            Check(generated.ReadStrokeGlyph("矩", glyph) &&
                      !ui.ApplyStrokeGlyph("矩", std::move(glyph)),
                  "discard vector glyph after leaving dictionary");
        }
        std::cout << "PASS: actual LVGL pages rendered; navigation, IPA categories, strokes, timer "
                     "and lookup checks passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
