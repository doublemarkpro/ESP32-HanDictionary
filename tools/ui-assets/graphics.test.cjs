const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const sharp = require('sharp');
const {vectorAssets,weatherIds,controlIds} = require('./graphics-vectors.cjs');
const {weatherAssetId} = require('./build-graphics.cjs');
const root = path.resolve(__dirname,'../..');
const folder = path.join(root,'content/sdcard/handict/ui/graphics');
const manifest = JSON.parse(fs.readFileSync(path.join(folder,'manifest.json')));
const sha256 = data => crypto.createHash('sha256').update(data).digest('hex');

test('complete unique inventory; weather fallback does not invent sunny weather',()=>{
  assert.equal(weatherIds.length,22);
  assert.equal(controlIds.length,50);
  assert.equal(manifest.assets.length,90);
  assert.equal(new Set(manifest.assets.map(a=>a.id)).size,90);
  assert.equal(weatherAssetId('rain'),'weather-rain');
  for(const value of ['not-received',null,undefined,'../clear-day',''])
    assert.equal(weatherAssetId(value),'weather-unknown');
  for(const a of vectorAssets()) {
    assert.doesNotMatch(a.svg,/<text|<script|href=|undefined|NaN/);
  }
});

test('every exported PNG has verified dimensions, alpha, hash and bounded size',async()=>{
  let count=0,bytes=0;
  for(const entry of manifest.assets) {
    const original=fs.readFileSync(path.join(root,entry.source));
    const canonical=entry.origin==='original-vector'?Buffer.from(original.toString('utf8').replaceAll('\r\n','\n')):original;
    assert.equal(sha256(canonical),entry.source_sha256);
    for(const v of entry.variants) {
      assert.match(v.path,/^[a-z0-9-]+\/[0-9]+\.png$/);
      const png=fs.readFileSync(path.join(folder,v.path));
      const m=await sharp(png).metadata(),s=await sharp(png).stats();
      assert.equal(sha256(png),v.sha256,v.path);
      assert.deepEqual([m.width,m.height,m.hasAlpha],[v.width,v.height,true],v.path);
      assert.equal(v.bytes,png.length);
      assert.equal(v.decoded_rgba_bytes,v.width*v.height*4);
      assert.ok(s.channels[3].max>=250 && s.channels[3].mean>0,v.path);
      if(!entry.opaque) assert.equal(s.channels[3].min,0,v.path);
      if(['home','illustration','weather','control','panel'].includes(entry.group)) {
        const corner=await sharp(png).extract({left:0,top:0,width:1,height:1}).raw().toBuffer();
        assert.equal(corner[3],0,v.path+' transparent corner');
      }
      count++; bytes+=png.length;
    }
  }
  assert.equal(count,252);
  assert.deepEqual(manifest.totals,{assets:90,pngs:count,png_bytes:bytes});
  assert.ok(bytes<4*1024*1024,'SD graphics budget');
});

test('small optional C pack matches PNG exports and stays below 128 KiB payload',()=>{
  const c=fs.readFileSync(path.join(root,'assets/graphics/lvgl/han_graphics_small.c'),'utf8');
  let total=0;
  for(const a of manifest.assets.filter(a=>['control','weather'].includes(a.group))) {
    const name=a.id.replaceAll('-','_');
    const array=c.match(new RegExp(`static const uint8_t ${name}_png\\[\\] = \\{([\\s\\S]*?)\\};`));
    assert.ok(array,a.id);
    const bytes=Buffer.from(array[1].match(/0x[0-9a-f]{2}/g).map(x=>parseInt(x,16)));
    const v=a.variants.find(v=>v.width===(a.group==='control'?48:96));
    assert.deepEqual(bytes,fs.readFileSync(path.join(folder,v.path)));
    total+=bytes.length;
  }
  assert.equal(manifest.small_c_pack.count,72);
  assert.equal(total,manifest.small_c_pack.png_bytes);
  assert.ok(total<128*1024);
  const cmake=fs.readFileSync(path.join(root,'main/CMakeLists.txt'),'utf8');
  assert.ok(!cmake.includes('han_graphics_small'),'not automatically included in firmware');
});
