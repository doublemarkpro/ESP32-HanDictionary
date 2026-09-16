const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');

const root = path.resolve(__dirname, '../..');
const output = path.join(root, 'main/han_dictionary/assets');
const font = path.join(root, 'assets/source/fonts/ResourceHanRoundedCN-Heavy.ttf');
const fontTool = require.resolve('lv_font_conv/lv_font_conv.js');
const specs = [
  ['han_font_flip_digits', 176, '0123456789:—'],
  ['han_font_flip_date', 40,
    ' 0123456789年月日星期一二三四五六七八九农历甲乙丙丁戊己庚辛壬癸子丑寅卯辰巳午未申酉戌亥正冬腊闰初十廿时间同步待后显示暂不可用·—'],
];

for (const [name, size, symbols] of specs) {
  execFileSync(process.execPath, [fontTool, '--font', font, '--symbols', symbols, '--size',
    String(size), '--bpp', '4', '--format', 'lvgl', '--no-kerning', '--lv-font-name', name,
    '--lv-include', 'lvgl.h', '-o', path.join(output, `${name}.c`)]);
  const generated = path.join(output, `${name}.c`);
  fs.writeFileSync(generated, `${fs.readFileSync(generated, 'utf8').trimEnd()}\n`);
}

console.log('Flip-clock fonts generated.');
