const fs = require('node:fs');
const path = require('node:path');
const sharp = require('sharp');

const root = path.resolve(__dirname, '../..');
const sourceFolder = path.join(root, 'assets/graphics/source/raster/settings-page');
const outputFolders = [
  path.join(root, 'assets/graphics/settings-page'),
  path.join(root, 'content/sdcard/handict/ui/graphics/settings-page'),
];
const assets = [
  ['network-wifi.png', 144, 144],
  ['storage-usb.png', 160, 160],
  ['usb-storage-mode.png', 220, 184],
  ['lock-screen.png', 220, 220],
  ['brightness-sun.png', 76, 76],
  ['volume-speaker.png', 76, 76],
  ['auto-lock.png', 76, 76],
];

async function build() {
  for (const folder of outputFolders) fs.mkdirSync(folder, {recursive: true});
  for (const [name, width, height] of assets) {
    const png = await sharp(path.join(sourceFolder, name))
      .trim({background: '#00000000', threshold: 2})
      .resize(width, height, {fit: 'contain', background: '#00000000'})
      .png({palette: false, compressionLevel: 9, adaptiveFiltering: true})
      .toBuffer();
    for (const folder of outputFolders) fs.writeFileSync(path.join(folder, name), png);
    console.log(`${name}: ${png.length} bytes at ${width}x${height}`);
  }
}

build().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
