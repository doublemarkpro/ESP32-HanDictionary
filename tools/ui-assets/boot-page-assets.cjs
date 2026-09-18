const fs = require('node:fs');
const path = require('node:path');
const sharp = require('sharp');

const root = path.resolve(__dirname, '..', '..');
const source = path.join(root, 'assets', 'graphics', 'source', 'raster', 'boot');
const output = path.join(
  root,
  'content',
  'sdcard',
  'handict',
  'ui',
  'graphics',
  'boot-page',
);

async function render(name) {
  const input = path.join(source, `miaozhi-boot-${name}.png`);
  const target = path.join(output, `miaozhi-boot-${name}.png`);
  const embedded = path.join(
    root,
    'main',
    'han_dictionary',
    'assets',
    `boot_${name}.png`,
  );
  if (!fs.existsSync(input)) throw new Error(`Missing boot source: ${input}`);
  fs.mkdirSync(output, { recursive: true });
  await sharp(input)
    .resize(1280, 720, { fit: 'fill' })
    .png({ palette: true, colours: 256, quality: 92, compressionLevel: 9 })
    .toFile(target);
  await sharp(input)
    .resize(640, 360, { fit: 'fill' })
    .png({ palette: true, colours: 64, quality: 84, compressionLevel: 9 })
    .toFile(embedded);
  console.log(path.relative(root, target));
  console.log(path.relative(root, embedded));
}

Promise.all([render('light'), render('dark')]).catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
