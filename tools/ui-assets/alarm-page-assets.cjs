const fs = require('node:fs');
const path = require('node:path');
const sharp = require('sharp');

const root = path.resolve(__dirname, '../..');
const source = path.join(root, 'assets/graphics/source/raster/alarm-page/alarm-sunrise.png');
const exported = path.join(root, 'assets/graphics/alarm-page');
const sdAlarm = path.join(root, 'content/sdcard/handict/ui/graphics/alarm-page');

async function build() {
  fs.mkdirSync(exported, {recursive: true});
  fs.mkdirSync(sdAlarm, {recursive: true});
  const png = await sharp(source)
    .resize(512, 512, {fit: 'contain', background: '#00000000'})
    .png({palette: false, compressionLevel: 9, adaptiveFiltering: true})
    .toBuffer();
  fs.writeFileSync(path.join(exported, 'alarm-sunrise.png'), png);
  fs.writeFileSync(path.join(sdAlarm, 'alarm-sunrise.png'), png);
  console.log(`Alarm page SD asset: ${png.length} PNG bytes at 512x512.`);
}

build().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
