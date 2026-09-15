# 音标音频

本内容包包含 44 个音标示范与 132 个例词点读位置，按下列结构存放：

```
en-GB/i-long/sound.ogg
en-GB/i-long/sheep.ogg
en-GB/i-long/tree.ogg
en-GB/i-long/tea.ogg
```

其他 sound ID 和例词见 `main/han_dictionary/phonetics.h`。格式要求：Ogg/Opus、单声道、24 kHz 输入采样率、60 ms 包、建议每段不超过 8 秒、每个文件上限 256 KiB。可用本机 ffmpeg 将已有录音转换：

课程沿用传统 44 音卡（12 单元音、8 双元音、24 辅音）。这不是所有英语口音的统一音位数量；`ʊə` 在现代口音中常有变化，教学符号 `r` 沿用传统标注。`catalog.json` 列出 44 个音标与 132 个例词位置的完整路径（176 个文件）。录音来源、许可和生成方式见 `AUDIO_SOURCES.json` 与随包附带的 GPLv3 许可文件。

```
ffmpeg -i source.wav -ac 1 -ar 24000 -c:a libopus -frame_duration 60 -b:a 32k sound.ogg
```

离线点读不依赖小智云端；语义对话仍需要网络。缺少文件时固件会显示明确的导入提示。
