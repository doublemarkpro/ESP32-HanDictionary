// Convert the two ImageGen source PNGs into compact LVGL RAW_ALPHA resources.
const fs = require('node:fs');
const path = require('node:path');
const sharp = require('sharp');

const root = path.resolve(__dirname, '../..');
const source = path.join(root, 'assets/source');
const output = path.join(root, 'main/han_dictionary/assets/dictionary_action_icons.c');
const icons = [
  ['pinyin_search', 'icon-pinyin-search.png'],
  ['definition_detail', 'icon-definition-detail.png'],
];

(async () => {
  let c = '#include "lvgl.h"\n';
  for (const [name, file] of icons) {
    const png = await sharp(path.join(source, file))
      .resize(96, 96, {fit: 'contain'})
      .png({compressionLevel: 9, palette: true, quality: 90})
      .toBuffer();
    c += `static const uint8_t data_${name}[] = {\n`;
    c += Array.from(png, value => `0x${value.toString(16).padStart(2, '0')}`).join(',');
    c += '\n};\n';
    c += `const lv_image_dsc_t han_icon_${name} = {` +
      `.header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RAW_ALPHA, ` +
      `.w = 96, .h = 96}, .data_size = sizeof(data_${name}), .data = data_${name}};\n`;
  }
  fs.writeFileSync(output, c);
  console.log(`Generated ${output}`);
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
