# 社区字典离线索引

`tools/import_xinhua.py` 将 Chinese-Mandarin-Dictionaries 项目中的新华字典 TAB/TSV 数据清洗为
Tab5 可直接查询的 SD 卡内容包。数据清洗只使用 Python 标准库；字体生成需要本机安装 Git、
Node.js 和 npm，首次运行会取得固定版本的 CBIN 转换器。设备查询不需要联网，也不会把整本
字典载入内存。

## 生成内容包

先从 [Chinese-Mandarin-Dictionaries](https://github.com/lxs602/Chinese-Mandarin-Dictionaries)
取得 `新华字典 Xīnhuá Zìdiǎn.tab.zip`，然后在仓库根目录运行：

```powershell
python tools/import_xinhua.py `
  --input "D:\Downloads\新华字典 Xīnhuá Zìdiǎn.tab.zip" `
  --output dist/sdcard-xinhua `
  --radicals "D:\src\cnchar\src\cnchar\plugin\radical\dict\radicals.json" `
  --structures "D:\src\cnchar\src\cnchar\plugin\radical\dict\struct.json" `
  --radical-license "D:\src\cnchar\LICENSE"
```

输出目录必须不存在，避免误删已有文件。转换器会复制完整的基础内容包，清除 HTML、输入法编码
和详细长篇引文，保留拼音、基本解释以及可可靠提取的组词；传入 cnchar 偏旁插件数据后，还会
补齐部首与汉字结构（例如“嗝”显示“部首 口、左右结构”），并完成索引、记录边界、UTF-8、
JSON 和 CRC32 校验。开发时可加 `--limit 100` 只转换前 100 个可用条目。

把生成的 `dist/sdcard-xinhua/handict` 整个复制到 SD 卡根目录，最终路径如下：

```text
SD:/handict/
  manifest.json
  dictionary/index.bin
  dictionary/data.bin
  dictionary/pinyin.idx
  dictionary/strokes.idx
  dictionary/strokes.dat
  dictionary/font-28-2.bin
  dictionary/SourceHanSansSC-Normal.otf
  dictionary/entries/89C4.json
  ...
```

安全弹出 SD 卡并重启设备。点“字库状态”应显示“SD 索引字典已加载（…字）”。单独的
`dictionary/entries/XXXX.json` 优先级更高，可用来覆盖社区索引中对应的字。

## 索引格式

`index.bin` 使用 `handict-index-v1`：32 字节小端头部后是 U+4E00—U+9FFF 的固定槽位，
每槽包含 `data.bin` 中记录的偏移和长度。查一个字只需读取一个 8 字节槽位和一条不超过
4096 字节的 JSON 记录；固件不扫描 TSV，也不常驻约 168 KB 的索引。约 2.2 MB 的
`SourceHanSansSC-Normal.otf` 由 LVGL 按控件实际需要生成 28、40 和 64 px 的抗锯齿字形：
释义使用 28 px，拼音候选和详情标题使用 40 px，主字使用 64 px。这样既覆盖生僻字，也避免
把小字号点阵等比放大造成锯齿。`font-28-2.bin` 作为旧内容包和字体初始化失败时的兼容后备；
旧版 `font-28-1.bin` 仍可读取。

`pinyin.idx` 使用 `handict-pinyin-v1`，转换时将带声调拼音归一化为小写拉丁字母，并把同音字
候选压缩为连续 UTF-8 数据。设备输入 `han` 后只二分读取一个拼音目录项和最多 20 个候选字，
点选候选字后再读取该字的释义与矢量笔顺；不会扫描 2 万条 JSON。常用字会排在同音候选前面。

## 加入矢量笔顺

字典条目转换完成后，再把固定版本 `hanzi-writer-data@2.0.1` 的路径数据写成设备索引；
可选传入 cnchar 的简体笔顺表，为可匹配的笔画补充“横、竖、撇”等名称：

```powershell
python tools/import_strokes.py `
  --source "D:\Downloads\hanzi-writer-data-2.0.1.tgz" `
  --pack dist/sdcard-xinhua/handict `
  --order "D:\src\cnchar\src\cnchar\plugin\order\dict\stroke-order-jian.json" `
  --order-license "D:\src\cnchar\LICENSE"
```

输出只有固定槽位索引 `strokes.idx` 和路径数据 `strokes.dat`。设备一次定位一个汉字，读取一次后
在 300×300 画布上实时着色，不再为一个字保存 N 张累计 PNG。当前全量包收录 9,534 个汉字的
矢量笔顺，其中 6,807 个可从 cnchar 匹配笔画名称；两文件约 15.7 MiB。转换器会移除目标包中
旧的 `dictionary/strokes/XXXX/01.png` 演示帧目录。

## 数据边界

该社区数据没有经本项目核验为商务印书馆《新华字典》第 12 版，因此：

- UI 明确标为“社区离线字典”，不显示或猜测第 12 版纸书页码；
- `hanzi-writer-data` 不含可靠字形结构字段；未命中矢量库时界面显示“笔顺资料未收录”；
- 上游 README 对这一数据集只写了可自由取得，未提供清晰的软件/内容许可。公开分发或商业使用前
  应另行确认授权，并保留来源说明。

笔顺几何来自 Hanzi Writer Data（Make Me A Hanzi / Arphic Public License）；cnchar 只用于补充
笔画名称，其 MIT 许可证会随包复制到 `licenses/CNCHAR-MIT.txt`。字典释义、笔顺几何和软件代码
是三个独立来源，转换工具不会把其中任何一个宣称为纸质《新华字典》的页码内容。
