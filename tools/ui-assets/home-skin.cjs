// UI3: native vector layers + downsampled existing cutouts, no text baked into PNGs.
const fs=require('node:fs'),path=require('node:path'),sharp=require('sharp');
const {execFileSync}=require('node:child_process');
const {vectorAssets}=require('./graphics-vectors.cjs');
const root=path.resolve(__dirname,'../..'),out=path.join(root,'main/han_dictionary/assets');
const dir=path.join(root,'assets/graphics/home-skin');
const svg=(w,h,b)=>Buffer.from(`<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}">${b}</svg>`);
const grad=(id,a,b)=>`<linearGradient id="${id}" x2=".25" y2="1"><stop stop-color="${a}"/><stop offset="1" stop-color="${b}"/></linearGradient>`;
const leaf=(x,y,s=1)=>`<g transform="translate(${x} ${y}) scale(${s})"><ellipse cx="0" cy="0" rx="19" ry="48" fill="url(#leaf)" transform="rotate(-24)"/><ellipse cx="33" cy="16" rx="20" ry="43" fill="url(#leaf)" transform="rotate(25 33 16)"/><path d="M18 53L-8-18M18 53L43-4" stroke="#43a96f" opacity=".4" stroke-width="5" fill="none" stroke-linecap="round"/></g>`;
const specs=[['dictionary','#e1ffe6','#aff0be'],['phonetics','#eee7ff','#cebcff'],['timetable','#dff7ff','#a8e5f6'],['timer','#fff0e1','#ffd1a8'],['alarm','#ffe9e9','#ffbfc8'],['weather','#d6f5ff','#a2e4f9']];
function panel(i) {
 const [id,a,b]=specs[i];
 let layers='';
 if(i===0||i===2||i===5) {
  layers+=`<path d="M0 175Q30 148 59 177Q93 139 136 176Q178 148 216 178Q266 145 303 173Q354 140 394 158V224H0Z" fill="${i===0?'#a6df9e':'#85dace'}" opacity=".8"/>`;
  layers+=leaf(i===0?20:58,184,i===0?1.2:.78)+leaf(365,190,.9);
  layers+=`<path d="M0 214Q39 180 83 213Q127 194 177 221Q226 190 277 217Q339 182 394 201V224H0Z" fill="${i===0?'#64c983':'#51c5b4'}" opacity=".4"/>`;
 } else {
  const color=i===1?'#baa5f5':i===3?'#ffc08f':'#ffa9b5';
  layers+=`<path d="M0 193Q20 165 41 195Q60 158 87 191Q109 176 137 201Q164 181 191 207Q222 164 257 196Q288 177 310 199Q354 167 394 199V224H0Z" fill="${color}" opacity=".42"/><path d="M0 216Q24 192 49 220Q77 192 106 219Q141 194 177 221Q203 200 233 219Q283 193 324 218Q370 195 394 213V224H0Z" fill="${color}" opacity=".45"/>`;
 }
 if(i===3) layers+='<g transform="translate(-10 139) rotate(-5)"><path d="M0 32L71 21L117 32V60L42 75L0 59Z" fill="#ffc245"/><path d="M44 46L112 37V55L44 68Z" fill="#fff1c8"/><path d="M0 0L54-9L91 2V36L32 47L0 29Z" fill="#5eafff"/><path d="M32 15L91 2V35L32 47Z" fill="#408ce3"/></g>';
 return svg(394,224,`<defs>${grad('base',a,b)}${grad('leaf','#b2dc76','#51ba87')}<clipPath id="clip"><rect x="1" y="1" width="392" height="222" rx="28"/></clipPath><radialGradient id="glow" cx=".28" cy="0" r="1"><stop stop-color="#fff" stop-opacity=".38"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></radialGradient></defs><g clip-path="url(#clip)"><rect width="394" height="224" fill="url(#base)"/><rect width="394" height="224" fill="url(#glow)"/>${layers}</g><rect x="1" y="1" width="392" height="222" rx="28" fill="none" stroke="#fff" stroke-opacity=".7" stroke-width="2"/>`);
}
async function main() {
 fs.mkdirSync(dir,{recursive:true});
 const assets=[];
 async function add(name,input,width,height) {
  const png=await sharp(input).resize(width,height,{fit:'contain',background:'#00000000'}).ensureAlpha().png({palette:true,colours:192,effort:10,dither:.25}).toBuffer();
  fs.writeFileSync(path.join(dir,name+'.png'),png);
  assets.push({name,width,height,png});
 }
 for(let i=0;i<6;i++) {
  const source=panel(i);fs.writeFileSync(path.join(dir,'panel-'+specs[i][0]+'.svg'),source);
  await add('panel_'+specs[i][0],source,394,224);
 }
 // Normalize only transparent padding; no semantic redrawing of generated artwork.
 for(const [id,width,height] of [['book',190,145],['headphones',264,156],['calendar',234,172],['timer',187,178],['alarm',200,172],['weather',262,166]]) {
  const input=path.join(root,'assets/graphics/source/raster/home-'+id+'.png');
  const trimmed=await sharp(input).trim({background:'#00000000',threshold:20}).png().toBuffer();
  await add('art_'+id,trimmed,width,height);
 }
 const footer=svg(1280,146,`<defs>${grad('leaf','#b6dc80','#68bb83')}</defs><path d="M0 116Q188 25 403 80Q647 151 863 89Q1112 15 1280 99V146H0Z" fill="#fff0bb"/>${leaf(20,110,1.65)}${leaf(1190,114,1.7)}<path d="M0 137Q135 90 214 146H0ZM1120 146Q1203 102 1280 124V146Z" fill="#cfeaa5"/>`);
 fs.writeFileSync(path.join(dir,'footer.svg'),footer);await add('footer',footer,1280,146);
 for(const id of ['wifi-1','wifi-2','wifi-3','wifi-off','battery-empty','battery-low','battery-half','battery-full','battery-charging','battery-unknown','mic']) {
  let source=vectorAssets().find(a=>a.id==='control-'+id).svg;
  if(id==='mic') source=source.replaceAll('#142b57','#ffffff');
  await add('status_'+id.replaceAll('-','_'),Buffer.from(source),48,48);
 }
 let c='#include "home_skin.h"\n';
 for(const a of assets) {
  c+=`static const uint8_t ${a.name}_png[] = {\n`;
  for(let j=0;j<a.png.length;j+=16)c+='    '+Array.from(a.png.subarray(j,j+16),n=>'0x'+n.toString(16).padStart(2,'0')).join(',')+',\n';
  c+=`};\nconst lv_image_dsc_t han_${a.name} = {.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RAW_ALPHA,.w=${a.width},.h=${a.height}},.data_size=sizeof(${a.name}_png),.data=${a.name}_png};\n`;
 }
 fs.writeFileSync(path.join(out,'home_skin.c'),c);
 const font=path.join(root,'assets/source/fonts/ResourceHanRoundedCN-Heavy.ttf');
 const tool=require.resolve('lv_font_conv/lv_font_conv.js');
 for(const [name,size,text] of [['han_font_home',48,'查字典英语音标课程表作业计时闹钟天气'],['han_font_brand',62,'小小助手联网设置查字典英语音标课程表作业计时闹钟天气'],['han_font_talk',36,'按住说话松开发送正在连接'],['han_font_clock',40,'0123456789:—']]) {
  execFileSync(process.execPath,[tool,'--font',font,'--symbols',text,'--size',String(size),'--bpp','4','--format','lvgl','--no-kerning','--lv-font-name',name,'--lv-include','lvgl.h','-o',path.join(out,name+'.c')]);
  const output=path.join(out,name+'.c');
  fs.writeFileSync(output,fs.readFileSync(output,'utf8').trimEnd()+'\n');
 }
 // Fixed 规 preview uses the project's licensed, standard-source character paths (not AI text).
 const strokes=JSON.parse(fs.readFileSync(path.join(root,'assets/source/strokes/89C4.json'))).strokes;
 const glyph=svg(116,116,`<g transform="scale(.11328) translate(0 900) scale(1 -1)" fill="#102b21">${strokes.map(d=>`<path d="${d}"/>`).join('')}</g>`);
 const glyphPng=await sharp(glyph).png().toBuffer();
 // Append one validated example character, independent of the artistic heading font.
 fs.appendFileSync(path.join(out,'home_skin.c'),`static const uint8_t gui_png[]={${Array.from(glyphPng).join(',')}};\nconst lv_image_dsc_t han_home_gui={.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RAW_ALPHA,.w=116,.h=116},.data_size=sizeof(gui_png),.data=gui_png};\n`);
 const totals={png_bytes:assets.reduce((n,a)=>n+a.png.length,0)+glyphPng.length,rgba_bytes:assets.reduce((n,a)=>n+a.width*a.height*4,0)+116*116*4,assets:assets.map(({name,width,height,png})=>({name,width,height,bytes:png.length}))};
 fs.writeFileSync(path.join(dir,'manifest.json'),JSON.stringify(totals,null,2)+'\n');console.log(totals.png_bytes+' bytes of UI3 PNG payload');
}
main().catch(e=>{console.error(e);process.exitCode=1;});
