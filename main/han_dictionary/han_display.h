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
#include <ctime>
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
    void SetUsbStorageRestoreAction(std::function<std::string()> action) {
        usb_storage_restore_action_ = std::move(action);
    }
    void SetupUI() override;
    void SetTheme(Theme* theme) override;
    void SetStatus(const char* status) override;
    void SetEmotion(const char*) override {}
    void SetChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void UpdateStatusBar(bool update_all = false) override;
    void ShowEntry(const han::Entry& entry, bool auto_play_strokes = false);
    void HandleKeyboardInput(const std::string& input);
    void ShowKeyboardConnected();
    bool OpenPage(const std::string& page);
    bool ApplyStrokeGlyph(const std::string& character, han::StrokeGlyph glyph);
    bool ApplyMissingStrokeGlyph(const std::string& character);
    bool ApplyTimetable(const std::string& json);
#ifdef HAN_UI_HOST_SIM
    void SetWeatherTextForTest(std::string text);
    void SetClockTimeForTest(int year, int month, int day, int hour, int minute, int second);
    void SetUsbStorageActiveForTest(bool active);
    void SetPinyinResultsForTest(const std::string& query, std::vector<std::string> results) {
        ApplyPinyinResults(query, std::move(results));
    }
#endif

private:
    enum class Page {
        Home,
        Dictionary,
        Phonetics,
        Timetable,
        Timer,
        Alarm,
        Weather,
        Network,
        Clock,
        KeyboardLookup
    };
    enum class ThemeMode {
        Light = 0,
        Dark = 1,
        Auto = 2,
    };
    struct FlipDigit {
        lv_obj_t* card = nullptr;
        lv_obj_t* steady_label = nullptr;
        lv_obj_t* old_top = nullptr;
        lv_obj_t* old_bottom = nullptr;
        lv_obj_t* new_bottom = nullptr;
        int value = -2;
    };
    struct Job {
        int type;
        char value[128];
    };
    struct ChatHistoryItem {
        bool user = false;
        std::string text;
    };
    static void OnClick(lv_event_t* event);
    static void OnScreenWake(lv_event_t* event);
    static void ShowLockScreenAsync(void* user_data);
    static void OnPinyinGesture(lv_event_t* event);
    static void OnSettingsGesture(lv_event_t* event);
    static void OnLockGesture(lv_event_t* event);
    static void OnLockReleased(lv_event_t* event);
    static void UnlockScreenAsync(void* user_data);
    static void OnSettingSliderChanged(lv_event_t* event);
    static void OnSettingSliderReleased(lv_event_t* event);
    static void OnTimerPlanChanged(lv_event_t* event);
    static void OnAlarmRingtoneChanged(lv_event_t* event);
    static void Tick(lv_timer_t* timer);
    static void TimerTick(lv_timer_t* timer);
    static void ClockTick(lv_timer_t* timer);
    static void HideKeyboardOverlay(lv_timer_t* timer);
    static void FlipTopExec(void* value, int32_t scale);
    static void FlipTopCompleted(lv_anim_t* animation);
    static void FlipBottomExec(void* value, int32_t scale);
    static void FlipBottomCompleted(lv_anim_t* animation);
    static void OnRefresh(lv_event_t* event);
    static void Worker(void* self);
    void Action(int action);
    void Render(Page page);
    void Home();
    void Dictionary();
    void KeyboardLookup();
    void Phonetics();
    void Timetable();
    void Timer();
    void Alarm();
    void Weather();
    void Network();
    void NetworkSettingsPage();
    void MqttMessageBoardPage();
    void ShowMqttSettingsPopup();
    void DrawSettingsPageNavigation(int selected_page);
    void FlipClock();
    void UpdateFlipClock(const struct tm& local, bool valid_time, bool animate);
    void AnimateFlipDigit(FlipDigit& digit, int value);
    void ResetFlipAnimation(FlipDigit& digit);
    void SetScreenOff(bool off);
    void SetScreenOffLocked();
    void ShowLockScreen();
    void ShowLockScreenLocked();
    void UnlockScreen();
    void UnlockScreenLocked();
    void SaveAutoLockSetting();
    void ShowAppearancePopup();
    void UpdateAppearancePopup();
    void CloseAppearancePopup();
    void SaveThemeSetting();
    bool ResolveDarkTheme(const struct tm* local = nullptr) const;
    void ApplyTheme(bool dark, bool rerender = true);
    void ShowBatteryPopup();
    void CloseBatteryPopup();
    void ShowWeatherIndexPopup();
    void CloseWeatherIndexPopup();
    void UpdateSettingLabels();
    void CacheUsbStorageArtwork();
    void UpdateTimer();
    void ShowTimerPlanPopup();
    void UpdateTimerPlanPopup();
    void CloseTimerPlanPopup();
    void SaveTimerPlan();
    void UpdateStroke();
    void RenderStroke();
    void HideStrokeArtwork();
    void OpenPinyinSearch();
    void OpenDefinitionDetails();
    void StartPinyinSearch();
    void UpdatePinyinToneButtons();
    void RenderPinyinResults(const char* status);
    void RenderKeyboardPinyinResults(const char* status);
    void ApplyPinyinResults(const std::string& query, std::vector<std::string> results);
    void SyncTimerWeek();
    std::array<int64_t, 3> TimerDaySeconds(int day, int64_t now_ms) const;
    void SaveTimer();
    void LoadPreferences();
    void ShowAssistantDialog();
    void HideAssistantDialog();
    void AppendAssistantHistory(const char* role, const char* text);
    void RebuildAssistantHistory();
    void ShowKeyboardConnectionOverlay();
    void StartPendingStrokePlayback();
    bool Queue(int type, const std::string& value);
    void Toast(const char* text);
    lv_obj_t* Box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color);
    lv_obj_t* Card(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color);
    lv_obj_t* Label(lv_obj_t* parent, const char* text, int x, int y, int w,
                    const lv_font_t* font = nullptr);
    const lv_font_t* DynamicTextFont() const;
    const lv_font_t* DictionaryTextFont() const;
    const lv_font_t* DictionaryLargeFont() const;
    const lv_font_t* DictionaryHeroFont() const;
    const lv_font_t* DictionaryCandidateFont() const;
    void ApplyDynamicTextFont(lv_obj_t* label);
    void ApplyDictionaryTextFont(lv_obj_t* label);
    void ApplyDictionaryLargeFont(lv_obj_t* label);
    void InstallDictionaryFont(std::string data);
    void InstallDictionaryCandidateFont(std::string data);
    void InstallScalableDictionaryFonts(const std::string& path);
    void ReleaseDictionaryFonts();
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
    lv_obj_t* battery_button_ = nullptr;
    lv_obj_t* battery_image_ = nullptr;
    lv_obj_t* battery_popup_ = nullptr;
    lv_obj_t* weather_index_popup_ = nullptr;
    lv_obj_t* page_icon_ = nullptr;
    lv_obj_t* assistant_card_ = nullptr;
    lv_obj_t* assistant_badge_ = nullptr;
    lv_obj_t* role_box_ = nullptr;
    lv_obj_t* role_label_ = nullptr;
    lv_obj_t* message_ = nullptr;
    lv_obj_t* status_box_ = nullptr;
    lv_obj_t* status_mic_badge_ = nullptr;
    lv_obj_t* assistant_dialog_scrim_ = nullptr;
    lv_obj_t* assistant_dialog_ = nullptr;
    lv_obj_t* assistant_dialog_status_box_ = nullptr;
    lv_obj_t* assistant_dialog_status_ = nullptr;
    lv_obj_t* assistant_dialog_mic_badge_ = nullptr;
    lv_obj_t* assistant_dialog_history_ = nullptr;
    lv_obj_t* assistant_dialog_hint_ = nullptr;
    lv_obj_t* assistant_dialog_navigation_ = nullptr;
    lv_obj_t* assistant_dialog_navigation_title_ = nullptr;
    lv_obj_t* assistant_dialog_navigation_detail_ = nullptr;
    lv_obj_t* network_info_ = nullptr;
    lv_obj_t* network_detail_ = nullptr;
    lv_obj_t* mqtt_settings_popup_ = nullptr;
    lv_obj_t* appearance_popup_ = nullptr;
    std::array<lv_obj_t*, 3> appearance_mode_buttons_{};
    lv_obj_t* appearance_start_value_ = nullptr;
    lv_obj_t* appearance_end_value_ = nullptr;
    lv_obj_t* brightness_value_ = nullptr;
    lv_obj_t* volume_value_ = nullptr;
    lv_obj_t* auto_lock_value_ = nullptr;
    lv_obj_t* brightness_slider_ = nullptr;
    lv_obj_t* volume_slider_ = nullptr;
    lv_obj_t* auto_lock_slider_ = nullptr;
    std::vector<uint8_t> usb_storage_art_data_;
    lv_image_dsc_t usb_storage_art_{};
    lv_obj_t* screen_wake_overlay_ = nullptr;
    lv_obj_t* keyboard_connection_overlay_ = nullptr;
    lv_timer_t* keyboard_connection_timer_ = nullptr;
    lv_obj_t* timer_value_ = nullptr;
    lv_obj_t* timer_progress_ = nullptr;
    lv_obj_t* timer_today_value_ = nullptr;
    lv_obj_t* timer_plan_popup_ = nullptr;
    std::array<lv_obj_t*, 3> timer_plan_arcs_{};
    std::array<lv_obj_t*, 3> timer_plan_values_{};
    lv_obj_t* totals_[3]{};
    std::array<lv_obj_t*, 5> timer_week_bars_{};
    lv_obj_t* stroke_value_ = nullptr;
    lv_obj_t* stroke_image_ = nullptr;
    lv_obj_t* stroke_placeholder_ = nullptr;
    lv_obj_t* glyph_title_image_ = nullptr;
    lv_obj_t* glyph_title_placeholder_ = nullptr;
    lv_draw_buf_t* stroke_draw_buf_ = nullptr;
    lv_draw_buf_t* glyph_title_draw_buf_ = nullptr;
    han::StrokeGlyph stroke_glyph_;
    std::string expected_stroke_character_;
    lv_obj_t* search_ = nullptr;
    lv_obj_t* search_overlay_ = nullptr;
    lv_obj_t* search_input_ = nullptr;
    lv_obj_t* search_results_ = nullptr;
    lv_obj_t* search_status_ = nullptr;
    lv_obj_t* pinyin_page_label_ = nullptr;
    lv_obj_t* definition_overlay_ = nullptr;
    std::array<lv_obj_t*, 6> pinyin_tone_buttons_{};
    std::string pinyin_query_;
    std::string pinyin_search_key_;
    std::string pinyin_status_text_;
    std::vector<std::string> pinyin_results_;
    int pinyin_page_ = 0;
    int pinyin_tone_ = -1;
    lv_obj_t* alarm_hour_ = nullptr;
    lv_obj_t* alarm_minute_ = nullptr;
    std::array<FlipDigit, 6> flip_digits_{};
    lv_obj_t* flip_date_ = nullptr;
    lv_obj_t* flip_lunar_ = nullptr;
    int64_t flip_date_key_ = -1;
    bool flip_clock_initialized_ = false;
    lv_timer_t* tick_ = nullptr;
    lv_timer_t* timer_tick_ = nullptr;
    lv_timer_t* clock_tick_ = nullptr;
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
    std::function<std::string()> usb_storage_restore_action_;
    std::atomic<bool> usb_storage_requested_{false};
    std::atomic<bool> usb_storage_restore_requested_{false};
    std::atomic<bool> usb_storage_active_{false};
    std::atomic<bool> mqtt_board_dirty_{false};
    std::atomic<bool> screen_off_{false};
    std::atomic<bool> lock_screen_visible_{false};
    std::atomic<bool> lock_screen_transition_pending_{false};
    std::atomic<bool> lock_screen_backlight_pending_{false};
    bool lock_unlock_gesture_ = false;
    int64_t lock_screen_shown_ms_ = 0;
    han::Entry entry_ = han::ContentStore::Demo();
    han::StudyTimer study_;
    std::array<std::array<int64_t, 3>, 5> timer_week_subject_seconds_{};
    std::array<int64_t, 3> timer_week_baseline_seconds_{};
    int timer_week_anchor_ = -1;
    int timer_today_index_ = -1;
    int timer_view_day_ = -1;
    int64_t timer_last_rendered_second_ = -1;
    std::array<int, 3> timer_plan_minutes_{{45, 60, 40}};
    std::array<int, 3> timer_plan_draft_{{45, 60, 40}};
    Page page_ = Page::Home;
    int settings_page_ = 0;
    int sound_ = 0;
    int category_ = 0;
    int sound_page_ = 0;
    // -1: untouched preview, [0, count): active stroke, count: playback complete.
    int stroke_ = -1;
    bool stroke_playing_ = false;
    bool auto_play_stroke_pending_ = false;
    bool assistant_dialog_active_ = false;
    bool assistant_dialog_dismissed_ = false;
    bool assistant_navigation_pending_ = false;
    int64_t assistant_dialog_hide_at_ms_ = 0;
    std::vector<ChatHistoryItem> assistant_history_;
    int alarm_minutes_ = 405;
    // Bit 0 is Monday and bit 6 is Sunday. A school-week alarm is the default.
    uint8_t alarm_days_ = 0x1f;
    bool alarm_enabled_ = false;
    int64_t alarm_last_day_ = -1;
    std::atomic<bool> alarm_ringing_{false};
    std::atomic<bool> alarm_previewing_{false};
    int alarm_ringtone_ = 0;
    std::atomic<bool> local_audio_{false};
    int64_t last_checkpoint_ms_ = 0;
    han::TimetableData timetable_;
    int timetable_today_ = -1;
    int64_t timetable_date_key_ = -1;
    std::string weather_text_;
    std::string weather_index_detail_;
