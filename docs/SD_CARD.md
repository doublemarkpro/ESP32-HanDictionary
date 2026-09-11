# SD 卡与新华字典第 12 版

已确认纸书：商务印书馆《新华字典》第 12 版单色本，ISBN **978-7-100-16807-6**，机器格式 `9787100168076`。

## 放哪些资源

| 资源 | 位置 | 本轮状态 |
| --- | --- | --- |
| 主界面、基础 UI 字体、IPA 字体、六个图标 | 内部固件 | 已包含，无卡可用 |
| 规字示例与八个笔画显示帧 | 内部固件 | 已包含；释义是开发示例，不是新华字典原文 |
| 正式字条、精确页码 | SD 卡 | 导入接口已完成，完整第12版数据未提供 |
| 音标和例词发音 | SD 卡 | 播放接口已完成，教学录音待导入 |
| 课程表、天气缓存 | SD 卡 | JSON 模板与读取已完成，默认空白 |
| 作业时长、每日提醒设置 | NVS | 暂停/完成时保存；计时中约每分钟检查点 |
| 全量笔顺 | SD 内容包 | 已支持按需读取帧；当前资源有规、矩，非全量 |
| 完整字库字体、录音历史 | 后续 SD 内容包 | 尚未接入 |
| 新版插画/天气/操作图标/背景 | SD `ui/graphics/` | 90 项/252 PNG，约 2.91 MiB；全包未整体编入，主页使用独立精简版 |

UI3 更新：主页精选图片已另行优化并嵌入固件（约 167 KiB PNG 载荷），因此新主页
无需 SD 卡即可显示；上表的完整图形包仍保留给其他页面后续使用。见 [UI3](ui/HOME_UI3.md)。

16 GB 或 32 GB 的 FAT32 卡可作为本项目内容卡；实际所需容量由最终字库和音频决定。当前示例包很小。第一次复制文件后关机插卡再启动；本轮只在启动挂载，不支持热插拔。不自动格式化卡，挂载失败时使用内置示例。

## 准备示例内容卡

在仓库根目录运行（Python 标准库，不需要下载字体工具）：

```powershell
python tools/content_pack.py prepare --output dist/sdcard-starter
python tools/content_pack.py validate dist/sdcard-starter/handict
```

将生成的 **handict 文件夹**复制到 SD 卡根目录：

```text
SD:/handict/
  manifest.json
  dictionary/entries/89C4.json
  phonetics/en-GB/i-long/sound.ogg
  phonetics/en-GB/i-long/sheep.ogg
  phonetics/en-GB/i-long/tree.ogg
  phonetics/en-GB/i-long/tea.ogg
  timetable.json
  weather.json
```

音频目录为需要自行补齐的路径说明；仓库不带这些发音文件。音频格式和转换方式见 [音频说明](../content/sdcard/handict/phonetics/README.md)。基本点读离线可用，小智语义识别与对话仍需要联网。

上述 Python 命令现在会同时复制已经生成并提交的 44 张音标卡、音频路径清单和规/矩两字笔顺帧。需要重新生成时，运行 `node tools/ui-assets/prepare-resources.cjs --output dist/sdcard-ui2`（需先 `npm ci --prefix tools/ui-assets`）。帧位于 `dictionary/strokes/77E9/01.png` 等路径，必须为 300×300 RGBA8 PNG、每张不超过 128 KiB，张数与条目笔画数一致；生成器与校验器会检查。固件在工作线程读取当前帧，不把全套笔顺装入 RAM。

现在还会复制 `ui/graphics/` 图形包及其 `manifest.json`。这些不是笔顺帧，尺寸规则不同；
图形专用校验、重新导出和 LVGL 示例见 [图形素材说明](ui/GRAPHICS.md)。图形原图不复制到 SD。

## 页码不会被猜测

商务印书馆有[官方第 12 版纸书与 App](https://www.cp.com.cn/xinhua12/)，本轮检索未找到可直接集成的公开完整离线数据包。当前固定目标 ISBN，但不据此声称已经取得整本内容。

每条数据有 source，释义来源与纸书页码关联分开记录。页码只有同时满足版本、ISBN、正整数页码和 `verified: true` 才能在固件中显示。不知道的页码保持 `null`，UI 显示“页码待核对”，MCP 返回 null。其他版本的页码不使用。

```json
"reference": {
  "edition": "新华字典第12版",
  "isbn": "9787100168076",
  "page": null,
  "verified": false
}
```

具备可使用的结构化字条后，按 `content/sdcard/handict/dictionary/entries/89C4.json` 格式整理成 JSON 数组，运行：

```powershell
python tools/content_pack.py prepare --entries local-dictionary.json --output dist/sdcard-imported
```

导入工具校验字条、笔画数、ISBN、页码及大小限制，不覆盖已有输出目录。JSON 文件按汉字 Unicode 编码命名，规为 `89C4.json`，矩为 `77E9.json`。本轮支持 BMP 基本汉字查询；完整任意字形字体、完整笔顺内容还需扩展。第二轮加入了 SD 帧读取与矩字开发示例，示例的未知页码仍为 null。

## 课程与天气

`timetable.json` 的 days 为周一至周五五个数组，每天最多八节，每项为课程名称。默认五个空数组，不虚构课程。

`weather.json` 提供 city、summary、updated_at；全部默认空。显示缓存时保留“缓存”和更新时间标记，在线天气 API 尚未接入。

## 硬件

SD 使用 [M5Stack 官方 SPI 引脚](https://docs.m5stack.com/en/arduino/m5tab5/microsd)：CS42、SCK43、MOSI44、MISO39。Wi-Fi 所用的 P4/C6 SDIO 配置保持原值。缺卡/坏卡只降级，不中止主页启动。
