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
const fontTool = require.resolve('lv_font_conv/lv_font_conv.js');
const files = fs.readdirSync(path.join(root, 'main/han_dictionary')).filter(f=>/\.(cc|h)$/.test(f));
const strings = files.map(f=>fs.readFileSync(path.join(root,'main/han_dictionary',f),'utf8')).join('');
const chinese = [...new Set(strings.match(/[\u2000-\u206f\u3000-\u9fff\uff00-\uffef]/g))].join('');
for(const size of [28,40]) {
  const name=`han_font_${size}`;
  execFileSync(process.execPath,[fontTool,'--font',path.join(fonts,'SourceHanSansSC-Normal.otf'),
    '--symbols',chinese,'--font',path.join(fonts,'DejaVuSans.ttf'),'--range','0x20-0x7e,0xa0-0x2ff',
    '--size',String(size),'--bpp','4','--format','lvgl','--no-compress','--no-kerning',
    '--lv-font-name',name,'--lv-include','lvgl.h','-o',path.join(out,name+'.c')],{stdio:'inherit'});
}
for(const [size,symbols,name,font] of [[100,'0123456789:/iːɪeæʌɑɔaʊəpbt dfv','han_font_large','DejaVuSans.ttf'],
 [240,'规','han_font_character','SourceHanSansSC-Normal.otf']]) {
 execFileSync(process.execPath,[fontTool,'--font',path.join(fonts,font),'--symbols',symbols,
 '--size',String(size),'--bpp','4','--format','lvgl','--no-compress','--no-kerning',
 '--lv-font-name',name,'--lv-include','lvgl.h','-o',path.join(out,name+'.c')],{stdio:'inherit'});
}
const icons={
 dictionary:`<path fill="#43bb86" d="M18 28Q46 17 64 32Q86 17 110 28V102Q86 93 64 107Q40 93 18 102Z"/><path stroke="#fff" stroke-width="5" d="M64 33v67M29 43l24 2M29 55l24 2M75 45l23-2M75 57l23-2"/><circle cx="47" cy="75" r="4" fill="#142b57"/><circle cx="80" cy="75" r="4" fill="#142b57"/><path d="M56 83q8 9 16 0" fill="none" stroke="#142b57" stroke-width="3"/>`,
 phonetics:`<path d="M23 71V58a41 41 0 0 1 82 0v13" fill="none" stroke="#9370df" stroke-width="13"/><rect x="16" y="61" width="23" height="42" rx="10" fill="#9370df"/><rect x="90" y="61" width="23" height="42" rx="10" fill="#9370df"/><path d="M50 69v20m14-34v49m14-35v20" fill="none" stroke="#9370df" stroke-width="7" stroke-linecap="round"/>`,
 timetable:`<rect x="17" y="26" width="94" height="86" rx="15" fill="#51a3eb"/><rect x="25" y="47" width="78" height="56" rx="7" fill="#fff"/><path d="M41 19v20m46-20v20" stroke="#277bc3" stroke-width="9" stroke-linecap="round"/><path d="M36 63h14m12 0h14m12 0h6M36 82h14m12 0h14" stroke="#8bcdbb" stroke-width="9"/>`,
 timer:`<circle cx="64" cy="73" r="42" fill="#ffad72"/><circle cx="64" cy="73" r="32" fill="#fff9f0"/><path d="M64 72V48m0 24l19 11M55 17h18m-9 0v15" stroke="#e97747" stroke-width="7" stroke-linecap="round"/><circle cx="64" cy="73" r="5" fill="#e97747"/>`,
 alarm:`<circle cx="64" cy="68" r="38" fill="#ff8d94"/><circle cx="64" cy="68" r="28" fill="#fff9f0"/><path d="M33 105l-9 10m71-10l9 10M64 66V48m0 18l-13 12" stroke="#d76372" stroke-width="7" stroke-linecap="round"/><path d="M16 38q0-32 35-18M78 20q34-14 34 18" fill="#ff8d94"/>`,
 weather:`<circle cx="80" cy="45" r="25" fill="#ffcf55"/><path d="M80 9V3m34 16l5-5M46 20l-5-5m72 44l8 4" stroke="#ffcf55" stroke-width="6" stroke-linecap="round"/><path d="M27 108a22 22 0 0 1-1-44 28 28 0 0 1 51-7 26 26 0 0 1 27 51Z" fill="#9bd8ef"/>`
};
(async()=>{
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
 console.log('Generated fonts and six transparent icons.');
})().catch(e=>{console.error(e);process.exitCode=1;});
