# UI 视觉稿生成提示词

英语音标学习页与六入口主页 v2 的完整提示词见 [PROMPTS-phonetics.md](PROMPTS-phonetics.md)。

所有图片均使用内置 ImageGen 生成，类型为 `ui-mockup`，目标是 M5Stack Tab5 的 1280×720 横屏嵌入式产品界面。通用约束如下：

```text
Style/medium: realistic high-fidelity embedded product UI, not concept art
Scene/backdrop: full-screen UI only, warm ivory background, rounded cards, subtle shadows
Color palette: warm ivory, jade green, sky blue, coral orange, soft lavender, dark navy text
Composition/framing: straight-on landscape screen capture, clear hierarchy, generous spacing, large touch targets
Constraints: legible correctly spelled Chinese text; no device bezel; no logos; no watermark; no industrial dashboard styling
```

各页面在通用约束上的最终内容提示如下。

## 主界面

```text
Design a child-friendly Chinese desktop learning assistant home screen. Header: open-book mascot, "小小助手", "9月11日 周五", "18:30", Wi-Fi and battery. Five app cards: large "查字典" card with the character "规"; four cards "课程表", "作业计时", "闹钟", "天气". Bottom voice action "按住说话" and helper "试试说：规矩的规怎么写？".
```

## 联网设置

```text
Design a child-friendly, parent-oriented Wi-Fi page. Fixed top-left back arrow and "联网设置". Three steps: "1 选择网络", "2 输入密码", "3 完成". Networks: "Home-WiFi", "ChinaNet-5G", "书房网络" and "重新搜索". Password card: "Wi-Fi 密码", masked field, "记住此网络", "连接", "用手机帮我联网". Footer: "联网设置需要家长确认".
```

## 课程表

```text
Design a weekly class timetable page. Back arrow, "课程表", "9月11日 周五", "本周". Columns 周一至周五, rows 第1节至第5节, subject pills 语文、数学、英语、科学、美术、体育; highlight Friday. Side card "明天要带" with "美术本" and "跳绳". Voice shortcut "问问明天上什么课".
```

## 作业计时

```text
Design a subject homework timer and daily statistics page. Back arrow and "作业计时". Subject chips 语文、数学、英语 with 数学 selected; timer "00:28:36", progress ring, "暂停", "完成本科", "专心完成这一科". Today list: 语文35分钟已完成, 数学28分钟进行中, 英语未开始. Weekly bar chart, "今天共 63 分钟", "调整记录".
```

## 闹钟

```text
Design a child-friendly alarms page. Back arrow, "闹钟", "+ 新建闹钟". Next alarm "06:45", "上学起床", "周一至周五". Other enabled alarms: "20:30 整理书包 每天" and "21:00 准备睡觉 周日至周四". Footer "明天早上 6:45 会叫醒你" and "试听铃声".
```

## 天气

```text
Design a child-friendly weather page. Back arrow and "天气". Main card: "上海", "26°", "多云", "体感 27°", "今天适合短袖，带一把伞". Forecast cells: "18时 26°", "20时 24°", "明天 22°", "后天 25°". Cards: "空气质量 优", "降雨 40%", "日落 18:07". Reminder "明天体育课，记得带水杯" and voice shortcut "问问明天会下雨吗".
```
