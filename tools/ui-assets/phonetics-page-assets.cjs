const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const sharp = require('sharp');

const root = path.resolve(__dirname, '../..');
const outputFolders = [
  path.join(root, 'assets/graphics/phonetics-page/words'),
  path.join(root, 'content/sdcard/handict/ui/graphics/phonetics-page/words'),
];
const phonetics = fs.readFileSync(path.join(root, 'main/han_dictionary/phonetics.h'), 'utf8');
const sourceRepo = process.argv[2];
const fontOutput = path.join(root, 'main/han_dictionary/assets');

// The visual need is semantic rather than orthographic: abstract example words deliberately reuse
// a familiar symbol. Keeping one small file per word makes device lookup constant-time and lets a
// future content pack replace any individual illustration without rebuilding firmware.
const wordIcons = {
  about: 'Information', air: 'Wind face', back: 'Back arrow', bag: 'Backpack',
  ball: 'Soccer ball', banana: 'Banana', bath: 'Bathtub', bed: 'Bed', bike: 'Bicycle',
  bird: 'Bird', blue: 'Blue circle', boat: 'Sailboat', book: 'Open book', box: 'Package',
  boy: 'Boy', bridge: 'Bridge at night', bus: 'Bus', cake: 'Shortcake', cap: 'Billed cap',
  car: 'Automobile', cat: 'Cat', chair: 'Chair', cheese: 'Cheese wedge', coin: 'Coin',
  cow: 'Cow', cup: 'Teacup without handle', cure: 'Medical symbol', day: 'Sun', deer: 'Deer',
  dog: 'Dog', door: 'Door', ear: 'Ear', fan: 'Folding hand fan', fish: 'Fish',
  five: 'Keycap 5', food: 'Pot of food', foot: 'Foot', game: 'Video game', go: 'Play button',
  good: 'Check mark button', hair: 'Hair pick', hand: 'Raised hand', hat: 'Top hat',
  home: 'House', hot: 'Fire', house: 'House with garden', jam: 'Jar', juice: 'Beverage box',
  key: 'Key', kite: 'Kite', leaf: 'Leaf fluttering in wind', leg: 'Leg', long: 'Right arrow',
  measure: 'Straight ruler', milk: 'Glass of milk', moon: 'Full moon', mother: 'Woman',
  mouse: 'Mouse', near: 'Bullseye', net: 'Goal net', nose: 'Nose',
  nurse: 'Woman health worker', park: 'National park', pen: 'Fountain pen', pig: 'Pig',
  pure: 'Sparkles', rain: 'Cloud with rain', red: 'Red circle', ring: 'Ring', room: 'Door',
  saw: 'Carpentry saw', sheep: 'Ewe', ship: 'Passenger ship', shoe: 'Running shoe',
  sing: 'Microphone', sit: 'Chair', sofa: 'Couch and lamp', star: 'Star', sun: 'Sun with face',
  tea: 'Hot beverage', that: 'Right arrow', thin: 'Pinching hand', think: 'Thinking face',
  this: 'Down arrow', top: 'Up arrow', tour: 'Globe showing europe-africa', toy: 'Teddy bear',
  tree: 'Deciduous tree', turn: 'Counterclockwise arrows button', usual: 'Repeat button',
  van: 'Minibus', vet: 'Health worker', vision: 'Eye', watch: 'Watch', water: 'Droplet',
  wet: 'Sweat droplets', win: 'Trophy', yellow: 'Yellow circle', yes: 'Check mark button',
  young: 'Baby', zip: 'Zipper-mouth face', zoo: 'Lion',
};

function wordsFromSource() {
  const words = new Set();
  const row = /\{"[^"]+",\s*"[^"]+",\s*\{"([^"]+)",\s*"([^"]+)",\s*"([^"]+)"\}/g;
  for (const match of phonetics.matchAll(row)) {
    words.add(match[1]);
    words.add(match[2]);
    words.add(match[3]);
  }
  return [...words].sort();
}

function git(...args) {
  return execFileSync('git', ['-C', sourceRepo, ...args], {encoding: null, maxBuffer: 32 << 20});
}

