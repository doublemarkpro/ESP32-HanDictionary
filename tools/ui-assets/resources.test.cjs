const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {sounds, paths, options} = require('./prepare-resources.cjs');
const {wordIcons, wordsFromSource} = require('./phonetics-page-assets.cjs');
const root = path.resolve(__dirname, '..', '..');

test('44 unique cards, consistent categories, 176 audio destinations', () => {
  const rows = sounds();
  assert.equal(new Set(rows.map(x=>x.id)).size,44);
  assert.deepEqual([0,1,2].map(c=>rows.filter(x=>x.category===c).length),[12,8,24]);
  const audio = rows.flatMap(s=>['sound',...s.words].map(w=>`${s.id}/${w}.ogg`));
  assert.equal(new Set(audio).size,176);
  assert.ok(rows.some(x=>x.ipa==='θ'));
  assert.ok(rows.some(x=>x.ipa==='ʊə'));
});
test('every phonetics example word has a stable illustration mapping',()=>{
  const words=wordsFromSource();
  assert.equal(words.length,102);
  assert.deepEqual(words.filter(word=>!wordIcons[word]),[]);
});
test('every phonetics control has a bounded device-compatible audio file',()=>{
  const rows=sounds();
  const folder=path.join(root,'content','sdcard','handict','phonetics');
  const manifest=JSON.parse(fs.readFileSync(path.join(folder,'AUDIO_SOURCES.json'),'utf8'));
  const expected=rows.flatMap(sound=>['sound',...sound.words]
    .map(name=>`en-GB/${sound.id}/${name}.ogg`));
  assert.equal(expected.length,176);
  assert.deepEqual([...manifest.files].sort(),[...expected].sort());
  for(const relative of expected) {
    const audio=fs.readFileSync(path.join(folder,...relative.split('/')));
    assert.equal(audio.subarray(0,4).toString(),'OggS',relative);
    assert.ok(audio.subarray(0,128).includes(Buffer.from('OpusHead')),relative);
    assert.ok(audio.length>100 && audio.length<=256*1024,relative);
  }
});
test('stroke paths reject markup and excessive allocation', () => {
  assert.deepEqual(paths({strokes:['M 0 0 L 10 10 Z']}),['M 0 0 L 10 10 Z']);
  for(const strokes of [[],Array(65).fill('M 0 0'),['<image href="https://example.com"/>'],['M'.repeat(8193)],[null]]) {
    assert.throws(()=>paths({strokes}));
  }
});
test('outputs cannot overwrite an existing directory or omit arguments', () => {
  assert.throws(()=>options(['--output',__dirname]));
  assert.throws(()=>options(['--output']));
  assert.throws(()=>options(['--unknown','value']));
});
