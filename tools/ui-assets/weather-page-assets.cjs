const fs = require('node:fs');
const path = require('node:path');
const sharp = require('sharp');
const {execFileSync} = require('node:child_process');

const root = path.resolve(__dirname, '../..');
const source = path.join(root, 'assets/graphics/source/raster/weather-page');
const exported = path.join(root, 'assets/graphics/weather-page');
const sdWeather = path.join(root, 'content/sdcard/handict/ui/graphics/weather-page');
const output = path.join(root, 'main/han_dictionary/assets');

function save(name, data) {
  fs.writeFileSync(path.join(exported, name), data);
  fs.writeFileSync(path.join(sdWeather, name), data);
}

async function build() {
  fs.mkdirSync(exported, {recursive: true});
  fs.mkdirSync(sdWeather, {recursive: true});
  let total = 0;

  const heroSource = fs.readFileSync(path.join(source, 'qingdao-hero.png'));
  const hero = await sharp(heroSource)
    .resize(760, 344, {fit: 'cover', position: 'centre'})
    .png({palette: false, compressionLevel: 9, adaptiveFiltering: true})
    .toBuffer();
  save('qingdao-hero.png', hero);
  save('qingdao-hero-original.png', heroSource);
  total += hero.length;
  total += heroSource.length;

  for (const [name, file] of [
    ['air_quality', 'air-quality.png'],
    ['precipitation', 'precipitation.png'],
    ['sunrise_sunset', 'sunrise-sunset.png'],
    ['lifestyle_index', 'lifestyle-index.png'],
  ]) {
    const png = await sharp(path.join(source, file))
      .trim({background: '#00000000', threshold: 8})
      .resize(76, 76, {fit: 'contain', background: '#00000000'})
      .png({palette: true, colours: 96, compressionLevel: 9})
      .toBuffer();
    save(file, png);
    total += png.length;
  }

  for (const name of ['sunny', 'partly-cloudy', 'cloudy', 'rain', 'thunderstorm', 'snow',
    'fog', 'wind']) {
    const png = await sharp(path.join(source, `condition-${name}-v2.png`))
      .trim({background: '#00000000', threshold: 8})
      .resize(192, 192, {fit: 'contain', background: '#00000000'})
      .png({palette: true, colours: 128, compressionLevel: 9})
      .toBuffer();
    save(`condition-${name}.png`, png);
    total += png.length;
  }

  const font = path.join(root, 'assets/source/fonts/ResourceHanRoundedCN-Heavy.ttf');
  const fontTool = require.resolve('lv_font_conv/lv_font_conv.js');
  const specs = [
    ['han_font_weather', 30,
      '天气青岛今天明天后天周一二三四五六日空气质量优良轻度中度重度严重污染降水概率日出日落生活指数穿衣运动舒适感冒紫外线洗车旅游钓鱼适宜不宜较冷炎热凉爽和风缓存更新暂无数据点击查看完整建议晴少间多云阴阵雷雨冰雹夹冻小中大暴特强极端毛毛雪雾霾沙尘浮扬浓重热冷未知转到局部体感湿度级东西南北偏旋转力闹钟已未开启正在响铃重复日期保存并保持关闭停止声请至少选择✓℃0123456789%~—:/·'],
    ['han_font_weather_title', 40,
      '青岛生活指数完整建议晴少间多云阴阵雷雨冰雹夹冻小中大暴特强极端毛毛雪雾霾沙尘浮扬浓重度严重热冷未知转到局部风'],
    ['han_font_weather_hero', 72, '0123456789℃-:'],
  ];
  for (const [name, size, symbols] of specs) {
    const args = [fontTool, '--font', font, '--symbols', symbols, '--size', String(size),
      '--bpp', '4', '--format', 'lvgl', '--no-kerning', '--lv-font-name', name,
      '--lv-include', 'lvgl.h', '-o', path.join(output, name + '.c')];
    if (name === 'han_font_weather') args.push('--range', '0x20-0x7e');
    execFileSync(process.execPath, args);
    const file = path.join(output, name + '.c');
    fs.writeFileSync(file, fs.readFileSync(file, 'utf8').trimEnd() + '\n');
  }
  console.log(`Weather page SD assets: ${total} PNG bytes; original Qingdao art retained; 8 condition icons; rounded fonts generated.`);
}

build().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
