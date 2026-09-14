#pragma once
#include "content_store.h"
#ifdef HAN_UI_HOST_SIM
#include "platform.h"
#else
#include <esp_lcd_touch.h>
#include <freertos/queue.h>
#include "display/lcd_display.h"
#endif
#include <atomic>
#include <functional>
#include "study_timer.h"
#include "timetable.h"

// Opt-in product display. The generic LCD and all other boards remain independent.
class HanDisplay : public MipiLcdDisplay {
public:
    using MipiLcdDisplay::MipiLcdDisplay;
    void AttachTouch(esp_lcd_touch_handle_t touch);
    void SetNetworkAction(std::function<void()> action) { network_action_ = std::move(action); }
    void SetUsbStorageAction(std::function<std::string()> action) {
        usb_storage_action_ = std::move(action);
    }
    void SetupUI() override;
    void SetTheme(Theme* theme) override;
    void SetStatus(const char* status) override;
    void SetEmotion(const char*) override {}
    void SetChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void UpdateStatusBar(bool update_all = false) override;
    void ShowEntry(const han::Entry& entry);
    bool OpenPage(const std::string& page);
    bool ApplyStrokeGlyph(const std::string& character, han::StrokeGlyph glyph);
    bool ApplyMissingStrokeGlyph(const std::string& character);
    bool ApplyTimetable(const std::string& json);
#ifdef HAN_UI_HOST_SIM
    void SetWeatherTextForTest(std::string text);
#endif

private:
    enum class Page { Home, Dictionary, Phonetics, Timetable, Timer, Alarm, Weather, Network };
    struct Job {
        int type;
        char value[128];
    };
    static void OnClick(lv_event_t* event);
    static void Tick(lv_timer_t* timer);
    static void OnRefresh(lv_event_t* event);
    static void Worker(void* self);
    void Action(int action);
    void Render(Page page);
    void Home();
    void Dictionary();
    void Phonetics();
    void Timetable();
    void Timer();
    void Alarm();
    void Weather();
    void Network();
    void SetScreenOff(bool off);
    void UpdateSettingLabels();
    void UpdateTimer();
    void UpdateStroke();
    void RenderStroke();
    void ShowStrokeFallback(const std::string& character);
    void OpenPinyinSearch();
    void OpenDefinitionDetails();
    void StartPinyinSearch();
    void UpdatePinyinToneButtons();
    void RenderPinyinResults(const char* status);
    void ApplyPinyinResults(const std::string& query, std::vector<std::string> results);
    void ApplyWeatherArt(std::string id, std::string data);
    void SaveTimer();
    void LoadPreferences();
    bool Queue(int type, const std::string& value);
    void Toast(const char* text);
    lv_obj_t* Box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color);
    lv_obj_t* Card(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color);
    lv_obj_t* Label(lv_obj_t* parent, const char* text, int x, int y, int w,
                    const lv_font_t* font = nullptr);
    const lv_font_t* DynamicTextFont() const;
    const lv_font_t* DictionaryTextFont() const;
    void ApplyDynamicTextFont(lv_obj_t* label);
    void ApplyDictionaryTextFont(lv_obj_t* label);
    void InstallDictionaryFont(std::string data);
    lv_obj_t* Button(lv_obj_t* parent, const char* text, int x, int y, int w, int h, uint32_t color,
                     int action);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* body_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* back_ = nullptr;
    lv_obj_t* back_image_ = nullptr;
    lv_obj_t* clock_ = nullptr;
    lv_obj_t* date_ = nullptr;
    lv_obj_t* mascot_ = nullptr;
    lv_obj_t* footer_ = nullptr;
    lv_obj_t* top_divider_left_ = nullptr;
    lv_obj_t* top_divider_right_ = nullptr;
    lv_obj_t* wifi_button_ = nullptr;
    lv_obj_t* wifi_image_ = nullptr;
    lv_obj_t* battery_image_ = nullptr;
    lv_obj_t* page_icon_ = nullptr;
    lv_obj_t* assistant_card_ = nullptr;
    lv_obj_t* assistant_badge_ = nullptr;
    lv_obj_t* role_box_ = nullptr;
    lv_obj_t* role_label_ = nullptr;
    lv_obj_t* message_ = nullptr;
    lv_obj_t* status_box_ = nullptr;
    lv_obj_t* timetable_voice_label_ = nullptr;
    lv_obj_t* timetable_reply_card_ = nullptr;
    lv_obj_t* timetable_message_ = nullptr;
    lv_obj_t* network_info_ = nullptr;
    lv_obj_t* brightness_value_ = nullptr;
    lv_obj_t* volume_value_ = nullptr;
    lv_obj_t* brightness_bar_ = nullptr;
    lv_obj_t* volume_bar_ = nullptr;
    lv_obj_t* screen_wake_overlay_ = nullptr;
    lv_obj_t* timer_value_ = nullptr;
    lv_obj_t* totals_[3]{};
    lv_obj_t* stroke_value_ = nullptr;
    lv_obj_t* stroke_image_ = nullptr;
    lv_obj_t* stroke_placeholder_ = nullptr;
    lv_obj_t* stroke_fallback_character_ = nullptr;
    lv_obj_t* glyph_title_image_ = nullptr;
    lv_obj_t* glyph_title_placeholder_ = nullptr;
    lv_draw_buf_t* stroke_draw_buf_ = nullptr;
    lv_draw_buf_t* glyph_title_draw_buf_ = nullptr;
    han::StrokeGlyph stroke_glyph_;
    std::string expected_stroke_character_;
    std::array<lv_obj_t*, 64> stroke_chips_{};
    std::array<lv_obj_t*, 64> stroke_chip_images_{};
    std::array<lv_draw_buf_t*, 64> stroke_chip_draw_bufs_{};
    lv_obj_t* search_ = nullptr;
    lv_obj_t* search_overlay_ = nullptr;
    lv_obj_t* search_input_ = nullptr;
    lv_obj_t* search_results_ = nullptr;
    lv_obj_t* search_status_ = nullptr;
    lv_obj_t* definition_overlay_ = nullptr;
    std::array<lv_obj_t*, 6> pinyin_tone_buttons_{};
    std::string pinyin_query_;
    std::string pinyin_search_key_;
    std::vector<std::string> pinyin_results_;
    int pinyin_tone_ = -1;
    lv_obj_t* alarm_hour_ = nullptr;
    lv_obj_t* alarm_minute_ = nullptr;
    lv_timer_t* tick_ = nullptr;
    bool page_refresh_pending_ = false;
    bool page_refresh_active_ = false;
    int64_t page_render_started_ms_ = 0;
    int64_t page_build_ms_ = 0;
    int64_t refresh_started_ms_ = 0;
    int64_t flush_started_us_ = 0;
    int64_t flush_wait_started_us_ = 0;
    int64_t flush_submit_us_ = 0;
    int64_t flush_wait_us_ = 0;
    uint32_t flush_count_ = 0;
    uint32_t flush_pixels_ = 0;
    QueueHandle_t jobs_ = nullptr;
    std::function<void()> network_action_;
    std::function<std::string()> usb_storage_action_;
    std::atomic<bool> usb_storage_requested_{false};
    std::atomic<bool> usb_storage_active_{false};
    std::atomic<bool> screen_off_{false};
    han::Entry entry_ = han::ContentStore::Demo();
    han::StudyTimer study_;
    Page page_ = Page::Home;
    int sound_ = 0;
    int category_ = 0;
    int sound_page_ = 0;
    int stroke_ = 0;
    bool stroke_playing_ = false;
    int alarm_minutes_ = 405;
    bool alarm_enabled_ = false;
    int64_t alarm_last_day_ = -1;
    bool alarm_ringing_ = false;
    std::atomic<bool> local_audio_{false};
    int64_t last_checkpoint_ms_ = 0;
    han::TimetableData timetable_;
    int timetable_week_ = 0, timetable_day_group_ = 0;
    int timetable_today_ = -1, supplies_page_ = 0;
    int64_t timetable_date_key_ = -1;
    std::array<bool, 8> supplies_checked_{};
    std::string weather_text_;
    std::string weather_art_id_;
    std::string weather_art_data_;
    lv_image_dsc_t weather_art_dsc_{};
#ifndef HAN_UI_HOST_SIM
    std::string dictionary_font_data_;
    lv_font_t* dictionary_font_ = nullptr;
    lv_font_t dictionary_ui_font_{};
    bool dictionary_ui_font_ready_ = false;
#endif
    int brightness_setting_ = 75;
    int volume_setting_ = 70;
    bool initial_banner_pending_ = true;
};
