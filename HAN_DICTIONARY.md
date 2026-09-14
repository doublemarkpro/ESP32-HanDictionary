# ESP32 Han Dictionary

这是面向 M5Stack Tab5 的儿童语音查字工具，基于 XiaoZhi ESP32 `v2.4.2`。

**2026-09-13 真机进展**：设备已到手，COM7 确认为 Tab5 / ESP32-P4 Rev 1.3（Legacy），
使用 ST7121 屏幕。当前 Legacy USB-C MSC 版已烧录，用户确认显示和基础触摸正常、切页迅速，
返回主页时小书同步出现；“你好小智”可以唤醒并听到回复，串口也记录了查字开页链路。
四个 Tab5 变体均已构建通过，真机验收仅覆盖 Legacy 字典变体。
128 GB 卡已通过 Tab5 USB-C 枚举为 119.25 GB 可移动磁盘，原格式确认为 exFAT；
备份后格式化为 FAT32、正常内容卡读取、长时间稳定性、播放中打断/AEC 等专项仍待验收。
详见 [真机调试与验收记录](docs/HARDWARE_BRINGUP.md)。

当前产品原型已实现：

- 固定的 ESP-IDF 6.0.2 开发环境；
- Tab5 Legacy 与 P4X 两种芯片修订版的独立固件变体；
- 六入口 LVGL 主页、返回导航、手机配网页面和持续可见的语音入口；
- 查字结果联动、内置“规”字八步笔顺、SD “矩”字示例、44 个英式音标卡片与分页/点读接口；
- 分科作业计时及重启恢复、单个每日提醒、课程表，以及和风天气实时刷新与离线缓存；
- `self.dictionary.lookup`、`self.study.open` 本地 MCP 工具；
- SD 内容包校验与导入，绑定新华字典第 12 版单色本 ISBN `9787100168076`；
- Legacy 字典变体的 USB-C microSD 读卡器模式，切换前停止应用侧内容访问；
- 设置页的亮度、音量与立即关屏控制；亮度和音量写入 NVS，重启后保持；
- 关屏后触摸屏幕任意位置即可恢复显示；
- 点击主页电池图标查看电量、2S 电池包电压、充放电电流与瞬时功率；
- Windows 环境探测、构建、硬件识别与跨电脑导出脚本。

下面是同一份固件 LVGL 页面代码在电脑上渲染的实际界面，不是概念设计图。
模拟器替代了硬件服务；截图本身不是硬件验收证据，实际测试结果以上述真机记录为准。

![实际主页](docs/ui/rendered/home.png)

[实施计划与边界](docs/IMPLEMENTATION_PLAN.md) · [实际截图与设计基准](docs/ui/README.md) · [SD 卡准备](docs/SD_CARD.md)

第二轮的容量评估、资源准备方法和逐模块完成度见 [开发状态](docs/DEVELOPMENT_STATUS.md)。

## 公司电脑首次构建

```powershell
cd D:\ESP32Projects\ESP32-HanDictionary
. .\tools\setup.ps1
.\tools\build.ps1 -HardwareRevision Legacy
```

首次接入另一台设备时，先识别 ESP32-P4 芯片修订版。下例 `COM5` 为示例，本次设备为 `COM7`：

```powershell
.\tools\detect-hardware.ps1 -Port COM5
```

如果脚本提示 `P4X`，使用：

```powershell
.\tools\build.ps1 -HardwareRevision P4X
```

不要把 P4X 固件强制写入 Rev 1.x 芯片。

本版为加入字体与 UI 使用了专用 `partitions/han16m.csv`：两个 5 MiB OTA 应用分区。**从旧固件升级时，首次应通过 USB 完整烧录（包含分区表和 assets），不能只 OTA 替换应用。** 先备份重要设备设置，确认型号与串口后运行：

```powershell
idf.py -p COM5 flash monitor
```

必须先构建对应硬件版本；共享 `build` 目录总是代表最后一次构建。可在构建后运行 `tools/package-firmware.ps1 -OutputPath dist/firmware/my-build` 留存独立烧录包，输出目录需不存在。

日常开发这台 Rev 1.3 设备只需构建 `m5stack-tab5-han-dictionary`（Legacy）。只有修改
Tab5 共用板级代码、准备发布或明确需要兼容新硬件时，才扩展验证 P4X/原版变体。

需要通过电脑管理 microSD 时，点主页右上角 Wi-Fi 图标进入“设置”，再点
“USB 读卡器”。COM7 会暂时消失，电脑随后看到 microSD 可移动磁盘；完成操作后先安全
弹出，再重启设备。USB 模式期间字典内容和语音唤醒暂停。

