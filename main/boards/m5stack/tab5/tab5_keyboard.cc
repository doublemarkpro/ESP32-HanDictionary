#include "tab5_keyboard.h"

#include <esp_log.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace {
constexpr char kTag[] = "Tab5Keyboard";
constexpr uint8_t kAddress = 0x6d;
constexpr uint8_t kInterruptConfig = 0x00;
constexpr uint8_t kInterruptStatus = 0x01;
constexpr uint8_t kEventCount = 0x02;
constexpr uint8_t kBrightness = 0x03;
constexpr uint8_t kKeyboardMode = 0x10;
constexpr uint8_t kRgbMode = 0x11;
constexpr uint8_t kCharacterLength = 0x40;
constexpr uint8_t kCharacterData = 0x50;
constexpr uint8_t kRgbData = 0x60;
constexpr uint8_t kVersion = 0xfe;
constexpr uint8_t kCharacterMode = 2;
constexpr uint8_t kCharacterInterrupt = 0x04;
constexpr uint8_t kAllInterruptsEnabled = 0x07;
constexpr uint8_t kCustomRgbMode = 1;
constexpr TickType_t kProbePeriod = pdMS_TO_TICKS(500);
constexpr TickType_t kPollPeriod = pdMS_TO_TICKS(20);
constexpr int kDisconnectFailureLimit = 5;

std::string NormalizeCharacterEvent(const uint8_t* data, size_t length) {
    std::string input(reinterpret_cast<const char*>(data), length);
    while (!input.empty() && input.back() == '\0')
        input.pop_back();

    std::string name = input;
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (name == "tab")
        return "\t";
    if (name == "enter" || name == "return")
        return "\r";
    if (name == "esc" || name == "escape")
        return "\x1b";
    if (name == "backspace" || name == "bksp")
        return "\b";
    if (name == "delete" || name == "del")
        return "\x7f";
    return input;
}
}  // namespace

Tab5Keyboard::~Tab5Keyboard() { Shutdown(); }

bool Tab5Keyboard::Initialize(InputCallback callback, ConnectionCallback connection_callback) {
    callback_ = std::move(callback);
    connection_callback_ = std::move(connection_callback);
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_0,
        .scl_io_num = GPIO_NUM_1,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags =
            {
                .enable_internal_pullup = true,
                .allow_pd = false,
            },
    };
    if (i2c_new_master_bus(&bus_config, &bus_) != ESP_OK)
        return false;

    // ExtPort1 is hot-pluggable. Keep the bus alive even when the keyboard is absent so the poll
    // task can discover an insertion without rebooting the Tab5.
    if (xTaskCreate(PollTask, "tab5_keyboard", 4096, this, 4, &task_) != pdPASS) {
        Shutdown();
        return false;
    }
    ESP_LOGI(kTag, "Watching ExtPort1 for the Tab5 Keyboard");
    return true;
}

bool Tab5Keyboard::AttachDevice() {
    if (device_ || !bus_)
        return device_ != nullptr;
    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = kAddress,
        .scl_speed_hz = 100000,
        .scl_wait_us = 0,
        .flags = {},
    };
    if (i2c_master_bus_add_device(bus_, &device_config, &device_) != ESP_OK) {
        return false;
    }

    uint8_t version = 0;
    if (!Read(kVersion, &version, 1) || !Write(kInterruptConfig, &kAllInterruptsEnabled, 1) ||
        !Write(kKeyboardMode, &kCharacterMode, 1)) {
        DetachDevice();
        return false;
    }
    const uint8_t clear = 0;
    Write(kEventCount, &clear, 1);
    Write(kInterruptStatus, &clear, 1);

    // Character mode is purple in the keyboard's bound-RGB scheme. Use a dim dictionary-green
    // status color so the light communicates that the dock is ready without being distracting.
    constexpr uint8_t keyboard_brightness = 8;
    constexpr std::array<uint8_t, 7> ready_rgb = {0x75, 0x9b, 0x1d, 0x00, 0x75, 0x9b, 0x1d};
    Write(kRgbMode, &kCustomRgbMode, 1);
    Write(kBrightness, &keyboard_brightness, 1);
    Write(kRgbData, ready_rgb.data(), ready_rgb.size());
    ESP_LOGI(kTag, "Keyboard ready on ExtPort1 (firmware 0x%02x)", version);
    if (connection_callback_)
        connection_callback_(true);
    return true;
}

void Tab5Keyboard::DetachDevice() {
    if (!device_)
        return;
    i2c_master_bus_rm_device(device_);
    device_ = nullptr;
    ESP_LOGI(kTag, "Keyboard removed from ExtPort1");
    if (connection_callback_)
        connection_callback_(false);
}

bool Tab5Keyboard::Read(uint8_t reg, uint8_t* data, size_t size) {
    return device_ &&
           i2c_master_transmit_receive(device_, &reg, 1, data, size, pdMS_TO_TICKS(100)) == ESP_OK;
}

bool Tab5Keyboard::Write(uint8_t reg, const uint8_t* data, size_t size) {
    if (!device_ || size > 16)
        return false;
    std::array<uint8_t, 17> buffer{};
    buffer[0] = reg;
    memcpy(buffer.data() + 1, data, size);
    return i2c_master_transmit(device_, buffer.data(), size + 1, pdMS_TO_TICKS(100)) == ESP_OK;
}

bool Tab5Keyboard::DrainEvents() {
    uint8_t status = 0;
    uint8_t count = 0;
    if (!Read(kInterruptStatus, &status, 1) || !Read(kEventCount, &count, 1))
        return false;
    if (count == 0)
        return true;
    if ((status & kCharacterInterrupt) == 0) {
        ESP_LOGD(kTag, "Draining %u queued character event(s) with status 0x%02x", count, status);
    }
    count = std::min<uint8_t>(count, 32);
    while (count-- > 0) {
        uint8_t length = 0;
        if (!Read(kCharacterLength, &length, 1) || length == 0 || length > 15)
            return false;
        std::array<uint8_t, 16> event{};
        if (!Read(kCharacterData, event.data(), length + 1))
            return false;
        ESP_LOGI(kTag, "Key event: modifier=0x%02x length=%u first=0x%02x", event[0], length,
                 event[1]);
        ESP_LOG_BUFFER_HEX_LEVEL(kTag, event.data() + 1, length, ESP_LOG_INFO);
        if (callback_)
            callback_(NormalizeCharacterEvent(event.data() + 1, length));
    }
    const uint8_t clear = 0;
    return Write(kInterruptStatus, &clear, 1);
}

void Tab5Keyboard::PollTask(void* argument) {
    auto self = static_cast<Tab5Keyboard*>(argument);
    int consecutive_failures = 0;
    for (;;) {
        if (!self->device_) {
            if (i2c_master_probe(self->bus_, kAddress, pdMS_TO_TICKS(100)) == ESP_OK)
                self->AttachDevice();
            consecutive_failures = 0;
            vTaskDelay(kProbePeriod);
            continue;
        }

        if (self->DrainEvents()) {
            consecutive_failures = 0;
        } else if (++consecutive_failures >= kDisconnectFailureLimit) {
            self->DetachDevice();
            consecutive_failures = 0;
        }
        vTaskDelay(kPollPeriod);
    }
}

void Tab5Keyboard::Shutdown() {
    if (task_) {
        vTaskDelete(task_);
        task_ = nullptr;
    }
    if (device_)
        DetachDevice();
    if (bus_) {
        i2c_del_master_bus(bus_);
        bus_ = nullptr;
    }
}
