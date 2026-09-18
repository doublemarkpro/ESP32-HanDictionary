#include <cJSON.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include "assets/home_skin.h"
#include "assets/timetable_assets.h"
#include "assets/ui_assets.h"
#include "dictionary_service.h"
#include "han_display.h"
#include "lunar_calendar.h"
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
lv_obj_t* FindLabelAt(lv_obj_t* obj, int x, int y) {
    if (lv_obj_check_type(obj, &lv_label_class) && lv_obj_get_x(obj) == x && lv_obj_get_y(obj) == y)
        return obj;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        if (auto label = FindLabelAt(lv_obj_get_child(obj, i), x, y))
            return label;
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
lv_obj_t* FindCanvas(lv_obj_t* obj, int width, int height) {
    if (lv_obj_check_type(obj, &lv_canvas_class) && lv_obj_get_width(obj) == width &&
        lv_obj_get_height(obj) == height)
        return obj;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        if (auto canvas = FindCanvas(lv_obj_get_child(obj, i), width, height))
            return canvas;
    return nullptr;
}
lv_obj_t* FindArc(lv_obj_t* obj) {
    if (lv_obj_check_type(obj, &lv_arc_class))
        return obj;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        if (auto arc = FindArc(lv_obj_get_child(obj, i)))
            return arc;
    return nullptr;
}
lv_obj_t* FindDropdown(lv_obj_t* obj) {
    if (lv_obj_check_type(obj, &lv_dropdown_class))
        return obj;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        if (auto dropdown = FindDropdown(lv_obj_get_child(obj, i)))
            return dropdown;
    return nullptr;
}
int CountArcs(lv_obj_t* obj) {
    int count = lv_obj_check_type(obj, &lv_arc_class) ? 1 : 0;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        count += CountArcs(lv_obj_get_child(obj, i));
    return count;
}
int CountBars(lv_obj_t* obj) {
    int count = lv_obj_check_type(obj, &lv_bar_class) ? 1 : 0;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        count += CountBars(lv_obj_get_child(obj, i));
    return count;
}
lv_obj_t* FindArcBySize(lv_obj_t* obj, int size) {
    if (lv_obj_check_type(obj, &lv_arc_class) && lv_obj_get_width(obj) == size &&
        lv_obj_get_height(obj) == size)
        return obj;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        if (auto arc = FindArcBySize(lv_obj_get_child(obj, i), size))
            return arc;
    return nullptr;
}
void ClickImage(const lv_image_dsc_t* source) {
    auto image = FindImage(lv_screen_active(), source);
    Check(image != nullptr, "image button exists");
    lv_obj_send_event(lv_obj_get_parent(image), LV_EVENT_CLICKED, nullptr);
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
    size_t title_light = 0;
    for (int y = 20; y < 90; ++y)
        for (int x = 110; x < 600; ++x) {
            const auto i = (y * 1280 + x) * 3;
            if (pixels[i] < 60 && pixels[i + 1] < 80 && pixels[i + 2] < 120)
                ++title_ink;
            if (pixels[i] > 205 && pixels[i + 1] > 205 && pixels[i + 2] > 205)
                ++title_light;
        }
    const std::string shot_name(name);
    if (shot_name != "clock" && shot_name.find("boot-") != 0 && shot_name.find("dark-") != 0 &&
        shot_name.find("assistant-dialog") == std::string::npos)
        Check(title_ink > 100, "screenshot must contain visibly rendered title text");
    if (shot_name.find("dark-") == 0 && shot_name != "dark-clock" && shot_name.find("boot-") != 0)
        Check(title_light > 100, "dark screenshot must contain a visible light title");
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
        Check(DictionaryService::IsStrokePlaybackQuery("智能的智怎么写"),
              "writing query requests automatic stroke playback");
        Check(DictionaryService::IsStrokePlaybackQuery("请播放智的笔顺"),
              "explicit stroke-order query requests automatic playback");
        Check(!DictionaryService::IsStrokePlaybackQuery("智能的智是什么意思"),
              "definition lookup does not start stroke playback");
        Check(han::ContentStore::NormalizePinyin(" Han4 ") == "han4", "pinyin tone normalization");
        Check(han::ContentStore::NormalizePinyin(" ma5 ") == "ma0", "neutral tone normalization");
        Check(han::ContentStore::IsCommonCharacter("去") &&
                  !han::ContentStore::IsCommonCharacter("阒"),
              "common-character priority distinguishes familiar and rare candidates");
        han::LunarDate lunar;
        Check(han::LunarFromGregorian(2026, 9, 15, lunar) && lunar.year == 2026 &&
                  lunar.month == 8 && lunar.day == 5 && !lunar.leap_month,
              "concept date converts to lunar date");
        Check(han::FormatLunarDate(lunar) == "农历丙午年 · 八月初五",
              "lunar date has the approved compact wording");
        Check(han::LunarFromGregorian(2024, 2, 10, lunar) &&
                  han::FormatLunarDate(lunar) == "农历甲辰年 · 正月初一",
              "lunar new year boundary");
        Check(han::LunarFromGregorian(2023, 3, 22, lunar) && lunar.leap_month &&
                  han::FormatLunarDate(lunar) == "农历癸卯年 · 闰二月初一",
              "leap lunar month boundary");
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
        const bool dark_smoke = std::getenv("HAN_UI_DARK_SMOKE") != nullptr;
        const bool gallery_smoke = std::getenv("HAN_UI_GALLERY_SMOKE") != nullptr;
        const bool timer_plan_smoke = std::getenv("HAN_UI_TIMER_PLAN_SMOKE") != nullptr;
        const bool timer_rollover_smoke = std::getenv("HAN_UI_TIMER_ROLLOVER_SMOKE") != nullptr;
        const bool assistant_smoke = std::getenv("HAN_UI_ASSISTANT_SMOKE") != nullptr;
        const bool keyboard_smoke = std::getenv("HAN_UI_KEYBOARD_SMOKE") != nullptr;
        const bool font_compare_smoke = std::getenv("HAN_UI_FONT_COMPARE_SMOKE") != nullptr;
        const bool keyboard_connect_smoke = std::getenv("HAN_UI_KEYBOARD_CONNECT_SMOKE") != nullptr;
        const bool settings_smoke = std::getenv("HAN_UI_SETTINGS_SMOKE") != nullptr;
        const bool boot_smoke = std::getenv("HAN_UI_BOOT_SMOKE") != nullptr;
        const bool touch_pinyin_smoke = std::getenv("HAN_UI_TOUCH_PINYIN_SMOKE") != nullptr;
        const bool dictionary_first_frame_smoke =
            std::getenv("HAN_UI_DICTIONARY_FIRST_FRAME_SMOKE") != nullptr;
        if (dark_smoke) {
            Settings::values["displaytheme_mode"] = 1;
            Settings::values["displaydark_active"] = 1;
        }
        if (timer_rollover_smoke) {
            Settings::values["han_studys0"] = 43 * 60 + 57;
            Settings::values["han_studyc0"] = 1;
            Settings::values["han_studyday"] = 20000101;
            Settings::values["han_studyday_idx"] = 0;
        }
        HanDisplay ui(nullptr, nullptr, 1280, 720, 0, 0, false, false, false);
        ui.SetupUI();
        if (font_compare_smoke) {
            const char* font_path = std::getenv("HAN_UI_CANDIDATE_TTF");
            const char* preview_name = std::getenv("HAN_UI_FONT_PREVIEW_NAME");
            Check(font_path != nullptr && preview_name != nullptr,
                  "font comparison needs a font path and preview name");
            std::ifstream font_file(font_path, std::ios::binary);
            Check(font_file.good(), "comparison font file opens");
            std::vector<uint8_t> font_data((std::istreambuf_iterator<char>(font_file)),
                                           std::istreambuf_iterator<char>());
            Check(!font_data.empty(), "comparison font has data");
            ui.HandleKeyboardInput("\t");
            ui.HandleKeyboardInput("pai4");
            const std::vector<std::string> candidates = {"派", "湃", "排", "蒎", "汖", "渒", "鎃"};
            ui.SetPinyinResultsForTest("pai4", candidates);
            auto preview_font = lv_tiny_ttf_create_data(font_data.data(), font_data.size(), 56);
            Check(preview_font != nullptr, "comparison font opens in LVGL");
            for (const auto& candidate : candidates) {
                auto label = FindLabel(lv_screen_active(), candidate.c_str());
                Check(label != nullptr, "comparison candidate exists");
                lv_obj_set_style_text_font(label, preview_font, LV_PART_MAIN);
                lv_obj_set_style_transform_scale(label, 256, LV_PART_MAIN);
                lv_obj_set_height(label, preview_font->line_height + 12);
                lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
                lv_obj_update_layout(label);
                lv_area_t label_area{};
                lv_area_t card_area{};
                lv_obj_get_coords(label, &label_area);
                lv_obj_get_coords(lv_obj_get_parent(label), &card_area);
                Check(std::abs((label_area.x1 + label_area.x2) - (card_area.x1 + card_area.x2)) <=
                              2 &&
                          std::abs((label_area.y1 + label_area.y2) -
                                   (card_area.y1 + card_area.y2)) <= 2,
                      "candidate label line box is centred in its rounded card");
            }
            Shot(folder, preview_name);
            std::cout << "PASS: candidate font preview rendered with " << font_path << ".\n";
            return 0;
        }
        if (touch_pinyin_smoke) {
            Check(ui.OpenPage("dictionary"), "touch pinyin smoke opens dictionary");
            ClickImage(&han_icon_pinyin_search);
            lv_obj_update_layout(lv_screen_active());
            auto touch_title = FindLabel(lv_screen_active(), "拼音查字");
            auto touch_input = FindLabel(lv_screen_active(), "输入拼音，例如 han");
            auto touch_search = FindLabel(lv_screen_active(), "查找");
            auto touch_close = FindLabel(lv_screen_active(), "关闭");
            auto tone_title = FindLabel(lv_screen_active(), "音调");
            auto tone_all = FindLabel(lv_screen_active(), "全部");
            lv_area_t touch_title_area{}, touch_input_area{}, touch_search_area{},
                touch_close_area{}, tone_title_area{}, tone_all_area{};
            lv_obj_get_coords(touch_title, &touch_title_area);
            lv_obj_get_coords(touch_input, &touch_input_area);
            lv_obj_get_coords(touch_search, &touch_search_area);
            lv_obj_get_coords(touch_close, &touch_close_area);
            lv_obj_get_coords(tone_title, &tone_title_area);
            lv_obj_get_coords(tone_all, &tone_all_area);
            const auto visual_center_y = [](const lv_area_t& area) { return area.y1 + area.y2; };
            Check(std::abs(visual_center_y(touch_title_area) - visual_center_y(touch_input_area)) <=
                          4 &&
                      std::abs(visual_center_y(touch_search_area) -
                               visual_center_y(touch_input_area)) <= 4 &&
                      std::abs(visual_center_y(touch_close_area) -
                               visual_center_y(touch_input_area)) <= 4,
                  "touch pinyin header shares one visual centre line");
            Check(std::abs(visual_center_y(tone_title_area) - visual_center_y(tone_all_area)) <= 4,
                  "touch tone heading and filter chips share one visual centre line");
            Click("m");
            Click("a");
            Click("查找");
            Check(FindLabel(lv_screen_active(), "正在离线字库中查找…") &&
                      FindLabel(lv_screen_active(), "0%") && CountBars(lv_screen_active()) == 1,
                  "touch pinyin lookup exposes a real progress indicator");
            ui.AdvancePinyinProgressForTest(61);
            Check(FindLabel(lv_screen_active(), "8%") &&
                      FindLabel(lv_screen_active(), "正在读取拼音索引…"),
                  "touch pinyin progress advances through its first lookup phase");
            ui.AdvancePinyinProgressForTest(100);
            Check(FindLabel(lv_screen_active(), "16%"),
                  "touch pinyin progress animates continuously instead of jumping");
            Shot(folder, "touch-pinyin-progress");
            ui.SetPinyinResultsForTest("ma", {"妈", "麻", "马", "骂", "吗", "嘛", "码", "玛"});
            Check(FindLabel(lv_screen_active(), "ma · 共8字，点击查看") &&
                      FindLabel(lv_screen_active(), "妈") && CountBars(lv_screen_active()) == 0,
                  "touch pinyin completion replaces progress with candidate characters");
            Shot(folder, "touch-pinyin-results");
            std::cout << "PASS: touch pinyin lookup progresses and renders candidates.\n";
            return 0;
        }
        if (dictionary_first_frame_smoke) {
            Check(argc == 3, "dictionary first-frame smoke needs an indexed content pack");
            han::ContentStore generated(argv[2]);
            Check(generated.Initialize(), "first-frame indexed card");
            han::Entry indexed_entry;
            han::StrokeGlyph indexed_glyph;
            Check(generated.Lookup("汉", indexed_entry) &&
                      generated.ReadStrokeGlyph("汉", indexed_glyph),
                  "first-frame entry and vector glyph load together");
            ui.ShowEntry(indexed_entry, false, true, false, std::move(indexed_glyph));
            auto grid_canvas = FindCanvas(lv_screen_active(), 400, 400);
            auto heading_canvas = FindCanvas(lv_screen_active(), 96, 88);
            Check(grid_canvas && heading_canvas, "both dictionary vector canvases are created");
#if LV_USE_VECTOR_GRAPHIC
            Check(!lv_obj_has_flag(grid_canvas, LV_OBJ_FLAG_HIDDEN) &&
                      !lv_obj_has_flag(heading_canvas, LV_OBJ_FLAG_HIDDEN),
                  "grid and heading vector glyphs are both visible on the first frame");
#endif
            std::cout << "PASS: dictionary entry and both vector glyphs render in one frame.\n";
            return 0;
        }
        if (boot_smoke) {
            lv_tick_inc(1050);
            lv_timer_handler();
            Check(!FindLabel(lv_layer_top(), "妙") && FindLabel(lv_layer_top(), "妙智学伴") &&
                      FindLabel(lv_layer_top(), "小小字典·陪你妙学每一天") &&
                      FindLabel(lv_layer_top(), "正在读取 microSD 卡…"),
                  "the themed boot screen renders its complete identity and progress");
            Shot(folder, dark_smoke ? "boot-dark" : "boot-light");
            ui.SetStatus("等待唤醒");
            lv_tick_inc(2700);
            lv_timer_handler();
            Check(FindLabel(lv_layer_top(), "准备完成"),
                  "the completed boot screen remains visible before dismissal");
            lv_tick_inc(500);
            lv_timer_handler();
            lv_tick_inc(350);
            lv_timer_handler();
            Check(!FindLabel(lv_layer_top(), "妙智学伴"),
                  "the boot screen dismisses without blocking the UI");
            Check(FindLabel(lv_screen_active(), "等待唤醒") &&
                      FindLabel(lv_screen_active(), "说“你好小智”，我来帮你学习") &&
                      !FindLabel(lv_screen_active(), "准备中"),
                  "the home page does not replay startup messaging after boot");
            Check(FindImage(lv_screen_active(), &han_home_miao),
                  "home dictionary card uses the centred Miao stroke glyph");
            Shot(folder, "home-miao");
            Click("查字典");
            const auto default_entry = han::ContentStore::Demo();
            Check(FindLabel(lv_screen_active(), default_entry.character.c_str()) &&
                      FindLabel(lv_screen_active(), default_entry.definition.c_str()) &&
                      FindLabel(lv_screen_active(), default_entry.words.front().c_str()),
                  "home dictionary card opens the Miao entry and its learning content");
            Shot(folder, "dictionary-miao");
            std::cout << "PASS: boot state and default Miao dictionary entry rendered.\n";
            return 0;
        }
        if (settings_smoke) {
            Check(ui.OpenPage("network"), "settings smoke page opens");
            auto appearance_control =
                FindLabel(lv_screen_active(), dark_smoke ? "深色模式" : "外观模式");
            auto screen_off_control = FindLabel(lv_screen_active(), "立即关屏");
            Check(appearance_control && screen_off_control &&
                      lv_obj_get_style_text_font(appearance_control, LV_PART_MAIN) ==
                          lv_obj_get_style_text_font(screen_off_control, LV_PART_MAIN) &&
                      (dark_smoke ||
                       lv_color_eq(lv_obj_get_style_bg_color(lv_obj_get_parent(screen_off_control),
                                                             LV_PART_MAIN),
                                   lv_color_hex(0xf05b78))),
                  "screen-off control matches the appearance font and deeper red fill");
            Shot(folder, dark_smoke ? "dark-settings-controls" : "settings-controls");
            std::cout << "PASS: settings control typography and screen-off color rendered.\n";
            return 0;
        }
        if (keyboard_connect_smoke) {
            ui.ShowKeyboardConnected();
            lv_tick_inc(600);
            lv_timer_handler();
            Check(FindLabel(lv_layer_top(), "键盘已连接") &&
                      FindLabel(lv_layer_top(), "现在可以开始输入啦"),
                  "keyboard insertion presents a global connection overlay");
            Shot(folder, dark_smoke ? "keyboard-connect-dark" : "keyboard-connect");
            std::cout << "PASS: keyboard connection overlay rendered.\n";
            return 0;
        }
        if (assistant_smoke) {
            Check(ui.OpenPage("timetable"), "assistant smoke page opens");
            ui.SetStatus("正在聆听");
            ui.SetChatMessage("assistant", "你好小智");
            ui.SetChatMessage("user", "智能的智怎么写");
            ui.SetChatMessage("assistant", "打开字典并播放笔顺");
            ui.SetChatMessage("user", "可以组成什么词");
            ui.SetChatMessage("assistant", "可以组成智能");
            Check(FindImage(lv_screen_active(), &han_assistant_robot) &&
                      FindImage(lv_screen_active(), &han_assistant_child),
                  "assistant and child avatars are rendered");
            Shot(folder, "assistant-dialog");
            auto stop_label = FindLabel(lv_screen_active(), "停止对话");
            auto dialog_scrim = stop_label;
            for (int level = 0; level < 4; ++level)
                dialog_scrim = lv_obj_get_parent(dialog_scrim);
            auto& app = Application::GetInstance();
            app.state = kDeviceStateSpeaking;
            Click("停止对话");
            Check(app.stops == 1 && app.state == kDeviceStateIdle && dialog_scrim &&
                      lv_obj_has_flag(dialog_scrim, LV_OBJ_FLAG_HIDDEN),
                  "one stop-dialog click ends a speaking conversation and hides the modal");
            std::cout << "PASS: assistant dialog rendered and stopped with one click.\n";
            return 0;
        }
        if (keyboard_smoke) {
            ui.HandleKeyboardInput("\t");
            Check(FindLabel(lv_screen_active(), "查字典") &&
                      FindLabel(lv_screen_active(), "键盘已连接"),
                  "Tab shortcut opens the dedicated keyboard lookup page");
            Check(!FindLabel(lv_screen_active(), "Tab") &&
                      !FindLabel(lv_screen_active(), "Enter") &&
                      !FindLabel(lv_screen_active(), "Esc"),
                  "keyboard lookup omits the shortcut sidebar");
            Check(!FindLabel(lv_screen_active(), "例如 qu4") &&
                      !FindLabel(lv_screen_active(), "例如 gai"),
                  "an empty keyboard query does not masquerade as typed pinyin");
            ui.HandleKeyboardInput("ke3");
            Check(FindLabel(lv_screen_active(), "正在离线字库中查找…") &&
                      FindLabel(lv_screen_active(), "0%") && CountBars(lv_screen_active()) == 1,
                  "typing a tone digit automatically starts lookup with a real progress indicator");
            ui.AdvancePinyinProgressForTest(61);
            Check(FindLabel(lv_screen_active(), "8%") &&
                      FindLabel(lv_screen_active(), "正在读取拼音索引…"),
                  "pinyin lookup animates through bounded visible loading steps");
            ui.AdvancePinyinProgressForTest(61);
            Check(FindLabel(lv_screen_active(), "16%"),
                  "pinyin loading progress continues instead of jumping to the worker value");
            Shot(folder, dark_smoke ? "dark-keyboard-search-progress" : "keyboard-search-progress");
            ui.SetPinyinResultsForTest("ke3", {"可", "渴", "坷", "岢", "炣", "敤", "嵑", "渇"});
            auto pinyin_prompt = FindLabel(lv_screen_active(), "拼音");
            auto pinyin_input = FindLabel(lv_screen_active(), "ke3");
            auto first_candidate = FindLabel(lv_screen_active(), "可");
            Check(pinyin_prompt && pinyin_input && first_candidate &&
                      FindLabel(lv_screen_active(), "渇") && FindLabel(lv_screen_active(), "常用"),
                  "keyboard query filters a numeric tone and renders large candidates");
            Check(lv_obj_get_style_text_font(pinyin_prompt, LV_PART_MAIN) ==
                      lv_obj_get_style_text_font(pinyin_input, LV_PART_MAIN),
                  "pinyin prompt and typed query use the same font size");
            lv_obj_update_layout(lv_screen_active());
            lv_area_t prompt_area{}, input_area{};
            lv_obj_get_coords(pinyin_prompt, &prompt_area);
            lv_obj_get_coords(pinyin_input, &input_area);
            Check(
                std::abs((prompt_area.y1 + prompt_area.y2) - (input_area.y1 + input_area.y2)) <= 2,
                "pinyin prompt and typed query share one visual centre line");
            Check(lv_obj_get_style_text_font(pinyin_prompt, LV_PART_MAIN) == &han_font_timer &&
                      lv_obj_get_style_text_font(FindLabel(lv_screen_active(), "查找"),
                                                 LV_PART_MAIN) == &han_font_timer &&
                      lv_obj_get_style_text_font(FindLabel(lv_screen_active(), "键盘已连接"),
                                                 LV_PART_MAIN) == &han_font_timer &&
                      lv_obj_get_style_text_font(FindLabel(lv_screen_active(), "全部"),
                                                 LV_PART_MAIN) == &han_font_timer,
                  "keyboard lookup controls match the settings-page control font");
            Check(!FindLabel(lv_screen_active(), "3声") &&
                      !FindLabel(lv_screen_active(), "清空重输") &&
                      !FindLabel(lv_screen_active(),
                                 "提示：输入 ke3，只显示三声汉字；点击大字进入学习页"),
                  "candidate cards omit tone annotations and redundant footer hints");
            lv_obj_update_layout(first_candidate);
            auto first_card = lv_obj_get_parent(first_candidate);
            Check(lv_obj_get_height(first_candidate) >= 50 && lv_obj_get_height(first_card) == 146,
                  "candidate glyph has an explicit unclipped label and card height");
            Check(!lv_color_eq(
                      lv_obj_get_style_bg_color(first_card, LV_PART_MAIN),
                      lv_obj_get_style_bg_color(
                          lv_obj_get_parent(FindLabel(lv_screen_active(), "炣")), LV_PART_MAIN)),
                  "all eight candidate positions can use distinct pastel backgrounds");
            Check(lv_obj_get_style_border_width(first_card, LV_PART_MAIN) == 4,
                  "the first candidate starts selected");
            ui.HandleKeyboardInput("\x1d");
            auto second_candidate = FindLabel(lv_screen_active(), "渴");
            Check(
                second_candidate &&
                    lv_obj_get_style_border_width(lv_obj_get_parent(second_candidate),
                                                  LV_PART_MAIN) == 4 &&
                    lv_obj_get_style_border_width(
                        lv_obj_get_parent(FindLabel(lv_screen_active(), "可")), LV_PART_MAIN) == 2,
                "right arrow advances the visible candidate selection");
            ui.ShowEntry(entry, true, false, true);
            Check(FindLabel(lv_screen_active(), "小小字典"),
                  "selected keyboard candidate opens the dictionary page");
            Click("<");
            Check(FindLabel(lv_screen_active(), "ke3") && FindLabel(lv_screen_active(), "渴"),
                  "dictionary back returns to the preserved keyboard lookup results");
            ui.HandleKeyboardInput("ling");
            Check(FindLabel(lv_screen_active(), "ling") && !FindLabel(lv_screen_active(), "ke3") &&
                      !FindLabel(lv_screen_active(), "渴"),
                  "typing after results starts a fresh pinyin query without backspacing");
            Shot(folder, dark_smoke ? "dark-keyboard-dictionary" : "keyboard-dictionary");
            ui.HandleKeyboardInput("\x1b");
            Check(ui.IsScreenOffForTest(), "Esc uses the global one-key screen-lock path");
            std::cout << "PASS: keyboard dictionary page accepts ke3 and renders candidates.\n";
            return 0;
        }
        if (timer_plan_smoke) {
            Check(ui.OpenPage("timer"), "timer page opens");
            Check(FindLabel(lv_screen_active(), "计划时间"), "timer plan entry exists");
            Shot(folder, dark_smoke ? "dark-timer-plan-entry" : "timer-plan-entry");
            Click("计划时间");
            Check(FindLabel(lv_screen_active(), "计划完成时间") &&
                      FindLabel(lv_screen_active(), "45") && FindLabel(lv_screen_active(), "60") &&
                      FindLabel(lv_screen_active(), "40") && CountArcs(lv_screen_active()) == 4,
                  "timer plan popup has three rotary controls");
            Check(!FindLabel(lv_screen_active(), "可设置 30~99 分钟"),
                  "timer plan popup omits the redundant range hint");
            Shot(folder, dark_smoke ? "dark-timer-plan" : "timer-plan");
            auto first_plan_arc = FindArcBySize(lv_screen_active(), 220);
            Check(first_plan_arc != nullptr, "first timer plan arc exists");
            Check(lv_obj_get_y(first_plan_arc) * 2 + lv_obj_get_height(first_plan_arc) == 136 + 438,
                  "timer plan rings are vertically centred between subjects and actions");
            lv_arc_set_value(first_plan_arc, 75);
            lv_obj_send_event(first_plan_arc, LV_EVENT_VALUE_CHANGED, nullptr);
            Check(FindLabel(lv_screen_active(), "75"), "rotary control updates its minute value");
            Click("保存");
            auto updated_timer_arc = FindArcBySize(lv_screen_active(), 336);
            Check(updated_timer_arc && lv_arc_get_max_value(updated_timer_arc) == 75 * 60,
                  "saving a new plan immediately refreshes the active timer ring range");
            Click("计划时间");
            Check(FindLabel(lv_screen_active(), "75"), "saved plan is retained when reopened");
            std::cout << "PASS: timer plan popup rendered.\n";
            return 0;
        }
        if (timer_rollover_smoke) {
            Check(ui.OpenPage("timer"), "timer rollover page opens");
            Check(FindLabel(lv_screen_active(), "00:00") && Settings::values["han_studys0"] == 0 &&
                      Settings::values["han_studyc0"] == 0,
                  "a stale completed session resets at the next valid calendar day");
            std::cout << "PASS: study timer starts a fresh session on a new day.\n";
            return 0;
        }
        if (dark_smoke || gallery_smoke) {
            ui.UpdateStatusBar();
            auto header_date = FindLabelAt(lv_screen_active(), 670, 41);
            auto header_clock = FindLabelAt(lv_screen_active(), 970, 41);
            Check(header_date && header_clock &&
                      lv_obj_get_style_text_font(header_date, LV_PART_MAIN) ==
                          lv_obj_get_style_text_font(header_clock, LV_PART_MAIN),
                  "header date and clock share one size, weight and typeface");
            const std::string prefix = dark_smoke ? "dark-" : "";
            Shot(folder, (prefix + "home").c_str());
            const char* pages[] = {"dictionary", "phonetics", "timetable", "timer",
                                   "alarm",      "weather",   "network",   "clock"};
            for (const auto* page : pages) {
                Check(ui.OpenPage(page), "gallery page opens");
                Shot(folder, (prefix + page).c_str());
                if (std::string(page) == "timer") {
                    Click("计划时间");
                    Check(FindLabel(lv_screen_active(), "计划完成时间") &&
                              CountArcs(lv_screen_active()) == 4,
                          "gallery timer plan popup has three rotary controls");
                    Shot(folder, (prefix + "timer-plan").c_str());
                    Click("取消");
                }
                if (dark_smoke && std::string(page) == "network") {
                    Click("深色模式");
                    Check(FindLabel(lv_screen_active(), "浅色") &&
                              FindLabel(lv_screen_active(), "自动") &&
                              FindLabel(lv_screen_active(), "夜间开启") &&
                              FindLabel(lv_screen_active(), "保存并应用"),
                          "appearance popup exposes manual and scheduled modes");
                    Shot(folder, "dark-appearance");
                    Click("取消");
                }
            }
            std::cout << "PASS: all gallery pages rendered.\n";
            return 0;
        }
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
        auto home_clock = FindLabelAt(lv_screen_active(), 970, 41);
        Check(home_clock != nullptr && lv_obj_has_flag(home_clock, LV_OBJ_FLAG_CLICKABLE),
              "home time is a clock-page entry");
        lv_obj_send_event(home_clock, LV_EVENT_CLICKED, nullptr);
        ui.SetClockTimeForTest(2026, 9, 15, 8, 26, 47);
        Check(FindLabel(lv_screen_active(), "2026年9月15日    星期二") &&
                  FindLabel(lv_screen_active(), "农历丙午年 · 八月初五"),
              "clock page shows synchronized Gregorian and lunar dates");
        Check(lv_obj_has_flag(FindLabel(lv_screen_active(), "小小助手"), LV_OBJ_FLAG_HIDDEN) &&
                  lv_obj_has_flag(FindImage(lv_screen_active(), &han_status_wifi_off),
                                  LV_OBJ_FLAG_HIDDEN),
              "clock page removes distracting shared chrome");
        Shot(folder, "clock");
        Click("<");
        Check(FindLabel(lv_screen_active(), "小小助手"), "clock back returns home");
        Click("查字典");
        ui.SetStatus("连接中...");
        ui.SetStatus("正在聆听");
        ui.SetChatMessage("user", "智能的智怎么写");
        ui.SetChatMessage("assistant", "好，我帮你打开字典并播放笔顺。");
        ui.SetChatMessage("user", "它可以组成什么词？");
        ui.SetChatMessage("assistant", "可以组成“智慧”，表示聪明和见识。");
        ui.SetChatMessage("user", "再告诉我一句例句吧");
        ui.SetChatMessage("assistant", "我们要用智慧解决学习中遇到的问题。");
        auto history_message = FindLabel(lv_screen_active(), "智能的智怎么写");
        auto history_scroller =
            history_message ? lv_obj_get_parent(lv_obj_get_parent(history_message)) : nullptr;
        Check(history_message && FindLabel(lv_screen_active(), "停止对话") && history_scroller &&
                  lv_obj_has_flag(history_scroller, LV_OBJ_FLAG_SCROLLABLE),
              "feature-page conversation keeps a scrollable chat history and stop action");
        Shot(folder, "assistant-dialog");
        ui.ShowEntry(entry, true);
        Check(FindLabel(lv_screen_active(), "正在打开“小小字典”"),
              "stroke query shows its navigation handoff");
        Shot(folder, "assistant-dialog-navigation");
        auto stop_label = FindLabel(lv_screen_active(), "停止对话");
        auto dialog_scrim = stop_label;
        for (int level = 0; level < 4; ++level)
            dialog_scrim = lv_obj_get_parent(dialog_scrim);
        auto& app = Application::GetInstance();
        app.state = kDeviceStateSpeaking;
        const int stop_count = app.stops;
        Click("停止对话");
        Check(app.stops == stop_count + 1 && app.state == kDeviceStateIdle && dialog_scrim &&
                  lv_obj_has_flag(dialog_scrim, LV_OBJ_FLAG_HIDDEN),
              "one stop-dialog click ends a speaking conversation and hides the modal");
        Click("<");
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
        ClickImage(&han_status_battery_full);
        Check(FindLabel(lv_screen_active(), "电池详情"), "battery details popup opens");
        Check(FindLabel(lv_screen_active(), "7.600 V"), "battery voltage is shown");
        Check(FindLabel(lv_screen_active(), "-180 mA"), "battery current is shown");
        Click("×");
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
        const char* page_titles[] = {"小小字典", "英语音标", "课程表", "作业计时", "闹钟", "天气"};
        for (int i = 0; i < 6; ++i) {
            Click(pages[i]);
            Shot(folder, files[i]);
            if (i != 2) {
                auto page_title = FindLabel(lv_screen_active(), page_titles[i]);
                auto page_date = FindLabelAt(lv_screen_active(), 747, 37);
                auto page_clock = FindLabelAt(lv_screen_active(), 970, 41);
                auto page_wifi = FindImage(lv_screen_active(), &han_status_wifi_off);
                auto page_battery = FindImage(lv_screen_active(), &han_status_battery_unknown);
                Check(page_title && page_date && page_clock && page_wifi && page_battery &&
                          lv_obj_get_y(page_title) == 24 && lv_obj_get_y(page_date) == 37 &&
                          lv_obj_get_y(page_clock) == 41 &&
                          lv_image_get_scale_x(page_wifi) == 288 &&
                          lv_image_get_scale_x(page_battery) == 352,
                      "shared page header keeps one optical centre and balanced status icons");
            }
            if (i == 0) {
                Check(!FindLabel(lv_screen_active(), "语音查字"),
                      "dictionary voice button removed");
                Check(!FindLabel(lv_screen_active(), "离线汉字学习"),
                      "redundant dictionary subtitle removed");
                Check(!FindLabel(lv_screen_active(), "离线字库"),
                      "redundant dictionary status button removed");
                const auto demo = han::ContentStore::Demo();
                han::StrokeGlyph automatic_glyph;
                Check(card.ReadStrokeGlyph(demo.character, automatic_glyph),
                      "automatic playback test glyph loaded");
                ui.ShowEntry(demo, true);
                Check(ui.ApplyStrokeGlyph(demo.character, std::move(automatic_glyph)),
                      "voice lookup glyph accepted");
                lv_tick_inc(800);
                lv_timer_handler();
                const auto first_stroke = "1/" + std::to_string(demo.stroke_count);
                Check(FindLabel(lv_screen_active(), first_stroke.c_str()),
                      "voice writing query automatically starts stroke playback");
                ui.ShowEntry(demo);
                auto definition_text = FindLabel(lv_screen_active(), demo.definition.c_str());
                auto word_text = FindLabel(lv_screen_active(), demo.words.front().c_str());
                Check(definition_text && word_text, "dictionary definition and words are present");
                const auto dictionary_body_font =
                    lv_obj_get_style_text_font(definition_text, LV_PART_MAIN);
                Check(lv_obj_get_style_text_font(word_text, LV_PART_MAIN) == dictionary_body_font,
                      "definitions and words share one font");
                lv_obj_update_layout(lv_screen_active());
                lv_area_t definition_area{};
                lv_area_t first_word_area{};
                lv_obj_get_coords(definition_text, &definition_area);
                lv_obj_get_coords(lv_obj_get_parent(word_text), &first_word_area);
                Check(first_word_area.y1 - definition_area.y2 >= 0 &&
                          first_word_area.y1 - definition_area.y2 <= 10,
                      "word chips closely follow the rendered definition");
                const auto removed_stroke_heading =
                    "笔顺 · 共" + std::to_string(demo.stroke_count) + "画";
                Check(!FindLabel(lv_screen_active(), removed_stroke_heading.c_str()) &&
                          !FindLabel(lv_screen_active(), demo.strokes.front().c_str()),
                      "right-side stroke details are removed");
                const auto stroke_preview = "0/" + std::to_string(demo.stroke_count);
                const auto stroke_first = "1/" + std::to_string(demo.stroke_count);
                const auto stroke_final =
                    std::to_string(demo.stroke_count) + "/" + std::to_string(demo.stroke_count);
                Check(FindLabel(lv_screen_active(), stroke_preview.c_str()),
                      "stroke preview starts before the first stroke");
                Click("下一步");
                Check(FindLabel(lv_screen_active(), stroke_first.c_str()),
                      "first next selects stroke one");
                Click("上一步");
                Check(FindLabel(lv_screen_active(), stroke_preview.c_str()),
                      "previous returns to untouched preview");
                Click("播放笔顺");
                for (int tick = 0; tick <= demo.stroke_count; ++tick) {
                    lv_tick_inc(800);
                    lv_timer_handler();
                }
                Check(FindLabel(lv_screen_active(), stroke_final.c_str()),
                      "playback reaches its final state");
                auto pinyin_icon = FindImage(lv_screen_active(), &han_icon_pinyin_search);
                auto definition_icon = FindImage(lv_screen_active(), &han_icon_definition_detail);
                Check(pinyin_icon && definition_icon, "dictionary action icons are present");
                auto pinyin_button = lv_obj_get_parent(pinyin_icon);
                auto definition_button = lv_obj_get_parent(definition_icon);
                Check(lv_obj_get_width(pinyin_button) >= 136 &&
                          lv_obj_get_width(definition_button) >= 136 &&
                          lv_obj_get_x(pinyin_button) - (lv_obj_get_x(definition_button) +
                                                         lv_obj_get_width(definition_button)) >=
                              20 &&
                          FindLabel(definition_button, "释义") && FindLabel(pinyin_button, "拼音"),
                      "dictionary actions are large, labeled and separated");
                ClickImage(&han_icon_definition_detail);
                const auto full_definition_title = demo.character + " 的完整释义";
                auto definition_title =
                    FindLabel(lv_screen_active(), full_definition_title.c_str());
                Check(definition_title, "full definition opens above dictionary");
                auto definition_overlay = lv_obj_get_parent(definition_title);
                auto full_definition = FindLabel(definition_overlay, demo.definition.c_str());
                Check(full_definition &&
                          lv_obj_get_style_text_font(full_definition, LV_PART_MAIN) ==
                              dictionary_body_font &&
                          lv_obj_get_style_text_font(definition_title, LV_PART_MAIN) ==
                              dictionary_body_font,
                      "definition overlay keeps the dictionary body font");
                Check(!FindLabel(definition_overlay, demo.pinyin.c_str()),
                      "definition overlay omits redundant pinyin");
                Check(
                    lv_obj_has_flag(lv_obj_get_parent(full_definition), LV_OBJ_FLAG_CLICKABLE) &&
                        lv_obj_has_flag(lv_obj_get_parent(full_definition), LV_OBJ_FLAG_SCROLLABLE),
                    "definition detail accepts vertical touch scrolling");
                Shot(folder, "dictionary-definition");
                Click("关闭");
                auto long_definition_entry = demo;
                long_definition_entry.definition.clear();
                for (int paragraph = 0; paragraph < 12; ++paragraph) {
                    long_definition_entry.definition += demo.definition;
                    long_definition_entry.definition += '\n';
                }
                ui.ShowEntry(long_definition_entry);
                ClickImage(&han_icon_definition_detail);
                auto long_definition =
                    FindLabel(lv_screen_active(), long_definition_entry.definition.c_str());
                auto definition_scroller = lv_obj_get_parent(long_definition);
                lv_obj_update_layout(definition_scroller);
                Check(lv_obj_get_scroll_bottom(definition_scroller) > 0,
                      "long definition creates vertical scroll range");
                lv_obj_scroll_to_y(definition_scroller,
                                   lv_obj_get_scroll_bottom(definition_scroller), LV_ANIM_OFF);
                Check(lv_obj_get_scroll_y(definition_scroller) > 0,
                      "definition detail can reach its final paragraph");
                Shot(folder, "dictionary-definition-scrolled");
                Click("关闭");
                ui.ShowEntry(demo);
                ClickImage(&han_icon_pinyin_search);
                Check(FindLabel(lv_screen_active(), "输入拼音，例如 han"), "pinyin search opens");
                Check(
                    FindLabel(lv_screen_active(), "轻声") && FindLabel(lv_screen_active(), "四声"),
                    "pinyin tone filters are visible");
                Check(lv_obj_get_style_text_font(FindLabel(lv_screen_active(), "拼音查字"),
                                                 LV_PART_MAIN) == &han_font_timer &&
                          lv_obj_get_style_text_font(FindLabel(lv_screen_active(), "查找"),
                                                     LV_PART_MAIN) == &han_font_timer,
                      "touch pinyin controls use the rounded emphasis font");
                lv_obj_update_layout(lv_screen_active());
                auto touch_title = FindLabel(lv_screen_active(), "拼音查字");
                auto touch_input = FindLabel(lv_screen_active(), "输入拼音，例如 han");
                auto touch_search = FindLabel(lv_screen_active(), "查找");
                auto touch_close = FindLabel(lv_screen_active(), "关闭");
                auto tone_title = FindLabel(lv_screen_active(), "音调");
                auto tone_all = FindLabel(lv_screen_active(), "全部");
                lv_area_t touch_title_area{}, touch_input_area{}, touch_search_area{},
                    touch_close_area{}, tone_title_area{}, tone_all_area{};
                lv_obj_get_coords(touch_title, &touch_title_area);
                lv_obj_get_coords(touch_input, &touch_input_area);
                lv_obj_get_coords(touch_search, &touch_search_area);
                lv_obj_get_coords(touch_close, &touch_close_area);
                lv_obj_get_coords(tone_title, &tone_title_area);
                lv_obj_get_coords(tone_all, &tone_all_area);
                const auto visual_center_y = [](const lv_area_t& area) {
                    return area.y1 + area.y2;
                };
                Check(std::abs(visual_center_y(touch_title_area) -
                               visual_center_y(touch_input_area)) <= 4 &&
                          std::abs(visual_center_y(touch_search_area) -
                                   visual_center_y(touch_input_area)) <= 4 &&
                          std::abs(visual_center_y(touch_close_area) -
                                   visual_center_y(touch_input_area)) <= 4,
                      "touch pinyin header shares one visual centre line");
                Check(std::abs(visual_center_y(tone_title_area) - visual_center_y(tone_all_area)) <=
                          4,
                      "touch tone heading and filter chips share one visual centre line");
                Click("h");
                Click("a");
                Click("n");
                Check(FindLabel(lv_screen_active(), "han"), "pinyin keypad entry");
                Click("四声");
                Check(FindLabel(lv_screen_active(), "正在离线字库中查找…") &&
                          FindLabel(lv_screen_active(), "0%") && CountBars(lv_screen_active()) == 1,
                      "touch pinyin lookup starts with a real progress indicator");
                ui.AdvancePinyinProgressForTest(61);
                Check(FindLabel(lv_screen_active(), "8%") &&
                          FindLabel(lv_screen_active(), "正在读取拼音索引…"),
                      "touch pinyin progress advances through the real lookup phases");
                ui.AdvancePinyinProgressForTest(61);
                Check(FindLabel(lv_screen_active(), "16%"),
                      "touch pinyin progress remains visibly continuous");
                ui.SetPinyinResultsForTest(
                    "han4",
                    {"鉲", "鯻", "三", "四", "五", "六", "七", "八", "九", "十", "人", "大", "中",
                     "小", "天", "地", "上", "下", "左", "右", "前", "后", "学", "习", "字"});
                Check(FindLabel(lv_screen_active(), "han4 · 共25字，点击查看"),
                      "search reports the complete homophone count");
                Check(FindLabel(lv_screen_active(), "1/3 · 左右滑动翻页"),
                      "search results expose pagination");
                auto first_candidate = FindLabel(lv_screen_active(), "鉲");
                Check(first_candidate &&
                          lv_obj_get_style_text_font(first_candidate, LV_PART_MAIN) ==
                              dictionary_body_font &&
                          lv_obj_get_style_transform_scale_x(first_candidate, LV_PART_MAIN) == 320,
                      "candidate characters use the antialiased dictionary candidate font");
                lv_font_glyph_dsc_t rare{};
                Check(lv_font_get_glyph_dsc(dictionary_body_font, &rare, 0x9272, 0) &&
                          !rare.is_placeholder &&
                          lv_font_get_glyph_dsc(dictionary_body_font, &rare, 0x9bfb, 0) &&
                          !rare.is_placeholder,
                      "rare pinyin candidates have real glyphs");
                Shot(folder, "dictionary-search-rare");
                Click("›");
                Check(FindLabel(lv_screen_active(), "2/3 · 左右滑动翻页"),
                      "search results can move to the next page");
                Shot(folder, "dictionary-search");
                Click("关闭");
                auto long_entry = demo;
                long_entry.stroke_count = 13;
                long_entry.strokes.resize(13, "横");
                long_entry.strokes[12] = "横撇弯钩";
                long_entry.definition = demo.definition + "\n" + demo.definition + "\n" +
                                        demo.definition + "\n" + demo.definition;
                long_entry.words = {"横撇弯钩", "词二", "词三", "词四", "词五",   "词六",
                                    "词七",     "词八", "词九", "词十", "词十一", "词十二"};
                ui.ShowEntry(long_entry);
                auto empty_canvas = FindCanvas(lv_screen_active(), 400, 400);
                Check(empty_canvas && lv_obj_has_flag(empty_canvas, LV_OBJ_FLAG_HIDDEN),
                      "new character canvas stays hidden until its glyph renders");
                auto expanded_definition =
                    FindLabel(lv_screen_active(), long_entry.definition.c_str());
                auto wide_word = FindLabel(lv_screen_active(), "横撇弯钩");
                lv_obj_update_layout(lv_screen_active());
                lv_point_t wide_word_size{};
                lv_text_get_size(&wide_word_size, "横撇弯钩", dictionary_body_font, 0, 0,
                                 LV_COORD_MAX, LV_TEXT_FLAG_NONE);
                Check(expanded_definition && lv_obj_get_height(expanded_definition) > 91,
                      "freed stroke area displays a taller definition");
                Check(wide_word && lv_obj_get_width(wide_word) >= wide_word_size.x,
                      "word pills use their measured rendered width");
                Check(FindLabel(lv_screen_active(), "词八"),
                      "flowing word pills use multiple available rows");
                Check(!FindLabel(lv_screen_active(), "笔顺 · 共13画") &&
                          !FindLabel(lv_screen_active(), "当前：横"),
                      "right-side stroke details stay removed");
                Shot(folder, "dictionary-expanded-content");
                Check(ui.ApplyMissingStrokeGlyph(long_entry.character),
                      "missing vector data is handled for the current character");
                Check(empty_canvas && lv_obj_has_flag(empty_canvas, LV_OBJ_FLAG_HIDDEN),
                      "missing vector data leaves the grid artwork empty");
                Shot(folder, "dictionary-missing-strokes");
            }
            if (i == 1) {
                Check(!FindLabel(lv_screen_active(), "英式音标 · 44 音学习卡") &&
                          !FindLabel(lv_screen_active(), "当前音标") &&
                          !FindLabel(lv_screen_active(), "点选音标开始学习") &&
                          !FindLabel(lv_screen_active(), "听一听，跟着读") &&
                          !FindLabel(lv_screen_active(), "点音标或单词即可播放发音") &&
                          FindLabel(lv_screen_active(), "▶  听示范"),
                      "phonetics uses the approved full-height learning composition");
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
                Check(FindLabel(lv_screen_active(), "今日作业") &&
                          FindLabel(lv_screen_active(), "本周用时"),
                      "timer includes the polished homework and weekly summaries");
                Check(!FindLabel(lv_screen_active(), "专心完成这一科") &&
                          !FindLabel(lv_screen_active(), "调整记录") &&
                          !FindLabel(lv_screen_active(), "新一轮作业"),
                      "timer omits the rejected footer and record controls");
                Check(FindLabel(lv_screen_active(), "00:00"),
                      "timer uses minutes and seconds only");
                const char* timer_subjects[] = {"语文", "数学", "英语"};
                for (int subject = 0; subject < 3; ++subject) {
                    auto subject_label = FindLabel(lv_screen_active(), timer_subjects[subject]);
                    auto subject_chip = lv_obj_get_parent(subject_label);
                    auto subject_icon =
                        FindImage(subject_chip, subject == 0   ? &han_subject_book
                                                : subject == 1 ? &han_subject_calculator
                                                               : &han_subject_english);
                    Check(subject_icon && lv_obj_get_x(subject_icon) == 41 &&
                              lv_obj_get_y(subject_icon) == 18 &&
                              lv_obj_get_x(subject_label) == 93 &&
                              lv_obj_get_y(subject_label) == 22 &&
                              lv_obj_get_width(subject_label) == 72 &&
                              lv_obj_get_x(subject_chip) == 18 + subject * 226,
                          "subject icon and label are centred as one unit");
                }
                auto timer_value = FindLabel(lv_screen_active(), "00:00");
                auto timer_arc = FindArc(lv_screen_active());
                Check(timer_arc && timer_value, "timer ring and value exist");
                auto timer_card = lv_obj_get_parent(timer_arc);
                Check(lv_obj_get_x(timer_arc) == 179 && lv_obj_get_y(timer_arc) == 112 &&
                          lv_obj_get_width(timer_arc) == 336 && lv_obj_get_x(timer_value) == 199 &&
                          lv_obj_get_y(timer_value) == 250 &&
                          lv_obj_get_height(timer_value) == 60 &&
                          lv_obj_get_x(timer_arc) * 2 + lv_obj_get_width(timer_arc) ==
                              lv_obj_get_width(timer_card) &&
                          lv_obj_get_y(timer_arc) * 2 + lv_obj_get_height(timer_arc) ==
                              lv_obj_get_y(timer_value) * 2 + lv_obj_get_height(timer_value),
                      "timer ring and value share the exact card centre");
                Check(!FindLabel(lv_screen_active(), "六") && !FindLabel(lv_screen_active(), "日"),
                      "weekly chart keeps school days only");
                Check(FindLabel(lv_screen_active(), "计划时间"),
                      "timer exposes a discoverable plan-time entry");
                Click("计划时间");
                Check(FindLabel(lv_screen_active(), "计划完成时间") &&
                          FindLabel(lv_screen_active(), "45") &&
                          FindLabel(lv_screen_active(), "60") &&
                          FindLabel(lv_screen_active(), "40") && CountArcs(lv_screen_active()) == 4,
                      "timer plan popup has three aligned per-subject rotary controls");
                Shot(folder, "timer-plan");
                Click("取消");
                Check(!FindLabel(lv_screen_active(), "计划完成时间"),
                      "timer plan popup closes without changing values");
                lv_font_glyph_dsc_t timer_glyph{};
                Check(lv_font_get_glyph_dsc(&han_font_timer, &timer_glyph, 0x8bb0, 0) &&
                          lv_font_get_glyph_dsc(&han_font_timer, &timer_glyph, 0x5f55, 0) &&
                          lv_font_get_glyph_dsc(&han_font_timer, &timer_glyph, 0x76d8, 0) &&
                          lv_font_get_glyph_dsc(&han_font_timer_title, &timer_glyph, 0x4e00, 0) &&
                          lv_font_get_glyph_dsc(&han_font_timer_title, &timer_glyph, 0x4e94, 0),
                      "timer fonts contain historical status and weekday glyphs");
                const char* chart_days[] = {"一", "二", "三", "四", "五"};
                const char* chart_titles[] = {"周一作业", "周二作业", "周三作业", "周四作业",
                                              "周五作业"};
                const auto now = std::time(nullptr);
                std::tm local{};
                localtime_s(&local, &now);
                const int today = (local.tm_wday + 6) % 7;
                const int history_day = today == 0 ? 1 : 0;
                Click(chart_days[history_day]);
                Check(FindLabel(lv_screen_active(), chart_titles[history_day]),
                      "weekday selection refreshes the homework card");
                if (today >= 0 && today < 5)
                    Click(chart_days[today]);
                Click("开始计时");
                lv_tick_inc(999);
                lv_timer_handler();
                Check(FindLabel(lv_screen_active(), "00:00"),
                      "timer does not advance before a whole second");
                lv_tick_inc(1);
                lv_timer_handler();
                Check(FindLabel(lv_screen_active(), "00:01"),
                      "timer advances exactly on the first whole second");
                lv_tick_inc(64000);
                lv_timer_handler();
                Click("<");
                Click("作业计时");
                Click("暂停");
                Check(FindLabel(lv_screen_active(), "01:05"), "timer survives back in MM:SS");
                Shot(folder, "timer-paused");
            }
            if (i == 5) {
                ui.SetWeatherTextForTest(
                    R"({"schema_version":2,"city":"青岛","updated_at":"09-15 08:49","cached":true,"current":{"condition":"晴间多云","condition_code":"101","temperature":24,"feels_like":22,"humidity":45,"wind":"西北风","wind_scale":3},"days":[{"date":"2026-09-15","condition":"晴","condition_code":"100","temperature_min":20,"temperature_max":26,"precipitation_probability":10,"sunrise":"05:42","sunset":"18:08"},{"date":"2026-09-16","condition":"多云","condition_code":"101","temperature_min":19,"temperature_max":25,"precipitation_probability":30,"sunrise":"05:43","sunset":"18:06"},{"date":"2026-09-17","condition":"小雨","condition_code":"305","temperature_min":18,"temperature_max":22,"precipitation_probability":70,"sunrise":"05:44","sunset":"18:05"},{"date":"2026-09-18","condition":"阴","condition_code":"104","temperature_min":19,"temperature_max":24,"precipitation_probability":20,"sunrise":"05:45","sunset":"18:03"}],"air":{"category":"优","aqi":"34"},"index":{"name":"穿衣","category":"舒适","text":"天气舒适，适合穿长袖衬衫或薄外套。早晚海边风大，记得及时添衣。"}})");
                Check(FindLabel(lv_screen_active(), "缓存"), "cached weather is explicit");
                Check(FindLabel(lv_screen_active(), "空气质量") &&
                          FindLabel(lv_screen_active(), "降水概率") &&
                          FindLabel(lv_screen_active(), "日出日落") &&
                          FindLabel(lv_screen_active(), "生活指数"),
                      "weather metrics are all visible");
                Shot(folder, "weather-data");
                Click("生活指数");
                Check(FindLabel(lv_screen_active(), "生活指数完整建议"),
                      "lifestyle index opens a complete detail popup");
                Shot(folder, "weather-index-detail");
                Click("关闭");
                Check(!FindLabel(lv_screen_active(), "生活指数完整建议"),
                      "lifestyle index popup closes");
            } else if (i == 4) {
                Check(!FindLabel(lv_screen_active(), "设置提醒时间") &&
                          !FindLabel(lv_screen_active(), "上学起床"),
                      "alarm removes redundant time and purpose labels");
                Check(!FindLabel(lv_screen_active(), "重复日期") &&
                          FindLabel(lv_screen_active(), "周一") &&
                          FindLabel(lv_screen_active(), "周日"),
                      "alarm exposes all seven day choices without a redundant heading");
                Check(FindLabel(lv_screen_active(), "保存并开启") &&
                          FindLabel(lv_screen_active(), "关闭闹钟"),
                      "alarm actions remain large and explicit");
                auto ringtone_title = FindLabel(lv_screen_active(), "铃声");
                auto ringtone_dropdown = FindDropdown(lv_screen_active());
                auto preview_text = FindLabel(lv_screen_active(), "试听");
                auto save_text = FindLabel(lv_screen_active(), "保存并开启");
                auto disable_text = FindLabel(lv_screen_active(), "关闭闹钟");
                Check(ringtone_title && ringtone_dropdown && preview_text && save_text &&
                          disable_text &&
                          lv_obj_get_style_text_font(ringtone_title, LV_PART_MAIN) ==
                              lv_obj_get_style_text_font(ringtone_dropdown, LV_PART_MAIN) &&
                          lv_obj_get_style_text_font(preview_text, LV_PART_MAIN) ==
                              lv_obj_get_style_text_font(save_text, LV_PART_MAIN) &&
                          lv_color_eq(lv_obj_get_style_bg_color(lv_obj_get_parent(preview_text),
                                                                LV_PART_MAIN),
                                      lv_color_hex(0x3a9df5)) &&
                          lv_color_eq(lv_obj_get_style_bg_color(lv_obj_get_parent(disable_text),
                                                                LV_PART_MAIN),
                                      lv_color_hex(0xf05b78)),
                      "alarm ringtone and actions use consistent bold high-contrast styling");
                Click("保存并开启");
                Check(FindLabel(lv_screen_active(), "闹钟已开启") &&
                          FindLabel(lv_screen_active(), "✓"),
                      "enabled alarm uses the checked status pill");
                Shot(folder, "alarm-enabled");
            }
            if (i == 2) {
                Check(FindLabel(lv_screen_active(), "还没有课程，请导入课表"),
                      "empty template does not invent lessons");
                Check(!FindLabel(lv_screen_active(), "明天要带") &&
                          !FindLabel(lv_screen_active(), "问问明天上什么课"),
                      "timetable keeps the full canvas for lessons");
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
                          FindLabel(lv_screen_active(), "社团") &&
                          FindLabel(lv_screen_active(), "第7节"),
                      "all seven lessons and custom subjects fit on one screen");
                Check(!FindLabel(lv_screen_active(), "本周") &&
                          !FindLabel(lv_screen_active(), "下周") &&
                          !FindLabel(lv_screen_active(), "周末") &&
                          !FindLabel(lv_screen_active(), "周六") &&
                          !FindLabel(lv_screen_active(), "周日"),
                      "weekly timetable has no redundant week or weekend navigation");
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
        Check(FindLabel(lv_screen_active(), "网络与存储") &&
                  FindLabel(lv_screen_active(), "显示与声音") &&
                  FindLabel(lv_screen_active(), "自动锁屏") &&
                  FindLabel(lv_screen_active(), "10 分钟") &&
                  FindLabel(lv_screen_active(), "立即关屏") &&
                  !FindLabel(lv_screen_active(), "+") && !FindLabel(lv_screen_active(), "-"),
              "settings page exposes storage, sliders, auto lock and screen-off controls");
        auto appearance_control = FindLabel(lv_screen_active(), "外观模式");
        auto screen_off_control = FindLabel(lv_screen_active(), "立即关屏");
        Check(appearance_control && screen_off_control &&
                  lv_obj_get_style_text_font(appearance_control, LV_PART_MAIN) ==
                      lv_obj_get_style_text_font(screen_off_control, LV_PART_MAIN) &&
                  lv_color_eq(lv_obj_get_style_bg_color(lv_obj_get_parent(screen_off_control),
                                                        LV_PART_MAIN),
                              lv_color_hex(0xf05b78)),
              "screen-off control matches the appearance font and uses the deeper red fill");
        auto settings_assistant = FindLabel(lv_screen_active(), "小智");
        Check(settings_assistant &&
                  lv_obj_has_flag(lv_obj_get_parent(lv_obj_get_parent(settings_assistant)),
                                  LV_OBJ_FLAG_HIDDEN),
              "settings page uses the full content height without the assistant footer");
        ui.SetUsbStorageActiveForTest(true);
        Check(FindLabel(lv_screen_active(), "USB 读卡器已开启") &&
                  FindLabel(lv_screen_active(), "电脑可以访问 microSD 卡") &&
                  FindLabel(lv_screen_active(), "重启并恢复"),
              "USB storage mode provides safe-eject guidance and a recovery action");
        Shot(folder, "network-usb-storage");
        Click("重启并恢复");
        Check(FindLabel(lv_screen_active(), "网络与存储"),
              "USB recovery returns to normal settings in the host simulator");
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
            Check(generated.SearchPinyin("gui", pinyin_results) &&
                      std::find(pinyin_results.begin(), pinyin_results.end(), "规") !=
                          pinyin_results.end(),
                  "indexed pinyin candidate search");
            Check(generated.SearchPinyin("gui1", pinyin_results) &&
                      std::find(pinyin_results.begin(), pinyin_results.end(), "规") !=
                          pinyin_results.end(),
                  "tone filtering works with legacy base-only pinyin index");
            Check(generated.SearchPinyin("yi", pinyin_results, 1024) && pinyin_results.size() > 24,
                  "full homophone search is not truncated to the first page");
            Check(generated.SearchPinyin("fa2", pinyin_results, 1024) && !pinyin_results.empty(),
                  "tone-specific candidates are available for stroke-count ordering");
            bool reached_rare_candidates = false;
            int previous_stroke_count = 0;
            for (const auto& candidate : pinyin_results) {
                const bool common = han::ContentStore::IsCommonCharacter(candidate);
                Check(!common || !reached_rare_candidates,
                      "common candidates remain ahead of rare candidates");
                if (!common && !reached_rare_candidates) {
                    reached_rare_candidates = true;
                    previous_stroke_count = 0;
                }
                han::Entry candidate_entry;
                const int stroke_count =
                    generated.Lookup(candidate, candidate_entry) && candidate_entry.stroke_count > 0
                        ? candidate_entry.stroke_count
                        : 65;
                Check(stroke_count >= previous_stroke_count,
                      "same-tone candidates are ordered by increasing stroke count");
                previous_stroke_count = stroke_count;
            }
            Check(generated.Lookup("汉", entry), "indexed dictionary lookup");
            Check(entry.source == "guoxuedashi-xinhua-community" && entry.stroke_count > 0,
                  "indexed dictionary metadata");
            han::StrokeGlyph glyph;
            Check(generated.ReadStrokeGlyph("汉", glyph), "read indexed vector strokes");
            ui.ShowEntry(entry, false, true, false, std::move(glyph));
            auto heading_canvas = FindCanvas(lv_screen_active(), 96, 88);
            auto grid_canvas = FindCanvas(lv_screen_active(), 400, 400);
            Check(heading_canvas, "large dictionary heading canvas exists");
#if LV_USE_VECTOR_GRAPHIC
            Check(grid_canvas && !lv_obj_has_flag(grid_canvas, LV_OBJ_FLAG_HIDDEN) &&
                      !lv_obj_has_flag(heading_canvas, LV_OBJ_FLAG_HIDDEN),
                  "both preloaded vector glyphs are visible on the first frame");
#endif
            auto heading_pinyin = FindLabel(lv_screen_active(), entry.pinyin.c_str());
            lv_obj_update_layout(lv_screen_active());
            lv_area_t heading_area{}, pinyin_area{};
            lv_obj_get_coords(heading_canvas, &heading_area);
            lv_obj_get_coords(heading_pinyin, &pinyin_area);
            Check(std::abs((heading_area.y1 + heading_area.y2) -
                           (pinyin_area.y1 + pinyin_area.y2)) <= 8,
                  "large dictionary character and pinyin are vertically centered");
            Shot(folder, "dictionary-han-indexed");
            Check(generated.Lookup("嗝", entry) && entry.radical == "口" &&
                      entry.structure == "左右结构" && entry.stroke_count == 13,
                  "cnchar metadata and full stroke list for 嗝");
            ui.ShowEntry(entry);
            Check(generated.ReadStrokeGlyph("嗝", glyph), "read 13-stroke vector glyph");
            Check(ui.ApplyStrokeGlyph("嗝", std::move(glyph)), "apply 13-stroke vector glyph");
            Check(!FindLabel(lv_screen_active(), "笔顺 · 共13画"),
                  "indexed entries omit right-side stroke details");
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
