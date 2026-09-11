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

// Opt-in product display. The generic LCD and all other boards remain independent.
class HanDisplay : public MipiLcdDisplay {
public:
    using MipiLcdDisplay::MipiLcdDisplay;
    void AttachTouch(esp_lcd_touch_handle_t touch);
    void SetNetworkAction(std::function<void()> action) { network_action_ = std::move(action); }
    void SetupUI() override;
    void SetTheme(Theme* theme) override;
    void SetEmotion(const char*) override {}
    void SetChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void UpdateStatusBar(bool update_all = false) override;
    void ShowEntry(const han::Entry& entry);
    bool OpenPage(const std::string& page);
    bool ApplyStrokeFrame(const std::string& path, std::string data);

private:
    enum class Page { Home, Dictionary, Phonetics, Timetable, Timer, Alarm, Weather, Network };
    struct Job {
        int type;
        char value[128];
    };
    static void OnClick(lv_event_t* event);
    static void OnTalk(lv_event_t* event);
    void ReleaseTalk();
    static void Tick(lv_timer_t* timer);
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
    void UpdateTimer();
    void UpdateStroke();
    void SaveTimer();
    void LoadPreferences();
    void Queue(int type, const std::string& value);
    void Toast(const char* text);
    lv_obj_t* Box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color);
    lv_obj_t* Label(lv_obj_t* parent, const char* text, int x, int y, int w,
                    const lv_font_t* font = nullptr);
    lv_obj_t* Button(lv_obj_t* parent, const char* text, int x, int y, int w, int h, uint32_t color,
                     int action);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* body_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* back_ = nullptr;
    lv_obj_t* clock_ = nullptr;
    lv_obj_t* date_ = nullptr;
    lv_obj_t* mascot_ = nullptr;
    lv_obj_t* wifi_image_ = nullptr;
    lv_obj_t* battery_image_ = nullptr;
    lv_obj_t* talk_button_ = nullptr;
    lv_obj_t* talk_label_ = nullptr;
    std::atomic<bool> talk_held_{false};
    std::atomic<bool> talk_started_{false};
    std::atomic<bool> talk_release_pending_{false};
    int64_t talk_pressed_ms_ = 0;
    lv_obj_t* message_ = nullptr;
    lv_obj_t* network_info_ = nullptr;
    lv_obj_t* timer_value_ = nullptr;
    lv_obj_t* totals_[3]{};
    lv_obj_t* stroke_value_ = nullptr;
    lv_obj_t* stroke_image_ = nullptr;
    lv_obj_t* stroke_placeholder_ = nullptr;
    lv_image_dsc_t sd_stroke_{};
    std::string stroke_png_;
    std::string expected_stroke_path_;
    lv_obj_t* search_ = nullptr;
    lv_obj_t* alarm_hour_ = nullptr;
    lv_obj_t* alarm_minute_ = nullptr;
    lv_timer_t* tick_ = nullptr;
    QueueHandle_t jobs_ = nullptr;
    std::function<void()> network_action_;
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
    std::string timetable_text_;
    std::string weather_text_;
};
