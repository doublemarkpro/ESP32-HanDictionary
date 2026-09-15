#include "application.h"
#include "button.h"
#include "display/lcd_display.h"
#include "esp_cam_sensor_xclk.h"
#include "esp_lcd_ili9881c.h"
#include "esp_lcd_st7121.h"
#include "esp_lcd_st7123.h"
#include "esp_video.h"
#include "esp_video_init.h"
#include "tab5_audio_codec.h"
#include "wifi_board.h"

// config.h declares panel initialization tables using the driver types above.
#include "config.h"

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_idf_version.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include "esp_check.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_touch_st7123.h"
#include "esp_ldo_regulator.h"
#include "i2c_device.h"

#if CONFIG_HAN_DICTIONARY
#include "dictionary_service.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "hal/usb_wrap_ll.h"
#include "han_display.h"
#include "mcp_server.h"
#include "sdmmc_cmd.h"
#include "soc/usb_wrap_struct.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "tusb.h"
using Tab5ProductDisplay = HanDisplay;
// Render a page before copying it to the panel. A 50-line buffer repeats the
// landscape scene traversal about 26 times and visibly paints it in strips.
static constexpr MipiLcdDisplayConfig kTab5DisplayConfig = {
    .buffer_pixels = DISPLAY_WIDTH * DISPLAY_HEIGHT,
    .buffer_in_psram = true,
};
#else
using Tab5ProductDisplay = MipiLcdDisplay;
static constexpr MipiLcdDisplayConfig kTab5DisplayConfig = {};
#endif

#define TAG "M5StackTab5Board"

#define AUDIO_CODEC_ES8388_ADDR ES8388_CODEC_DEFAULT_ADDR
#define LCD_MIPI_DSI_PHY_PWR_LDO_CHAN 3  // LDO_VO3 is connected to VDD_MIPI_DPHY
#define LCD_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500
#define ST712X_TOUCH_I2C_ADDRESS 0x55

enum class St712xPanel {
    kSt7121,
    kSt7123,
};

// PI4IO registers
#define PI4IO_REG_CHIP_RESET 0x01
#define PI4IO_REG_IO_DIR 0x03
#define PI4IO_REG_OUT_SET 0x05
#define PI4IO_REG_OUT_H_IM 0x07
#define PI4IO_REG_IN_DEF_STA 0x09
#define PI4IO_REG_PULL_EN 0x0B
#define PI4IO_REG_PULL_SEL 0x0D
#define PI4IO_REG_IN_STA 0x0F
#define PI4IO_REG_INT_MASK 0x11
#define PI4IO_REG_IRQ_STA 0x13

// Bit manipulation macros
#define setbit(x, bit) ((x) |= (1U << (bit)))
#define clrbit(x, bit) ((x) &= ~(1U << (bit)))

class Pi4ioe1 : public I2cDevice {
public:
    Pi4ioe1(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(PI4IO_REG_CHIP_RESET, 0xFF);
        ReadReg(PI4IO_REG_CHIP_RESET);
        WriteReg(PI4IO_REG_IO_DIR, 0b01111111);      // 0: input 1: output
        WriteReg(PI4IO_REG_OUT_H_IM, 0b00000000);    // 使用到的引脚关闭 High-Impedance
        WriteReg(PI4IO_REG_PULL_SEL, 0b01111111);    // pull up/down select, 0 down, 1 up
        WriteReg(PI4IO_REG_PULL_EN, 0b01111111);     // pull up/down enable, 0 disable, 1 enable
        WriteReg(PI4IO_REG_IN_DEF_STA, 0b10000000);  // P1, P7 默认高电平
        WriteReg(PI4IO_REG_INT_MASK, 0b01111111);    // P7 中断使能 0 enable, 1 disable
        WriteReg(PI4IO_REG_OUT_SET, 0b01110110);  // Output Port Register P1(SPK_EN), P2(EXT5V_EN),
                                                  // P4(LCD_RST), P5(TP_RST), P6(CAM)RST 输出高电平
    }

    uint8_t ReadOutSet() { return ReadReg(PI4IO_REG_OUT_SET); }
    void WriteOutSet(uint8_t value) { WriteReg(PI4IO_REG_OUT_SET, value); }
};

