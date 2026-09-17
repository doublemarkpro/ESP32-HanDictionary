#pragma once

#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <functional>
#include <string>

// Minimal driver for the M5Stack Tab5 Keyboard (SKU A164). The keyboard owns a
// small STM32 and exposes character events over the Tab5 ExtPort1 I2C pins.
class Tab5Keyboard {
public:
    using InputCallback = std::function<void(const std::string&)>;
    using ConnectionCallback = std::function<void(bool)>;

    ~Tab5Keyboard();
    bool Initialize(InputCallback callback, ConnectionCallback connection_callback = {});

private:
    static void PollTask(void* argument);
    bool AttachDevice();
    void DetachDevice();
    bool Read(uint8_t reg, uint8_t* data, size_t size);
    bool Write(uint8_t reg, const uint8_t* data, size_t size);
    bool DrainEvents();
    void Shutdown();

    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t device_ = nullptr;
    TaskHandle_t task_ = nullptr;
    InputCallback callback_;
    ConnectionCallback connection_callback_;
};
