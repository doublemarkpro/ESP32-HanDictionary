# 使用说明 

* [M5Stack Tab5 docs](https://docs.m5stack.com/zh_CN/core/Tab5)

## 快速体验

到 [M5Burner](https://docs.m5stack.com/zh_CN/uiflow/m5burner/intro) 选择 Tab5 搜索小智下载固件

## 基础使用

* idf version: v5.5.2 or above (recommended: v6.0.2)

* No dependency override needed — the project already specifies the correct `esp_video` and `esp_ipa` versions in `main/idf_component.yml`. Do NOT change the dependency versions unless you are also modifying the source code to match the older API.

`build.py` 会根据当前 ESP-IDF 版本选择芯片版本：

- `m5stack-tab5` 面向 Rev < 3，并包含 `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` 和 `CONFIG_ESP32P4_REV_MIN_100=y`。
- `m5stack-tab5-p4x` 面向 Rev >= 3。
- ESP-IDF 6 构建需要 `esp-sr` 2.4.7 或更高版本，两个芯片修订版均可用。

不要把 `m5stack-tab5-p4x` 固件强制刷入 Rev 1.x 芯片；应改用无后缀的 `m5stack-tab5` 固件。误用 P4X 固件时，正常情况下烧录工具会报告：`bootloader/bootloader.bin requires chip revision in range [v3.0 - v3.99] (this chip is revision v1.x)`。

1. 使用 build.py 编译

```shell
python ./scripts/build.py m5stack-tab5
```

如需手动编译，请参考 `m5stack-tab5/config.json` 修改 menuconfig 对应选项。

2. 编译烧录程序

```shell
idf.py flash monitor
```

> [!NOTE]
> 进入下载模式：长按复位按键（约 2 秒），直至内部绿色 LED 指示灯开始快速闪烁，松开按键。


## Han Dictionary 产品变体

`m5stack-tab5-han-dictionary` 与 `m5stack-tab5-han-dictionary-p4x` 分别对应上述两种芯片版本，通过 `CONFIG_HAN_DICTIONARY` 启用 1280×720 横屏学习 UI。原小智变体不启用这些页面。

产品使用 `partitions/han16m.csv`，首次从原版迁移需通过 USB 烧录完整分区表及资源。SD 使用 SPI CS42/SCK43/MOSI44/MISO39，不改变 Wi-Fi 的 SDIO 配置；缺卡可启动，不自动格式化。详细构建、资源与验收说明见仓库根目录 `HAN_DICTIONARY.md`。

屏幕和触摸继续复用本文件夹已有驱动探测；新 UI 设置横屏旋转并注册触摸设备。尚未完成真机验证，需检查实际设备的 LCD/触摸版本和四角坐标。电量读取未接通时显示未知，不提供模拟百分比。

## 历史测试记录

@2025/05/17 测试问题

1. listening... 需要等几秒才能获取语音输入???
2. 亮度调节不对
3. 音量调节不对
