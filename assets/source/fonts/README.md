# Rounded UI heading font

`ResourceHanRoundedCN-Heavy.ttf` is the unmodified CN Heavy face from the author's
[v0.990 CN release](https://github.com/CyanoHao/Resource-Han-Rounded/releases/download/v0.990/RHR-CN-0.990.7z).
Project: https://github.com/CyanoHao/Resource-Han-Rounded

SHA-256: `DFC757D9C5AE597CAC15B12A51A1CEE07AF500CC8CF7439B243E1BB94B2B1F83`

Copyright 2018–2019 Cyano Hao; Copyright 2014, 2015, 2018 Adobe.
License: SIL Open Font License 1.1, full notice in `assets/licenses/ResourceHanRounded-LICENSE.txt`.

The AI concept image has no recoverable font metadata. This face was chosen by visually comparing
its rounded heavy Chinese glyphs with the accepted concept, not by claiming an exact font identification.
Used for decorative UI headings only, not as a handwriting/stroke-order teaching font.
Normal builds consume committed LVGL C subsets; do not copy the whole TTF into firmware or SD.

## Screen-friendly dictionary candidate font

`LXGWWenKaiGBScreen.ttf` is the unmodified GB screen-reading face from the official
[LXGW WenKai Screen v1.522 release](https://github.com/lxgw/LxgwWenKai-Screen/releases/tag/v1.522).
It supplies the large keyboard-lookup candidates, where its Medium-weight Kai forms remain legible
for complex and uncommon Chinese characters.

SHA-256: `23EC023913E1851925EB94462C4B0CCD1D78BB89533745AAA8CC682CCD339DC0`

Copyright 2021–2026 LXGW and contributors. License: SIL Open Font License 1.1; the upstream notice
is preserved in `LXGW-WenKai-Screen-OFL.txt`.

## Antialiased dictionary candidate font

`NotoSansSC-Medium.ttf` is a static 500-weight instance generated from the official
[Noto Sans SC variable TrueType font](https://github.com/google/fonts/tree/main/ofl/notosanssc).
The device reads it from SD and lets
LVGL rasterize only the visible 56 px candidates into a bounded grayscale glyph cache. This is the
preferred candidate renderer because it avoids the jagged black/white edges inherent to a 1 bpp
full-CJK bitmap while retaining uncommon-character coverage.

SHA-256: `A0F7BCCC73BFEF1B0C6382F11AD43749EE11B2CB4C9ACAB39957950C50755AC8`

Copyright 2014–2021 Adobe. License: SIL Open Font License 1.1; the upstream notice is preserved in
`Noto-CJK-OFL.txt`.
