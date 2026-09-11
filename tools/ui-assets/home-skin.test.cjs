const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),sharp=require('sharp');
const root=path.resolve(__dirname,'../..');
const folder=path.join(root,'assets/graphics/home-skin');
test('UI3 payload and per-image decode budgets are bounded',async()=>{
 const manifest=JSON.parse(fs.readFileSync(path.join(folder,'manifest.json')));
 assert.equal(manifest.assets.length,24);
 assert.ok(manifest.png_bytes<200*1024);
 for(const a of manifest.assets) {
  const p=path.join(folder,a.name+'.png'),m=await sharp(p).metadata();
  assert.deepEqual([m.width,m.height],[a.width,a.height],a.name);
  assert.ok(m.hasAlpha,a.name);
  assert.equal(fs.statSync(p).size,a.bytes);
  assert.ok(a.width*a.height*4<=1280*146*4);
 }
});
test('real rounded fonts are subsets; firmware assets remain opt-in',()=>{
 for(const name of ['home','brand','talk','clock']) {
  const c=fs.readFileSync(path.join(root,`main/han_dictionary/assets/han_font_${name}.c`),'utf8');
  assert.match(c,/\.bpp = 4/);
  assert.match(c,/\.bitmap_format = 1/);
 }
 const cmake=fs.readFileSync(path.join(root,'main/CMakeLists.txt'),'utf8');
 const conditional=cmake.slice(cmake.indexOf('if(CONFIG_HAN_DICTIONARY)'),cmake.indexOf('endif()',cmake.indexOf('if(CONFIG_HAN_DICTIONARY)')));
 assert.ok(conditional.includes('home_skin.c'));
 assert.ok(conditional.includes('han_font_home.c'));
 assert.ok(fs.readFileSync(path.join(root,'assets/licenses/ResourceHanRounded-LICENSE.txt'),'utf8').includes('SIL OPEN FONT LICENSE'));
});
