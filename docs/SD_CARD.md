# SD 内容卡

16 GB 或 32 GB 的 FAT32 卡足够本项目使用。第一次复制后请安全弹出、重启设备；固件只在
启动时挂载，不支持热插拔，也不会自动格式化卡。挂载失败时主页仍可启动，并降级到内置示例。

## 目录

将完整的 `handict` 文件夹复制到 SD 卡根目录：

```text
SD:/handict/
  manifest.json
  timetable.json
  qweather.json
  weather.json
  dictionary/
    index.bin
    data.bin
    pinyin.idx
    strokes.idx
    strokes.dat
    font-28-1.bin
  phonetics/
  ui/graphics/
  licenses/
```

字典不再保存或显示纸书页码、版次和 ISBN。释义来自社区数据并明确标注为离线学习资料，
不是商务印书馆纸书内容的电子复刻。

## 生成完整字典和笔顺

先转换 Chinese-Mandarin-Dictionaries 的 TAB 压缩包，再加入 Hanzi Writer Data 的矢量笔顺：

```powershell
python tools/import_xinhua.py `
  --input "D:\Downloads\新华字典 Xīnhuá Zìdiǎn.tab.zip" `
  --output dist/sdcard-xinhua

python tools/import_strokes.py `
  --source "D:\Downloads\hanzi-writer-data-2.0.1.tgz" `
  --pack dist/sdcard-xinhua/handict `
  --order "D:\src\cnchar\src\cnchar\plugin\order\dict\stroke-order-jian.json" `
  --order-license "D:\src\cnchar\LICENSE"

python tools/content_pack.py validate dist/sdcard-xinhua/handict
```

设备按 Unicode 固定槽位读取一条释义或一个汉字的矢量路径，不扫描全库，也不把全库装进
RAM。笔顺画面由 LVGL 实时生成，一字只有一份路径数据，不再保存第 1 笔到第 N 笔的累计 PNG。
格式、数据来源和许可证边界见 [字典导入说明](DICTIONARY_IMPORT.md)。

开发用小内容包仍可这样生成：

```powershell
python tools/content_pack.py prepare --output dist/sdcard-starter
python tools/content_pack.py validate dist/sdcard-starter/handict
```

## 课程表

`timetable.json` 的 `days` 从周一开始，每天最多八节；当前页面可将七节课程完整显示在一屏。
直接复制 [演示模板](../content/sdcard/handict/timetable.json) 到
`SD:/handict/timetable.json`，按孩子实际课程修改并保存为 UTF-8。修改后重启设备重新载入。

课程表可独立使用，不要求同目录存在字典 `manifest.json`。可选的 `supplies` 用同样的七天数组
记录当天用品；每项最多 32 UTF-8 字节，整个文件最多 8192 字节。完整说明见
[课程表 UI4](ui/TIMETABLE_UI4.md)。

## 和风天气

复制 `qweather.example.json` 为 `qweather.json`，填写项目专属 API Host、API KEY、城市和经纬度。
成功请求会更新不含密钥的 `weather.json`，断网时显示带时间戳的缓存。完整步骤见
[和风天气配置](QWEATHER.md)。

## USB 读卡器模式

Legacy 字典变体支持 USB MSC。点主页右上角 Wi-Fi 图标进入“联网设置”，再点“USB 读卡器”。
固件会停止内容读取、卸载文件系统，并把 USB-C 切给 microSD；此时 COM 口消失属于正常现象。
复制完成后先在电脑安全弹出磁盘，再重启 Tab5。SD 使用 M5Stack 官方 SPI 引脚：CS42、SCK43、
MOSI44、MISO39；Wi-Fi 所用 P4/C6 SDIO 配置不变。