async function build() {
  if (!sourceRepo || !fs.existsSync(path.join(sourceRepo, '.git'))) {
    throw new Error('Pass a local microsoft/fluentui-emoji checkout as the first argument.');
  }
  const words = wordsFromSource();
  const missingMappings = words.filter(word => !wordIcons[word]);
  if (missingMappings.length) throw new Error(`Missing mappings: ${missingMappings.join(', ')}`);

  const revision = git('rev-parse', 'HEAD').toString('utf8').trim();
  const tree = git('ls-tree', '-r', '--name-only', 'HEAD').toString('utf8').split(/\r?\n/);
  for (const folder of outputFolders) fs.mkdirSync(folder, {recursive: true});

  let total = 0;
  for (const word of words) {
    const asset = wordIcons[word];
    const prefix = `assets/${asset}/`;
    const candidates = tree.filter(file => file.startsWith(prefix) && /\/3D\/.*_3d(?:_default)?\.png$/.test(file));
    const source = candidates.find(file => file.includes('/Default/3D/')) || candidates[0];
    if (!source) throw new Error(`No Fluent Emoji 3D asset found for ${word}: ${asset}`);
    const raw = git('show', `HEAD:${source}`);
    const png = await sharp(raw)
      .trim({background: '#00000000', threshold: 4})
      .resize(142, 102, {fit: 'contain', background: '#00000000'})
      .png({palette: true, colours: 128, compressionLevel: 9})
      .toBuffer();
    for (const folder of outputFolders) fs.writeFileSync(path.join(folder, `${word}.png`), png);
    total += png.length;
  }

  const provenance = `${JSON.stringify({
    name: 'Microsoft Fluent Emoji word illustrations',
    repository: 'https://github.com/microsoft/fluentui-emoji',
    revision,
    license: 'MIT',
    words: words.map(word => ({word, asset: wordIcons[word]})),
  }, null, 2)}\n`;
  const license = git('show', 'HEAD:LICENSE');
  for (const folder of outputFolders.map(folder => path.dirname(folder))) {
    fs.writeFileSync(path.join(folder, 'provenance.json'), provenance);
    fs.writeFileSync(path.join(folder, 'LICENSE-MICROSOFT-FLUENT-EMOJI.txt'), license);
  }

  const fontTool = require.resolve('lv_font_conv/lv_font_conv.js');
  const fontSpecs = [
    {
      name: 'han_font_phonetics', size: 30,
      font: path.join(root, 'assets/source/fonts/ResourceHanRoundedCN-Heavy.ttf'),
      symbols: '单元音双元音辅音标听一跟着读示范上下页点选开始学习或词即可播放发缺少文件请导入包，▶‹›0123456789/',
      range: '0x20-0x7e',
    },
    {
      name: 'han_font_phonetics_ipa', size: 48,
      font: path.join(root, 'managed_components/lvgl__lvgl/scripts/built_in_font/DejaVuSans.ttf'),
      symbols: '/iːɪeæʌɑɒɔʊuɜəaɡθðʃʒŋpjbdtkfvszhtʃdʒmnlrwy/',
    },
  ];
  for (const spec of fontSpecs) {
    const args = [fontTool, '--font', spec.font, '--symbols', spec.symbols,
      '--size', String(spec.size), '--bpp', '4', '--format', 'lvgl', '--no-kerning',
      '--lv-font-name', spec.name, '--lv-include', 'lvgl.h',
      '-o', path.join(fontOutput, `${spec.name}.c`)];
    if (spec.range) args.push('--range', spec.range);
    execFileSync(process.execPath, args);
    const file = path.join(fontOutput, `${spec.name}.c`);
    fs.writeFileSync(file, `${fs.readFileSync(file, 'utf8').trimEnd()}\n`);
  }
  console.log(`Phonetics SD art: ${words.length} word icons, ${total} PNG bytes; two anti-aliased fonts; Fluent Emoji ${revision}.`);
}

if (require.main === module) {
  build().catch(error => {
    console.error(error);
    process.exitCode = 1;
  });
}

module.exports = {wordIcons, wordsFromSource};
