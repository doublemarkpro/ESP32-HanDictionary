// Editable original vector art. These extend the approved cream/pastel visual language.
const ink='#142b57';
const gradient=(id,a,b)=>`<linearGradient id="${id}" x1="0" y1="0" x2=".65" y2="1"><stop stop-color="${a}"/><stop offset="1" stop-color="${b}"/></linearGradient>`;
const defs=gradient('sun','#fff49b','#ffbb34')+gradient('cloud','#ffffff','#c5e5fc')+
  gradient('storm','#b5d9f4','#75abe1')+gradient('moon','#fff1a0','#ffd258')+
  gradient('rain','#79d1ff','#2884ee')+gradient('leaf','#acd97c','#44ad87')+
  '<filter id="shadow" x="-25%" y="-25%" width="150%" height="170%"><feGaussianBlur stdDeviation="3"/></filter>';
const face=(x,y,s=1)=>`<g transform="translate(${x} ${y}) scale(${s})"><ellipse cx="-13" cy="0" rx="3.5" ry="4.5" fill="${ink}"/><ellipse cx="13" cy="0" rx="3.5" ry="4.5" fill="${ink}"/><circle cx="-21" cy="10" r="6" fill="#ffab83" opacity=".7"/><circle cx="21" cy="10" r="6" fill="#ffab83" opacity=".7"/><path d="M-7 9Q0 18 7 9" fill="none" stroke="${ink}" stroke-width="3" stroke-linecap="round"/></g>`;
const sun=(x=128,y=111,r=53,smile=true)=>`<g>${Array.from({length:8},(_,i)=>`<rect x="${x-4}" y="${y-r-26}" width="8" height="17" rx="4" fill="#ffd257" transform="rotate(${i*45} ${x} ${y})"/>`).join('')}<circle cx="${x}" cy="${y+3}" r="${r}" fill="#e0ba67" opacity=".16" filter="url(#shadow)"/><circle cx="${x}" cy="${y}" r="${r}" fill="url(#sun)"/><path d="M${x-r*.6} ${y-r*.4}Q${x-r*.1} ${y-r*.95} ${x+r*.35} ${y-r*.63}" fill="none" stroke="#fffbd6" opacity=".8" stroke-width="5" stroke-linecap="round"/>${smile?face(x,y+2,r/53):''}</g>`;
const moon=(x=113,y=107,s=1)=>`<g transform="translate(${x} ${y}) scale(${s})"><path d="M17-58C-53-70-87 11-38 50C1 81 60 53 64 13C17 48-24 2 17-58Z" fill="url(#moon)"/>${face(-22,12,.65)}<path d="M59-40l4 10 11 1-8 7 2 11-9-6-10 6 3-11-9-7 12-1Z" fill="#bfa9f2"/><circle cx="37" cy="-60" r="5" fill="#d7c8fc"/></g>`;
const cloud=(x=128,y=149,s=1,dark=false)=>`<g transform="translate(${x} ${y}) scale(${s})"><ellipse cx="0" cy="40" rx="79" ry="8" fill="#6d9bba" opacity=".1" filter="url(#shadow)"/><path d="M-62 33C-111 28-96-35-55-32C-39-85 33-81 47-33C99-44 113 27 65 34Z" fill="url(#${dark?'storm':'cloud'})"/><path d="M-51-21C-34-58 9-60 32-37" fill="none" stroke="#fff" opacity=".38" stroke-width="6" stroke-linecap="round"/></g>`;
const drop=(x,y,s=1)=>`<path d="M0-12C-3-7-12 1-10 7C-7 19 9 16 10 6C11 0 3-8 0-12Z" transform="translate(${x} ${y}) rotate(18) scale(${s})" fill="url(#rain)"/>`;
const snowflake=(x,y,s=.8)=>`<g transform="translate(${x} ${y}) scale(${s})" stroke="#6daedc" stroke-width="4" stroke-linecap="round">${[0,60,120].map(a=>`<path d="M0-13V13M-5-8L0-4L5-8M-5 8L0 4L5 8" transform="rotate(${a})"/>`).join('')}</g>`;
const rain=(n,heavy=false)=>Array.from({length:n},(_,i)=>drop(76+i*104/(n-1),194+(i%2)*10,heavy?1:.8)).join('');
function weather(name) {
  switch(name) {
    case 'clear-day': return sun(128,126,61);
    case 'clear-night': return moon(126,124,1.2);
    case 'partly-cloudy-day': return sun(99,88,45)+cloud(144,161,.95);
    case 'partly-cloudy-night': return moon(104,101,.95)+cloud(145,165,.87);
    case 'cloudy': return cloud(98,107,.7,true)+cloud(139,161,1);
    case 'overcast': return cloud(104,111,.75,true)+cloud(141,170,1,true);
    case 'light-rain': return cloud(128,129,1)+rain(2);
    case 'rain': return cloud(128,126,1,true)+rain(3);
    case 'heavy-rain': return cloud(128,118,1,true)+rain(4,true)+drop(91,234,.65)+drop(162,231,.65);
    case 'showers': return sun(94,79,40,false)+cloud(141,132,.94)+rain(3);
    case 'thunderstorm': return cloud(128,121,1,true)+drop(77,199,.8)+drop(187,203,.8)+'<path d="M130 159L105 202H128L116 239L160 185H137L150 159Z" fill="#ffcb49" stroke="#ffb944" stroke-width="2" stroke-linejoin="round"/>';
    case 'snow': return cloud(128,123,1)+snowflake(79,202)+snowflake(130,218)+snowflake(181,202);
    case 'sleet': return cloud(128,123,1,true)+drop(82,202)+snowflake(137,211)+drop(187,202,.75);
    case 'hail': return cloud(128,120,1,true)+[77,128,179].map((x,i)=>`<circle cx="${x}" cy="${200+i%2*17}" r="10" fill="#c1e7fa" stroke="#78b3db" stroke-width="3"/>`).join('');
    case 'fog': return cloud(128,112,.9)+[177,199,221].map((y,i)=>`<path d="M${51+i*9} ${y}H${211-i*12}" stroke="#a7bdcc" stroke-width="10" stroke-linecap="round"/>`).join('');
    case 'wind': return '<g fill="none" stroke="#72bac9" stroke-width="12" stroke-linecap="round"><path d="M35 103H154C205 103 192 47 165 66"/><path d="M55 137H195C238 137 226 93 208 101"/><path d="M36 170H155C197 170 191 219 167 204"/></g><path d="M52 215Q91 204 106 220Q81 246 52 215Z" fill="url(#leaf)"/>';
    case 'sand': return '<path d="M31 192Q69 137 107 180Q157 140 226 193V219H31Z" fill="#ecd0a2"/><g fill="none" stroke="#cdb99a" stroke-width="10" stroke-linecap="round"><path d="M52 76H168Q194 76 184 52"/><path d="M28 114H218M65 149H177"/></g>';
    case 'hot': return sun(109,103,46,false)+thermometer('#ff8267');
    case 'cold': return snowflake(99,105,2.9)+thermometer('#65b7f3');
    case 'sunrise': case 'sunset': return `<path d="M54 156a74 74 0 0 1 148 0Z" fill="url(#sun)"/><g stroke="#ffbc68" stroke-width="9" stroke-linecap="round"><path d="M34 176H222M65 197H191M96 218H160"/></g><path d="M128 107V47m-18 ${name==='sunrise'?'18':'24'}l18 ${name==='sunrise'?'-18':'18'} 18 ${name==='sunrise'?'18':'-18'}" fill="none" stroke="#e58d56" stroke-width="8" stroke-linecap="round" stroke-linejoin="round"/>`;
    default: return cloud(128,151,1)+'<path d="M114 113C111 91 151 90 151 111C151 125 131 124 131 140M131 155v1" fill="none" stroke="#698da7" stroke-width="9" stroke-linecap="round"/>';
  }
}
function thermometer(color) { return `<g><path d="M177 97a13 13 0 0 1 26 0v82a29 29 0 1 1-26 0Z" fill="#f9fcff" stroke="#b8ccd9" stroke-width="6"/><path d="M190 119v77" stroke="${color}" stroke-width="12" stroke-linecap="round"/><circle cx="190" cy="199" r="19" fill="${color}"/></g>`; }
const actions={
  back:'M31 10L16 24l15 14M17 24h26', next:'M17 10l15 14-15 14M31 24H5',
  home:'M6 22L24 7l18 15M11 20v21h10V28h7v13h9V20',
  play:'M17 10l22 14-22 14Z', pause:'M17 11v26M31 11v26', stop:'M13 13h22v22H13Z',
  refresh:'M39 19A16 16 0 1 0 38 33M39 8v12H27',
  check:'M10 24l9 10L39 13', close:'M13 13l22 22M35 13L13 35',
  plus:'M24 10v28M10 24h28', minus:'M10 24h28',
  edit:'M9 32L31 10l8 8-22 22-10 2ZM27 14l8 8',
  lock:'M12 21h24v20H12ZM17 21V13a7 7 0 0 1 14 0v8M24 29v5',
  eye:'M4 24Q24 1 44 24Q24 47 4 24ZM29 24a5 5 0 1 1-10 0a5 5 0 1 1 10 0',
  'eye-off':'M7 11l34 28M5 24Q23 3 43 24Q37 31 31 34M9 29Q16 36 23 35',
  mic:'M18 10a6 6 0 0 1 12 0v14a6 6 0 0 1-12 0ZM12 22v3a12 12 0 0 0 24 0v-3M24 37v7M17 44h14',
  'mic-off':'M18 15v-5a6 6 0 0 1 12 0v14M18 23a6 6 0 0 0 8 7M12 23a12 12 0 0 0 20 12M24 37v7M17 44h14M6 6l36 36',
  speaker:'M6 18h9L27 8v32L15 30H6ZM34 16q9 8 0 16M39 9q16 15 0 30',
  'speaker-off':'M6 18h9L27 8v32L15 30H6ZM34 19l10 10M44 19L34 29',
  download:'M24 6v25M13 22l11 11 11-11M8 34v8h32v-8',
  sd:'M14 5h23v38H10V12ZM18 6v10M25 6v10M32 6v10',
  warning:'M24 5L44 41H4ZM24 18v10M24 34v1',
  info:'M24 7a17 17 0 1 1 0 34a17 17 0 1 1 0-34M24 21v12M24 15v1',
  search:'M31 19a12 12 0 1 1-24 0a12 12 0 1 1 24 0M28 28l14 14',
  clock:'M24 5a19 19 0 1 1 0 38a19 19 0 1 1 0-38M24 12v13l10 6',
  calendar:'M9 12h30v29H9ZM9 20h30M16 6v11M32 6v11M16 28h2M25 28h2M16 35h2M25 35h2',
  save:'M9 7h25l7 8v27H7V7ZM15 7v12h17V7M14 42V28h20v14',
  trash:'M8 12h32M17 12V6h14v6M12 13l2 29h20l2-29M20 21v13M28 21v13',
  'chevron-up':'M11 30l13-13 13 13', 'chevron-down':'M11 18l13 13 13-13',
  star:'M24 5l6 12 14 2-10 10 2 14-12-7-12 7 2-14L4 19l14-2Z',
  book:'M24 12Q14 5 5 10v29q11-5 19 1q9-6 19-1V10q-10-5-19 2ZM24 12v28',
  calculator:'M11 5h26v38H11ZM16 11h16v8H16ZM17 26h2M28 26h2M17 35h2M28 35h2',
  science:'M18 5h12M20 5v14L8 38q-2 5 4 5h24q6 0 4-5L28 19V5M14 29h20M19 36h1M28 34h1',
  art:'M24 6C-1 6 1 43 21 42c11 0-1-11 7-12h8C52 27 43 5 24 6ZM16 16h1M29 13h1M36 22h1M11 28h1',
  sport:'M27 5a4 4 0 1 1 0 8a4 4 0 1 1 0-8M15 23l8-8 9 10h9M22 18l-5 13 12 7-2 6M18 30L6 39',
  backpack:'M17 12V8q7-7 14 0v4M13 12h22q5 0 5 6v23H8V18q0-6 5-6ZM15 28h18v9H15Z',
  umbrella:'M5 24a19 19 0 0 1 38 0Q37 17 31 24Q24 17 18 24Q11 17 5 24ZM24 24v14q0 10-8 4',
  bottle:'M19 4h10v8l5 7v24H14V19l5-7ZM14 24h20M19 9h10',
};
function control(id) {
  if(id.startsWith('wifi')) {
    const strength=id==='wifi-off'?3:Number(id.slice(-1));
    return `<g fill="none" stroke="${ink}" stroke-width="4" stroke-linecap="round">${strength>=3?'<path d="M5 15q19-17 38 0"/>':''}${strength>=2?'<path d="M12 23q12-10 24 0"/>':''}${strength>=1?'<path d="M19 31q5-4 10 0"/>':''}<path d="M24 38v1"/>${id==='wifi-off'?'<path d="M8 7l32 35" stroke="#ef887a"/>':''}</g>`;
  }
  if(id.startsWith('battery')) {
    const state=id.slice(8),level={empty:0,low:5,half:13,full:25,charging:20,unknown:0}[state];
    return `<rect x="4" y="13" width="35" height="22" rx="5" fill="none" stroke="${ink}" stroke-width="3"/><path d="M43 20v8" stroke="${ink}" stroke-width="4" stroke-linecap="round"/>${level?`<rect x="9" y="18" width="${level}" height="12" rx="2" fill="${state==='low'?'#f48b79':'#51c494'}"/>`:''}${state==='charging'?'<path d="M25 9L18 25h6l-4 15 13-20h-7l4-11Z" fill="#ffce55" stroke="#fff9f0" stroke-width="1"/>':''}${state==='unknown'?'<path d="M18 21q0-6 8-3q5 3-1 7l-3 2m0 4v1" fill="none" stroke="#91a5b5" stroke-width="3" stroke-linecap="round"/>':''}`;
  }
  return `<path transform="translate(2.4 2.4) scale(.9)" d="${actions[id]}" fill="none" stroke="${ink}" stroke-width="3.5" stroke-linecap="round" stroke-linejoin="round"/>`;
}
const weatherIds=['clear-day','clear-night','partly-cloudy-day','partly-cloudy-night','cloudy','overcast','light-rain','rain','heavy-rain','showers','thunderstorm','snow','sleet','hail','fog','wind','sand','hot','cold','sunrise','sunset','unknown'];
const controlIds=[...Object.keys(actions),'wifi-0','wifi-1','wifi-2','wifi-3','wifi-off','battery-empty','battery-low','battery-half','battery-full','battery-charging','battery-unknown'];
function leafCorner() { return `<path d="M0 220V110Q66 70 92 152Q159 106 203 169Q263 137 320 220Z" fill="#d9edb4"/><g fill="url(#leaf)"><ellipse cx="56" cy="139" rx="28" ry="79" transform="rotate(-23 56 139)"/><ellipse cx="104" cy="144" rx="26" ry="73" transform="rotate(25 104 144)"/><ellipse cx="175" cy="177" rx="22" ry="53" transform="rotate(45 175 177)"/></g><path d="M83 219L46 107M85 219l36-123M114 220l77-65" fill="none" stroke="#4fac86" stroke-width="7" stroke-linecap="round" opacity=".5"/>`; }
function vectorAssets() {
  const assets=weatherIds.map(id=>({id:'weather-'+id,group:'weather',width:256,height:256,sizes:[96,192,320],body:weather(id)}));
  assets.push(...controlIds.map(id=>({id:'control-'+id,group:'control',width:48,height:48,sizes:[32,48,64],body:control(id)})));
  assets.push({id:'decor-leaves-left',group:'decor',width:320,height:220,sizes:[320],body:leafCorner()});
  assets.push({id:'decor-leaves-right',group:'decor',width:320,height:220,sizes:[320],body:`<g transform="translate(320 0) scale(-1 1)">${leafCorner()}</g>`});
  assets.push({id:'background-cream',group:'background',width:1280,height:720,sizes:[1280],opaque:true,body:'<rect width="1280" height="720" fill="#fff9f0"/><path d="M0 670Q235 591 504 674Q823 589 1280 675V720H0Z" fill="#fff0c5" opacity=".6"/>'});
  for(const [name,a,b] of [['mint','#e7ffe9','#bef0cf'],['lavender','#f0e9ff','#d2c0ff'],['sky','#e5f7ff','#b7e8fa'],['peach','#fff0dc','#ffdcc0'],['rose','#ffecee','#ffcdd5'],['sunrise','#fff4cc','#ffe5a9']]) {
    assets.push({id:'panel-'+name,group:'panel',width:396,height:232,sizes:[396],body:gradient('panel',a,b)+'<clipPath id="rounded"><rect x="1" y="1" width="394" height="230" rx="24"/></clipPath><g clip-path="url(#rounded)"><rect x="1" y="1" width="394" height="230" rx="24" fill="url(#panel)" stroke="#ffffff" stroke-opacity=".65"/><path d="M0 204Q54 159 109 208Q186 155 249 205Q321 175 396 194V232H0Z" fill="#fff" opacity=".16"/></g>'});
  }
  return assets.map(a=>({...a,svg:`<svg xmlns="http://www.w3.org/2000/svg" width="${a.width}" height="${a.height}" viewBox="0 0 ${a.width} ${a.height}"><defs>${defs}</defs>${a.body}</svg>`}));
}
module.exports={vectorAssets,weatherIds,controlIds};
