const test = require('node:test');
const assert = require('node:assert/strict');
const {sounds, paths, options} = require('./prepare-resources.cjs');

test('44 unique cards, consistent categories, 176 audio destinations', () => {
  const rows = sounds();
  assert.equal(new Set(rows.map(x=>x.id)).size,44);
  assert.deepEqual([0,1,2].map(c=>rows.filter(x=>x.category===c).length),[12,8,24]);
  const audio = rows.flatMap(s=>['sound',...s.words].map(w=>`${s.id}/${w}.ogg`));
  assert.equal(new Set(audio).size,176);
  assert.ok(rows.some(x=>x.ipa==='θ'));
  assert.ok(rows.some(x=>x.ipa==='ʊə'));
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
