#include <cJSON.h>
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
        ui.UpdateStatusBar();
        Shot(folder, "home");
        auto& app = Application::GetInstance();
        auto talk = lv_obj_get_parent(FindLabel(lv_screen_active(), "按住说话"));
        lv_obj_send_event(talk, LV_EVENT_PRESSED, nullptr);
        Check(app.starts == 0, "offline hold must not enter audio test or start listening");
        auto offline_notice = FindLabel(lv_screen_active(), "请先联网，再按住说话");
        Check(offline_notice != nullptr, "offline explanation");
        lv_obj_add_flag(offline_notice, LV_OBJ_FLAG_HIDDEN);
        WifiManager::GetInstance().connected = true;
        Board::GetInstance().battery_known = true;
        Board::GetInstance().battery = 86;
        ui.UpdateStatusBar();
        Check(FindImage(lv_screen_active(), &han_status_wifi_3), "connected Wi-Fi icon");
        Check(FindImage(lv_screen_active(), &han_status_battery_full), "known battery icon");
        Shot(folder, "home-online-fixture");
        lv_obj_send_event(talk, LV_EVENT_PRESSED, nullptr);
        Check(app.starts == 1 && FindLabel(lv_screen_active(), "松开发送"),
              "hold starts manual session");
        lv_obj_send_event(talk, LV_EVENT_RELEASED, nullptr);
        Check(app.stops == 1 && FindLabel(lv_screen_active(), "按住说话"), "release stops session");
        lv_obj_send_event(talk, LV_EVENT_RELEASED, nullptr);
        Check(app.stops == 1, "duplicate release is harmless");
        lv_obj_send_event(talk, LV_EVENT_PRESSED, nullptr);
        app.state = kDeviceStateConnecting;
        lv_obj_send_event(talk, LV_EVENT_PRESS_LOST, nullptr);
        Check(app.stops == 2 && app.state == kDeviceStateIdle,
              "lost pointer cancels connecting session");
        app.defer = true;
        lv_obj_send_event(talk, LV_EVENT_PRESSED, nullptr);
        lv_obj_send_event(talk, LV_EVENT_RELEASED, nullptr);
        app.Drain();
        Check(app.starts == 2 && app.stops == 2, "quick release cancels queued start");
        app.defer = false;
        lv_obj_send_event(talk, LV_EVENT_PRESSED, nullptr);
        lv_tick_inc(61000);
        lv_timer_handler();
        Check(app.starts == 3 && app.stops == 3, "60-second hold safety release");
        lv_obj_send_event(talk, LV_EVENT_PRESSED, nullptr);
        Check(ui.OpenPage("dictionary"), "page navigation during hold");
        Check(app.starts == 4 && app.stops == 4, "navigation releases microphone");
        Check(ui.OpenPage("home"), "restore home");
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
                Click("下一步");
                Check(FindLabel(lv_screen_active(), "2 / 8    横"), "stroke advance");
                Click("上一步");
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
        Check(FindLabel(lv_screen_active(), "查字典"), "MCP result opens dictionary");
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
        Check(han::ContentStore::StrokePath("矩", 0) == "dictionary/strokes/77E9/01.png",
              "stroke path");
        Check(han::ContentStore::StrokePath("../", 0).empty(), "unsafe stroke target");
        Check(han::ContentStore::StrokePath("矩", 64).empty(), "stroke bound");
        if (argc == 3) {
            han::ContentStore generated(argv[2]);
            Check(generated.Initialize(), "generated card");
            Check(generated.Lookup("规矩的矩", entry), "second dictionary entry");
            ui.ShowEntry(entry);
            const auto first = han::ContentStore::StrokePath("矩", 0);
            Check(generated.ReadStroke(first, json), "read real matrix frame");
            Check(ui.ApplyStrokeFrame(first, json), "apply current frame");
            Shot(folder, "dictionary-ju");
            Click("下一步");
            Check(!ui.ApplyStrokeFrame(first, json), "reject stale asynchronous frame");
            const auto second = han::ContentStore::StrokePath("矩", 1);
            Check(generated.ReadStroke(second, json) && ui.ApplyStrokeFrame(second, json),
                  "apply next SD frame");
            Click("<");
            Check(!ui.ApplyStrokeFrame(second, json), "discard frame after leaving dictionary");
        }
        std::cout << "PASS: actual LVGL pages rendered; navigation, IPA categories, strokes, timer "
                     "and lookup checks passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
