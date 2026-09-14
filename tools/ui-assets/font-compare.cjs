const fs=require('node:fs'),path=require('node:path'),sharp=require('sharp');
const opentype=require(require.resolve('opentype.js',{paths:[path.dirname(require.resolve('lv_font_conv/package.json'))]}));
const root=path.resolve(__dirname,'../..');
const files=[['原来的思源黑体 Normal','managed_components/lvgl__lvgl/scripts/built_in_font/SourceHanSansSC-Normal.otf'],['资源圆体 Heavy','assets/source/fonts/ResourceHanRoundedCN-Heavy.ttf']];
let svg='<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="405"><rect width="1280" height="405" fill="#fffdf5"/>';
const normal=opentype.loadSync(path.join(root,files[0][1]));
for(const [i,[name,file]] of files.entries()) {
 const font=opentype.loadSync(path.join(root,file));
 const p=normal.getPath(name,32,42+i*195,24);p.fill='#657080';svg+=p.toSVG();
 const t=font.getPath('小小助手   查字典   英语音标',32,123+i*195,64);t.fill='#082148';svg+=t.toSVG();
 const s=font.getPath('课程表  作业计时  闹钟  天气  叫我小智小智',32,174+i*195,42);s.fill='#143455';svg+=s.toSVG();
}
sharp(Buffer.from(svg+'</svg>')).png().toFile(path.join(root,'docs/ui/rendered-ui3/font-comparison.png')).catch(e=>{console.error(e);process.exitCode=1;});