class Pi4ioe2 : public I2cDevice {
public:
    Pi4ioe2(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(PI4IO_REG_CHIP_RESET, 0xFF);
        ReadReg(PI4IO_REG_CHIP_RESET);
        WriteReg(PI4IO_REG_IO_DIR, 0b10111001);      // 0: input 1: output
        WriteReg(PI4IO_REG_OUT_H_IM, 0b00000110);    // 使用到的引脚关闭 High-Impedance
        WriteReg(PI4IO_REG_PULL_SEL, 0b10111001);    // pull up/down select, 0 down, 1 up
        WriteReg(PI4IO_REG_PULL_EN, 0b11111001);     // pull up/down enable, 0 disable, 1 enable
        WriteReg(PI4IO_REG_IN_DEF_STA, 0b01000000);  // P6 默认高电平
        WriteReg(PI4IO_REG_INT_MASK, 0b10111111);    // P6 中断使能 0 enable, 1 disable
        WriteReg(PI4IO_REG_OUT_SET, 0b10001001);     // Output Port Register P0(WLAN_PWR_EN),
                                                     // P3(USB5V_EN), P7(CHG_EN) 输出高电平
    }

    uint8_t ReadOutSet() { return ReadReg(PI4IO_REG_OUT_SET); }
    void WriteOutSet(uint8_t value) { WriteReg(PI4IO_REG_OUT_SET, value); }
    bool ReadInput(uint8_t& value) {
        uint8_t reg = PI4IO_REG_IN_STA;
        return i2c_master_transmit_receive(i2c_device_, &reg, 1, &value, 1, 100) == ESP_OK;
    }
};

class Tab5PowerMonitor {
public:
    bool Initialize(i2c_master_bus_handle_t bus) {
        i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = 0x41,
            .scl_speed_hz = 400 * 1000,
            .scl_wait_us = 0,
            .flags = {},
        };
        auto err = i2c_master_bus_add_device(bus, &config, &device_);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Could not attach INA226: %s", esp_err_to_name(err));
            return false;
        }
        uint16_t die_id = 0;
        if (!ReadRegister(0xFF, die_id) || die_id != 0x2260) {
            ESP_LOGW(TAG, "INA226 not detected (die id 0x%04x)", die_id);
            i2c_master_bus_rm_device(device_);
            device_ = nullptr;
            return false;
        }
        // Match the official M5Unified Tab5 setup: 16 samples, 1.1 ms shunt/bus
        // conversion, continuous shunt+bus mode, 5 mOhm shunt, 2 A expected maximum.
        if (!WriteRegister(0x00, 0x0527) || !WriteRegister(0x05, 0x4189)) {
            ESP_LOGW(TAG, "Could not configure INA226");
            i2c_master_bus_rm_device(device_);
            device_ = nullptr;
            return false;
        }
        uint16_t millivolts = 0;
        int current_ma = 0;
        if (Read(millivolts, current_ma))
            ESP_LOGI(TAG, "INA226 ready: battery=%u mV current=%d mA", millivolts, current_ma);
        return true;
    }

    bool Read(uint16_t& millivolts, int& current_ma) {
        uint16_t bus_raw = 0, shunt_raw = 0;
        if (device_ == nullptr || !ReadRegister(0x02, bus_raw) || !ReadRegister(0x01, shunt_raw))
            return false;
        millivolts = static_cast<uint16_t>((static_cast<uint32_t>(bus_raw) * 5) / 4);
        // 2.5 uV per bit / 5 mOhm = 0.5 mA per bit. On Tab5, charge current is
        // negative at the shunt, so expose positive current for charging.
        current_ma = -static_cast<int16_t>(shunt_raw) / 2;
        return millivolts >= 5000 && millivolts <= 9000;
    }

private:
    bool ReadRegister(uint8_t reg, uint16_t& value) {
        uint8_t bytes[2]{};
        auto err = i2c_master_transmit_receive(device_, &reg, 1, bytes, sizeof(bytes), 100);
        if (err != ESP_OK)
            return false;
        value = static_cast<uint16_t>(bytes[0] << 8 | bytes[1]);
        return true;
    }

    bool WriteRegister(uint8_t reg, uint16_t value) {
        uint8_t bytes[] = {reg, static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
        return i2c_master_transmit(device_, bytes, sizeof(bytes), 100) == ESP_OK;
    }

    i2c_master_dev_handle_t device_ = nullptr;
};

int Tab5BatteryLevelFromVoltage(uint16_t pack_mv) {
    // The Tab5 battery is a 2-cell Li-ion pack. Voltage is not a coulomb counter, so use a
    // conservative discharge curve instead of presenting the pack voltage as a linear gauge.
    struct Point {
        int cell_mv;
        int level;
    };
    static constexpr Point curve[] = {
        {3000, 0}, {3400, 5},  {3600, 15}, {3700, 30}, {3800, 50},
        {3900, 70}, {4000, 85}, {4100, 97}, {4150, 100},
    };
    const int cell_mv = pack_mv / 2;
    if (cell_mv <= curve[0].cell_mv)
        return curve[0].level;
    for (size_t index = 1; index < std::size(curve); ++index) {
        if (cell_mv <= curve[index].cell_mv) {
            const auto& low = curve[index - 1];
            const auto& high = curve[index];
            return low.level + (cell_mv - low.cell_mv) * (high.level - low.level) /
                                   (high.cell_mv - low.cell_mv);
        }
    }
    return 100;
}