#ifndef HAN_UI_HOST_SIM
    std::string dictionary_font_data_;
    std::string dictionary_candidate_font_data_;
    lv_font_t* dictionary_font_ = nullptr;
    lv_font_t* dictionary_candidate_font_ = nullptr;
    lv_font_t* dictionary_large_font_ = nullptr;
    lv_font_t* dictionary_hero_font_ = nullptr;
    bool dictionary_font_is_ttf_ = false;
#endif
    int brightness_setting_ = 75;
    int volume_setting_ = 70;
    int auto_lock_minutes_ = 10;
    ThemeMode theme_mode_ = ThemeMode::Light;
    ThemeMode appearance_draft_mode_ = ThemeMode::Light;
    bool dark_theme_ = false;
    bool theme_render_pending_ = false;
    bool keyboard_connection_pending_ = false;
    int dark_start_minutes_ = 19 * 60;
    int dark_end_minutes_ = 7 * 60;
    int appearance_draft_start_ = 19 * 60;
    int appearance_draft_end_ = 7 * 60;
    int64_t theme_minute_key_ = -1;
    int battery_level_ = -1;
    int battery_voltage_mv_ = -1;
    int battery_current_ma_ = 0;
    bool battery_charging_ = false;
    bool battery_discharging_ = false;
    bool initial_banner_pending_ = true;
};
