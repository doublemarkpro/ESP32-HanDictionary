# Tab5 首次真机调试

2026-09-13，代码起点 `442bec4`，使用 ESP-IDF 6.0.2。

## 当前状态

当前已烧录并校验 `dist/firmware/ui4-20260913-Legacy-final`，应用 4,540,528 字节。
用户已确认最终版切页正常、小书与主页主体同时出现；说“你好小智”能够可靠唤醒，
并听到回复。这两项主要问题已通过本轮真机验收。
正确参考版的串口另记录了唤醒、对话及字典查询开页链路。收尾版保留这些改动，
将主页合并为一次提交，并移除临时音频统计。
四个受影响 Tab5 变体均已构建通过；只有 Legacy 字典变体完成上述真机验收，其他三个仅验证了构建。

## 硬件识别

- 用户设备连接 COM7，esptool 5.3.1 读取 ESP32-P4 **Rev 1.3**，使用 Legacy 字典变体。
- Flash 实测 16 MB；启动日志识别到 32 MB PSRAM，200 MHz。
- 触摸 I²C 地址 0x55，固件主版本 1，对应 **ST7121** 屏幕；旧代码仅凭地址误判为 ST7123。
  旧驱动读取的 LCD ID 为 `80 A0 FB`，不能据此确认 ST7123 型号。
- 触摸驱动报告 720×1280、最多十点，版本 `1(1.80.1.16)`；驱动初始化成功不代表横屏坐标已验收。
- 未挂载到可用 SD 卡，日志为 `ESP_ERR_TIMEOUT`，已走内置示例回退。
- 摄像头探测到 SC202CS，PID `0xeb52`。

## 已复现的启动问题

### SDIO 通信内存池分配失败

原始 UI4 构建可通过编译，但在 `app_main()` 之前触发：

```text
assert failed: sdio_mempool_create sdio_drv.c:258 (buf_mp_g)
```

本次依赖锁选择 ESP-Hosted 2.12.13。其默认传输缓冲区分配只使用内部 DMA RAM。
在 Legacy 字典变体启用 `CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y` 后，
真机成功越过该崩溃点并进入屏幕、触摸、SD 和摄像头初始化。
该选项优先分配 DMA-capable PSRAM，失败时由组件回退到内部 RAM。
保留现有 64 字节 L2 cache line；联网收发和语音并发仍需验证。

### 摄像头自动 ISP 算法非法指令

修复内存池后，启动在 `esp_ipa_pipeline_create` 内稳定触发非法指令。
该轮 ELF 的 PC 为 `0x481ab528`，异常指令 `0x20fac7b3`，
反汇编确认来自 ESP-IPA 2.3.0 的 IDF 6 预编译库，而非页面代码。

Legacy 字典变体暂设 `CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=n`，
避开该自动算法库的启动路径；保留摄像头、CSI 与基本 ISP 驱动。
自动曝光、白平衡等画质调节不视为可用；需要兼容 Rev 1.3 的算法库并单独验收后再恢复。
重新构建并完整烧录后，设备成功越过两处崩溃点，进入配网模式。
摄像头驱动日志报告 5 秒内成功采集 141 帧，但没有检查照片色彩和曝光质量。

### 背光亮但画面全黑

用户确认能听到配网提示音，但屏幕只有背光。旧代码将地址 0x55 一律当作 ST7123，
未识别触摸固件主版本 1 对应的 ST7121；初始化前也缺少 LCD/触摸硬件复位。

