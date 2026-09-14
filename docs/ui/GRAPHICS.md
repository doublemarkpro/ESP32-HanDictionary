# 图形素材包 v1：给后续 LVGL 换肤使用

后续更新：主页已经在 UI3 中接入精选优化素材与真实圆体字体，见 [UI3](HOME_UI3.md)。
本页记录完整 v1 素材包的准备状态，未被整体编入固件。

日期：2026-09-11。**素材准备完成，不等于当前固件已经换肤。**
本轮不改变实际主页布局、查字逻辑、天气服务或配网方式。

## 看图

- [首页素材排版预览](graphics/home-composition.png)：用本包素材组合，不是真机或 LVGL 页面截图。
- [首页与学习插画](graphics/illustrations.png)
- [22 种天气状态](graphics/weather.png)
- [50 个操作/设备状态图标](graphics/controls.png)
- [背景、卡片与叶子装饰](graphics/backgrounds.png)
- [实际 LVGL 9 解码和透明合成截图](graphics/lvgl/graphics-lvgl.png)

## 清单与尺寸

共 **90 项素材、252 张 PNG**。所有文件为无损 RGBA8 PNG、straight alpha，
无需色键；SVG 不要求在设备上解析。除奶油色全屏底图外均带透明区域。

| 类别 | 项数 | PNG 尺寸（宽；高按原比例） | 用途 |
| --- | ---: | --- | --- |
| 首页入口插画 | 6 | 192 / 256 / 384 | 书本、耳机、课表、番茄计时、闹钟、天气 |
| 学习装饰插画 | 3 | 320 / 480 / 640 | 写作业的孩子、发芽书堆、叶子 |
| 天气状态 | 22 | 96 / 192 / 320，正方形 | 预报小图、当前天气大图 |
| 操作与设备状态 | 50 | 32 / 48 / 64，正方形 | 导航、播放、课程、联网、电池等 |
| 矢量叶子角饰 | 2 | 320×220 | 轻量左右角装饰 |
| 卡片底图 | 6 | 396×232 | 薄荷、薰衣草、天蓝、桃色、粉色、浅黄 |
| 全屏底图 | 1 | 1280×720 | 奶油底色与淡黄色底部波浪 |

插画保持生成原图比例，不强行拉伸。需要统一点击区域时由 LVGL 容器负责。
一个例子：`home-book/256.png` 是 256×171，不是 256×256。

机器可读清单：[manifest.json](../../content/sdcard/handict/ui/graphics/manifest.json)。
每项包含 ID、来源、原图校验值（SVG 统一 LF 换行后校验，兼容 Git 自动换行）；每个尺寸含路径、宽高、压缩字节数、RGBA
解码字节数和 SHA-256。路径相对 `SD:/handict/ui/graphics/`。
这个独立图形清单暂由开发工具验证，尚未加入固件内容加载器。

## 天气与设备状态约定

天气 ID 统一前缀 `weather-`：

```text
clear-day            clear-night           partly-cloudy-day
partly-cloudy-night  cloudy                overcast
light-rain           rain                  heavy-rain
showers              thunderstorm          snow
sleet                hail                  fog
wind                 sand                  hot
cold                 sunrise               sunset
unknown
```

天气服务还未接入。以上是本项目语义 ID，不是某家 API 数字天气码；以后服务层
需要明确映射。没收到、无法识别或不支持的状态使用 `weather-unknown`，
并保留“未获取/缓存/更新时间”文本，不能默认显示晴天。

设备 ID 包括 `control-wifi-0/1/2/3/off` 和
`control-battery-empty/low/half/full/charging/unknown`。
没有真实电池读数时用 unknown，不把漂亮的满电图标当成真实数据。
返回为 `control-back`，首页为 `control-home`；其他 ID 见清单和图集。

## LVGL 9 调用

小图标可选 C 包在 `assets/graphics/lvgl/`。把 `.c` 加入**启用学习界面的**
构建源文件，并添加该目录为 include path；依赖 `LV_USE_LODEPNG=1`。
本轮仅原生测试目标链接它，`main/CMakeLists.txt` 没有加入，因此不会影响
其他板卡或无显示/OLED 固件。

```cpp
#include "han_graphics_small.h"

auto image = lv_image_create(parent);
const auto* source = han_graphics_find("weather-rain");
if (!source) source = han_graphics_find("weather-unknown");
lv_image_set_src(image, source);
```

