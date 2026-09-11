# 音标音频

本示例包不包含未经核对的教学发音。准备经过校对且允许使用的英式录音后，按下列结构存放：

```
en-GB/i-long/sound.ogg
en-GB/i-long/sheep.ogg
en-GB/i-long/tree.ogg
en-GB/i-long/tea.ogg
```

其他 sound ID 和例词见 `main/han_dictionary/phonetics.h`。格式要求：Ogg/Opus、单声道、24 kHz 输入采样率、60 ms 包、建议每段不超过 8 秒、每个文件上限 256 KiB。可用本机 ffmpeg 将已有录音转换：

```
ffmpeg -i source.wav -ac 1 -ar 24000 -c:a libopus -frame_duration 60 -b:a 32k sound.ogg
```

音标符号不能直接交给普通 TTS 字符串朗读代替单音示范。离线点读不依赖小智云端；语义对话仍需要网络。缺少文件时固件显示导入提示。