class M5StackTab5Board : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    LcdDisplay* display_;
    EspVideo* camera_ = nullptr;
    Pi4ioe1* pi4ioe1_;
    Pi4ioe2* pi4ioe2_;
    Tab5PowerMonitor power_monitor_;
    bool power_monitor_ready_ = false;
    esp_lcd_touch_handle_t touch_ = nullptr;
#if CONFIG_HAN_DICTIONARY
    sdmmc_card_t* sd_card_ = nullptr;
    bool sd_bus_initialized_ = false;
    bool sd_card_mounted_ = false;
    tinyusb_msc_storage_handle_t usb_storage_ = nullptr;
#endif

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags =
                {
                    .enable_internal_pullup = 1,
                },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    static esp_err_t bsp_enable_dsi_phy_power() {
        esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
        esp_ldo_channel_config_t ldo_mipi_phy_config = {
            .chan_id = LCD_MIPI_DSI_PHY_PWR_LDO_CHAN,
            .voltage_mv = LCD_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
        };
        return esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy);
    }

    void I2cDetect() {
        uint8_t address;
        printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
        for (int i = 0; i < 128; i += 16) {
            printf("%02x: ", i);
            for (int j = 0; j < 16; j++) {
                fflush(stdout);
                address = i + j;
                esp_err_t ret = i2c_master_probe(i2c_bus_, address, pdMS_TO_TICKS(200));
                if (ret == ESP_OK) {
                    printf("%02x ", address);
                } else if (ret == ESP_ERR_TIMEOUT) {
                    printf("UU ");
                } else {
                    printf("-- ");
                }
            }
            printf("\r\n");
        }
    }

    void InitializePi4ioe() {
        ESP_LOGI(TAG, "Init I/O Exapander PI4IOE");
        pi4ioe1_ = new Pi4ioe1(i2c_bus_, 0x43);
        pi4ioe2_ = new Pi4ioe2(i2c_bus_, 0x44);
    }

    void ResetLcdAndTouch() {
        ESP_LOGI(TAG, "Reset LCD and touch via PI4IOE");
        gpio_reset_pin(TOUCH_INT_GPIO);

        uint8_t value = pi4ioe1_->ReadOutSet();
        clrbit(value, 4);  // P4 = LCD_RST
        clrbit(value, 5);  // P5 = TP_RST
        pi4ioe1_->WriteOutSet(value);
        vTaskDelay(pdMS_TO_TICKS(100));

        setbit(value, 4);
        setbit(value, 5);
        pi4ioe1_->WriteOutSet(value);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    static esp_lcd_panel_io_i2c_config_t St712xTouchIoConfig() {
        esp_lcd_panel_io_i2c_config_t config = {};
        config.scl_speed_hz = 100000;
        config.dev_addr = ST712X_TOUCH_I2C_ADDRESS;
        config.control_phase_bytes = 1;
        config.lcd_cmd_bits = 16;
        config.flags.disable_control_phase = 1;
        return config;
    }

    St712xPanel DetectSt712xPanel() {
        esp_lcd_panel_io_handle_t touch_io = nullptr;
        auto touch_io_config = St712xTouchIoConfig();
        esp_err_t ret = esp_lcd_new_panel_io_i2c(i2c_bus_, &touch_io_config, &touch_io);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to create ST712x detection IO (%s); falling back to ST7123",
                     esp_err_to_name(ret));
            return St712xPanel::kSt7123;
        }

        uint8_t firmware_version = 0;
        ret = esp_lcd_panel_io_rx_param(touch_io, 0x0000, &firmware_version,
                                        sizeof(firmware_version));
        esp_lcd_panel_io_del(touch_io);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read ST712x firmware version (%s); falling back to ST7123",
                     esp_err_to_name(ret));
            return St712xPanel::kSt7123;
        }

        if (firmware_version == 1) {
            ESP_LOGI(TAG, "Detected ST7121 panel (touch firmware version %u)", firmware_version);
            return St712xPanel::kSt7121;
        }
        if (firmware_version != 3) {
            ESP_LOGW(TAG, "Unknown ST712x touch firmware version %u; falling back to ST7123",
                     firmware_version);
        } else {
            ESP_LOGI(TAG, "Detected ST7123 panel (touch firmware version %u)", firmware_version);
        }
        return St712xPanel::kSt7123;
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    void InitializeGt911TouchPad() {
        ESP_LOGI(TAG, "Init GT911");

        /* Initialize Touch Panel */
        ESP_LOGI(TAG, "Initialize touch IO (I2C)");
        const esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_WIDTH,
            .y_max = DISPLAY_HEIGHT,
            .rst_gpio_num = GPIO_NUM_NC,
            .int_gpio_num = TOUCH_INT_GPIO,
            .levels =
                {
                    .reset = 0,
                    .interrupt = 0,
                },
            .flags =
                {
                    .swap_xy = 0,
                    .mirror_x = 0,
                    .mirror_y = 0,
                },
        };
        esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        esp_lcd_panel_io_i2c_config_t tp_io_config = {
            .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
            .control_phase_bytes = 1,
            .dc_bit_offset = 0,
            .lcd_cmd_bits = 16,
            .flags = {
                .disable_control_phase = 1,
            }};
        tp_io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;  // 更改 GT911 地址
        tp_io_config.scl_speed_hz = 100000;
        esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle);
        esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &touch_);
    }

    void InitializeIli9881cDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGI(TAG, "Turn on the power for MIPI DSI PHY");
        esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
        esp_ldo_channel_config_t ldo_mipi_phy_config = {
            .chan_id = LCD_MIPI_DSI_PHY_PWR_LDO_CHAN,
            .voltage_mv = LCD_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
        };
        ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy));

        ESP_LOGI(TAG, "Install MIPI DSI LCD control panel");
        esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
        esp_lcd_dsi_bus_config_t bus_config = {
            .bus_id = 0,
            .num_data_lanes = 2,
            .lane_bit_rate_mbps = 900,  // 900MHz
        };
        ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

        ESP_LOGI(TAG, "Install panel IO");
        esp_lcd_dbi_io_config_t dbi_config = {
            .virtual_channel = 0,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &panel_io));

        ESP_LOGI(TAG, "Install LCD driver of ili9881c");
        esp_lcd_dpi_panel_config_t dpi_config = {
            .virtual_channel = 0,
            .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
            .dpi_clock_freq_mhz = 60,
            .in_color_format = LCD_COLOR_FMT_RGB565,
            .out_color_format = LCD_COLOR_FMT_RGB565,
            .num_fbs = 2,
            .video_timing =
                {
                    .h_size = DISPLAY_WIDTH,
                    .v_size = DISPLAY_HEIGHT,
                    .hsync_pulse_width = 40,
                    .hsync_back_porch = 140,
                    .hsync_front_porch = 40,
                    .vsync_pulse_width = 4,
                    .vsync_back_porch = 20,
                    .vsync_front_porch = 20,
                },
        };
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
        dpi_config.flags.use_dma2d = true;
