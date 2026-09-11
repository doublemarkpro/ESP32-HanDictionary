const test = require('node:test'), assert = require('node:assert/strict');
const fs = require('node:fs'), path = require('node:path'), sharp = require('sharp');
const root = path.resolve(__dirname, '../..');
test('timetable reuses six small icons, not a full-page bitmap', async () => {
  let bytes = 0;
  const source = fs.readFileSync(path.join(root, 'main/han_dictionary/assets/timetable_assets.c'), 'utf8');
  for (const name of ['book', 'calculator', 'science', 'art', 'sport', 'backpack']) {
    const png = fs.readFileSync(path.join(root, `assets/graphics/timetable/${name}.png`));
    const m = await sharp(png).metadata();
    assert.deepEqual([m.width, m.height], [40, 40]);
    assert.ok(m.hasAlpha);
    assert.ok(source.includes(`{${Array.from(png).join(',')}}`));
    bytes += png.length;
  }
  assert.ok(bytes < 6 * 1024);
});
test('timetable font remains compressed and product-only', () => {
  const source = fs.readFileSync(path.join(root, 'main/han_dictionary/assets/han_font_schedule.c'), 'utf8');
  assert.match(source, /\.bpp = 4/);
  assert.match(source, /\.bitmap_format = 1/);
  const cmake = fs.readFileSync(path.join(root, 'main/CMakeLists.txt'), 'utf8');
  const start = cmake.indexOf('if(CONFIG_HAN_DICTIONARY)');
  const block = cmake.slice(start, cmake.indexOf('endif()', start));
  assert.ok(block.includes('timetable_assets.c') && block.includes('han_font_schedule.c'));
});
