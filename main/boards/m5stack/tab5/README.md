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

### 局域网页面 OTA

汉字学习机变体联网后会在设备的 IPv4 地址上启动本地升级页面。让电脑或手机连接同一局域网，在浏览器访问设置页显示的地址（例如 `http://192.168.5.131/`），选择为当前硬件变体编译的 `xiaozhi.bin` 后即可写入备用 OTA 分区。上传成功后设备自动重启并切换分区；上传、校验或写入失败不会切换当前启动分区。

升级接口只接受升级页面生成的当前启动令牌，并检查 `.bin` 文件名、镜像头、项目名、镜像大小以及 ESP-IDF OTA 镜像校验。该页面仅用于可信局域网，不应通过路由器端口转发暴露到互联网。

产品使用 `partitions/han16m.csv`，首次从原版迁移需通过 USB 烧录完整分区表及资源。SD 使用 SPI CS42/SCK43/MOSI44/MISO39，不改变 Wi-Fi 的 SDIO 配置；缺卡可启动，不自动格式化。详细构建、资源与验收说明见仓库根目录 `HAN_DICTIONARY.md`。

Legacy 字典变体支持通过机身 USB-C 直接访问 microSD：点主页右上角 Wi-Fi 图标，进入
“设置”后点“USB 读卡器”。切换时 USB-C 会从 USB-Serial/JTAG 交给 TinyUSB MSC，
因此 COM 口消失是正常现象。电脑安全弹出磁盘后点“重启并恢复”，固件会先停止 MSC、
释放 TinyUSB 并把 USB-C PHY 交还 USB-Serial/JTAG，然后软件重启，字典读取和语音唤醒随之恢复。
2026-09-13 已在 Rev 1.3 真机将 128 GB 卡枚举为 119.25 GB USB 可移动磁盘。

闹钟铃声放在 microSD 的 `handict/alarms/` 目录。闹钟页可在“晨光、轻步、小铃、启程”
之间选择并主动试听；到点后循环播放，点击“停止铃声”只停止本次响铃，不会关闭后续重复日。
铃声使用 Ogg/Opus，缺少或误放为 Ogg/Vorbis 时界面会提示重新复制铃声包。

该设置页还提供 10%–100% 的亮度和播放音量调节，并写入 NVS。点“立即关屏”会关闭
背光，触摸屏幕任意位置即可恢复。机身按键由硬件用于重启，不作为关屏键。

屏幕和触摸继续复用本文件夹已有驱动探测；新 UI 设置横屏旋转并注册触摸设备。Rev 1.3 / ST7121 已通过显示和基础触摸测试，四角精度与其他面板仍待验证。INA226 已接入真实电池电压、电流和充电状态；读取失败时显示未知。点击电池图标可查看电量、2S 电池包电压、带符号电流和瞬时功率。百分比按保守的 2S 锂电池放电曲线估算，不是库仑积分结果。

字典产品变体不使用 USB-A 和外接 5 V 输出，启动后会关闭这两路升压电源；关屏时保持 Wi-Fi `LOW_POWER`。软件闹钟、联网和语音唤醒继续运行，因此一天续航仍需真机放电测试。若要进入深度睡眠并保留闹钟，应先把下一次闹钟写入 RX8130 RTC 并配置 RTC 唤醒。

## 历史测试记录

2026-09-13 Han Dictionary 真机：Rev 1.3 已完成配网并播放提示音；
触摸固件主版本 1 对应 ST7121，旧代码按 0x55 地址误判 ST7123，出现背光亮但画面黑屏。
板级代码已移植上游 PR #2227 的 ST7121/ST7123 识别、硬件复位及 DMA2D 修复，
重新烧录后用户确认有画面、触摸正常；设备已联网激活并进入待机。
Legacy 字典变体启用 `CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM`，解决 SDIO 内存池启动分配失败；
暂时关闭 `CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER`，避开 ESP-IPA 2.3.0 预编译库的非法指令。
摄像头驱动仍保留且成功采帧，自动画质调节暂不可用。详见根目录 `docs/HARDWARE_BRINGUP.md`。
两项启动配置仅作用于 Legacy 字典变体，显示修复由四个 Tab5 变体共用；不要把这个启动测试当作全功能验收。

后续语音测试确认手动说话能录音并得到回复，但免按键唤醒失败。
`ui4-20260913-Legacy-aec-ppa` 上板后用户仍不能唤醒，也未感到进一步提速。
HanDisplay 的 8 MiB 按需图片缓存首轮曾将切页从十多秒改善到约 2–3 秒，但性能仍未达标。
`Legacy-frame-diagnostics` 已启用全屏单个 PSRAM 绘制缓冲（PARTIAL 模式）及页面、音频诊断。
用户确认主要切页已经很快、效果满意，但返回主页时左上角小书图标仍慢，免按键唤醒仍失败。

参考通道调查曾错误地把 MIC 模拟输入编号当成 TDM 时隙号，将掩码 0+1 改为 0+2，
`aec-ppa` 与 `frame-diagnostics` 均包含此错误。
[ES7210 数据手册第 6 页图 2e](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/K128%20CoreS3/ES7210.PDF#page=6)
规定 I²S TDM 时隙顺序为 MIC1、MIC3、MIC2、MIC4；当前驱动使用 I²S normal 模式，
应取时隙 0+1 才能得到 MIC1 和 MIC3（`AEC_P`）回放参考。代码已恢复正确掩码并说明时隙关系，
`Legacy-correct-reference` 不更改 I²S 引脚；串口已记录免按键唤醒、服务端回复、
`self.dictionary.lookup` 及字典页打开（`queued=321 ms`）。
当前实机已刷并校验 `Legacy-final`，应用 4,540,528 字节，应用槽余 702,352 字节。
返回主页合并整屏刷新已运行，日志记录 `queued=397 ms`、一次提交；
临时音频统计已移除，保留逐页耗时诊断。用户确认最终版小书与页面同时出现、切页正常，
“你好小智”能够可靠唤醒并听到回复，本轮上述项目真机验收通过。
播放中打断/AEC、专门的断网重连、长时间稳定性及正常 SD 内容卡仍待专项测试。
四个受影响 Tab5 变体均已构建通过；上述真机验收仅覆盖 Legacy 字典变体，其他三个仅验证构建。

儿童使用反馈表明普通 WakeNet9 对较快、高音高语音仍可能漏唤醒。字典产品的规范构建现改用
`wn9l_nihaoxiaozhi_tts3`，并在 AFE 中使用 `DET_MODE_95` 高召回模式；Tab5 麦克风增益继续保持
36 dB，避免近距离大声说话削波。该调整可能略微增加误唤醒，需要在安静、电视背景声和播放中
三种场景分别做真机统计。

@2025/05/17 测试问题

1. listening... 需要等几秒才能获取语音输入???
2. 亮度调节不对
3. 音量调节不对