#endif

        ili9881c_vendor_config_t vendor_config = {
            .init_cmds = tab5_lcd_ili9881c_specific_init_code_default,
            .init_cmds_size = sizeof(tab5_lcd_ili9881c_specific_init_code_default) /
                              sizeof(tab5_lcd_ili9881c_specific_init_code_default[0]),
            .mipi_config =
                {
                    .dsi_bus = mipi_dsi_bus,
                    .dpi_config = &dpi_config,
                    .lane_num = 2,
                },
        };

        esp_lcd_panel_dev_config_t lcd_dev_config = {};
        lcd_dev_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        lcd_dev_config.reset_gpio_num = GPIO_NUM_NC;
        lcd_dev_config.bits_per_pixel = 16;
        lcd_dev_config.vendor_config = &vendor_config;

        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9881c(panel_io, &lcd_dev_config, &panel));
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        ESP_ERROR_CHECK(esp_lcd_dpi_panel_enable_dma2d(panel));
#endif
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

        display_ = new Tab5ProductDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                          DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                          DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY, kTab5DisplayConfig);
    }

    void InitializeSt712xDisplay(St712xPanel panel_type) {
        const bool is_st7121 = panel_type == St712xPanel::kSt7121;
        const char* panel_name = is_st7121 ? "ST7121" : "ST7123";
        esp_err_t ret = ESP_OK;
        esp_lcd_panel_io_handle_t io = NULL;
        esp_lcd_panel_handle_t disp_panel = NULL;
        esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;

        // Declare all config structures at the top to avoid goto issues
        // Initialize with memset to avoid any initialization syntax that might confuse the compiler
        esp_lcd_dsi_bus_config_t bus_config;
        esp_lcd_dbi_io_config_t dbi_config;
        esp_lcd_dpi_panel_config_t dpi_config;
        st7121_vendor_config_t st7121_vendor_config;
        st7123_vendor_config_t st7123_vendor_config;
        esp_lcd_panel_dev_config_t lcd_dev_config;

        memset(&bus_config, 0, sizeof(bus_config));
        memset(&dbi_config, 0, sizeof(dbi_config));
        memset(&dpi_config, 0, sizeof(dpi_config));
        memset(&st7121_vendor_config, 0, sizeof(st7121_vendor_config));
        memset(&st7123_vendor_config, 0, sizeof(st7123_vendor_config));
        memset(&lcd_dev_config, 0, sizeof(lcd_dev_config));

        ESP_ERROR_CHECK(bsp_enable_dsi_phy_power());

        /* create MIPI DSI bus first, it will initialize the DSI PHY as well */
        bus_config.bus_id = 0;
        bus_config.num_data_lanes = 2;
        bus_config.lane_bit_rate_mbps = 965;
        ret = esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "New DSI bus init failed");
            goto err;
        }

        ESP_LOGI(TAG, "Install MIPI DSI LCD control panel for %s", panel_name);
        // we use DBI interface to send LCD commands and parameters
        dbi_config.virtual_channel = 0;
        dbi_config.lcd_cmd_bits = 8;    // according to the LCD spec
        dbi_config.lcd_param_bits = 8;  // according to the LCD spec
        ret = esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "New panel IO failed");
            goto err;
        }

        ESP_LOGI(TAG, "Install LCD driver of %s", panel_name);
        dpi_config.virtual_channel = 0;
        dpi_config.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
        dpi_config.dpi_clock_freq_mhz = 70;
        dpi_config.in_color_format = LCD_COLOR_FMT_RGB565;
        dpi_config.out_color_format = LCD_COLOR_FMT_RGB565;
        dpi_config.num_fbs = 1;
        dpi_config.video_timing.h_size = 720;
        dpi_config.video_timing.v_size = 1280;
        dpi_config.video_timing.hsync_pulse_width = 2;
        dpi_config.video_timing.hsync_back_porch = 40;
        dpi_config.video_timing.hsync_front_porch = 40;
        dpi_config.video_timing.vsync_pulse_width = is_st7121 ? 20 : 2;
        dpi_config.video_timing.vsync_back_porch = is_st7121 ? 24 : 8;
        dpi_config.video_timing.vsync_front_porch = is_st7121 ? 200 : 220;
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
        dpi_config.flags.use_dma2d = true;
