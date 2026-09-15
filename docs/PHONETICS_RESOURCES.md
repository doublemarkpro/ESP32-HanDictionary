# 英语音标资源选择

## 当前结论

设备继续使用传统教学用 44 音分类与 132 个例词。界面不再显示“英式发音”和“离线点读”标签；这只是去掉不必要的界面文案，不代表可以把来源不明的录音当成标准示范音。

音频文件放在 SD 卡 `handict/phonetics/en-GB/<sound-id>/`，音标示范名为 `sound.ogg`，三个例词分别以英文单词命名。路径中的 `en-GB` 为兼容既有内容包而保留，不再作为界面口音标签。

## GitHub 项目评估

- `alfredang/phonicsapp`：有完整音素课程、例词及 Wiktionary/Wikimedia 真人单词录音，但仓库本身没有 LICENSE，且没有为每个音频文件提供足够细的作者与许可清单。可参考课程组织，不直接复制文件。
- `xiaozhah/phoneme_audio`：单音与例词文件结构非常接近本项目需要，但 README 明确说明音频来自 Oxford Dictionary，仓库也没有可用于再分发这些录音的许可，因此不导入。
- `suragch/aePronunciation`：GPLv3，包含作者重新录制的美式单音。当前将其作为独立的 SD 第三方内容使用，并随包保留 GPLv3 许可；不改变固件源代码的许可。
- `open-dict-data/ipa-dict`：可用于核对例词 IPA，US 数据为 MIT，UK 数据来源为 GPLv3；它不提供音频。

当前 44 个 `sound.ogg` 来自上述开源录音并按传统卡片做了最接近映射；132 个例词由本机 Microsoft Zira Desktop 离线合成，未通过扬声器播放。所有文件统一响度并转换为 24 kHz 单声道 Ogg/Opus。生成工具为 `tools/ui-assets/phonetics-audio-pack.ps1`，详细来源记录随内容包保存。

## 例词插图

例词卡使用 Microsoft Fluent Emoji 的 3D 图形，许可为 MIT。102 个唯一例词各生成一张 142×102 的透明 PNG，放在：

```text
handict/ui/graphics/phonetics-page/words/<word>.png
```

映射、固定上游版本与生成脚本见 `tools/ui-assets/phonetics-page-assets.cjs`。图片只进入 SD 内容包，不占用固件 Flash；许可和来源记录随图片目录一同分发。
