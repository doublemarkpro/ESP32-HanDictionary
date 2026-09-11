// Reproducible offline resource preparation. Never overwrites an existing output pack.
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const sharp = require('sharp');
const root = path.resolve(__dirname, '../..');
const opentype = require(require.resolve('opentype.js', {paths: [path.dirname(require.resolve('lv_font_conv/package.json'))]}));

function sounds() {
  const source = fs.readFileSync(path.join(root,'main/han_dictionary/phonetics.h'),'utf8');
  const rows = [...source.matchAll(/\{"([a-z-]+)",\s*"([^"]+)",\s*\{"([a-z]+)",\s*"([a-z]+)",\s*"([a-z]+)"\},\s*([0-2])\}/g)];
  if (rows.length !== 44) throw Error('Expected the complete 44-card inventory');
  return rows.map(m=>({id:m[1],ipa:m[2],words:m.slice(3,6),category:Number(m[6])}));
}
function paths(record) {
  if (!record || !Array.isArray(record.strokes) || !record.strokes.length || record.strokes.length>64) throw Error('Invalid stroke count');
  for(const d of record.strokes) {
    if(typeof d!=='string' || !d.length || d.length>8192 || !/^[MmLlHhVvCcSsQqTtAaZz0-9eE+.,\s-]+$/.test(d)) throw Error('Invalid SVG path');
  }
  return record.strokes;
}
function options(args) {
  const parsed = {};
  for(let i=0;i<args.length;i+=2) {
    if(!['--output','--entries','--strokes-dir'].includes(args[i]) || !args[i+1]) throw Error('Usage: --output NEW_DIRECTORY [--entries JSON] [--strokes-dir DIRECTORY]');
    parsed[args[i].slice(2)] = path.resolve(args[i+1]);
  }
  if(!parsed.output || fs.existsSync(parsed.output)) throw Error('Choose a new output directory');
  return parsed;
}
async function prepare(args) {
  const opts = options(args);
  const inventory = sounds();
  const sources = [];
  const sourceDir = opts['strokes-dir'] || path.join(root,'assets/source/strokes');
  for(const file of fs.readdirSync(sourceDir).filter(x=>/^[4-9A-F][0-9A-F]{3}\.json$/.test(x))) {
    const code = parseInt(file.slice(0,4),16);
    if(code<0x4e00 || code>0x9fff) throw Error('Only BMP CJK source filenames supported');
    const filename = path.join(sourceDir,file);
    if(fs.statSync(filename).size>512*1024) throw Error('Stroke source too large');
    sources.push({hex:file.slice(0,4),filename,paths:paths(JSON.parse(fs.readFileSync(filename,'utf8')))});
  }
  const command = [path.join(root,'tools/content_pack.py'),'prepare','--output',opts.output];
  if(opts.entries) command.push('--entries',opts.entries);
  execFileSync('python',command,{stdio:'inherit'});
  const card = path.join(opts.output,'handict');
  const fontsDir = path.join(root,'managed_components/lvgl__lvgl/scripts/built_in_font');
  const font = opentype.loadSync(path.join(fontsDir,'DejaVuSans.ttf'));
  function textOutline(text,y,size,color) {
    if([...text].some(c=>!font.charToGlyphIndex(c))) throw Error('Missing glyph: '+text);
    const x=(480-font.getAdvanceWidth(text,size))/2;
    const outline=font.getPath(text,x,y,size); outline.fill=color; return outline.toSVG(2);
  }
  const cards = path.join(card,'phonetics/cards'); fs.mkdirSync(cards,{recursive:true});
  const catalog = {schema_version:1,accent:'en-GB',inventory:'traditional-44',audio_included:false,
    note:'Teaching convention; accent variants exist, especially /ʊə/. r is the conventional teaching symbol. Audio must be independently licensed and reviewed.',sounds:[]};
  for(const sound of inventory) {
    const svg=`<svg xmlns="http://www.w3.org/2000/svg" width="480" height="300" viewBox="0 0 480 300"><rect width="480" height="300" rx="24" fill="#e9dffc"/>${textOutline('/'+sound.ipa+'/',155,100,'#142b57')}${textOutline(sound.words.join('   '),236,26,'#142b57')}</svg>`;
    fs.writeFileSync(path.join(cards,sound.id+'.svg'),svg+'\n');
    await sharp(Buffer.from(svg)).ensureAlpha().png({palette:false}).toFile(path.join(cards,sound.id+'.png'));
    catalog.sounds.push({...sound,audio:['sound',...sound.words].map(word=>`phonetics/en-GB/${sound.id}/${word}.ogg`)});
  }
  fs.writeFileSync(path.join(card,'phonetics/catalog.json'),JSON.stringify(catalog,null,2)+'\n');
  for(const source of sources) {
    const entryPath=path.join(card,'dictionary/entries',source.hex+'.json');
    if(!fs.existsSync(entryPath)) continue;
    const entry=JSON.parse(fs.readFileSync(entryPath,'utf8'));
    if(entry.stroke_count!==source.paths.length) throw Error('Stroke/entry count mismatch: '+source.hex);
    const originals=path.join(card,'licenses/stroke-sources'); fs.mkdirSync(originals,{recursive:true});
    fs.copyFileSync(source.filename,path.join(originals,source.hex+'.json'));
    const dir=path.join(card,'dictionary/strokes',source.hex); fs.mkdirSync(dir,{recursive:true});
    for(let frame=0;frame<source.paths.length;frame++) {
      const body=source.paths.map((d,i)=>`<path d="${d}" fill="${i===frame?'#ff7866':i<frame?'#142b57':'#e3dbd6'}"/>`).join('');
      const svg=`<svg xmlns="http://www.w3.org/2000/svg" width="300" height="300" viewBox="0 0 1024 1024"><g transform="translate(0,900) scale(1,-1)">${body}</g></svg>`;
      await sharp(Buffer.from(svg)).ensureAlpha().png({palette:false}).toFile(path.join(dir,String(frame+1).padStart(2,'0')+'.png'));
    }
  }
  fs.cpSync(path.join(root,'assets/licenses'),path.join(card,'licenses'),{recursive:true});
  fs.copyFileSync(path.join(root,'assets/README.md'),path.join(card,'ASSET-NOTICES.md'));
  execFileSync('python',[path.join(root,'tools/content_pack.py'),'validate',card],{stdio:'inherit'});
  console.log('Prepared 44 outlined IPA cards and bounded stroke frames:',card);
}
module.exports = {sounds,paths,options};
if(require.main===module) prepare(process.argv.slice(2)).catch(e=>{console.error(e.message);process.exitCode=1;});