查找函数返回 `const lv_image_dsc_t*`，描述符和 PNG 数据均为静态生命周期。
未知 ID 和空指针返回 NULL；通过 `han_graphics_count/at` 可枚举清单。
这个包包含全部 22 个 96 px 天气图和 50 个 48 px 小图标：PNG 载荷
**92,335 字节，约 90.17 KiB**；还需少量描述符/索引/函数空间，且解码占 RAM。
不是整个包只有这么多运行内存。

大插画使用 SD PNG。后续加载方式沿用后台读取、UI 线程提交的原则：

1. 工作线程有大小上限地读取可信清单中的 PNG；不接受任意网络路径。
2. 切回显示线程，在 UI 锁内设置 `LV_COLOR_FORMAT_RAW_ALPHA` 描述符。
3. 图片对象使用期间，描述符和 PNG buffer 都必须持续存活。
4. 换图/离页时先解除引用、丢弃对应图片缓存，再释放旧 buffer；丢弃过期读取结果。

目前未注册通用 `S:` LVGL 文件系统驱动，因此不要直接假设
`lv_image_set_src(obj, "S:/handict/...")` 已经可用。
当前 300×300 笔顺校验器也不能直接拿来校验任意插画尺寸。

## 存储与流畅性

全部设备 PNG **3,047,419 字节，约 2.91 MiB**，随 SD 卡内容模板分发。
原始插画约 8.47 MiB 只放 Git 源资源目录，不放设备。
这次没有把图形包塞进应用或小智资源分区；现有烧录包不因此改变。

- 优先选接近实际显示尺寸的 PNG，避免在 ESP32 上连续缩放大原图。
- 全屏 1280×720 RGBA 解码需 **3.52 MiB**，还不含显示双缓冲、缓存与解码临时内存。
- 六张 396×232 卡片同时解码约 **2.10 MiB**。量产界面宜直接用 LVGL 圆角渐变，
  加小装饰；底图导出保留作为对照和可选方案，不必全部加载。
- 动态文字、字形、笔顺、温度、电量、时间仍使用控件，不能烘焙到背景中。
- 首次加载/翻页耗时、PSRAM 最大连续块、真机帧率还需到货后实测。

## 在另一台电脑使用

仓库中已提交 PNG/C/SVG，普通开发不需要重新调用 AI，也不需要密钥。
直接准备 SD 卡（输出目录必须不存在）：

```powershell
python tools/content_pack.py prepare --output dist/my-graphics-card
python tools/content_pack.py validate dist/my-graphics-card/handict
```

将 `handict` 整个目录复制到卡根目录。注意：Python 内容校验器负责现有字典内容；
本图形清单/PNG 的专用校验是下方 Node 测试。

重新导出（已有原图，无需再次生成）：

```powershell
npm ci --prefix tools/ui-assets
node tools/ui-assets/build-graphics.cjs
node --test tools/ui-assets/graphics.test.cjs tools/ui-assets/resources.test.cjs
.\tools\preview-ui.ps1 -Graphics
```

导出器只写固定的图形输出目录，不覆盖原来的 `assets/ui` 轻量图标、字体、
概念图和 `docs/ui/rendered` 页面截图。生成图集中的中文使用项目已有思源字体
转轮廓；重新导出需先按项目环境说明完成 ESP-IDF 配置/构建并下载 managed_components。
原生 LVGL 验证另需 Visual Studio C++ 工具；路径由脚本自动发现，无公司绝对路径。

## 本轮验证

- 6 项 Node 测试通过：90 项 ID、252 PNG 尺寸/透明度/校验值、未知天气兜底、
  C 包字节一致性与 128 KiB 预算，以及原音标/笔顺资源测试。
- 本机 LVGL 9 实际解码全部 252 个 SD PNG；72 个 C 描述符逐一解码、渲染，
  检查每个图标格子有像素，并查看输出截图。
- 75 项 Python 回归测试通过，SD 内容打包、内容校验通过。
- 容量脚本复查现有 Legacy 构建：应用 4,347,072 字节，剩 895,808 字节；
  小智资源分区剩 3,174,487 字节。本轮无固件源代码变更，未重新刷机/编译双版本。

来源与提示词：[素材说明](../../assets/graphics/README.md)。
后续工作：把插画和控件皮肤接入真实页面，再做真机显示、SD 按需加载及性能验收。
