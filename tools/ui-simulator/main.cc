#include <cJSON.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include "dictionary_service.h"
#include "han_display.h"

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
void Click(const char* label) {
    auto obj = FindLabel(lv_screen_active(), label);
    Check(obj != nullptr, label);
    lv_obj_send_event(lv_obj_get_parent(obj), LV_EVENT_CLICKED, nullptr);
}
void Shot(const std::filesystem::path& folder, const char* name) {
    lv_refr_now(nullptr);
    std::ofstream f(folder / (std::string(name) + ".ppm"), std::ios::binary);
    f << "P6\n1280 720\n255\n";
    f.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
}
int main(int argc, char** argv) {
    try {
        Check(argc == 2, "Pass output directory");
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
        han::Entry entry;
        Check(han::ContentStore().Lookup("规", entry), "embedded sample");
        Check(entry.page == 0, "unknown page");
        han::ContentStore card(std::string(HAN_SOURCE_ROOT) + "/content/sdcard/handict");
        Check(card.Initialize(), "SD manifest");
        Check(card.Lookup("规", entry) && entry.page == 0, "SD entry preserves unknown page");
        std::string json;
        Check(!card.Read("../manifest.json", json, 4096), "path traversal rejected");
        Check(card.Read("dictionary/entries/89C4.json", json, 16384), "bounded entry read");
        auto obj = cJSON_Parse(json.c_str());
        auto ref = cJSON_GetObjectItem(obj, "reference");
        cJSON_ReplaceItemInObject(ref, "page", cJSON_CreateNumber(123));
        cJSON_ReplaceItemInObject(ref, "verified", cJSON_CreateBool(true));
        auto modified = cJSON_PrintUnformatted(obj);
        Check(han::ContentStore::ParseEntry(modified, "规", entry) && entry.page == 123,
              "verified page accepted (synthetic)");
        cJSON_free(modified);
        cJSON_ReplaceItemInObject(ref, "isbn", cJSON_CreateString("9780000000000"));
        modified = cJSON_PrintUnformatted(obj);
        Check(han::ContentStore::ParseEntry(modified, "规", entry) && entry.page == 0,
              "wrong ISBN does not expose page");
        cJSON_free(modified);
        cJSON_Delete(obj);
        lv_init();
        auto display = lv_display_create(1280, 720);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
        static uint8_t buffer[1280 * 48 * 4];
        lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer),
                               LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_set_flush_cb(display, Flush);
        HanDisplay ui(nullptr, nullptr, 1280, 720, 0, 0, false, false, false);
        ui.SetupUI();
        ui.UpdateStatusBar();
        Shot(folder, "home");
        const char* pages[] = {"查字典", "英语音标", "课程表", "作业计时", "闹钟", "天气"};
        const char* files[] = {"dictionary", "phonetics", "timetable", "timer", "alarm", "weather"};
        for (int i = 0; i < 6; ++i) {
            Click(pages[i]);
            Shot(folder, files[i]);
            if (i == 0) {
                Click("下一步");
                Check(FindLabel(lv_screen_active(), "2 / 8    横"), "stroke advance");
                Click("上一步");
            }
            if (i == 1) {
                Click("双元音");
                Check(FindLabel(lv_screen_active(), "eɪ"), "category");
                Click("辅音");
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
            Click("<");
            Check(FindLabel(lv_screen_active(), "小小助手"), "back home");
        }
        Click("联网");
        ui.UpdateStatusBar();
        Shot(folder, "network");
        Click("<");
        ui.ShowEntry(han::ContentStore::Demo());
        Check(FindLabel(lv_screen_active(), "查字典"), "MCP result opens dictionary");
        std::cout << "PASS: actual LVGL pages rendered; navigation, IPA categories, strokes, timer "
                     "and lookup checks passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
