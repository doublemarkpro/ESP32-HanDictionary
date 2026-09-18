#pragma once
// Host-only hardware doubles. The LVGL page implementation/assets are compiled unchanged.
#include <lvgl.h>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
using esp_lcd_touch_handle_t = void*;
using QueueHandle_t = void*;
constexpr int pdTRUE = 1, pdPASS = 1, portMAX_DELAY = 0, ESP_OK = 0, ESP_FAIL = -1,
              ESP_ERR_NO_MEM = -2;
#define pdMS_TO_TICKS(x) (x)
#define ESP_ERROR_CHECK(x)                               \
    do {                                                 \
        if ((x) != 0)                                    \
            throw std::runtime_error("ESP stub failed"); \
    } while (0)
#define ESP_LOGI(tag, format, ...) ((void)0)
inline int64_t esp_timer_get_time() { return lv_tick_get() * 1000LL; }
inline void localtime_r(const time_t* t, tm* result) { localtime_s(result, t); }
inline void vTaskDelay(int) {}
inline QueueHandle_t xQueueCreate(int, int) { return reinterpret_cast<void*>(1); }
inline int xQueueSend(QueueHandle_t, const void*, int) { return 1; }
inline int xQueueReceive(QueueHandle_t, void*, int) { return 0; }
inline int xTaskCreate(void (*)(void*), const char*, int, void*, int, void*) { return 1; }
struct lvgl_port_touch_cfg_t {
    lv_display_t* disp;
    void* handle;
    struct {
        float x, y;
    } scale;
};
inline void* lvgl_port_add_touch(lvgl_port_touch_cfg_t*) { return reinterpret_cast<void*>(1); }
struct Theme {};
struct Display {
    bool setup_ui_called_ = false;
    Theme* current_theme_ = nullptr;
    int width_ = 1280, height_ = 720;
    virtual ~Display() = default;
    virtual void SetupUI() { setup_ui_called_ = true; }
    virtual void SetTheme(Theme*) {}
    virtual void SetStatus(const char*) {}
    virtual void SetEmotion(const char*) {}
    virtual void SetChatMessage(const char*, const char*) {}
    virtual void ClearChatMessages() {}
    virtual void UpdateStatusBar(bool = false) {}
};
struct MipiLcdDisplay : Display {
    lv_display_t* display_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* notification_label_ = nullptr;
    lv_obj_t* network_label_ = nullptr;
    lv_obj_t* battery_label_ = nullptr;
    MipiLcdDisplay(void*, void*, int, int, int, int, bool, bool, bool) {
        display_ = lv_display_get_default();
    }
    void SetStatus(const char* text) override {
        if (status_label_)
            lv_label_set_text(status_label_, text ? text : "");
    }
    void ShowNotification(const char* text, int) {
        if (notification_label_) {
            lv_label_set_text(notification_label_, text);
            lv_obj_remove_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
};
struct DisplayLockGuard {
    explicit DisplayLockGuard(Display*) {}
};
enum DeviceState {
    kDeviceStateIdle,
    kDeviceStateWifiConfiguring,
    kDeviceStateSpeaking,
    kDeviceStateListening,
    kDeviceStateConnecting
};
struct AudioService {
    void PlaySound(std::string_view) {}
    void PlaySound(std::string_view, const std::function<bool()>&) {}
    bool IsPlaybackIdle() { return true; }
    bool IsWakeWordRunning() { return false; }
    void EnableWakeWordDetection(bool) {}
    void ResetDecoder() {}
};
struct Application {
    bool defer = false;
    std::vector<std::function<void()>> pending;
    int starts = 0, stops = 0;
    bool conversation_active = true;
    DeviceState state = kDeviceStateIdle;
    static Application& GetInstance() {
        static Application a;
        return a;
    }
    void Schedule(std::function<void()> f) {
        if (defer)
            pending.push_back(std::move(f));
        else
            f();
    }
    void Drain() {
        auto tasks = std::move(pending);
        pending.clear();
        for (auto& f : tasks)
            f();
    }
    void ToggleChatState() {}
    void StartListening() {
        ++starts;
        state = kDeviceStateListening;
    }
    void StopListening() {
        ++stops;
        state = kDeviceStateIdle;
    }
    void StopConversation() {
        conversation_active = false;
        ++stops;
        state = kDeviceStateIdle;
    }
    bool IsConversationActive() const { return conversation_active; }
    void SetDeviceState(DeviceState value) { state = value; }
    void PlaySound(std::string_view) {}
    AudioService& GetAudioService() {
        static AudioService a;
        return a;
    }
    DeviceState GetDeviceState() { return state; }
};
struct Board {
    struct BatteryInfo {
        int level = -1;
        int voltage_mv = -1;
        int current_ma = 0;
        bool charging = false;
        bool discharging = false;
    };
    bool battery_known = false, charging = false;
    int battery = 0;
    static Board& GetInstance() {
        static Board b;
        return b;
    }
    bool GetBatteryLevel(int& level, bool& charge, bool& discharge) {
        level = battery;
        charge = charging;
        discharge = !charging;
        return battery_known;
    }
    bool GetBatteryInfo(BatteryInfo& info) {
        info.level = battery;
        info.voltage_mv = 7600;
        info.current_ma = charging ? 420 : -180;
        info.charging = charging;
        info.discharging = !charging;
        return battery_known;
    }
};
struct WifiManager {
    bool connected = false;
    int rssi = -55;
    std::string ssid = "家庭WiFi";
    std::string ip_address = "192.168.1.88";
    static WifiManager& GetInstance() {
        static WifiManager w;
        return w;
    }
    bool IsConfigMode() { return false; }
    bool IsConnected() { return connected; }
    int GetRssi() { return rssi; }
    std::string GetApSsid() { return ""; }
    std::string GetApWebUrl() { return ""; }
    std::string GetSsid() { return ssid; }
    std::string GetIpAddress() { return ip_address; }
};
struct Settings {
    inline static std::map<std::string, int> values;
    std::string ns;
    explicit Settings(std::string name, bool = false) : ns(name) {}
    int GetInt(std::string key, int fallback) {
        auto p = values.find(ns + key);
        return p == values.end() ? fallback : p->second;
    }
    bool GetBool(std::string key, bool fallback) { return GetInt(key, fallback) != 0; }
    void SetInt(std::string key, int v) { values[ns + key] = v; }
    void SetBool(std::string key, bool v) { SetInt(key, v); }
};
namespace Lang::Sounds {
inline constexpr const char* OGG_SUCCESS = "";
}
