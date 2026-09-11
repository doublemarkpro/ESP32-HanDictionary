const fs=require('node:fs'),path=require('node:path');
const sharp=require('../ui-assets/node_modules/sharp');
const folder=path.resolve(process.argv[2]);
(async()=>{for(const name of fs.readdirSync(folder).filter(n=>n.endsWith('.ppm'))){
 const data=fs.readFileSync(path.join(folder,name));
 const header=Buffer.from('P6\n1280 720\n255\n');
 await sharp(data.subarray(header.length),{raw:{width:1280,height:720,channels:3}}).png().toFile(path.join(folder,name.replace('.ppm','.png')));
}})().catch(e=>{console.error(e);process.exitCode=1;});
