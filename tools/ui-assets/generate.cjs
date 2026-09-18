// Run npm ci in this directory, then node generate.cjs. Normal firmware builds use committed C files.
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const sharp = require('sharp');
const root = path.resolve(__dirname, '../..');
const out = path.join(root, 'main/han_dictionary/assets');
const source = path.join(root, 'assets/source');
fs.mkdirSync(out, {recursive:true}); fs.mkdirSync(source, {recursive:true});
const fonts = path.join(root, 'managed_components/lvgl__lvgl/scripts/built_in_font');
const roundedHeavy = path.join(root, 'assets/source/fonts/ResourceHanRoundedCN-Heavy.ttf');
const fontTool = require.resolve('lv_font_conv/lv_font_conv.js');
// The full dictionary fonts live on microSD and are loaded dynamically. Keep only the static
// device UI and localized status strings in the firmware fallback fonts; including dictionary
// entries or simulator-only text here duplicates hundreds of KiB in every OTA application.
const embeddedStrings = ['han_display.cc', 'han_display.h']
  .map(f=>fs.readFileSync(path.join(root, 'main/han_dictionary', f), 'utf8')).join('') +
  fs.readFileSync(path.join(root, 'main/assets/locales/zh-CN/language.json'), 'utf8');
const embeddedChinese = [...new Set(embeddedStrings
  .match(/[\u2000-\u206f\u3000-\u9fff\uff00-\uffef]/g))].join('');
// han_font_40 is used directly only by these large headings. Dictionary content and its
// uncommon glyphs use the SD font, while Latin, pinyin, IPA, and numbers come from the range
// below. Keeping the large bitmap font focused saves considerably more space per glyph.
const largeFallbackStrings = [
  '小智对话', '还没有天气', 'USB 读卡器已开启', '使用完成后',
  '正在安全恢复…', '重启并恢复', '↑', '向上滑动解锁', '电池详情', '键盘已连接'
].join('');
const largeFallbackSymbols = [...new Set(largeFallbackStrings
  .match(/[\u2000-\u206f\u2190-\u21ff\u3000-\u9fff\uff00-\uffef]/g))].join('');
for(const size of [28,40]) {
  const name=`han_font_${size}`;
  execFileSync(process.execPath,[fontTool,'--font',roundedHeavy,
    '--symbols',size === 28 ? embeddedChinese : largeFallbackSymbols,
    '--font',path.join(fonts,'DejaVuSans.ttf'),'--range','0x20-0x7e,0xa0-0x2ff,0x3b8',
    '--size',String(size),'--bpp','4','--format','lvgl','--no-kerning',
    '--lv-font-name',name,'--lv-include','lvgl.h','-o',path.join(out,name+'.c')],{stdio:'inherit'});
}
execFileSync(process.execPath,[fontTool,'--font',roundedHeavy,
  '--symbols','横折撇竖弯点钩提捺斜第0123456789笔/',
  '--size','18','--bpp','4','--format','lvgl','--no-kerning',
  '--lv-font-name','han_font_stroke_name','--lv-include','lvgl.h',
  '-o',path.join(out,'han_font_stroke_name.c')],{stdio:'inherit'});
