# 奶油色学习助手图形资源 v1

按 `docs/ui/tab5-*-screen*.png` 已确认的概念图拆分制作；不是把整张概念图压成背景。
插画为本轮内置 imagegen 生成的独立透明素材，小图标/卡片/背景为项目原创矢量。
原始生成提示词和参考图路径保存在 [prompts.json](prompts.json)。

入口文档：[尺寸、资源 ID、LVGL 调用与验收](../../docs/ui/GRAPHICS.md)。

- `source/raster/`：9 张带 alpha 的生成原图，保留原始分辨率，约 8.47 MiB；不要复制到 SD。
- `source/vector/`：81 个 SVG 导出源；编辑 `tools/ui-assets/graphics-vectors.cjs` 后重新生成。
- `lvgl/`：72 个小图标的可选 LVGL 9 C 资源包；当前没有加入固件。
- `../../content/sdcard/handict/ui/graphics/`：252 张设备用 PNG 和机器可读清单。
- `../../docs/ui/graphics/`：分组预览、首页排版预览和真正的 LVGL 图标渲染截图。

这些素材不包含新华字典原文、纸书页码、商标或教学录音。生成插画不作为
字形、笔顺、音标或实时钟表读数的教学依据；例如闹钟指针只是入口装饰。
音标耳机中的卡片刻意留白，实际 IPA 必须由字体绘制。

原创矢量和生成脚本按仓库 MIT 许可证提供。AI 插画按生成结果原样随项目提供，
记录来源，不额外声称独占性或对第三方权利作担保。预览中文字转为思源黑体轮廓，
字体许可见 `assets/licenses/SourceHanSansSC-LICENSE.txt`；设备 PNG 本身不含文字。
SD 分发保留 `handict/licenses/graphics-provenance.md`。
