const fs=require('node:fs'),path=require('node:path'),sharp=require('sharp');
const {execFileSync}=require('node:child_process');
const {vectorAssets}=require('./graphics-vectors.cjs');
const root=path.resolve(__dirname,'../..'),out=path.join(root,'main/han_dictionary/assets');
const folder=path.join(root,'assets/graphics/timetable');fs.mkdirSync(folder,{recursive:true});
(async()=>{
 let c='#include "timetable_assets.h"\n',bytes=0;
 for(const [id,color] of [['book','#ef7768'],['calculator','#2487dc'],['science','#31af89'],['art','#ed9b43'],['sport','#129f9d'],['backpack','#efa134']]) {
  const svg=vectorAssets().find(a=>a.id==='control-'+id).svg.replaceAll('#142b57',color);
  const png=await sharp(Buffer.from(svg)).resize(40,40).png().toBuffer();bytes+=png.length;
  fs.writeFileSync(path.join(folder,id+'.png'),png);
  c+=`static const uint8_t ${id}_png[]={${Array.from(png).join(',')}};\nconst lv_image_dsc_t han_subject_${id}={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RAW_ALPHA,.w=40,.h=40},.data_size=sizeof(${id}_png),.data=${id}_png};\n`;
 }
 fs.writeFileSync(path.join(out,'timetable_assets.c'),c);
 const font=path.join(root,'assets/source/fonts/ResourceHanRoundedCN-Heavy.ttf'),name='han_font_schedule';
 execFileSync(process.execPath,[require.resolve('lv_font_conv/lv_font_conv.js'),'--font',font,'--symbols','周一二三四五六日第12345678节语文数学英语科学美术体育音乐劳动阅读班会明天要带本下周末已备课表好好学习天天向上问程ABCT?—','--size','32','--bpp','4','--format','lvgl','--no-kerning','--lv-font-name',name,'--lv-include','lvgl.h','-o',path.join(out,name+'.c')]);
 const p=path.join(out,name+'.c');fs.writeFileSync(p,fs.readFileSync(p,'utf8').trimEnd()+'\n');
 console.log('Timetable icons: '+bytes+' bytes; rounded 32px font is a subset.');
})().catch(e=>{console.error(e);process.exitCode=1;});