#endif

        lcd_dev_config.reset_gpio_num = GPIO_NUM_NC;
        lcd_dev_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        lcd_dev_config.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE;
        lcd_dev_config.bits_per_pixel = 24;
        if (is_st7121) {
            st7121_vendor_config.mipi_config.dsi_bus = mipi_dsi_bus;
            st7121_vendor_config.mipi_config.dpi_config = &dpi_config;
            lcd_dev_config.vendor_config = &st7121_vendor_config;
            ret = esp_lcd_new_panel_st7121(io, &lcd_dev_config, &disp_panel);
        } else {
            st7123_vendor_config.init_cmds = st7123_vendor_specific_init_default;
            st7123_vendor_config.init_cmds_size = sizeof(st7123_vendor_specific_init_default) /
                                                  sizeof(st7123_vendor_specific_init_default[0]);
            st7123_vendor_config.mipi_config.dsi_bus = mipi_dsi_bus;
            st7123_vendor_config.mipi_config.dpi_config = &dpi_config;
            st7123_vendor_config.mipi_config.lane_num = 2;
            lcd_dev_config.vendor_config = &st7123_vendor_config;
            ret = esp_lcd_new_panel_st7123(io, &lcd_dev_config, &disp_panel);
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "New LCD panel %s failed", panel_name);
            goto err;
        }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
        ret = esp_lcd_dpi_panel_enable_dma2d(disp_panel);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Enable DPI DMA2D failed");
            goto err;
        }