移植 [小智上游 PR #2227](https://github.com/78/xiaozhi-esp32/pull/2227)
（补丁提交 `804f713ea6cae594214af91cfb859b8d7d82dfce`）的板级显示修复：

- 在探测前用已有 PI4IOE P4/P5 引脚复位屏幕和触摸，不修改接线定义。
- 以 16 位 I²C 寄存器读触摸固件版本：1 选择 ST7121，3 选择 ST7123，读取失败保留 ST7123 回退。
- 增加 Apache-2.0 的 ST7121 驱动及初始化表，使用对应的垂直时序。
- 按 ESP-IDF 版本启用 DPI DMA2D；保留 ILI9881C 和 ST7123 路径。
- 保留 HanDisplay、字典服务、SD 与横屏触摸集成，未整文件覆盖上游板级代码。

修复后启动日志明确识别 ST7121 并完成初始化。用户确认“有画面，触摸正常”，
主页入口和返回按钮通过基础测试；四角精度、长时间使用及快速切页仍需专项验收。

## 显示修复版上板记录

- 固件：`dist/firmware/ui4-20260913-Legacy-displayfix`，应用 4,540,208 字节。
- 5 MiB 应用槽余 702,672 字节（13.4%），通过 512 KiB 安全门槛；完整烧录及哈希校验通过。
- 自动重连 Wi-Fi，MQTT 连接成功，完成小智账号激活并进入 `idle`。
- WakeNet、VAD、录音及播放设备初始化成功；不代表实际语音对话已验收。
- 唤醒/录音服务启动后约 31 秒，内部 SRAM 空闲 100,263 字节，历史最低 68,955 字节。
  与前一版配网状态的内存读数不处于同一工作状态，不能直接比较。
- 显示与基础触摸获用户确认。后续语音查字和切页测试暴露明显卡顿，见下节。

### 切页逐行刷新及语音受阻

用户随后反馈切页、返回主页需要十多秒；按住说话有状态变化并能听到回复，
但免按键唤醒未通过验收。串口实际记录到识别“规矩的规”、调用字典工具及回答文本。
随后 AFE 输入环形缓冲区持续满，CPU0 的 `taskLVGL` 触发任务看门狗。

该固件 PC `0x48188fc2` / RA `0x48188fd4` 经其对应 ELF 解析为
`lodepng_convert` / `getPixelColorsRGBA8`。LVGL 默认图片缓存为 0，分块重绘反复解码 PNG。
在 HanDisplay 初始化时设置 8 MiB 的解码图片缓存预算，按需分配并按缓存策略淘汰，
大块内存由现有 Tab5 PSRAM malloc 配置承接；不预先占满 8 MiB，也不影响原小智页面。
按内置图像描述符统计，主页、图标、笔顺、课程图形以每像素 4 字节展开合计约 6.88 MiB，
8 MiB 预算能覆盖这批静态图片及少量管理开销；动态 SD 内容仍由淘汰策略控制。
SD 笔顺在更换底层数据前已有 `lv_image_cache_drop`，继续保留以避免复用旧图。

缓存修复已构建并烧录，该轮包为 `dist/firmware/ui4-20260913-Legacy-cachefix`，
应用 4,540,464 字节、应用槽余 702,416 字节（13.4%），各段烧录哈希校验通过。
启动约 24 秒时内部 SRAM 空闲 97,843 字节、历史最低 69,083 字节。
用户确认切页大幅改善，但仍需 2–3 秒完成，免按键唤醒仍失败。
这轮复测日志未见原先的看门狗及 AFE 环形缓冲区满警告，不能据此视为性能已达标。
相关旧版故障日志为
`runtime-Legacy-displayfix.log` 和 `manual-voice-Legacy-displayfix.log`。

### PPA 版与参考通道误判（已上板，唤醒仍失败）

[M5Stack 官方原理图第 3 页](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Tab5_Schematics_PDF.pdf#page=3)
显示：ES7210 MIC1/MIC2 是两只板载麦克风，MIC3 经 C83 连接 `AEC_P`，
而 `AEC_P` 从 ES8388 `HPOUT_L` 取回放参考。
上一轮仅依据模拟输入编号推断串行时隙顺序，错误地将原掩码 0+1 改成 0+2。
`aec-ppa` 与随后 `frame-diagnostics` 都烧录了这个错误掩码 `0x5`，用户仍无法唤醒。

后续核对 [ES7210 数据手册第 6 页图 2e](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/K128%20CoreS3/ES7210.PDF#page=6)，
I²S TDM 的输出顺序是 **MIC1、MIC3、MIC2、MIC4**，不能直接用模拟输入编号减一作为时隙号。
当前 codec 组件配置为 `ES_I2S_NORMAL`，因此 0+1（掩码 `0x3`）才是 MIC1 + MIC3 回放参考；
0+2 实际选择 MIC1 + MIC2，将第二只麦克风误当作回声参考。
板级代码已恢复 0+1 并补充时隙注释，保留两通道 MR 数据格式及 I²S 引脚；
`correct-reference` 已构建并复刷。恢复正确参考通道不能预先证明最初的唤醒问题已解决，
仍需检查实测信号、唤醒和播放中打断。

两个字典变体启用 `CONFIG_LVGL_PORT_ENABLE_PPA`，通过现有 LVGL port 加速横屏旋转。
HanDisplay 只在状态栏、计时数字等内容变化时更新对象，避免重复触发重绘；
该轮串口记录超过 100 ms 的刷新耗时及 PSRAM 余量。启动首屏记录 789 ms，
只能说明该次绘制耗时，不能替代用户切页测试。用户仍反馈速度慢，与上一版没有明显区别。

该轮包为 `dist/firmware/ui4-20260913-Legacy-aec-ppa`，应用 4,540,432 字节，
5 MiB 应用槽余 702,448 字节（13.4%）；完整烧录及各段哈希校验通过。
日志为 `boot-Legacy-aec-ppa.log`，构建成功和启动成功均不代表唤醒、性能通过验收。

### 全屏绘制缓冲与诊断版（已上板，取得首批测量）

原 MIPI 绘制缓冲为 36,000 像素，仅相当于横屏约 28 行，全屏重绘最多分为约 26 块。
字典变体改用 921,600 像素的单个 PSRAM 绘制缓冲，继续使用 PARTIAL 模式，
小范围更新仍只绘制失效区域；配合 PPA 旋转缓冲合计约 3.52 MiB。
目的是减少完整页面的重复遍历、字形解码和分块提交。

页面诊断只记录 `Render()` 后的一次刷新：对象构建、绘制、提交总耗时、刷新块数与像素量，
以及 PPA 提交和 DMA 等待时间。`queued` 表示最后一次 DMA 已提交，不代表屏幕已经显示完毕。
音频诊断使用固定计数器，记录输入两通道与 AFE 输出的 RMS、峰值、直流偏移，
以及处理速率、VAD、唤醒计数和控制状态，用来区分采样、AFE 处理和唤醒检测问题。

已烧录包为 `dist/firmware/ui4-20260913-Legacy-frame-diagnostics`，
应用 4,543,328 字节，5 MiB 应用槽余 699,552 字节（13.3%）。
`boot-frame-diagnostics.log` 记录首屏 `queued=838 ms`，整屏仅提交 1 块；
后续字典、音标、主页和闹钟切换为 169–405 ms、4–5 个独立失效区域。
`runtime-frame-diagnostics-01.log` 又记录到 162–410 ms、4–7 个区域。
页面构建约 6–17 ms，剩余主要是绘制；这些采样不代表所有页面、长时间运行或完整触摸响应延迟。

音频 Feed/Fetch 通常接近 16 kHz，采集期间 Fetch `failures=0`；
`Controls: bits=0x5 device_aec=0 wake_result=1 aec_result=1` 确认唤醒和 AEC 控制均已启用。
有 VAD 人声帧但 `wakes=0`。后续切页期间曾短暂降到约 14 kHz，不能宣称音频始终无阻塞。
此版仍使用上节所述错误参考掩码，不能据此判断正确参考下的 AEC 或唤醒效果。
用户随后确认主要切页已经很快、效果满意，但返回主页时左上角小书图标仍慢，
免按键唤醒仍失败。主要切页的改善已获得用户确认；小书局部刷新及唤醒继续排查。

### 正确回声参考版（已复刷，日志确认唤醒与查字链路）

`dist/firmware/ui4-20260913-Legacy-correct-reference` 恢复正确的 I²S TDM 参考时隙 0+1，
保留全屏绘制缓冲与诊断；应用仍为 4,543,328 字节。
`flash-correct-reference.log` 已记录应用写入、哈希校验通过及复位，
启动采样见 `boot-correct-reference.log`。
`runtime-correct-reference-01.log` 在设备运行 57.071 秒时记录 `Wake word detected: 你好小智`，
随后从 `idle` 进入 `connecting`、`listening` 和 `speaking`，58.119 秒出现服务端回复文本。
经过一次澄清后，77.072 秒调用 `self.dictionary.lookup`，打开字典页（`queued=321 ms`），
随后返回“规”的释义文本。串口已证明这次唤醒、联网对话与查字开页链路成功，
最终版的用户确认见下一节；播放中打断等其他音频场景尚未验收。

### 收尾版（已刷入，切页与唤醒获用户确认）

`dist/firmware/ui4-20260913-Legacy-final` 已完成烧录和哈希校验。
应用 4,540,528 字节，5 MiB 应用槽余 702,352 字节（13.4%），
`xiaozhi.bin` SHA-256 为 `1A65E4D718F224D0B6B0419A07A2A2C780E6B3FC4C59036A20D39BD7ABABAB22`。
该版本仅在返回主页时使整个根对象失效，
利用全屏缓冲将小书与主体合并提交，避免独立失效区域先后出现；保留其他局部更新。
`boot-final-Legacy.log` 记录首屏 `queued=838 ms`、`flushes=1`、`pixels=921600`，
后续返回主页记录 `queued=397 ms` 且同为一次整屏提交，确认该合并路径已经运行；
字典页的一次提交总耗时为 259 ms。用户确认小书与页面同时出现，切页速度正常；
“你好小智”能够可靠唤醒并听到回复。此次页面刷新、免按键唤醒与回复播放验收通过。
设备已重新联网并回到 `idle`；播放中打断、专门的断网重连及长时间稳定性仍需单独测试。

临时音频统计已从源代码移除，AFE 引擎文件恢复到本轮代码基线；
诊断补丁保存在本机 `dist/bringup-2026-09-13/audio-diagnostics.patch`，必要时可重用。
逐页耗时诊断继续保留。

### 四变体构建回归

Legacy 字典收尾版及其他三个 Tab5 变体均已构建通过。
`regression-validation.json` 记录其余三个变体退出码均为 0，逐项日志在同一调试目录；
84 项 Python 主机测试通过。

| 变体 | 应用大小（字节） | 验证范围 |
| --- | ---: | --- |
| `m5stack-tab5` | 3,711,344 | 构建通过，未做该变体真机测试 |
| `m5stack-tab5-p4x` | 3,735,200 | 构建通过，无 P4X 实物 |
| `m5stack-tab5-han-dictionary` | 4,540,528 | 构建及本轮 Legacy 真机项目通过 |
| `m5stack-tab5-han-dictionary-p4x` | 4,625,424 | 构建通过，无 P4X 实物 |

两种字典变体的 5 MiB 应用槽分别余 702,352 和 617,456 字节，均高于 512 KiB 安全门槛。
共享 `build` 目录已恢复并完成 Legacy 字典构建，回归脚本退出码为 0。
已核对 `han16m.csv`、Legacy Rev 1.0–1.99、PPA 与 PSRAM 配置及应用容量；
恢复日志为 `build-regression-Legacy-restored.log`。
当前板上固件的串口调试应使用独立 `ui4-20260913-Legacy-final` 包内的 `xiaozhi.elf`。

### 字体、电池与和风天气版（已上板验证启动）

动态回复、状态提示和天气正文改用资源分区中的 Noto 通用字体，产品标题和插画继续使用
字典本地字体；待机状态不再在“按住说话”右侧重复显示。资源主题切换时在 LVGL 锁内
重新绑定动态字体，避免刷新资源阶段与绘制任务并发。修复版已在 COM7 启动至 `idle`，
45 秒日志内没有崩溃或异常复位。

Tab5 的 INA226（I2C `0x41`）现按官方 5 mΩ 分流电阻参数读取 2S 电池电压和电流，
充电状态读取 PI4IOE2 P6。COM7 在 USB 供电时实测 `8.141 V / +305 mA`；
电池图标的屏上显示、拔掉 USB 后的放电符号和电量曲线仍需人工验收。

天气页已接入和风天气实时天气 v1，配置从私有
`/sdcard/handict/qweather.json` 读取，API KEY 只放请求头且不会进入日志或缓存。
成功请求会保存无密钥的离线缓存。当前没有真实凭据，在线响应尚未联调。

插入的 128 GB microSD 能进入 SD 协议初始化，但 FATFS 返回挂载错误 13
（没有可识别的 FAT 卷）。Legacy 字典变体随后加入安全的 USB-C MSC 模式：切换前停止
内容读取并卸载应用侧文件系统，再把 USB-C 的 FSLS PHY 从 USB-Serial/JTAG 交给 TinyUSB。
真机切换后 COM7 按预期消失，Windows 枚举出 119.25 GB USB 可移动磁盘并确认卷为 exFAT。
用户先备份原内容，后续再格式化为 FAT32 并导入 `handict`。

本轮重新构建四个 Tab5 变体均通过：标准 Legacy `3,712,704` 字节、标准 P4X
`3,736,560` 字节、字典 P4X `4,637,136` 字节、字典 Legacy `4,552,256` 字节。
最终 Legacy 字典应用槽余 `690,624` 字节，SHA-256 为
`3E26E67D6C4E260E30AEF709C34E305FF5885B8263E6B270EDADE032B8CBCF46`。
主机测试 84 项、内容包校验、`git diff --check` 和已改 C/C++ 文件格式检查均通过。

## 启动修复版结果（显示修复前）

- 固件：`dist/firmware/ui4-20260913-Legacy-bootfix`，应用 4,533,232 字节。
- 5 MiB 应用槽余 709,648 字节（13.5%），通过 512 KiB 安全门槛。
- Bootloader、分区表、OTA 初始数据、资源和应用均完成串口烧录及哈希校验。
- ESP32-C6 SDIO 通信初始化成功，Wi-Fi AP 与扫描完成；进入 `wifi_configuring` 状态。
- ES7210、ES8388 和音频服务初始化；用户确认能听到配网提示音，麦克风仍待验证。
- 手机配网后成功获得局域网 IP，连接小智服务，进入账号激活等待；尚未验收对话。
- 配网状态启动约 19 秒时内部 SRAM 空闲 198,615 字节，历史最低 172,939 字节。
  这不是语音会话期间测量，也不代表 PSRAM 空闲量或最大连续块。
- C6 报告版本 `0.0.0`，Host 有版本不匹配警告；当前 AP/扫描成功，后续需检查联网与 RPC 稳定性，
  本轮没有刷写 C6。
- 未检测到可用 SD 卡；无卡回退成功，不能据此判定正常内容卡读取已经通过。

## 本机复现与记录

普通 PowerShell 中执行：

```powershell
cd D:\ESP32-handdict
$env:IDF_TOOLS_PATH = 'D:\Espressif\.espressif'
$env:PATH = 'D:\Espressif\.espressif\python_env\idf6.0_py3.11_env\Scripts;' + $env:PATH
.\tools\build.ps1 -HardwareRevision Legacy -IdfPath 'D:\Espressif\esp-idf'
```

上述是本次电脑的路径，不要求其他电脑使用相同目录。
构建子进程必须继承 SDK 的 PATH；在受限执行环境中曾出现 Python/CMake 看不到 Ninja 的情况。
本轮电脑曾死机并由用户强制重启，具体原因未确认。恢复后使用本地
`dist/bringup-2026-09-13/build_limited.py` 包装原构建入口，通过 CMake job pools
将编译限制为 4 个并发任务、链接限制为 1 个，减少构建资源峰值；没有修改 SDK 或生成文件。

- 原设备完整 Flash 备份：`dist/bringup-2026-09-13/original-flash-16mb.bin`，16,777,216 字节，附 SHA-256。
- 原始基线包：`dist/firmware/ui4-20260913-Legacy`，已确认不能正常启动，不作为可用固件交付。
- 第一轮内存池修复包：`dist/firmware/ui4-20260913-Legacy-psram`，仍有摄像头启动崩溃。
- 启动修复包：`dist/firmware/ui4-20260913-Legacy-bootfix`，两处启动崩溃已消除，但屏幕黑屏。
- 显示修复包：`dist/firmware/ui4-20260913-Legacy-displayfix`，ST7121 显示和基础触摸确认正常，但重绘缓慢。
- 缓存修复包：`dist/firmware/ui4-20260913-Legacy-cachefix`，用户确认切页改善至约 2–3 秒，但仍慢且不能唤醒。
- PPA 版：`dist/firmware/ui4-20260913-Legacy-aec-ppa`，错误使用参考掩码 0+2，用户仍不能唤醒且未感到进一步提速。
- 诊断版：`dist/firmware/ui4-20260913-Legacy-frame-diagnostics`，用户确认主要切页已很快，小书局部刷新仍慢；仍含错误参考掩码且不能唤醒。
- 正确参考版：`dist/firmware/ui4-20260913-Legacy-correct-reference`，日志已记录唤醒、回复、字典查询开页。
- 当前上板包：`dist/firmware/ui4-20260913-Legacy-final`，用户确认切页与小书显示正常，免按键唤醒及有声回复正常。
- 烧录、启动与主机测试日志：`dist/bringup-2026-09-13/`。
- 84 项 Python 主机测试通过；包内保存依赖锁以复现本次组件版本。

所有 `dist` 输出仅保存在本机，不提交 Git。两项启动配置仅调整 Legacy 字典变体；
显示修复由四个 Tab5 变体共用。其他面板和 P4X 没有可用实物，不能作硬件验收。

## 待验收

- 长时间稳定性、触摸四角精度及所有页面的完整交互。
- 不同距离及噪声下的唤醒、播放中打断/AEC、专门的断线重连、校时和运行内存采样。
- FAT32 内容卡、笔顺、课程表、计时恢复及提醒；USB-C 已能访问当前 128 GB exFAT 卡，待备份后格式化。
- 摄像头拍摄与画质、电池图标、拔掉 USB 后的放电状态、电量曲线和续航。
- 使用真实 API Host、API KEY 和位置联调和风天气；USB 读卡器已完成枚举，安全弹出与 FAT32 读回待验收。
