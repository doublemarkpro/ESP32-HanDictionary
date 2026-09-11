# 英语音标与主页 v2：完整生成提示词

生成方式：内置 ImageGen。两次调用均以 `tab5-home-screen.png` 为视觉参考，分别生成新页面。旧版主页保留。

## tab5-home-screen-v2.png

```text
Use case: ui-mockup. Asset type: high-fidelity M5Stack Tab5 children's desk learning assistant screen, landscape 16:9 targeting 1280x720.
Input image 1 is a STYLE REFERENCE only: preserve its warm ivory backdrop, navy rounded Chinese typography, soft green/blue/coral/lavender cards and cheerful soft illustrated icons. Full-screen UI screenshot without physical device bezel. Match this visual system closely. Large readable Chinese labels, spacious 56px or larger touch targets at target resolution. No watermark. No added slogans.
Primary request: Create the UPDATED HOME SCREEN with exactly SIX equal size feature cards in a 3-column, 2-row grid. Header height about 100px, grid from y=120 to y=565, footer voice area from y=590 to y=700. Layout must leave all six card labels fully visible.
Header: friendly open-book icon, exact title "小小助手", date "9月11日 周五", clock "18:30", Wi-Fi icon, battery icon.
Top row in order:
1 jade green card labelled "查字典", icon an open book and small writing grid with character "规".
2 soft lavender card labelled "英语音标", icon headphones surrounding large exact IPA symbol "/iː/". This icon must convey pronunciation, not alphabet learning, no ABC.
3 sky blue card labelled "课程表", friendly calendar icon.
Bottom row:
4 peach card labelled "作业计时", orange timer icon.
5 coral pink card labelled "闹钟", friendly alarm clock icon.
6 pale blue card labelled "天气", sun and cloud icon.
Bottom center blue microphone pill "按住说话". Under it helper text exactly "试试说：我想学英语音标".
No back button on home. Avoid an oversized hero card; all six equal size. Illustrations fit inside cards, do not obscure text.
```

## tab5-phonetics-screen.png

```text
Use case: ui-mockup. Asset type: high-fidelity M5Stack Tab5 children's desk learning assistant screen, landscape 16:9 targeting 1280x720.
Input image 1 is a STYLE REFERENCE only: preserve its warm ivory backdrop, navy rounded Chinese typography, soft green/blue/coral/lavender cards and cheerful soft illustrated icons. Full-screen UI screenshot without physical device bezel. Match this visual system closely. Large readable Chinese labels, spacious 56px or larger touch targets at target resolution. No watermark. No added slogans.
Primary request: Create ENGLISH IPA PRONUNCIATION LEARNING detail screen for a child. Header large circular back arrow and exact title "英语音标"; right header small neutral chip "英式发音", small chip "离线点读". Use actual phonetic Unicode symbols carefully.
Below header three large tabs: "单元音" selected lavender, "双元音", "辅音".
Body is two columns: left 39%, right 61%. Left rounded lavender-white panel has a 3 columns by 2 rows selectable symbol grid: first row "/iː/" selected lavender, "/ɪ/", "/e/"; second row "/æ/", "/ʌ/", "/ɑː/". Below these 6 cards navigation "上一页", "1 / 2", "下一页". These are six sounds on page 1, not the entire inventory.
Right main white card presents giant "/iː/" centered near top, label "听一听，跟着读". One wide violet speaker button "听示范". Under this button a row of three friendly picture word tiles: a smiling sheep labelled "sheep", a green tree labelled "tree", a cup of tea labelled "tea"; small speaker icon in each, no IPA transcription under the words. Bottom of right card two clear large buttons: blue microphone "录音跟读", pale green speaker "听听我的录音".
Bottom strip contains a short gentle tip "先听示范，再自己试一试". No scores, no grading stars, no invented pronunciation evaluations, no mouth or tongue anatomy drawing. No ABC icon, no numbered claim of 44 or 48 total sounds. Do not confuse symbol iː with i: or l. All text crisp and exact.
```

视觉检查：六入口与全部按钮文案齐全。生成图的 IPA 字形存在字体近似，正式页面应使用真实 IPA 字体和文本数据；图片仅用于 UI 布局评审。