#endif

        ret = esp_lcd_panel_reset(disp_panel);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "LCD panel reset failed");
            goto err;
        }

        ret = esp_lcd_panel_init(disp_panel);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "LCD panel init failed");
            goto err;
        }

        ret = esp_lcd_panel_disp_on_off(disp_panel, true);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "LCD panel display on failed");
            goto err;
        }

        display_ = new Tab5ProductDisplay(io, disp_panel, 720, 1280, DISPLAY_OFFSET_X,
                                          DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
                                          DISPLAY_SWAP_XY, kTab5DisplayConfig);

        ESP_LOGI(TAG, "%s display initialized with resolution %dx%d", panel_name, 720, 1280);

        return;

    err:
        if (disp_panel) {
            esp_lcd_panel_del(disp_panel);
        }
        if (io) {
            esp_lcd_panel_io_del(io);
        }
        if (mipi_dsi_bus) {
            esp_lcd_del_dsi_bus(mipi_dsi_bus);
        }
        ESP_ERROR_CHECK(ret);
    }

    void InitializeSt712xTouchPad() {
        ESP_LOGI(TAG, "Init ST712x touch");

        /* Initialize Touch Panel */
        ESP_LOGI(TAG, "Initialize touch IO (I2C)");
        const esp_lcd_touch_config_t tp_cfg = {
            .x_max = 720,
            .y_max = 1280,
            .rst_gpio_num = GPIO_NUM_NC,
            .int_gpio_num = TOUCH_INT_GPIO,
            .levels =
                {
                    .reset = 0,
                    .interrupt = 0,
                },
            .flags =
                {
                    .swap_xy = 0,
                    .mirror_x = 0,
                    .mirror_y = 0,
                },
        };
        esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        auto tp_io_config = St712xTouchIoConfig();
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle));
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_st7123(tp_io_handle, &tp_cfg, &touch_));
    }

    void InitializeDisplay() {
        ResetLcdAndTouch();

        esp_err_t ret = i2c_master_probe(i2c_bus_, ST712X_TOUCH_I2C_ADDRESS, 200);
        if (ret == ESP_OK) {
            St712xPanel panel_type = DetectSt712xPanel();
            InitializeSt712xDisplay(panel_type);
            InitializeSt712xTouchPad();
        } else {
            ESP_LOGI(TAG, "ST712x not found at 0x%02X (ret=0x%x), using default ILI9881C+GT911",
                     ST712X_TOUCH_I2C_ADDRESS, ret);
            InitializeIli9881cDisplay();
            InitializeGt911TouchPad();
        }
    }

    void InitializeCamera() {
        esp_cam_sensor_xclk_handle_t xclk_handle = NULL;
        esp_cam_sensor_xclk_config_t cam_xclk_config = {};

#if CONFIG_CAMERA_XCLK_USE_ESP_CLOCK_ROUTER
        if (esp_cam_sensor_xclk_allocate(ESP_CAM_SENSOR_XCLK_ESP_CLOCK_ROUTER, &xclk_handle) ==
            ESP_OK) {
            cam_xclk_config.esp_clock_router_cfg.xclk_pin = CAMERA_MCLK;
            cam_xclk_config.esp_clock_router_cfg.xclk_freq_hz = 12000000;  // 12MHz
            (void)esp_cam_sensor_xclk_start(xclk_handle, &cam_xclk_config);
        }
#elif CONFIG_CAMERA_XCLK_USE_LEDC
        if (esp_cam_sensor_xclk_allocate(ESP_CAM_SENSOR_XCLK_LEDC, &xclk_handle) == ESP_OK) {
            cam_xclk_config.ledc_cfg.timer = LEDC_TIMER_0;
            cam_xclk_config.ledc_cfg.clk_cfg = LEDC_AUTO_CLK;
            cam_xclk_config.ledc_cfg.channel = LEDC_CHANNEL_0;
            cam_xclk_config.ledc_cfg.xclk_freq_hz = 12000000;  // 12MHz
            cam_xclk_config.ledc_cfg.xclk_pin = CAMERA_MCLK;
            (void)esp_cam_sensor_xclk_start(xclk_handle, &cam_xclk_config);
        }
#endif

        esp_video_init_sccb_config_t sccb_config = {
            .init_sccb = false,
            .i2c_handle = i2c_bus_,
            .freq = 400000,
        };

        esp_video_init_csi_config_t csi_config = {
            .sccb_config = sccb_config,
            .reset_pin = GPIO_NUM_NC,
            .pwdn_pin = GPIO_NUM_NC,
        };

        esp_video_init_config_t video_config = {
            .csi = &csi_config,
        };

        camera_ = new EspVideo(video_config);
    }

public:
    M5StackTab5Board() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        I2cDetect();
        InitializePi4ioe();
        power_monitor_ready_ = power_monitor_.Initialize(i2c_bus_);
        InitializeDisplay();  // Auto-detect and initialize display + touch
