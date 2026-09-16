const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const sharp = require('sharp');

const root = path.resolve(__dirname, '../..');
const source = path.join(root, 'assets/graphics/source/raster/timer-page/homework-boy.png');
const outputFolders = [
  path.join(root, 'assets/graphics/timer-page'),
  path.join(root, 'content/sdcard/handict/ui/graphics/timer-page'),
];
const fontOutput = path.join(root, 'main/han_dictionary/assets');

async function build() {
  for (const folder of outputFolders) fs.mkdirSync(folder, {recursive: true});
  const art = await sharp(source)
    .trim({background: '#00000000', threshold: 5})
    .resize(320, 345, {fit: 'contain', background: '#00000000'})
    .png({palette: true, colours: 192, compressionLevel: 9})
    .toBuffer();
  for (const folder of outputFolders) fs.writeFileSync(path.join(folder, 'homework-boy.png'), art);

  const fontTool = require.resolve('lv_font_conv/lv_font_conv.js');
  const font = path.join(root, 'assets/source/fonts/ResourceHanRoundedCN-Heavy.ttf');
  const specs = [
    ['han_font_timer', 28,
      '语文数学英语今日作业已完成进行中未开始有记录本周用时今天共分钟暂停开始继续完成本科准备正在专注周一二三四五六日不足网络与存储显示声音连接读取状态手机配网模式可使用本地功能卡内容字典音插画资源屏幕亮度播放自动锁立即关长按唤醒再向上滑动解正在切换器启服务器端口订阅主题留言板还没有收到最新刷新设置重新从配置账号登录客户端失败断开等待家庭匿名密码保存最近条外观浅深色自动时段夜间开启早晨关闭并应用取消计划Ⅱ▶✓—…'],
    ['han_font_timer_title', 38,
      '今日作业本周用时开始计时暂停周一二三四五网络与存储显示声音连接最新留言设置外观模式计划完成时间保存取消'],
  ];
  for (const [name, size, symbols] of specs) {
    const file = path.join(fontOutput, `${name}.c`);
    execFileSync(process.execPath, [fontTool, '--font', font, '--symbols', symbols,
      '--range', '0x20-0x7e', '--size', String(size), '--bpp', '4', '--format', 'lvgl',
      '--no-kerning', '--lv-font-name', name, '--lv-include', 'lvgl.h', '-o', file]);
    fs.writeFileSync(file, `${fs.readFileSync(file, 'utf8').trimEnd()}\n`);
  }
  console.log(`Timer SD art: ${art.length} PNG bytes at 320x345; rounded timer fonts generated.`);
}

build().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