// Keep the large numeric/IPA font independent from unrelated {id, label} tables in services.
const phonetics = fs.readFileSync(path.join(root, 'main/han_dictionary/phonetics.h'), 'utf8');
const ipa = [...phonetics.matchAll(/\{"[a-z-]+",\s*"([^"]+)"/g)].map(m=>m[1]).join('');
for(const [size,symbols,name,font] of [[100,'0123456789:/°C'+ipa,'han_font_large','DejaVuSans.ttf'],
 [240,'规','han_font_character','SourceHanSansSC-Normal.otf']]) {
 execFileSync(process.execPath,[fontTool,'--font',path.join(fonts,font),'--symbols',symbols,
 '--size',String(size),'--bpp','4','--format','lvgl','--no-kerning',
 '--lv-font-name',name,'--lv-include','lvgl.h','-o',path.join(out,name+'.c')],{stdio:'inherit'});
}
const icons={
 dictionary:`<path fill="#43bb86" d="M18 28Q46 17 64 32Q86 17 110 28V102Q86 93 64 107Q40 93 18 102Z"/><path stroke="#fff" stroke-width="5" d="M64 33v67M29 43l24 2M29 55l24 2M75 45l23-2M75 57l23-2"/><circle cx="47" cy="75" r="4" fill="#142b57"/><circle cx="80" cy="75" r="4" fill="#142b57"/><path d="M56 83q8 9 16 0" fill="none" stroke="#142b57" stroke-width="3"/>`,
 phonetics:`<path d="M23 71V58a41 41 0 0 1 82 0v13" fill="none" stroke="#9370df" stroke-width="13"/><rect x="16" y="61" width="23" height="42" rx="10" fill="#9370df"/><rect x="90" y="61" width="23" height="42" rx="10" fill="#9370df"/><path d="M50 69v20m14-34v49m14-35v20" fill="none" stroke="#9370df" stroke-width="7" stroke-linecap="round"/>`,
 timetable:`<rect x="17" y="26" width="94" height="86" rx="15" fill="#51a3eb"/><rect x="25" y="47" width="78" height="56" rx="7" fill="#fff"/><path d="M41 19v20m46-20v20" stroke="#277bc3" stroke-width="9" stroke-linecap="round"/><path d="M36 63h14m12 0h14m12 0h6M36 82h14m12 0h14" stroke="#8bcdbb" stroke-width="9"/>`,
 timer:`<circle cx="64" cy="73" r="42" fill="#ffad72"/><circle cx="64" cy="73" r="32" fill="#fff9f0"/><path d="M64 72V48m0 24l19 11M55 17h18m-9 0v15" stroke="#e97747" stroke-width="7" stroke-linecap="round"/><circle cx="64" cy="73" r="5" fill="#e97747"/>`,
 alarm:`<circle cx="64" cy="68" r="38" fill="#ff8d94"/><circle cx="64" cy="68" r="28" fill="#fff9f0"/><path d="M33 105l-9 10m71-10l9 10M64 66V48m0 18l-13 12" stroke="#d76372" stroke-width="7" stroke-linecap="round"/><path d="M16 38q0-32 35-18M78 20q34-14 34 18" fill="#ff8d94"/>`,
 weather:`<circle cx="80" cy="45" r="25" fill="#ffcf55"/><path d="M80 9V3m34 16l5-5M46 20l-5-5m72 44l8 4" stroke="#ffcf55" stroke-width="6" stroke-linecap="round"/><path d="M27 108a22 22 0 0 1-1-44 28 28 0 0 1 51-7 26 26 0 0 1 27 51Z" fill="#9bd8ef"/>`,
 settings:`<path fill="#75b7eb" d="M56 11h16l5 14 12 5 14-6 11 12-7 13 5 13 15 5v16l-15 5-5 12 7 14-11 11-14-7-12 5-5 15H56l-5-15-12-5-14 7-11-11 7-14-5-12-15-5V67l15-5 5-13-7-13 11-12 14 6 12-5Z"/><circle cx="64" cy="75" r="22" fill="#fff9f0"/><circle cx="64" cy="75" r="10" fill="#4a8fcb"/>`
};
(async()=>{
 if (process.argv.includes('--fonts-only')) {
  console.log('Generated firmware fonts only.');
  return;
 }
 let c='#include "lvgl.h"\n';
 for(const [name,body] of Object.entries(icons)){
  const svg=`<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 128 128">${body}</svg>`;
  fs.writeFileSync(path.join(source,`icon-${name}.svg`),svg+'\n');
  const png=await sharp(Buffer.from(svg)).png().toBuffer();
  fs.writeFileSync(path.join(source,`icon-${name}.png`),png);
  c+=`static const uint8_t data_${name}[] = {\n${Array.from(png).map(n=>'0x'+n.toString(16).padStart(2,'0')).join(',')}\n};\n`;
  c+=`const lv_image_dsc_t han_icon_${name} = {.header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RAW_ALPHA, .w = 128, .h = 128}, .data_size = sizeof(data_${name}), .data = data_${name}};\n`;
 }
 fs.writeFileSync(path.join(out,'han_icons.c'),c);
 const strokes=JSON.parse(fs.readFileSync(path.join(source,'strokes/89C4.json'),'utf8')).strokes;
 let strokeC='// Derived 2026-09-11 from Hanzi Writer / Make Me A Hanzi: rendered 8 cumulative highlighted frames.\n// Copyright (C) 1999 Arphic Technology Co., Ltd. ARPHIC PUBLIC LICENSE; see assets/licenses/ARPHICPL.TXT.\n#include "lvgl.h"\n';
 for(let i=0;i<strokes.length;i++) {
   const body=strokes.map((d,j)=>`<path d="${d}" fill="${j===i?'#ff7866':j<i?'#142b57':'#e3dbd6'}"/>`).join('');
   const svg=`<svg xmlns="http://www.w3.org/2000/svg" width="300" height="300" viewBox="0 0 1024 1024"><g transform="translate(0,900) scale(1,-1)">${body}</g></svg>`;
   const png=await sharp(Buffer.from(svg)).png().toBuffer();
   fs.writeFileSync(path.join(source,`strokes/gui-${i+1}.png`),png);
   strokeC+=`static const uint8_t stroke_${i}[] = {${Array.from(png).map(n=>'0x'+n.toString(16).padStart(2,'0')).join(',')}};\n`;
 }
 strokeC+='const lv_image_dsc_t han_gui_strokes[8] = {\n';
 for(let i=0;i<strokes.length;i++) strokeC+=`{.header = {.magic=LV_IMAGE_HEADER_MAGIC, .cf=LV_COLOR_FORMAT_RAW_ALPHA, .w=300, .h=300}, .data_size=sizeof(stroke_${i}), .data=stroke_${i}},\n`;
 strokeC+='};\n';
 fs.writeFileSync(path.join(out,'han_strokes.c'),strokeC);
 console.log('Generated fonts and seven transparent icons.');
})().catch(e=>{console.error(e);process.exitCode=1;});