#if CONFIG_HAN_DICTIONARY
        auto product = static_cast<HanDisplay*>(display_);
        product->AttachTouch(touch_);
        product->SetNetworkAction([this] {
            if (!IsInWifiConfigMode())
                EnterWifiConfigMode();
        });
        product->SetUsbStorageAction([this] { return StartUsbStorageMode(); });
        InitializeContentCard();
        DictionaryService::GetInstance().SetResultCallback(
            [product](const han::Entry& entry, bool auto_play_strokes) {
                Application::GetInstance().Schedule([product, entry, auto_play_strokes] {
                    product->ShowEntry(entry, auto_play_strokes);
                });
            });
        McpServer::GetInstance().AddTool(
            "self.study.open",
            "打开学习界面。page可选home、dictionary、phonetics、timetable、timer、alarm、weather、n"
            "etwork。用户说学习英语音标时传phonetics。本工具只导航，不会设置闹钟或开始计时。",
            PropertyList({Property("page", kPropertyTypeString)}),
            [product](const PropertyList& args) -> ReturnValue {
                return product->OpenPage(args["page"].value<std::string>());
            });
#endif
        InitializeCamera();
        InitializeButtons();
        SetChargeQcEn(true);
        SetChargeEn(true);
#if CONFIG_HAN_DICTIONARY
        // The dictionary product does not use the outward-facing 5 V rails. Leaving both boost
        // converters enabled wastes battery even while the screen is off.
        SetUsb5vEn(false);
        SetExt5vEn(false);
#else
        SetUsb5vEn(true);
        SetExt5vEn(true);
#endif
        GetBacklight()->RestoreBrightness();
#if CONFIG_HAN_DICTIONARY
        DictionaryService::GetInstance().RegisterMcpTools();
#endif
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Tab5AudioCodec audio_codec(
            i2c_bus_, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8388_ADDR, AUDIO_CODEC_ES7210_ADDR,
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    virtual Camera* GetCamera() override { return camera_; }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    bool GetBatteryInfo(BatteryInfo& info) override {
        info = {};
        uint16_t voltage_mv = 0;
        if (!power_monitor_ready_ || !power_monitor_.Read(voltage_mv, info.current_ma))
            return false;

        info.voltage_mv = voltage_mv;
        info.level = Tab5BatteryLevelFromVoltage(voltage_mv);

        uint8_t input = 0;
        if (pi4ioe2_->ReadInput(input)) {
            info.charging = (input & (1U << 6)) != 0;  // P6 = CHG_STAT, matches M5Unified.
            info.discharging = !info.charging && info.current_ma < -10;
        } else {
            info.charging = info.current_ma > 10;
            info.discharging = info.current_ma < -10;
        }
        return true;
    }

    bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        BatteryInfo info;
        if (!GetBatteryInfo(info))
            return false;
        level = info.level;
        charging = info.charging;
        discharging = info.discharging;
        return true;
    }

    // BSP power control functions
