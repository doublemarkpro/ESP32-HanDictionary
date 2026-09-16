const fs = require('node:fs');
const path = require('node:path');
const sharp = require('sharp');

const root = path.resolve(__dirname, '../..');
const lightFolder = path.resolve(process.argv[2] || path.join(root, 'build-ui-dark/gallery-light'));
const darkFolder = path.resolve(process.argv[3] || path.join(root, 'build-ui-dark/gallery-dark'));
const outputFolder = path.join(root, 'docs/ui/gallery');

const lightPages = [
  'home', 'dictionary', 'phonetics', 'timetable', 'timer', 'timer-plan', 'alarm', 'weather',
  'network', 'clock',
];
const darkPages = [
  'home', 'dictionary', 'phonetics', 'timetable', 'timer', 'timer-plan', 'alarm', 'weather',
  'network', 'appearance', 'clock',
];

async function exportPage(sourceFolder, inputName, outputName) {
  const source = path.join(sourceFolder, `${inputName}.ppm`);
  if (!fs.existsSync(source)) throw new Error(`Missing gallery render: ${source}`);
  const output = path.join(outputFolder, `${outputName}.png`);
  const ppm = fs.readFileSync(source);
  const header = /^P6\s+(\d+)\s+(\d+)\s+255\s/.exec(ppm.toString('ascii', 0, 64));
  if (!header) throw new Error(`Unsupported PPM header: ${source}`);
  const width = Number(header[1]);
  const height = Number(header[2]);
  await sharp(ppm.subarray(header[0].length), {raw: {width, height, channels: 3}})
    .resize(960, 540, {fit: 'fill'})
    .png({palette: true, colours: 192, compressionLevel: 9, adaptiveFiltering: true})
    .toFile(output);
  return output;
}

async function main() {
  fs.mkdirSync(outputFolder, {recursive: true});
  for (const page of lightPages) await exportPage(lightFolder, page, page);
  for (const page of darkPages) await exportPage(darkFolder, `dark-${page}`, `dark-${page}`);
  console.log(`README gallery: ${lightPages.length + darkPages.length} current UI renders.`);
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
