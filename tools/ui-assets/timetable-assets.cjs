const fs=require('node:fs'),path=require('node:path'),sharp=require('sharp');
const {execFileSync}=require('node:child_process');
const {vectorAssets}=require('./graphics-vectors.cjs');
const root=path.resolve(__dirname,'../..'),out=path.join(root,'main/han_dictionary/assets');
const folder=path.join(root,'assets/graphics/timetable');fs.mkdirSync(folder,{recursive:true});
(async()=>{
 let c='#include "timetable_assets.h"\n',bytes=0;
 const subjectPaths={
  computer:'M6 8h36v27H6ZM17 43h14M24 35v8M11 13h26v17H11Z',
  music:'M18 35V13l21-5v22M18 19l21-5M18 35a6 5 0 1 1-12 0a6 5 0 1 1 12 0M39 30a6 5 0 1 1-12 0a6 5 0 1 1 12 0',
  martial:'M24 5a4 4 0 1 1 0 8a4 4 0 1 1 0-8M11 23l12-8 10 8 9-5M22 18l-5 13-10 8M17 31l13 6 11 6',
  labor:'M17 7h14M24 7v23M15 29h18l-3 14H18ZM9 13l8 8M39 13l-8 8',
  flute:'M8 36L36 8M13 41L41 13M17 32l-4-4M23 26l-4-4M29 20l-4-4M35 14l-4-4'
 };
 for(const [id,color] of [['book','#ef7768'],['calculator','#2487dc'],['science','#31af89'],['art','#ed9b43'],['sport','#129f9d'],['computer','#2d91b8'],['music','#df6da5'],['martial','#d58a32'],['labor','#6c9b48'],['flute','#c56e50'],['star','#8d70c9'],['backpack','#efa134'],['bottle','#35a98a']]) {
  const shared=vectorAssets().find(a=>a.id==='control-'+id);
  const svg=shared ? shared.svg.replaceAll('#142b57',color) :
   `<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48" viewBox="0 0 48 48"><path d="${subjectPaths[id]}" fill="none" stroke="${color}" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round"/></svg>`;
  const png=await sharp(Buffer.from(svg)).resize(40,40).png().toBuffer();bytes+=png.length;
  fs.writeFileSync(path.join(folder,id+'.png'),png);
  c+=`static const uint8_t ${id}_png[]={${Array.from(png).join(',')}};\nconst lv_image_dsc_t han_subject_${id}={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RAW_ALPHA,.w=40,.h=40},.data_size=sizeof(${id}_png),.data=${id}_png};\n`;
 }
 const controls=[['back',48,48],['calendar',48,48],['chevron-down',32,32]];
 for(const [id,w,h] of controls) {
  const svg=vectorAssets().find(a=>a.id==='control-'+id).svg;
  const png=await sharp(Buffer.from(svg)).resize(w,h).png().toBuffer();bytes+=png.length;
  fs.writeFileSync(path.join(folder,'page-'+id+'.png'),png);
  const symbol=id.replaceAll('-','_');
  c+=`static const uint8_t page_${symbol}_png[]={${Array.from(png).join(',')}};\nconst lv_image_dsc_t han_timetable_${symbol}={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RAW_ALPHA,.w=${w},.h=${h}},.data_size=sizeof(page_${symbol}_png),.data=page_${symbol}_png};\n`;
 }
 const mascot=Buffer.from(`<svg xmlns="http://www.w3.org/2000/svg" width="180" height="132" viewBox="0 0 180 132">
  <defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#b8e66c"/><stop offset="1" stop-color="#3aae72"/></linearGradient></defs>
  <ellipse cx="86" cy="116" rx="72" ry="10" fill="#a8c990" opacity=".2"/>
  <path d="M87 35C85 18 73 7 57 5c1 17 12 30 29 32M91 35c5-18 19-29 37-28-4 18-17 30-37 31" fill="url(#g)" stroke="#62b55d" stroke-width="3"/>
  <path d="M24 96C5 93 5 113 22 112M151 94c23-5 24 19 5 18" fill="none" stroke="#50b676" stroke-width="15" stroke-linecap="round"/>
  <path d="M28 75C35 42 61 29 92 34c39 5 60 30 58 65-1 20-23 26-62 25-42 0-67-9-60-49" fill="url(#g)"/>
  <ellipse cx="91" cy="83" rx="47" ry="34" fill="#fff8e9"/>
  <ellipse cx="67" cy="82" rx="4" ry="6" fill="#142b57"/><ellipse cx="113" cy="82" rx="4" ry="6" fill="#142b57"/>
  <ellipse cx="57" cy="94" rx="9" ry="5" fill="#ffad91" opacity=".75"/><ellipse cx="123" cy="94" rx="9" ry="5" fill="#ffad91" opacity=".75"/>
  <path d="M81 94q10 12 20 0" fill="none" stroke="#142b57" stroke-width="4" stroke-linecap="round"/>
 </svg>`);
 const mascotPng=await sharp(mascot).png({palette:true,colours:128}).toBuffer();bytes+=mascotPng.length;
 fs.writeFileSync(path.join(folder,'mascot.png'),mascotPng);
 c+=`static const uint8_t mascot_png[]={${Array.from(mascotPng).join(',')}};\nconst lv_image_dsc_t han_timetable_mascot={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RAW_ALPHA,.w=180,.h=132},.data_size=sizeof(mascot_png),.data=mascot_png};\n`;
 const booksSource=path.join(root,'assets/graphics/source/raster/decor-books-sprout.png');
 const books=await sharp(booksSource).trim({background:'#00000000',threshold:20}).resize(154,112,{fit:'contain',background:'#00000000'}).png({palette:true,colours:128}).toBuffer();bytes+=books.length;
 fs.writeFileSync(path.join(folder,'books.png'),books);
 c+=`static const uint8_t books_png[]={${Array.from(books).join(',')}};\nconst lv_image_dsc_t han_timetable_books={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RAW_ALPHA,.w=154,.h=112},.data_size=sizeof(books_png),.data=books_png};\n`;
 const leavesSvg=vectorAssets().find(a=>a.id==='decor-leaves-left').svg;
 const leaves=await sharp(Buffer.from(leavesSvg)).resize(250,112,{fit:'contain',position:'bottom',background:'#00000000'}).png({palette:true,colours:96}).toBuffer();bytes+=leaves.length;
 fs.writeFileSync(path.join(folder,'leaves.png'),leaves);
 c+=`static const uint8_t leaves_png[]={${Array.from(leaves).join(',')}};\nconst lv_image_dsc_t han_timetable_leaves={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RAW_ALPHA,.w=250,.h=112},.data_size=sizeof(leaves_png),.data=leaves_png};\n`;
 fs.writeFileSync(path.join(out,'timetable_assets.c'),c);
 const font=path.join(root,'assets/source/fonts/ResourceHanRoundedCN-Heavy.ttf');
 const timetable=JSON.parse(fs.readFileSync(path.join(root,'content/sdcard/handict/timetable.json'),'utf8'));
 const timetableSymbols=[...(timetable.days||[]).flat(),...(timetable.supplies||[]).flat()].join('');
 const fontTool=require.resolve('lv_font_conv/lv_font_conv.js');
 for(const [name,size,symbols] of [
  ['han_font_schedule',32,'周一二三四五六日第12345678节语文数学英语科学美术体育音乐劳动阅读班会明天要带本下周末已备课表好好学习天天向上问程ABCT?—'+timetableSymbols],
  ['han_font_schedule_small',24,'ABC问问明天上什么课美术本跳绳水杯请先同步日期导入课程表未填写需带物品更多正在聆听小智回答…']
 ]) {
  execFileSync(process.execPath,[fontTool,'--font',font,'--symbols',symbols,'--size',String(size),'--bpp','4','--format','lvgl','--no-kerning','--lv-font-name',name,'--lv-include','lvgl.h','-o',path.join(out,name+'.c')]);
  const p=path.join(out,name+'.c');fs.writeFileSync(p,fs.readFileSync(p,'utf8').trimEnd()+'\n');
 }
 console.log('Timetable icons: '+bytes+' bytes; rounded 32px font is a subset.');
})().catch(e=>{console.error(e);process.exitCode=1;});