#if CONFIG_HAN_DICTIONARY
    esp_err_t InitializeSdBus() {
        if (sd_bus_initialized_)
            return ESP_OK;
        // Official Tab5 SPI pins. Wi-Fi uses its separate SDIO bus; never reconfigure it.
        spi_bus_config_t bus{};
        bus.mosi_io_num = GPIO_NUM_44;
        bus.miso_io_num = GPIO_NUM_39;
        bus.sclk_io_num = GPIO_NUM_43;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.max_transfer_sz = 8192;
        const auto err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
        if (err == ESP_OK)
            sd_bus_initialized_ = true;
        return err;
    }

    esp_err_t InitializeRawContentCard() {
        if (sd_card_)
            return ESP_OK;
        auto err = InitializeSdBus();
        if (err != ESP_OK)
            return err;
        err = sdspi_host_init();
        if (err != ESP_OK)
            return err;

        sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot.host_id = SPI2_HOST;
        slot.gpio_cs = GPIO_NUM_42;
        sdspi_dev_handle_t device = -1;
        err = sdspi_host_init_device(&slot, &device);
        if (err != ESP_OK)
            return err;

        auto card = static_cast<sdmmc_card_t*>(calloc(1, sizeof(sdmmc_card_t)));
        if (!card) {
            sdspi_host_remove_device(device);
            return ESP_ERR_NO_MEM;
        }
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot = device;
        host.max_freq_khz = SDMMC_FREQ_DEFAULT;
        err = sdmmc_card_init(&host, card);
        if (err != ESP_OK) {
            sdspi_host_remove_device(device);
            free(card);
            return err;
        }
        sd_card_ = card;
        ESP_LOGI(TAG, "SD card ready for raw access: %llu MiB",
                 static_cast<unsigned long long>(card->csd.capacity) * card->csd.sector_size /
                     (1024 * 1024));
        return ESP_OK;
    }

    void InitializeContentCard() {
        auto err = InitializeSdBus();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SD SPI unavailable: %s", esp_err_to_name(err));
            return;
        }
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot = SPI2_HOST;
        host.max_freq_khz = SDMMC_FREQ_DEFAULT;
        sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot.host_id = SPI2_HOST;
        slot.gpio_cs = GPIO_NUM_42;
        esp_vfs_fat_sdmmc_mount_config_t mount{};
        mount.format_if_mount_failed = false;
        mount.max_files = 6;
        mount.allocation_unit_size = 16 * 1024;
        err = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot, &mount, &sd_card_);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "No usable SD card (%s); using embedded sample", esp_err_to_name(err));
            spi_bus_free(SPI2_HOST);
            sd_bus_initialized_ = false;
            sd_card_ = nullptr;
            return;
        }
        sd_card_mounted_ = true;
        DictionaryService::GetInstance().store().Initialize();
    }

    std::string StartUsbStorageMode() {
#if !CONFIG_TINYUSB_MSC_ENABLED
        return "当前固件未启用 USB 读卡器";
#else
        if (usb_storage_)
            return {};

        auto& store = DictionaryService::GetInstance().store();
        store.Detach();
        if (sd_card_mounted_) {
            const auto err = esp_vfs_fat_sdcard_unmount("/sdcard", sd_card_);
            if (err != ESP_OK) {
                store.Initialize();
                return std::string("无法卸载 SD 卡：") + esp_err_to_name(err);
            }
            sd_card_ = nullptr;  // esp_vfs_fat_sdcard_unmount releases the descriptor.
            sd_card_mounted_ = false;
        }

        auto err = InitializeRawContentCard();
        if (err != ESP_OK)
            return std::string("无法读取 microSD 卡：") + esp_err_to_name(err);

        static char mount_path[] = "/sdcard";
        tinyusb_msc_storage_config_t storage_config{};
        storage_config.medium.card = sd_card_;
        storage_config.fat_fs.base_path = mount_path;
        storage_config.fat_fs.config.format_if_mount_failed = false;
        storage_config.fat_fs.config.max_files = 6;
        storage_config.fat_fs.config.allocation_unit_size = 16 * 1024;
        storage_config.fat_fs.do_not_format = true;
        storage_config.mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB;
        err = tinyusb_msc_new_storage_sdmmc(&storage_config, &usb_storage_);
        if (err != ESP_OK) {
            usb_storage_ = nullptr;
            return std::string("USB 存储初始化失败：") + esp_err_to_name(err);
        }

        // Tab5 routes the USB-C connector to FSLS PHY 0, while the default ESP32-P4
        // TinyUSB configuration uses the high-speed controller connected to USB-A.
        // Start OTG1.1 first on the unused PHY, then atomically hand the external
        // USB-C PHY from USB-Serial/JTAG to TinyUSB. COM logging intentionally stops
        // after the swap and returns on the next reboot.
        tinyusb_config_t usb_config = TINYUSB_CONFIG_FULL_SPEED(nullptr, nullptr);
        err = tinyusb_driver_install(&usb_config);
        if (err != ESP_OK) {
            tinyusb_msc_delete_storage(usb_storage_);
            usb_storage_ = nullptr;
            return std::string("USB 设备启动失败：") + esp_err_to_name(err);
        }
        tud_disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));
        usb_wrap_ll_phy_select(&USB_WRAP, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        tud_connect();
        ESP_LOGI(TAG, "microSD is now exported as a USB mass-storage device");
        return {};
#endif
    }
#endif
    void SetChargeQcEn(bool en) {
        if (pi4ioe2_) {
            uint8_t value = pi4ioe2_->ReadOutSet();
            if (en) {
                clrbit(value, 5);  // P5 = CHG_QC_EN (低电平使能)
            } else {
                setbit(value, 5);
            }
            pi4ioe2_->WriteOutSet(value);
        }
    }

    void SetChargeEn(bool en) {
        if (pi4ioe2_) {
            uint8_t value = pi4ioe2_->ReadOutSet();
            if (en) {
                setbit(value, 7);  // P7 = CHG_EN
            } else {
                clrbit(value, 7);
            }
            pi4ioe2_->WriteOutSet(value);
        }
    }

    void SetUsb5vEn(bool en) {
        if (pi4ioe2_) {
            uint8_t value = pi4ioe2_->ReadOutSet();
            if (en) {
                setbit(value, 3);  // P3 = USB5V_EN
            } else {
                clrbit(value, 3);
            }
            pi4ioe2_->WriteOutSet(value);
        }
    }

    void SetExt5vEn(bool en) {
        if (pi4ioe1_) {
            uint8_t value = pi4ioe1_->ReadOutSet();
            if (en) {
                setbit(value, 2);  // P2 = EXT5V_EN
            } else {
                clrbit(value, 2);
            }
            pi4ioe1_->WriteOutSet(value);
        }
    }
};

DECLARE_BOARD(M5StackTab5Board);