同一设置页可按 10% 步进调整屏幕亮度和播放音量，范围为 10%–100%，设置会在重启后
保留。点“立即关屏”可关闭背光，再触摸屏幕任意位置即可亮屏。
关屏只关闭显示背光，联网、语音唤醒、计时和提醒仍继续运行。

Tab5 通过 INA226 测量电池包电压和实时电流。当前百分比按 2S 锂电池静置电压曲线估算，
2000 mAh 标称容量不直接参与换算；负载、温度和电池老化都会让显示值与真实剩余容量存在偏差。
字典变体会关闭未使用的 USB-A 与外接 5 V 输出，并在关屏时保持 Wi-Fi 低功耗模式。
这能改善普通待机，但仍需保持联网、语音唤醒和软件闹钟，不能把 24 小时续航当作已保证指标。
要获得数量级更低的闹钟守候功耗，需要后续把闹钟写入 RX8130 RTC，并进入可由 RTC 唤醒的深度睡眠。

## 回家后复现环境

在家中电脑安装 **Espressif Installation Manager** 和 **ESP-IDF 6.0.2**，然后克隆自己的 Git 远程仓库并运行：

```powershell
cd ESP32-HanDictionary
. .\tools\setup.ps1
.\tools\build.ps1 -HardwareRevision Legacy
```

脚本不保存公司电脑的绝对 ESP-IDF 路径，会依次查找命令行环境、EIM 配置和常见安装目录。也可以显式传入：

```powershell
.\tools\build.ps1 -IdfPath 'C:\esp\v6.0.2\esp-idf'
```

自己的仓库是 [doublemarkpro/ESP32-HanDictionary](https://github.com/doublemarkpro/ESP32-HanDictionary)。公司与家中电脑使用同一分支并先拉取最新提交；不要复制公司电脑的 `build`、`sdkconfig`、`managed_components` 或虚拟环境。固件所需生成素材已提交，普通构建不需要 Node.js 或 Visual Studio。

也可在提交代码后生成可迁移 bundle：

```powershell
.\tools\export-bundle.ps1
```

将 `dist\ESP32-HanDictionary.bundle` 复制到家中电脑，再执行：

```powershell
git clone .\ESP32-HanDictionary.bundle ESP32-HanDictionary
```

## 当前演示

连接小智后可以问：

> 规矩的规怎么写？

模型应调用：

```text
self.dictionary.lookup({"query":"规矩的规"})
```

工具返回拼音、部首、笔画数、结构、组词和笔顺，并打开查字结果页。语音语义理解继续使用联网小智；离线可操作主页、内置示例、已提供的本地内容和计时。内置释义是开发示例，不是新华字典原文。

你已确认纸书为商务印书馆《新华字典》第 12 版单色本，ISBN `978-7-100-16807-6`。目前未取得全书数据和经核对页码，未知页码返回空值并显示“待核对”；不能把内容导入接口等同于完整离线新华字典。

## 资源和验证

UI 字体、图标与规字笔顺放内部 Flash；字条、页码、教学音频、课程表和天气私有配置放 SD；计时和提醒设置放 NVS。无卡也能启动，详见 [SD_CARD.md](docs/SD_CARD.md) 和 [和风天气配置](docs/QWEATHER.md)。音频未随示例包提供，缺失时会明确提示。

```powershell
python -m unittest discover -s scripts/tests -v
python tools/content_pack.py prepare --output dist/sdcard-starter
python tools/content_pack.py validate dist/sdcard-starter/handict
```

需要重新生成素材或截图时，额外安装 Node.js；电脑运行 LVGL 模拟器还需 Visual Studio C++ Build Tools。先完成一次固件配置以取得依赖：

```powershell
npm ci --prefix tools/ui-assets
node tools/ui-assets/generate.cjs
.\tools\preview-ui.ps1
```

模拟器直接编译产品页面代码，并检查导航、音标分类、笔顺、计时和查字规则；输出见 `docs/ui/rendered`。素材来源和许可证见 [assets/README.md](assets/README.md)。

## 后续里程碑

1. 完成正常 SD 内容卡、长时间稳定性、触摸四角、不同距离/噪声唤醒、播放中打断/AEC 和断网重连专项；保持本轮已验收的切页及唤醒体验。
2. 补齐可使用的字条、书本页码、完整中文字体和全量笔顺；当前图形笔顺仅有“规”。
3. 人工核对并导入音标示范音，加入录音回放；发音评分独立设计。
4. 完善触屏配网键盘、课程编辑、每日/每周计时历史、多个闹钟与 RTC 唤醒、天气预报卡片。
5. 细化插画、动效及语音操作。目前语音可查字和打开页面，不会自动修改闹钟或开始作业计时。
