// VoiceDeck front-end — broadcast console aesthetic.
"use strict";
const host = (window.chrome && window.chrome.webview) || null;
const $ = (id) => document.getElementById(id);
vdSetLocale(vdLocale);
vdWatchI18n();
function send(obj) { if (host) host.postMessage(obj); }

let toastTimer=null;
function toast(msg){const el=$("toast");el.textContent=msg;el.classList.remove("hidden");
  clearTimeout(toastTimer);toastTimer=setTimeout(()=>el.classList.add("hidden"),2600);}
function esc(s){return String(s==null?"":"").replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;")||String(s==null?"":s).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;");}
function fmtSec(s){const m=Math.floor(s/60);return m+":"+String(Math.round(s%60)).padStart(2,"0");}

const SVGP=(d)=>`<svg viewBox="0 0 24 24" aria-hidden="true"><path d="${d}"/></svg>`;
const ICONS={
  play:`<svg viewBox="0 0 24 24"><path d="M8 5.5v13l11-6.5Z"/></svg>`,
  stop:`<svg viewBox="0 0 24 24"><rect x="6.5" y="6.5" width="11" height="11" rx="1.5"/></svg>`,
  pause:`<svg viewBox="0 0 24 24"><rect x="7" y="5" width="3.5" height="14"/><rect x="13.5" y="5" width="3.5" height="14"/></svg>`,
  loop:`<svg viewBox="0 0 24 24"><path d="M17 4l3 3-3 3M4 16V9h16M7 20l-3-3 3-3M20 8v7H4"/></svg>`,
  trash:`<svg viewBox="0 0 24 24"><path d="M5 7h14M10 7V5h4v2M8 7l.8 13h6.4L16 7"/></svg>`,
  preview:`<svg viewBox="0 0 24 24"><path d="M8 5.5v13l11-6.5Z"/><path d="M4 4l16 16" stroke="currentColor" stroke-width="1.4" fill="none"/></svg>`,
  edit:`<svg viewBox="0 0 24 24"><path d="M14 5l5 5L8 21H3v-5L14 5ZM12 7l5 5"/></svg>`,
  volume:`<svg viewBox="0 0 24 24"><path d="M4 9v6h4l5 4V5L8 9H4Z"/><path d="M16.5 9a4 4 0 0 1 0 6"/></svg>`,
  keyboard:`<svg viewBox="0 0 24 24"><path d="M5 6h14a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2Z"/><path d="M7 10h.01M11 10h.01M15 10h.01M7 14h10"/></svg>`,
  tag:`<svg viewBox="0 0 24 24"><path d="M12.5 3H4v8.5L12.5 20 20 12.5 12.5 3Z"/><path d="M7.5 7.5h.01"/></svg>`,
  settings:`<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="3"/><path d="M12 3v3M12 18v3M3 12h3M18 12h3M5.6 5.6l2.1 2.1M16.3 16.3l2.1 2.1M18.4 5.6l-2.1 2.1M7.7 16.3l-2.1 2.1"/></svg>`,
};
function presetIcon(name){
  const k=String(name).toLowerCase();
  const paths={none:"M9 9h6v10l4-3H9ZM5 5l14 14",male:"M12 4a3.2 3.2 0 0 1 3.2 3.2v3.6a3.2 3.2 0 0 1-6.4 0V7.2A3.2 3.2 0 0 1 12 4ZM6 20c.8-3.2 3-5 6-5s5.2 1.8 6 5",
    female:"M12 4c2 0 3.4 1.6 3.4 3.8v2.4C15.4 12.4 14 14 12 14s-3.4-1.6-3.4-3.8V7.8C8.6 5.6 10 4 12 4ZM9 14c-2 1.4-2.8 3.4-3 6h12c-.2-2.6-1-4.6-3-6",
    baby:"M12 5a3.5 3.5 0 0 1 3.5 3.5v3A3.5 3.5 0 0 1 12 15a3.5 3.5 0 0 1-3.5-3.5v-3A3.5 3.5 0 0 1 12 5ZM6.5 18.5c1-2.2 3-3.5 5.5-3.5s4.5 1.3 5.5 3.5",
    helium:"M12 3.5a4.5 5 0 0 1 4.5 5 4.5 5 0 0 1-9 0 4.5 5 0 0 1 4.5-5ZM11 13c-.6 2.4.9 3.6 0 6M13 13c.6 2.4-.9 3.6 0 6",
    robot:"M7 8h10v9H7ZM12 8V5M11 5h2M9.6 12h1.4M13 12h1.4M7 14H5v-3h2M17 14h2v-3h-2",
    alien:"M12 3.5c4 0 6.5 3 6.5 7.5 0 4.4-2.7 9.5-6.5 9.5s-6.5-5.1-6.5-9.5C5.5 6.5 8 3.5 12 3.5ZM9 10h2M13 10h2",
    atari:"M7 8h10a2 2 0 0 1 2 2v4a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2v-4a2 2 0 0 1 2-2ZM9 11v2M8 12h2M14.5 11.5h.01M16 13.5h.01",
    clone:"M9 5.5a2.6 2.6 0 0 1 2.6 2.6v2.3a2.6 2.6 0 0 1-5.2 0V8.1A2.6 2.6 0 0 1 9 5.5ZM4 19c.6-2.5 2.4-4 5-4M15 5.5a2.6 2.6 0 0 1 2.6 2.6v2.3a2.6 2.6 0 0 1-5.2 0V8.1A2.6 2.6 0 0 1 15 5.5ZM10 19c.6-2.5 2.4-4 5-4",
    mutation:"M5 6c4 1.5 10 1.5 14 0M5 12c4 1.5 10 1.5 14 0M5 18c4 1.5 10 1.5 14 0M7 4.8v14.4M17 4.8v14.4",
    radio:"M4.5 9h15v10h-15ZM8 9 16 4M8 14.5h.01M11.5 14.5h.01",
    megaphone:"M4 10l12-4v12L4 14ZM4 10v4M18.5 9.5a3.5 3.5 0 0 1 0 5M7 14.5V19",
    helmet:"M5.5 14v-1.5a6.5 6.5 0 0 1 13 0V14M5.5 14h13v3.5h-13ZM9.5 15.8h5",
    cave:"M3.5 20 12 5l8.5 15M8.5 20 12 13l3.5 7",hall:"M4 20V9l8-4.5L20 9v11M4 20h16M8.5 20v-6M12 20v-6M15.5 20v-6",
    silence:"M4 9.5h4l4-3.5v12l-4-3.5H4ZM15 9.5l5 5M20 9.5l-5 5",
  };
  return SVGP(paths[k]||paths.male);
}

// State
let S=null,tab="voice",hkTarget=null;
const playingIds=new Set(),pausedIds=new Set(),previewIds=new Set();
let soundView="grid",searchQuery="",settingsRenderedOnce=false,editingPreset=-1,editorDismissed=false;
let editorChain=[],editorChainExplicit=false,editorChainDirty=false,editorModuleParams={},editingModuleId=null,chainDragIndex=-1,ambientTimer=null;
let presetClickGuardUntil=0;
let editorOriginalPreset=null;
let activeTag=null;                 // selected tag filter chip
let tagState=null;                  // {id, tags} for the tag dialog
let editState=null;                 // audio editor working copy
let ewDrag=null;                    // {mode:'trim'|'sel', side, anchor}
let liveTimer=null; // debounce for live preset updates
const editorFaders=[], voiceFaders=[];

const TITLES={voice:"Голосовые эффекты",sounds:"Саундборд",ai:"AI-голос",settings:"Устройства"};
const EQ_FREQS=[200,400,800,1600,3200,6400,10000,12000];
const SPACE_OPTS=["none","room","hall","cave","helmet","radio","megaphone"];
const EFFECT_OPTS=["none","robot","alien","atari","clone","silence"];
const LAB_MODULES=[
  {id:"pitch",label:"Pitch",desc:"транспозиция",icon:"M3 12h4l3-7 4 14 3-7h4"},
  {id:"formant",label:"Formant",desc:"форма резонанса",icon:"M4 15V9m4 9V6m4 13V5m4 13V6m4 9V9"},
  {id:"vocoder",label:"Vocoder",desc:"8 полос",icon:"M4 6v12m4-9v6m4-12v18m4-14v10m4-7v4"},
  {id:"bitcrusher",label:"Bitcrusher",desc:"6-bit / downsample",icon:"M3 12h3l2-6 3 12 3-12 3 12 2-6h2"},
  {id:"ringmod",label:"Ring Modulator",desc:"несущая 110 Hz",icon:"M3 12c3-8 6-8 9 0s6 8 9 0M3 12c3 8 6 8 9 0s6-8 9 0"},
  {id:"distortion",label:"Distortion",desc:"сатурация",icon:"M3 17c4 0 3-10 7-10s3 10 7 10 3-6 4-6"},
  {id:"chorus",label:"Chorus",desc:"модулированная задержка",icon:"M3 8c3-5 6 5 9 0s6 5 9 0M3 16c3-5 6 5 9 0s6 5 9 0"},
  {id:"reverb",label:"Reverb",desc:"пространство",icon:"M4 17V8l4-3 4 3v9M14 17V5l3-2 3 2v12M4 20h16"},
  {id:"delay",label:"Delay",desc:"feedback echo",icon:"M4 8h10a4 4 0 0 1 0 8H9M4 8l3-3M4 8l3 3M9 16l-3 3M9 16l-3-3"},
  {id:"eq",label:"8-band EQ",desc:"8 полос эквалайзера",icon:"M4 18v-5m4 5V8m4 10V5m4 13V9m4 9v-6"},
  {id:"tremolo",label:"Tremolo",desc:"амплитудное вибрато",icon:"M3 12h3l3-7 3 14 3-7h3"},
  {id:"flanger",label:"Flanger",desc:"короткая модулированная задержка",icon:"M3 12c4-10 8 10 12 0M4 17c4-8 8 8 12 0"},
  {id:"phaser",label:"Phaser",desc:"сканирующие all-pass фильтры",icon:"M3 12c2-8 4 8 6 0s4 8 6 0M3 18c2-6 4 6 6 0s4 6 6 0"},
  {id:"wahwah",label:"Wah Wah",desc:" sweeper резонанса",icon:"M4 17l4-10 4 14 4-14 4 10"},
  {id:"stutter",label:"Stutter",desc:"повтор фрагмента",icon:"M4 8h5v8H4ZM11 8h5v8h-5ZM18 8h3v8h-3"},
  {id:"stereowidth",label:"Stereo Width",desc:"расширение стерео",icon:"M3 7l7 5-7 5M21 7l-7 5 7 5"},
  {id:"compressor",label:"Compressor",desc:"компрессия динамики",icon:"M3 18h4l3-10 4 10 3-6 4 6"},
  {id:"octaver",label:"Octaver",desc:"октава вниз",icon:"M4 8h5v4H4ZM11 12h4v4h-4ZM17 16h4v4h-4"},
];
const LEGACY_CHAIN={
  room:["Room","реверберация"],hall:["Hall","реверберация"],cave:["Cave","пещера"],
  helmet:["Helmet","фильтр + реверберация"],radio:["Radio","полосовой фильтр + сатурация"],
  megaphone:["Megaphone","мегафон"],robot:["Robot","comb + ring mod"],alien:["Alien","ring mod"],
  atari:["Atari","bitcrush"],clone:["Clone","двойный голос"],silence:["Silence","тишина"],
};
const MODULE_PARAM_DEFS={
  pitch:[
    {key:"value",label:"Полутона",min:-24,max:24,step:1,unit:" st"},
    {key:"value2",label:"Глубина вибрато",min:0,max:12,step:.1,unit:" st"},
    {key:"value3",label:"Скорость вибрато",min:0,max:10,step:.1,unit:" Hz"},
  ],
  formant:[
    {key:"value",label:"Форманта",min:-24,max:24,step:1,unit:" st"},
    {key:"value2",label:"База форманты",min:70,max:250,step:1,unit:" Hz"},
  ],
  vocoder:[
    {key:"value",label:"Несущая",min:30,max:500,step:1,unit:" Hz"},
    {key:"value2",label:"Атака",min:1,max:50,step:1,unit:" ms"},
    {key:"value3",label:"Отпускание",min:10,max:500,step:5,unit:" ms"},
  ],
  bitcrusher:[
    {key:"value",label:"Битность",min:2,max:12,step:1,unit:" bit"},
    {key:"value2",label:"Даунсэмплинг",min:.1,max:1,step:.05,unit:"%",percent:true},
  ],
  ringmod:[
    {key:"value",label:"Частота",min:20,max:1200,step:1,unit:" Hz"},
    {key:"value2",label:"Глубина",min:0,max:1,step:.01,unit:"%",percent:true},
  ],
  distortion:[{key:"value",label:"Drive",min:0,max:1,step:.01,unit:"%",percent:true}],
  chorus:[
    {key:"value",label:"Rate",min:.05,max:5,step:.01,unit:" Hz"},
    {key:"value2",label:"Depth",min:0,max:20,step:.1,unit:" ms"},
    {key:"value3",label:"Базовая задержка",min:8,max:50,step:1,unit:" ms"},
  ],
  reverb:[
    {key:"value",label:"Decay",min:.1,max:6,step:.1,unit:" s"},
    {key:"value2",label:"Size",min:.1,max:1,step:.01,unit:"%",percent:true},
    {key:"value3",label:"Damping",min:0,max:1,step:.01,unit:"%",percent:true},
  ],
  delay:[
    {key:"value",label:"Время",min:20,max:800,step:10,unit:" ms"},
    {key:"value2",label:"Feedback",min:0,max:.9,step:.01,unit:"%",percent:true},
  ],
  eq:[],
  tremolo:[
    {key:"value",label:"Rate",min:.1,max:20,step:.1,unit:" Hz"},
    {key:"value2",label:"Depth",min:0,max:1,step:.01,unit:"%",percent:true},
  ],
  flanger:[
    {key:"value",label:"Rate",min:.05,max:5,step:.01,unit:" Hz"},
    {key:"value2",label:"Depth",min:.5,max:15,step:.1,unit:" ms"},
    {key:"value3",label:"Feedback",min:0,max:.8,step:.01,unit:"%",percent:true},
  ],
  phaser:[
    {key:"value",label:"Rate",min:.1,max:10,step:.1,unit:" Hz"},
    {key:"value2",label:"Depth",min:0,max:1,step:.01,unit:"%",percent:true},
  ],
  wahwah:[
    {key:"value",label:"Rate",min:.1,max:10,step:.1,unit:" Hz"},
    {key:"value2",label:"Центр",min:200,max:3000,step:10,unit:" Hz"},
    {key:"value3",label:"Диапазон",min:0,max:2,step:.05,unit:" oct"},
  ],
  stutter:[
    {key:"value",label:"Фрагмент",min:40,max:500,step:10,unit:" ms"},
    {key:"value2",label:"Повторы",min:1,max:8,step:1,unit:"x"},
  ],
  stereowidth:[{key:"value",label:"Width",min:0,max:3,step:.05,unit:"x"}],
  compressor:[
    {key:"value",label:"Threshold",min:-40,max:0,step:1,unit:" dB"},
    {key:"value2",label:"Ratio",min:1,max:12,step:.1,unit:":1"},
    {key:"value3",label:"Makeup",min:0,max:12,step:.1,unit:" dB"},
  ],
  octaver:[{key:"value",label:"Mix",min:0,max:1,step:.01,unit:"%",percent:true}],
};
function defaultModuleParams(id){
  const p={mix:1,value:0,value2:0,value3:0,eq:Array(8).fill(0)};
  if(id==="formant")p.value2=150;
  else if(id==="vocoder"){p.value=100;p.value2=5;p.value3=80;}
  else if(id==="bitcrusher"){p.value=6;p.value2=.55;}
  else if(id==="ringmod"){p.value=110;p.value2=.85;}
  else if(id==="distortion")p.value=.55;
  else if(id==="chorus"){p.value=.33;p.value2=3.5;p.value3=18;}
  else if(id==="reverb"){p.value=1.8;p.value2=.55;p.value3=.5;}
  else if(id==="delay"){p.value=280;p.value2=.34;}
  else if(id==="tremolo"){p.value=4;p.value2=.5;}
  else if(id==="flanger"){p.value=.5;p.value2=4;p.value3=.35;}
  else if(id==="phaser"){p.value=.5;p.value2=.8;}
  else if(id==="wahwah"){p.value=2;p.value2=800;p.value3=1;}
  else if(id==="stutter"){p.value=180;p.value2=2;}
  else if(id==="stereowidth")p.value=1.5;
  else if(id==="compressor"){p.value=-18;p.value2=4;}
  else if(id==="octaver")p.value=.5;
  return p;
}
function normaliseModuleParams(source,chain,preset){
  const src=source&&typeof source==="object"?source:{};
  const out=JSON.parse(JSON.stringify(src));
  for(const id of chain){
    if(!isLabModule(id))continue;
    const d=defaultModuleParams(id),had=Object.prototype.hasOwnProperty.call(src,id);
    const raw=had&&src[id]&&typeof src[id]==="object"?src[id]:{};
    const num=(key,fallback)=>raw[key]!=null&&Number.isFinite(+raw[key])?+raw[key]:fallback;
    const rawEq=Array.isArray(raw.eq)?raw.eq:null;
    const fallbackEq=(id==="eq"&&!rawEq&&preset&&Array.isArray(preset.eq))?preset.eq:null;
    out[id]={
      mix:Math.max(0,Math.min(1,num("mix",d.mix))),
      value:num("value",d.value),value2:num("value2",d.value2),value3:num("value3",d.value3),
      eq:Array.from({length:8},(_,b)=>{
        const source=rawEq||fallbackEq||d.eq;
        const v=Number.isFinite(+source[b])?+source[b]:d.eq[b];
        return Math.max(-12,Math.min(12,v));
      }),
    };
    if(id==="pitch"){
      if(!Object.prototype.hasOwnProperty.call(raw,"value"))out[id].value=+preset.transpose||0;
      if(!Object.prototype.hasOwnProperty.call(raw,"value2"))out[id].value2=+preset.lfoDepth||0;
      if(!Object.prototype.hasOwnProperty.call(raw,"value3"))out[id].value3=+preset.lfoRate||0;
    }else if(id==="formant"&&!Object.prototype.hasOwnProperty.call(raw,"value")){
      out[id].value=+preset.formant||0;
    }
  }
  return out;
}
function isLabModule(id){return LAB_MODULES.some(m=>m.id===id);}
function chainInfo(id){
  const m=LAB_MODULES.find(x=>x.id===id);
  if(m)return {label:m.label,desc:m.desc,icon:m.icon,lab:true};
  const l=LEGACY_CHAIN[id]||[id,"совместимый модуль"];
  return {label:l[0],desc:l[1],icon:"M4 7h16v10H4Z",lab:false};
}
function chainForEditor(p){
  const raw=Array.isArray(p.chain)?p.chain.filter(id=>id&&id!=="none"):[];
  if(p.chainExplicit||raw.some(isLabModule))return raw.slice(0,24);
  const out=[];
  if((p.eq||[]).some(v=>Math.abs(+v||0)>0.001))out.push("eq");
  if(+p.transpose||+p.lfoDepth)out.push("pitch");
  if(+p.formant)out.push("formant");
  if(raw.length)out.push(...raw);
  else{
    if(p.space&&p.space!=="none")out.push(p.space);
    if(p.effect&&p.effect!=="none")out.push(p.effect);
  }
  return out.slice(0,24);
}

// ---- Custom vertical fader -------------------------------------------------
// Native <input type=range> vertical mode breaks in WebView2 (global CSS sets
// height:4px, the thumb jumps to max, etc.). These div-based faders use pointer
// events and are fully predictable.
function makeFader(opt){
  const min=(opt.min!=null)?opt.min:-12, max=(opt.max!=null)?opt.max:12, step=(opt.step!=null)?opt.step:0.5;
  let v=(opt.value!=null)?opt.value:0;
  const el=document.createElement("div");
  el.className="fader";
  el.innerHTML='<div class="fader-track"><div class="fader-center"></div>'+
               '<div class="fader-fill"></div><div class="fader-knob"></div></div>';
  const track=el.querySelector(".fader-track");
  const fill=el.querySelector(".fader-fill");
  const knob=el.querySelector(".fader-knob");
  function render(){
    const t=(v-min)/(max-min);                 // 0 = bottom, 1 = top
    knob.style.bottom="calc("+(t*100).toFixed(2)+"% - 5.5px)";
    if(v>=0){fill.style.bottom="50%";fill.style.height=((t-0.5)*100).toFixed(2)+"%";}
    else{fill.style.bottom=(t*100).toFixed(2)+"%";fill.style.height=((0.5-t)*100).toFixed(2)+"%";}
  }
  function apply(e){
    const r=track.getBoundingClientRect();
    if(!r.height)return;
    let t=1-(e.clientY-r.top)/r.height;        // top = max
    t=Math.max(0,Math.min(1,t));
    let nv=min+t*(max-min);
    if(step)nv=Math.round(nv/step)*step;
    nv=Math.max(min,Math.min(max,nv));
    if(nv!==v){v=nv;render();if(opt.onChange)opt.onChange(v);}
  }
  let dragging=false;
  track.addEventListener("pointerdown",e=>{
    e.preventDefault();e.stopPropagation();
    dragging=true;
    try{track.setPointerCapture(e.pointerId);}catch(_){}
    apply(e);
  });
  track.addEventListener("pointermove",e=>{if(dragging)apply(e);});
  const end=()=>{dragging=false;};
  track.addEventListener("pointerup",end);
  track.addEventListener("pointercancel",end);
  render();
  return {el,get value(){return v;},set value(x){v=Math.max(min,Math.min(max,+x||0));render();}};
}

function buildEq(container,store,onChange){
  container.innerHTML="";store.length=0;
  for(let b=0;b<8;b++){
    const col=document.createElement("div");col.className="eq-col";
    const out=document.createElement("output");out.className="mono";out.textContent="0";
    const f=makeFader({value:0,onChange:v=>{
      out.textContent=(v>0?"+":"")+v.toFixed(1);
      if(onChange)onChange(b,v);
    }});
    const freq=document.createElement("span");freq.className="eq-freq mono";
    freq.textContent=EQ_FREQS[b]>=1000?(EQ_FREQS[b]/1000)+"k":String(EQ_FREQS[b]);
    col.appendChild(out);col.appendChild(f.el);col.appendChild(freq);
    container.appendChild(col);store.push(f);
  }
}
function setEqFaders(store,eq){
  for(let b=0;b<8;b++){
    const v=+(eq&&eq[b]!=null?eq[b]:0)||0;
    if(store[b])store[b].value=v;
    const out=store[b]&&store[b].el.parentElement.querySelector("output");
    if(out)out.textContent=(v>0?"+":"")+v.toFixed(1);
  }
}
function readEqFaders(store){return store.map(f=>+f.value.toFixed(2));}

function demoState(){
  const P=["None","Male","Female","Baby","Helium","Robot","Alien","Atari","Clone","Mutation","Radio","Megaphone","Helmet","Cave","Hall","Silence"]
    .map((n,i)=>({idx:i,name:n,transpose:i===2?4:0,formant:i===2?3:0,
      space:i===10?"radio":i===11?"megaphone":i===12?"helmet":i===13?"cave":i===14?"hall":"none",
      effect:i===5?"robot":i===6?"alien":i===7?"atari":i===8?"clone":i===15?"silence":"none",
      lfoRate:i===9?0.25:0,lfoDepth:i===9?3.5:0,gate:true,comp:true,
      custom:i===5,hotkey:i===5?"Ctrl+Alt+F1":"",eq:[0,0,0,0,0,0,0,0],chain:[],
      ambient:i===10?"radio":i===13?"cave":"none",ambientGain:i===10?0.2:0.18}));
  const pk=[];for(let i=0;i<256;i++)pk.push(0.3+0.6*Math.abs(Math.sin(i*0.08)*Math.cos(i*0.03)));
  return {type:"state",language:vdLocale,dspOn:true,bypassOn:true,duckingOn:false,ambientOn:true,soundFxSend:false,soundboardMonitor:false,monitorOn:false,voiceMonitorOn:false,preset:2,presets:P,
    sounds:[{id:1,name:"airhorn",dur:2.1,gain:1,loop:false,hotkey:"Ctrl+Alt+1",eq:[0,0,0,0,0],peaks:pk,
      rawPeaks:pk,sourceDur:2.1,tags:["hotkey","сигнал"],edits:{trimStart:0,trimEnd:0,gain:1,regions:[]}},
      {id:2,name:"bell",dur:1.2,gain:0.8,loop:false,hotkey:"",eq:[0,0,0,0,0],peaks:pk,
      rawPeaks:pk,sourceDur:1.2,tags:["мелочи"],edits:{trimStart:0,trimEnd:0,gain:1,regions:[]}},
      {id:3,name:"gong",dur:4.0,gain:1,loop:false,hotkey:"",eq:[0,0,0,0,0],peaks:pk,
      rawPeaks:pk,sourceDur:4.0,tags:[],edits:{trimStart:0,trimEnd:0,gain:1,regions:[]}}],
    allTags:["hotkey","мелочи","сигнал"],autoStart:false,runAsAdmin:false,hideConsole:true,elevated:false,
    ai:{enabled:true,ready:true,voice:"Narrator (male)",device:"GTX 1080",usesGpu:true,provider:"dml",
      modelsInstalled:true,latency:290,underruns:0,pitch:0,margin:150,fade:30,autolimit:true,flapLimit:8,
      voices:[{name:"Narrator (male)",gender:"male",pitch:0},{name:"Presenter (female)",gender:"female",pitch:12}]},
    devices:{capture:[{idx:0,name:"Микрофон (USB)",def:true}],
      playback:[{idx:0,name:"Line 1 (VAC)",def:true},{idx:1,name:"Динамики",def:false}],
      curCapture:0,curPlayback:0,curMonitor:-1,savedMonitor:-1,running:true},
    mic:0.03,voiceLatency:0,e2eLatency:47,micUnderruns:0,latencyMode:0,
    vadThresholdDb:-48,vadSleeping:false,hotkeys:{voice:"Ctrl+Alt+V",ai:"",stopAll:""}};
}

function setSwitch(el,on){el.classList.toggle("on",!!on);el.setAttribute("aria-pressed",String(!!on));}
function updateTopLive(){if(!S)return;
  const p=[];
  if(S.e2eLatency!=null)p.push("Тракт "+Math.round(S.e2eLatency)+" мс");
  if(S.dspOn)p.push("DSP "+Math.round(S.voiceLatency)+" мс");
  if(S.ai.ready&&S.ai.enabled)p.push("AI "+Math.round(S.ai.latency)+" мс");
  if(S.soundFxSend)p.push("FX Send");
  if(S.ambientOn&&S.presets[S.preset]&&S.presets[S.preset].ambient!=="none")p.push("Ambient");
  if(S.vadSleeping)p.push("VAD sleep");
  $("topLive").textContent=p.join("   ·   ");}

function renderHotkeyBadges(){
  if(!S)return;
  const hk=S.hotkeys||{};
  $("hkVoice").textContent=hk.voice||"— клик —";
  $("hkAi").textContent=hk.ai||"— клик —";
  $("hkStopAll").textContent=hk.stopAll||"— клик —";
}

function syncOriginalOutputSelection(){
  if(!S||!S.devices)return;
  const d=S.devices,idx=d.curMonitor>=0?d.curMonitor:d.savedMonitor;
  if(idx>=0)$("origSelect").value=String(idx);
}

function renderState(){
  if(!S)return;
  setSwitch($("powerBtn"),S.dspOn&&S.bypassOn);setSwitch($("aiPowerBtn"),S.ai.enabled);
  setSwitch($("ambientPowerBtn"),S.ambientOn);setSwitch($("fxSendBtn"),S.soundFxSend);
  $("fxPresetLabel").textContent=S.presets[S.preset]?S.presets[S.preset].name:"";
  $("uiLanguage").value=S.language||vdLocale;
  setSwitch($("monitorBtn"),!!S.voiceMonitorOn);
  $("monitorChk").checked=!!S.voiceMonitorOn;
  setSwitch($("soundboardMonitorBtn"),!!S.soundboardMonitor);
  syncOriginalOutputSelection();
  renderPresets();renderTagFilter();renderSounds();renderAi();renderHotkeyBadges();
  if(!settingsRenderedOnce){renderSettings();settingsRenderedOnce=true;}
  if(S.vadThresholdDb!=null&&document.activeElement!==$("vadThreshold")){
    $("vadThreshold").value=Math.round(S.vadThresholdDb);
    $("vadThresholdVal").textContent=Math.round(S.vadThresholdDb)+" dB";
  }
  updateTopLive();
  // Auto-open the editor for the currently active preset (safe: don't let
  // editor errors block the uiReady handshake).
  try {
    if(S.preset>=0&&S.preset<S.presets.length&&editingPreset<0&&!editorDismissed){
      openPresetEditor(S.preset);
    }
  } catch(e) { /* editor is optional */ }
}

// ---- Presets ----
function updateOverrideAvailability(){
  if(!S)return;
  const p=S.presets[S.preset];
  const explicit=!!(p&&(p.chainExplicit||(Array.isArray(p.chain)&&p.chain.some(isLabModule))));
  const has=id=>!explicit||(Array.isArray(p.chain)&&p.chain.includes(id));
  $("pitchOv").disabled=!has("pitch");$("formantOv").disabled=!has("formant");
  $("pitchOv").title=$("pitchOv").disabled?"Добавь Pitch в VoiceLab":"";
  $("formantOv").title=$("formantOv").disabled?"Добавь Formant в VoiceLab":"";
  const eqOff=!has("eq");$("voiceEq").classList.toggle("controls-disabled",eqOff);
  $("voiceEq").title=eqOff?"Добавь 8-band EQ в VoiceLab":"";
}
function renderPresets(){
  const grid=$("presetGrid");grid.innerHTML="";
  for(const p of S.presets){
    const tile=document.createElement("div");
    tile.className="tile"+(p.idx===S.preset?" active":"");
    const sub=[];
    if(p.transpose)sub.push((p.transpose>0?"+":"")+p.transpose);
    if(p.chainExplicit||(p.chain&&p.chain.length))sub.push(p.chain.length?p.chain.slice(0,3).join("→"):"direct");
    else{if(p.space!=="none")sub.push(p.space);if(p.effect!=="none")sub.push(p.effect);}
    if(p.ambient&&p.ambient!=="none")sub.push("amb:"+p.ambient.split("/").pop());
    tile.innerHTML=presetIcon(p.name)+`<div class="t-name">${esc(p.name)}</div><div class="t-sub">${esc(sub.join(" "))}</div>`+
      (p.hotkey?`<span class="keycap" data-hk="preset" data-idx="${p.idx}">${esc(p.hotkey)}</span>`:"");
    // Click = apply + open editor
    tile.addEventListener("click",(e)=>{
      if(e.target.classList.contains("keycap")){startHkCapture({target:"preset",idx:p.idx,combo:p.hotkey});return;}
      presetClickGuardUntil=performance.now()+250;
      if(editingPreset>=0&&editingPreset!==p.idx)restoreEditorPreset(false);
      send({cmd:"preset",idx:p.idx});S.preset=p.idx;renderPresets();
      $("fxPresetLabel").textContent=p.name;
      openPresetEditor(p.idx); // auto-open editor with this preset
    });
    // Edit button
    const edt=document.createElement("button");edt.className="t-del t-edit";edt.innerHTML=ICONS.edit;
    edt.title="Редактировать";edt.addEventListener("click",(e)=>{e.stopPropagation();openPresetEditor(p.idx);});
    tile.appendChild(edt);
    grid.appendChild(tile);
  }
  updateOverrideAvailability();
}

// ---- Preset editor panel ----
function collectEditorDef(){
  const eq=readEqFaders(editorFaders);
  const p=editingPreset>=0?S.presets[editingPreset]:null;
  return {
    name:$("peName").value.trim(),
    transpose:+$("pePitch").value,
    formant:+$("peFormant").value,
    timbre:+$("peTimbre").value,
    strength:+$("peStrength").value,
    lfoRate:p&&p.lfoRate!=null?+p.lfoRate:0,
    lfoDepth:p&&p.lfoDepth!=null?+p.lfoDepth:0,
    gate:p&&p.gate!=null?!!p.gate:true,
    comp:p&&p.comp!=null?!!p.comp:true,
    space:$("peSpace").value,
    effect:$("peEffect").value,
    chain:editorChainDirty?[...editorChain]:(p&&Array.isArray(p.chain)?[...p.chain]:[]),
    chainExplicit:editorChainDirty?true:!!(p&&p.chainExplicit),
    moduleParams:JSON.parse(JSON.stringify(editorModuleParams)),
    eq:eq,
    ambient:$("peAmbient").value,
    ambientGain:+$("peAmbientGain").value/100,
  };
}

function setAmbientEditor(p){
  const sel=$("peAmbient");
  sel.querySelectorAll("optgroup[data-custom]").forEach(o=>o.remove());
  const files=(S&&S.ambients||[]).slice();
  if(p.ambient&&p.ambient.indexOf("ambiences/")===0&&!files.some(f=>f.id===p.ambient))
    files.push({id:p.ambient,name:p.ambient.split("/").pop()});
  if(files.length){
    const group=document.createElement("optgroup");group.label="Мои эмбиенты";group.dataset.custom="1";
    for(const f of files){
      const o=document.createElement("option");o.value=f.id;o.textContent=f.name||f.id.split("/").pop();
      group.appendChild(o);
    }
    sel.appendChild(group);
  }
  sel.value=p.ambient||"none";
  const gain=Math.round((p.ambientGain!=null?+p.ambientGain:0.18)*100);
  $("peAmbientGain").value=gain;$("peAmbientGainVal").textContent=gain+"%";
}

function openPresetEditor(idx){
  if(idx<0||idx>=S.presets.length)return;
  editingPreset=idx;editorDismissed=false;
  editorOriginalPreset=JSON.parse(JSON.stringify(S.presets[idx]));
  const p=S.presets[idx];
  $("presetEditor").classList.remove("hidden");
  $("peName").value=p.name;
  $("pePitch").value=p.transpose;$("pePitchVal").textContent=(p.transpose>0?"+":"")+p.transpose;
  $("peFormant").value=p.formant;$("peFormantVal").textContent=(p.formant>0?"+":"")+p.formant;
  const timbre=+(p.timbre||0),strength=(p.strength!=null?p.strength:1);
  $("peTimbre").value=timbre;$("peTimbreVal").textContent=timbre.toFixed(1);
  $("peStrength").value=strength;$("peStrengthVal").textContent=strength.toFixed(2);
  $("peSpace").value=p.space||"none";
  $("peEffect").value=p.effect||"none";
  editorChainExplicit=!!p.chainExplicit||(Array.isArray(p.chain)&&p.chain.some(isLabModule));
  editorChainDirty=editorChainExplicit;
  editorChain=chainForEditor(p);
  editorModuleParams=normaliseModuleParams(p.moduleParams,editorChain,p);
  editingModuleId=null;$("peModuleParams").classList.add("hidden");
  renderChainList();renderModulePalette();renderModuleControls();
  setAmbientEditor(p);
  setEqFaders(editorFaders,p.eq);
}
function restoreEditorPreset(notifyHost){
  if(editingPreset<0||!editorOriginalPreset)return;
  S.presets[editingPreset]=JSON.parse(JSON.stringify(editorOriginalPreset));
  if(notifyHost)send({cmd:"resetPreset",idx:editingPreset});
  renderPresets();
}
function closePresetEditor(){
  restoreEditorPreset(true);
  $("presetEditor").classList.add("hidden");
  editingPreset=-1;editorDismissed=true;editorChain=[];editorChainExplicit=false;editorChainDirty=false;editorModuleParams={};editingModuleId=null;chainDragIndex=-1;editorOriginalPreset=null;
}

function sendLiveUpdate(){
  if(editingPreset<0)return;
  const idx=editingPreset;
  clearTimeout(liveTimer);
  liveTimer=setTimeout(()=>{
    if(editingPreset!==idx)return;
    const def=collectEditorDef();
    send({cmd:"livePreset",idx,...def});
  },150);
}

function commitEditorChain(){
  editorChainExplicit=true;editorChainDirty=true;
  editorModuleParams=normaliseModuleParams(editorModuleParams,editorChain,S.presets[editingPreset]||{});
  if(editingPreset>=0&&S.presets[editingPreset]){
    S.presets[editingPreset].chain=[...editorChain];
    S.presets[editingPreset].chainExplicit=true;
    S.presets[editingPreset].moduleParams=JSON.parse(JSON.stringify(editorModuleParams));
  }
  renderChainList();renderModulePalette();renderModuleControls();updateOverrideAvailability();sendLiveUpdate();
}
function moveChain(from,to){
  if(from<0||from>=editorChain.length||to<0||to>=editorChain.length||from===to)return;
  const item=editorChain.splice(from,1)[0];editorChain.splice(to,0,item);commitEditorChain();
}
function renderChainList(){
  const el=$("peChainList");el.innerHTML="";chainDragIndex=-1;
  if(!editorChain.length){el.innerHTML='<div class="chain-empty">Нет модулей — сигнал идёт напрямую</div>';return;}
  editorChain.forEach((id,i)=>{
    const info=chainInfo(id);
    const row=document.createElement("div");row.className="chain-row"+(editingModuleId===id?" selected":"");row.draggable=true;
    row.setAttribute("role","listitem");row.dataset.index=i;
    row.setAttribute("aria-label",`${i+1}. ${info.label}`);
    row.innerHTML=`<span class="drag-handle" title="Перетащить" aria-hidden="true"><svg viewBox="0 0 12 16"><circle cx="3" cy="3" r="1"/><circle cx="9" cy="3" r="1"/><circle cx="3" cy="8" r="1"/><circle cx="9" cy="8" r="1"/><circle cx="3" cy="13" r="1"/><circle cx="9" cy="13" r="1"/></svg></span>`+
      `<span class="chain-index">${String(i+1).padStart(2,"0")}</span>`+
      `<span class="chain-info"><b>${esc(info.label)}</b><small>${esc(info.desc)}</small></span>`+
      `<span class="chain-actions"></span>`;
    const actions=row.querySelector(".chain-actions");
    const up=document.createElement("button");up.type="button";up.className="s-ctl";up.textContent="↑";
    up.title="Выше";up.setAttribute("aria-label",`Выше: ${info.label}`);up.disabled=i===0;
    up.addEventListener("click",()=>moveChain(i,i-1));
    const dn=document.createElement("button");dn.type="button";dn.className="s-ctl";dn.textContent="↓";
    dn.title="Ниже";dn.setAttribute("aria-label",`Ниже: ${info.label}`);dn.disabled=i===editorChain.length-1;
    dn.addEventListener("click",()=>moveChain(i,i+1));
    if(info.lab){
      const cfg=document.createElement("button");cfg.type="button";cfg.className="s-ctl";cfg.innerHTML=ICONS.settings;
      cfg.title="Параметры";cfg.setAttribute("aria-label",`Параметры: ${info.label}`);
      cfg.setAttribute("aria-expanded",String(editingModuleId===id));
      cfg.addEventListener("click",()=>selectModule(id));
      actions.appendChild(cfg);
    }
    const rm=document.createElement("button");rm.type="button";rm.className="s-ctl del";rm.textContent="×";
    rm.title="Убрать";rm.setAttribute("aria-label",`Убрать: ${info.label}`);
    rm.addEventListener("click",()=>{
      editorChain.splice(i,1);
      if(editingModuleId===id){editingModuleId=null;$("peModuleParams").classList.add("hidden");}
      commitEditorChain();
    });
    actions.appendChild(up);actions.appendChild(dn);actions.appendChild(rm);
    row.addEventListener("dragstart",e=>{
      chainDragIndex=i;e.dataTransfer.effectAllowed="move";
      e.dataTransfer.setData("text/plain",String(i));
      requestAnimationFrame(()=>row.classList.add("dragging"));
      e.stopPropagation();
    });
    row.addEventListener("dragend",()=>{
      row.classList.remove("dragging");
      el.querySelectorAll(".drop-target").forEach(x=>x.classList.remove("drop-target"));
      chainDragIndex=-1;
    });
    row.addEventListener("dragenter",e=>{e.preventDefault();e.stopPropagation();row.classList.add("drop-target");});
    row.addEventListener("dragover",e=>{e.preventDefault();e.stopPropagation();e.dataTransfer.dropEffect="move";});
    row.addEventListener("dragleave",e=>{e.stopPropagation();row.classList.remove("drop-target");});
    row.addEventListener("drop",e=>{
      e.preventDefault();e.stopPropagation();
      const from=chainDragIndex>=0?chainDragIndex:parseInt(e.dataTransfer.getData("text/plain"),10);
      row.classList.remove("drop-target");moveChain(from,i);
    });
    el.appendChild(row);
  });
}
function formatModuleParam(def,v){
  if(def.percent)return Math.round(v*100)+"%";
  const digits=def.step>=1?0:(def.step<.1?2:1);
  return v.toFixed(digits)+(def.unit||"");
}
function syncModuleSliders(id){
  if(!editorModuleParams[id])return;
  if(id==="pitch"){
    const v=editorModuleParams[id].value;
    $("pePitch").value=v;$("pePitchVal").textContent=(v>0?"+":"")+Math.round(v);
  }else if(id==="formant"){
    const v=editorModuleParams[id].value;
    $("peFormant").value=v;$("peFormantVal").textContent=(v>0?"+":"")+Math.round(v);
  }
}
function selectModule(id){
  if(!isLabModule(id))return;
  editorModuleParams=normaliseModuleParams(editorModuleParams,[id],S.presets[editingPreset]||{});
  editingModuleId=id;renderChainList();renderModuleParams();
}
function renderModuleParams(){
  const panel=$("peModuleParams"),body=$("peModuleParamsBody");
  if(!editingModuleId){panel.classList.add("hidden");return;}
  panel.classList.remove("hidden");$("peModuleParamsTitle").textContent=chainInfo(editingModuleId).label+" · параметры";
  body.innerHTML="";
  const mix={key:"mix",label:"Сила / Mix",min:0,max:1,step:.01,percent:true};
  const defs=[mix,...(MODULE_PARAM_DEFS[editingModuleId]||[])];
  for(const def of defs){
    const row=document.createElement("label");row.className="module-param-row";
    const name=document.createElement("span");name.textContent=def.label;
    const input=document.createElement("input");input.type="range";input.min=def.min;input.max=def.max;input.step=def.step;input.dataset.key=def.key;
    const params=editorModuleParams[editingModuleId];input.value=params[def.key];
    const out=document.createElement("output");out.className="mono";out.textContent=formatModuleParam(def,+input.value);
    input.setAttribute("aria-label",def.label);input.setAttribute("aria-valuetext",out.textContent);
    input.addEventListener("input",()=>{
      let v=Math.max(def.min,Math.min(def.max,+input.value));
      editorModuleParams[editingModuleId][def.key]=v;out.textContent=formatModuleParam(def,v);
      input.setAttribute("aria-valuetext",out.textContent);
      if(editingPreset>=0)S.presets[editingPreset].moduleParams=JSON.parse(JSON.stringify(editorModuleParams));
      editorChainDirty=true;syncModuleSliders(editingModuleId);sendLiveUpdate();
    });
    row.appendChild(name);row.appendChild(input);row.appendChild(out);body.appendChild(row);
  }
  const eqTitle=document.createElement("div");eqTitle.className="panel-title module-eq-title";
  eqTitle.textContent="EQ этого модуля";body.appendChild(eqTitle);
  const eqGrid=document.createElement("div");eqGrid.className="module-eq-grid";
  const params=editorModuleParams[editingModuleId];
  for(let b=0;b<8;b++){
    const band=document.createElement("label");band.className="module-eq-band";
    const freq=document.createElement("span");freq.className="mono";
    freq.textContent=EQ_FREQS[b]>=1000?(EQ_FREQS[b]/1000)+"k":String(EQ_FREQS[b]);
    const input=document.createElement("input");input.type="range";input.min=-12;input.max=12;input.step=.5;
    input.value=params.eq[b];input.setAttribute("aria-label",`EQ ${freq.textContent} Hz`);
    const out=document.createElement("output");out.className="mono";
    out.textContent=(params.eq[b]>0?"+":"")+Number(params.eq[b]).toFixed(1);
    input.setAttribute("aria-valuetext",out.textContent);
    input.addEventListener("input",()=>{
      const v=Math.max(-12,Math.min(12,+input.value));
      editorModuleParams[editingModuleId].eq[b]=v;
      out.textContent=(v>0?"+":"")+v.toFixed(1);
      input.setAttribute("aria-valuetext",out.textContent);
      if(editingPreset>=0)S.presets[editingPreset].moduleParams=JSON.parse(JSON.stringify(editorModuleParams));
      editorChainDirty=true;sendLiveUpdate();
    });
    band.appendChild(freq);band.appendChild(input);band.appendChild(out);eqGrid.appendChild(band);
  }
  body.appendChild(eqGrid);
}
function renderModuleControls(){
  document.querySelectorAll("#presetEditor [data-module]").forEach(row=>{
    if(row.dataset.module==="eq"){
      // Explicit VoiceLab chains use the EQ inside each selected module; the
      // preset-level EQ only belongs to legacy automatic processing.
      row.classList.toggle("hidden",editorChainExplicit);
      return;
    }
    const on=editorChain.includes(row.dataset.module);
    row.classList.toggle("disabled",!on);
    row.title=on?"":`Добавь блок ${chainInfo(row.dataset.module).label} в VoiceLab`;
    row.querySelectorAll("input").forEach(i=>i.disabled=!on);
  });
}
function renderModulePalette(){
  const el=$("peModulePalette");el.innerHTML="";
  for(const m of LAB_MODULES){
    const b=document.createElement("button");b.type="button";b.className="module-btn";
    const used=editorChain.includes(m.id);b.disabled=used;
    b.title=used?`${m.label} уже в цепочке`:`Добавить ${m.label}`;
    b.setAttribute("aria-label",b.title);
    b.innerHTML=SVGP(m.icon)+`<span>${esc(m.label)}</span>`+(used?'<span class="module-state">✓</span>':'');
    b.addEventListener("click",()=>{
      if(editorChain.length>=24){toast("Максимум 24 модуля");return;}
      editorChain.push(m.id);commitEditorChain();selectModule(m.id);
    });
    el.appendChild(b);
  }
}

// Editor events — live apply on every change
$("peClose").addEventListener("click",closePresetEditor);
$("peModuleParamsClose").addEventListener("click",()=>{
  editingModuleId=null;$("peModuleParams").classList.add("hidden");renderChainList();
});
[["pePitch","pePitchVal","semi"],["peFormant","peFormantVal","semi"],
 ["peTimbre","peTimbreVal","dec"],["peStrength","peStrengthVal","dec"]].forEach(([id,val,fmt])=>{
  $(id).addEventListener("input",(e)=>{
    const v=+e.target.value;
    $(val).textContent=fmt==="dec"?v.toFixed(2):(v>0?"+":"")+v;
    if(id==="pePitch"&&editorModuleParams.pitch)editorModuleParams.pitch.value=v;
    if(id==="peFormant"&&editorModuleParams.formant)editorModuleParams.formant.value=v;
    if(editingPreset>=0&&(id==="pePitch"||id==="peFormant"))
      S.presets[editingPreset].moduleParams=JSON.parse(JSON.stringify(editorModuleParams));
    if((id==="pePitch"&&editingModuleId==="pitch")||(id==="peFormant"&&editingModuleId==="formant"))renderModuleParams();
    sendLiveUpdate(); // live apply
  });
});
function setLegacyChainValue(group,value){
  const ids=group==="space"?["room","hall","cave","helmet","radio","megaphone"]:["robot","alien","atari","clone","silence"];
  editorChain=editorChain.filter(id=>!ids.includes(id));
  if(value&&value!=="none")editorChain.push(value);
  commitEditorChain();
}
$("peSpace").addEventListener("change",e=>setLegacyChainValue("space",e.target.value));
$("peEffect").addEventListener("change",e=>setLegacyChainValue("effect",e.target.value));
$("peAmbient").addEventListener("change",e=>{
  if(editingPreset<0)return;
  const p=S.presets[editingPreset];p.ambient=e.target.value;
  send({cmd:"setPresetAmbient",idx:editingPreset,ambient:p.ambient,gain:+$("peAmbientGain").value/100});
});
$("peAmbientGain").addEventListener("input",e=>{
  const gain=+e.target.value,idx=editingPreset;$("peAmbientGainVal").textContent=gain+"%";
  if(idx>=0)S.presets[idx].ambientGain=gain/100;
  clearTimeout(ambientTimer);
  ambientTimer=setTimeout(()=>{
    if(idx<0||editingPreset!==idx)return;
    send({cmd:"setPresetAmbient",idx,ambient:$("peAmbient").value,gain:gain/100});
  },120);
});
$("peAmbientFile").addEventListener("click",()=>{
  if(editingPreset>=0)send({cmd:"ambientFileDialog",idx:editingPreset});
});
$("peSave").addEventListener("click",()=>{
  if(editingPreset<0)return;
  const name=$("peName").value.trim();
  if(!name){toast("Имя обязательно");return;}
  send({cmd:"addPreset",def:collectEditorDef()});
  closePresetEditor();
  toast("Сохранено как «"+name+"»");
});

// Build the preset editor EQ faders — every move applies live to the active voice.
buildEq($("peEq"),editorFaders,()=>sendLiveUpdate());

// ---- Sounds ----
function getFilteredSounds(){
  const q=searchQuery.toLowerCase().trim();
  return S.sounds.filter(s=>{
    if(activeTag && !(s.tags||[]).some(t=>String(t).toLowerCase()===activeTag.toLowerCase()))return false;
    if(!q)return true;
    if(String(s.name).toLowerCase().includes(q))return true;
    return (s.tags||[]).some(t=>String(t).toLowerCase().includes(q));
  });
}

// Tag filter chips above the library. The built-in "hotkey" tag is just
// another chip here — it is maintained by the host, not by the tag editor.
function renderTagFilter(){
  const c=$("tagFilter");if(!c||!S)return;
  c.innerHTML="";
  const mk=(label,count,tag,active)=>{
    const b=document.createElement("button");
    b.className="tag-chip"+(active?" active":"");
    b.textContent=label+" ("+count+")";
    if(tag===vdHotkeyTag())b.classList.add("builtin");
    b.addEventListener("click",()=>{
      activeTag=(activeTag===tag)?null:tag;
      renderTagFilter();renderSounds();
    });
    return b;
  };
  c.appendChild(mk("все",S.sounds.length,null,activeTag===null));
  for(const tag of (S.allTags||[])){
    const n=S.sounds.filter(s=>(s.tags||[]).some(t=>String(t).toLowerCase()===String(tag).toLowerCase())).length;
    c.appendChild(mk(tag,n,tag,activeTag===String(tag).toLowerCase()));
  }
}
function vdHotkeyTag(){return "hotkey";}

// Sound preview auto-restores voice self-monitoring when the clip ends.
let previewTimer=null;
function updatePreviewButtons(){
  document.querySelectorAll("#soundGrid [data-preview], #soundList [data-preview]").forEach(btn=>{
    const row=btn.closest(".scard, .srow");
    const on=!!row&&previewIds.has(+row.dataset.id);
    btn.classList.toggle("on",on);
    btn.setAttribute("aria-pressed",String(on));
    btn.title=on?"Остановить предпрослушивание":"Предпрослушать на динамики";
  });
}
function stopPreviewNow(){
  clearTimeout(previewTimer);previewTimer=null;
  if(host&&previewIds.size)send({cmd:"stopPreview"});
  previewIds.clear();updatePreviewButtons();
  toast("Предпрослушивание остановлено");
}
// One click starts preview, the next one stops it. Starting a different clip
// replaces the previous preview.
function doPreview(s){
  if(previewIds.has(s.id)){stopPreviewNow();return;}
  send({cmd:"preview",id:s.id,idx:+$("origSelect").value});
  previewIds.clear();previewIds.add(s.id);updatePreviewButtons();
  clearTimeout(previewTimer);
  previewTimer=setTimeout(()=>{
    send({cmd:"stopPreview"});
    previewTimer=null;previewIds.clear();updatePreviewButtons();
  },Math.max(1500,Math.ceil((s.dur||2)*1000)+500));
  toast("Предпрослушивание: "+s.name);
}

function makeSoundControls(container,s){
  // play/pause + preview (right-click) + stop + seek + loop + volume + delete + hotkey
  const play=container.querySelector("[data-play]");
  if(play){
    play.addEventListener("click",()=>{
      if(playingIds.has(s.id))send({cmd:"pause",id:s.id});
      else send({cmd:"play",id:s.id});
    });
    // RIGHT-CLICK on play = preview (original output only)
    play.addEventListener("contextmenu",(e)=>{
      e.preventDefault();e.stopPropagation();
      doPreview(s);
    });
  }
  const stop=container.querySelector("[data-stop]");
  if(stop)stop.addEventListener("click",()=>send({cmd:"stop",id:s.id}));
  const prev=container.querySelector("[data-preview]");
  if(prev)prev.addEventListener("click",e=>{
    e.stopPropagation();
    doPreview(s);
  });
  const vol=container.querySelector("[data-vol]");
  if(vol)vol.addEventListener("input",e=>{
    const out=container.querySelector("[data-volval]");
    if(out)out.textContent=e.target.value+"%";
    send({cmd:"volume",id:s.id,pct:+e.target.value});
  });
  const loop=container.querySelector("[data-loop]");
  if(loop)loop.addEventListener("click",()=>{
    send({cmd:"loop",id:s.id,on:!s.loop});s.loop=!s.loop;
    loop.classList.toggle("on",s.loop);loop.setAttribute("aria-pressed",String(!!s.loop));
    const sub=container.querySelector(".s-sub");
    if(sub)sub.textContent=fmtSec(s.dur)+(s.loop?" · loop":"");
  });
  const del=container.querySelector("[data-del]");
  if(del)del.addEventListener("click",async()=>{if(await uiConfirm("Удалить «"+s.name+"»?"))send({cmd:"remove",id:s.id});});
  const hk=container.querySelector("[data-hk]");
  if(hk)hk.addEventListener("click",e=>{e.stopPropagation();startHkCapture({target:"sound",id:s.id,combo:s.hotkey});});
  const tagsBtn=container.querySelector("[data-tags]");
  if(tagsBtn)tagsBtn.addEventListener("click",e=>{e.stopPropagation();openTagEditor(s.id);});
  const editBtn=container.querySelector("[data-edit]");
  if(editBtn)editBtn.addEventListener("click",e=>{e.stopPropagation();openEditDialog(s.id);});
  // Waveform seek
  const wave=container.querySelector("[data-wave]");
  if(wave)wave.addEventListener("click",(e)=>{
    if(!playingIds.has(s.id))return;
    const rect=wave.getBoundingClientRect();
    send({cmd:"seek",id:s.id,frac:Math.max(0,Math.min(1,(e.clientX-rect.left)/rect.width))});
  });
}

// ---- Tags: per-clip editor -------------------------------------------------
function findSound(id){return (S&&S.sounds||[]).find(s=>s.id===id)||null;}
function isHotkeyTag(t){return String(t).toLowerCase()==="hotkey";}

function openTagEditor(id){
  const s=findSound(id);if(!s)return;
  tagState={id:id,tags:(s.tags||[]).filter(t=>!isHotkeyTag(t))};
  $("tagEditName").textContent=s.name;
  $("tagNew").value="";
  renderTagEditChips();
  $("tagOverlay").classList.remove("hidden");
  setTimeout(()=>{try{$("tagNew").focus();}catch(_){}},30);
}
function closeTagEditor(){$("tagOverlay").classList.add("hidden");tagState=null;}
function renderTagEditChips(){
  const c=$("tagEditCurrent");c.innerHTML="";
  if(!tagState)return;
  if(!tagState.tags.length)c.innerHTML='<span class="dim">тегов нет</span>';
  tagState.tags.forEach((t,i)=>{
    const chip=document.createElement("span");chip.className="tag";
    chip.innerHTML=esc(t)+' <b class="tag-x" data-i="'+i+'" title="удалить">×</b>';
    chip.querySelector(".tag-x").addEventListener("click",()=>{
      tagState.tags.splice(i,1);renderTagEditChips();
    });
    c.appendChild(chip);
  });
  const s=findSound(tagState.id);
  if(s&&(s.tags||[]).some(isHotkeyTag)){
    const chip=document.createElement("span");chip.className="tag builtin";
    chip.innerHTML='hotkey <b class="tag-x" title="снять хоткей">×</b>';
    chip.querySelector(".tag-x").addEventListener("click",()=>{
      send({cmd:"hotkeyOff",target:"sound",id:tagState.id});
      toast("Хоткей снят — тег hotkey удалён");
      closeTagEditor();
    });
    c.appendChild(chip);
  }
}
function addTagFromInput(){
  if(!tagState)return;
  const v=($("tagNew").value||"").trim();if(!v)return;
  if(v.toLowerCase()==="hotkey"){toast("Тег hotkey назначается автоматически");$("tagNew").value="";return;}
  if(tagState.tags.some(t=>t.toLowerCase()===v.toLowerCase())){toast("Такой тег уже есть");return;}
  tagState.tags.push(v.slice(0,32));$("tagNew").value="";renderTagEditChips();
}
$("tagAdd").addEventListener("click",addTagFromInput);
$("tagNew").addEventListener("keydown",e=>{if(e.key==="Enter"){e.preventDefault();addTagFromInput();}});
$("tagDone").addEventListener("click",()=>{
  if(tagState)send({cmd:"setSoundTags",id:tagState.id,tags:tagState.tags});
  closeTagEditor();
});

// ---- In-app audio editor ---------------------------------------------------
function closeEditDialog(){$("editOverlay").classList.add("hidden");editState=null;ewDrag=null;}
function openEditDialog(id){
  const s=findSound(id);if(!s)return;
  const e=s.edits||{};
  const dur=+(s.sourceDur||s.dur||0);
  editState={
    id:id,name:s.name,dur:dur,
    peaks:(s.rawPeaks&&s.rawPeaks.length)?s.rawPeaks:(s.peaks||[]),
    trimStart:Math.max(0,+e.trimStart||0),
    trimEnd:+(e.trimEnd>0?e.trimEnd:dur),
    gain:(e.gain!=null?+e.gain:1),
    regions:(e.regions||[]).map(r=>({start:+r.start,end:+r.end,gain:+r.gain})),
    sel:null
  };
  $("ewTitle").textContent="Редактор: "+s.name;
  $("ewGain").value=Math.round(editState.gain*100);
  $("ewGainVal").textContent=Math.round(editState.gain*100)+"%";
  drawEditorWave();updateEditorValues();renderEditorRegions();
  $("editOverlay").classList.remove("hidden");
}
function editorPct(t){return Math.max(0,Math.min(100,(t/Math.max(0.001,editState.dur))*100));}
function drawEditorWave(){
  const el=$("ewWave"),peaks=editState.peaks||[];
  const step=Math.max(1,Math.ceil(peaks.length/128));
  let d="";
  for(let i=0;i<peaks.length;i+=step){
    let m=0;for(let j=i;j<Math.min(peaks.length,i+step);++j)m=Math.max(m,peaks[j]||0);
    const h=Math.max(2,Math.min(1,m)*96),x=i/step+0.2,y=(100-h)/2;
    d+=`M${x.toFixed(2)} ${y.toFixed(2)}h0.6v${h.toFixed(2)}h-0.6Z`;
  }
  const W=Math.max(1,Math.ceil(peaks.length/step))+1;
  el.innerHTML='<svg class="wv-svg" viewBox="0 0 '+W+' 100" preserveAspectRatio="none"><path d="'+d+'"/></svg>'+
    '<div class="ew-region-layer" id="ewRegLayer"></div>'+
    '<div class="ew-shade" id="ewBefore"></div><div class="ew-shade" id="ewAfter"></div>'+
    '<div class="ew-sel" id="ewSel"></div>'+
    '<div class="ew-handle" id="ewHStart" data-side="start"></div>'+
    '<div class="ew-handle" id="ewHEnd" data-side="end"></div>';

  const waveTime=ev=>{
    const r=el.getBoundingClientRect();
    return Math.max(0,Math.min(editState.dur,((ev.clientX-r.left)/Math.max(1,r.width))*editState.dur));
  };
  el.onpointerdown=ev=>{
    if(!editState)return;
    const t=waveTime(ev);
    if(ev.target.classList.contains("ew-handle"))ewDrag={mode:"trim",side:ev.target.dataset.side};
    else{ewDrag={mode:"sel",anchor:t};editState.sel={start:t,end:t};}
    try{el.setPointerCapture(ev.pointerId);}catch(_){}
    ev.preventDefault();
  };
  el.onpointermove=ev=>{
    if(!ewDrag||!editState)return;
    const t=waveTime(ev);
    if(ewDrag.mode==="trim"){
      if(ewDrag.side==="start")editState.trimStart=Math.max(0,Math.min(t,editState.trimEnd-0.02));
      else editState.trimEnd=Math.min(editState.dur,Math.max(t,editState.trimStart+0.02));
      updateEditorValues();
    }else{
      editState.sel={start:Math.min(ewDrag.anchor,t),end:Math.max(ewDrag.anchor,t)};
      updateEditorOverlays();
    }
  };
  const end=()=>{
    if(ewDrag&&ewDrag.mode==="sel"&&editState.sel&&editState.sel.end-editState.sel.start<0.02)
      editState.sel=null;
    ewDrag=null;updateEditorOverlays();updateAddRegionBtn();
  };
  el.onpointerup=end;el.onpointercancel=end;
  updateEditorOverlays();updateAddRegionBtn();
}
function updateEditorValues(){
  if(!editState)return;
  $("ewStart").value=editState.trimStart.toFixed(2);
  $("ewEnd").value=editState.trimEnd.toFixed(2);
  updateEditorOverlays();
}
function updateEditorOverlays(){
  if(!editState)return;
  const a=editorPct(editState.trimStart),b=editorPct(editState.trimEnd);
  $("ewBefore").style.width=a+"%";
  $("ewAfter").style.left=b+"%";$("ewAfter").style.width=(100-b)+"%";
  $("ewHStart").style.left=a+"%";$("ewHEnd").style.left=b+"%";
  const layer=$("ewRegLayer");
  if(layer)layer.innerHTML=editState.regions.map(r=>{
    const l=editorPct(r.start),w=Math.max(0,editorPct(r.end)-l);
    return '<div class="ew-region-span" style="left:'+l+'%;width:'+w+'%"></div>';
  }).join("");
  const sel=$("ewSel");
  if(editState.sel){
    sel.style.display="block";
    sel.style.left=editorPct(editState.sel.start)+"%";
    sel.style.width=Math.max(0,editorPct(editState.sel.end)-editorPct(editState.sel.start))+"%";
  }else sel.style.display="none";
}
function updateAddRegionBtn(){
  const ok=editState&&editState.sel&&editState.sel.end-editState.sel.start>=0.02;
  $("ewAddRegion").disabled=!ok;
}
function renderEditorRegions(){
  const c=$("ewRegions");c.innerHTML="";
  if(!editState||!editState.regions.length){c.innerHTML='<span class="dim">нет участков</span>';return;}
  editState.regions.forEach((r,i)=>{
    const row=document.createElement("div");row.className="reg-row";
    row.innerHTML='<span class="mono">'+r.start.toFixed(2)+'–'+r.end.toFixed(2)+' с</span>'+
      '<input type="range" min="0" max="400" step="5" value="'+Math.round(r.gain*100)+'">'+
      '<output class="mono">'+Math.round(r.gain*100)+'%</output>'+
      '<button class="s-ctl del" title="Удалить">✕</button>';
    row.querySelector("input").addEventListener("input",e=>{
      r.gain=(+e.target.value)/100;
      row.querySelector("output").textContent=e.target.value+"%";
    });
    row.querySelector("button").addEventListener("click",()=>{
      editState.regions.splice(i,1);renderEditorRegions();updateEditorOverlays();
    });
    c.appendChild(row);
  });
}
function applyEditor(reset){
  if(!editState)return;
  const def=reset?{trimStart:0,trimEnd:0,gain:1,regions:[]}:{
    trimStart:editState.trimStart,trimEnd:editState.trimEnd,gain:editState.gain,
    regions:editState.regions.map(r=>({start:r.start,end:r.end,gain:r.gain}))
  };
  const id=editState.id;
  send({cmd:"editSound",id:id,...def});
  closeEditDialog();
  toast(reset?"Правки сброшены":"Звук отредактирован");
}
$("ewClose").addEventListener("click",closeEditDialog);
$("ewCancel").addEventListener("click",closeEditDialog);
$("ewReset").addEventListener("click",()=>applyEditor(true));
$("ewApply").addEventListener("click",()=>applyEditor(false));
$("ewGain").addEventListener("input",e=>{
  if(!editState)return;
  editState.gain=(+e.target.value)/100;
  $("ewGainVal").textContent=e.target.value+"%";
});
$("ewStart").addEventListener("change",e=>{
  if(!editState)return;
  editState.trimStart=Math.max(0,Math.min(+e.target.value||0,editState.trimEnd-0.02));
  updateEditorValues();
});
$("ewEnd").addEventListener("change",e=>{
  if(!editState)return;
  const v=+e.target.value||0;
  editState.trimEnd=Math.min(editState.dur,Math.max(v,editState.trimStart+0.02));
  updateEditorValues();
});
$("ewAddRegion").addEventListener("click",()=>{
  if(!editState||!editState.sel)return;
  editState.regions.push({start:editState.sel.start,end:editState.sel.end,gain:1.5});
  editState.regions.sort((a,b)=>a.start-b.start);
  editState.sel=null;updateEditorOverlays();updateAddRegionBtn();renderEditorRegions();
});

function tagChipsHtml(s){
  const chips=(s.tags||[]).map(t=>`<span class="tag${String(t).toLowerCase()==="hotkey"?" builtin":""}" title="${esc(t)}">${esc(t)}</span>`).join("");
  return chips+`<button class="tag-add" data-tags title="Изменить теги" aria-label="Изменить теги">+</button>`;
}

// Shared building blocks for both views: one visual language, same controls.
function soundMetaHtml(s){
  return `<div class="s-meta">
    <div class="s-name" title="${esc(s.name)}">${esc(s.name)}</div>
    <div class="s-sub mono">${fmtSec(s.dur)}${s.loop?" · loop":""}</div>
  </div>`;
}
function soundVolumeHtml(s){
  const pct=Math.round(s.gain*100);
  return `<div class="s-volume">
    ${ICONS.volume}
    <input type="range" min="0" max="200" step="5" value="${pct}" data-vol aria-label="Громкость: ${esc(s.name)}">
    <output class="mono" data-volval>${pct}%</output>
  </div>`;
}
function soundActionsHtml(s, cardMode){
  const hk=`<button class="keycap${s.hotkey?"":" unset"}" data-hk title="Назначить хоткей" aria-label="Хоткей">${s.hotkey?esc(s.hotkey):"hotkey"}</button>`;
  const transport=`<div class="act-group">
      <button class="s-ctl" data-stop title="Стоп" aria-label="Стоп">${ICONS.stop}</button>
      <button class="s-ctl${s.loop?" on":""}" data-loop title="Повтор" aria-label="Повтор" aria-pressed="${!!s.loop}">${ICONS.loop}</button>
      <button class="s-ctl preview" data-preview title="Предпрослушать на динамики" aria-label="Предпрослушать">${ICONS.preview}</button>
    </div>`;
  const tools=`<button class="s-ctl" data-edit title="Редактор звука" aria-label="Редактор звука">${ICONS.edit}</button>
      <button class="s-ctl del" data-del title="Удалить" aria-label="Удалить">${ICONS.trash}</button>`;
  // In cards the hotkey chip fills the space between the two button groups.
  if(cardMode)
    return `<div class="s-actions card-actions">${transport}${hk}<div class="act-group">${tools}</div></div>`;
  return `<div class="s-actions">${transport}<div class="act-group">${hk}${tools}</div></div>`;
}
function playIcon(s){return (playingIds.has(s.id)&&!pausedIds.has(s.id))?ICONS.pause:ICONS.play;}

function renderSounds(){
  const grid=$("soundGrid"),list=$("soundList");
  grid.innerHTML="";list.innerHTML="";
  const isGrid=soundView==="grid";
  grid.classList.toggle("hidden",!isGrid);
  list.classList.toggle("hidden",isGrid);
  const container=isGrid?grid:list;
  const filtered=getFilteredSounds();
  $("soundEmpty").classList.toggle("hidden",filtered.length>0);
  for(const s of filtered){
    const el=document.createElement(isGrid?"article":"div");
    el.className=(isGrid?"scard":"srow")+(playingIds.has(s.id)?" playing":"");
    el.dataset.id=s.id;
    const play=`<button class="play-btn" data-play title="ЛКМ — играть, ПКМ — предпрослушать" aria-label="Воспроизвести">${playIcon(s)}</button>`;
    if(isGrid){
      el.innerHTML=`<div class="scard-head">${play}${soundMetaHtml(s)}</div>
        <div class="scard-tags">${tagChipsHtml(s)}</div>
        <div class="wave-canvas" data-wave id="wave-${s.id}" title="Клик — перемотать"></div>
        ${soundVolumeHtml(s)}${soundActionsHtml(s,true)}`;
    }else{
      el.innerHTML=`${play}${soundMetaHtml(s)}
        <div class="srow-tags">${tagChipsHtml(s)}</div>
        <div class="wave-canvas" data-wave id="wave-${s.id}" title="Клик — перемотать"></div>
        ${soundVolumeHtml(s)}${soundActionsHtml(s)}`;
    }
    drawWaveform(el.querySelector("[data-wave]"),s);
    makeSoundControls(el,s);
    container.appendChild(el);
  }
  updatePreviewButtons();
}

// Search
$("soundSearch").addEventListener("input",e=>{searchQuery=e.target.value;renderSounds();});
// View toggle
$("viewGrid").addEventListener("click",()=>{soundView="grid";$("viewGrid").classList.add("on");$("viewList").classList.remove("on");renderSounds();});
$("viewList").addEventListener("click",()=>{soundView="list";$("viewList").classList.add("on");$("viewGrid").classList.remove("on");renderSounds();});

// Waveform
function drawWaveform(el,s){
  const peaks=s.peaks||[];
  if(!peaks.length){el.innerHTML="";return;}
  // Downsample 256 buckets to ~64: 256 flex divs with 1px gaps collapse into
  // a solid rectangle in a narrow card. An SVG path scales cleanly instead.
  const step=Math.max(1,Math.ceil(peaks.length/64));
  const bars=[];
  for(let i=0;i<peaks.length;i+=step){
    let m=0;
    for(let j=i;j<Math.min(peaks.length,i+step);++j)m=Math.max(m,peaks[j]||0);
    bars.push(m);
  }
  const W=bars.length,H=100,BAR=0.6;
  let d="";
  for(let i=0;i<bars.length;i++){
    const h=Math.max(2,Math.min(1,bars[i])*(H-4));
    const x=i+(1-BAR)/2, y=(H-h)/2;
    d+=`M${x.toFixed(2)} ${y.toFixed(2)}h${BAR}v${h.toFixed(2)}h-${BAR}Z`;
  }
  el.innerHTML='<svg class="wv-svg" viewBox="0 0 '+W+' '+H+'" preserveAspectRatio="none">'+
    '<path d="'+d+'"/></svg><div class="wv-progress" data-prog style="width:0%"></div>';
}
function updateWaveProgress(id,frac){
  const el=document.getElementById("wave-"+id);if(!el)return;
  const prog=el.querySelector("[data-prog]");if(prog)prog.style.width=(frac*100).toFixed(1)+"%";
}
function updatePlayingRows(){
  document.querySelectorAll("#soundGrid .scard, #soundList .srow").forEach(row=>{
    const id=+row.dataset.id;const on=playingIds.has(id);const paused=pausedIds.has(id);
    row.classList.toggle("playing",on&&!paused);
    const btn=row.querySelector("[data-play]");
    if(btn){
      btn.innerHTML=(on&&!paused)?ICONS.pause:ICONS.play;
      btn.setAttribute("aria-label",(on&&!paused)?"Пауза":"Воспроизвести");
    }
  });
}

// AI
function renderAi(){
  const list=$("aiVoiceList");list.innerHTML="";
  $("aiMissing").classList.toggle("hidden",S.ai.modelsInstalled);
  for(const v of S.ai.voices){
    const row=document.createElement("div");
    const active=S.ai.voice===v.name&&S.ai.ready;
    row.className="vrow"+(active?" active":"");
    row.innerHTML=(v.gender==="female"?presetIcon("female"):presetIcon("male"))+
      `<div class="v-info"><b>${esc(v.name)}</b><span>${v.pitch?(v.pitch>0?"+":"")+v.pitch+"s":""}${v.precision==="fp16"?" · FP16":""}</span></div>`+
      `<span class="v-load">${active?"загружен":"клик"}</span>`;
    row.addEventListener("click",()=>{send({cmd:"aivoice",name:v.name});toast("Загружаю…");});
    list.appendChild(row);
  }
  if(document.activeElement!==$("aiPitch")){$("aiPitch").value=S.ai.pitch;$("aiPitchVal").textContent=(S.ai.pitch>0?"+":"")+S.ai.pitch;}
  $("aiProvider").value=S.ai.provider;
  if(document.activeElement!==$("aiMargin")){$("aiMargin").value=S.ai.margin;$("aiMarginVal").textContent=S.ai.margin+" мс";}
  if(document.activeElement!==$("aiFade")){$("aiFade").value=S.ai.fade;$("aiFadeVal").textContent=S.ai.fade+" мс";}
  $("aiAutolimit").checked=S.ai.autolimit;$("aiFlap").value=String(S.ai.flapLimit);
  $("aiDevice").textContent=S.ai.ready?S.ai.device:"-";
  $("aiLat").textContent=S.ai.ready?"≈"+Math.round(S.ai.latency)+" мс":"—";
  $("aiUnd").textContent=String(S.ai.underruns);
}

function renderSettings(){
  const d=S.devices;
  const fill=(sel,items,cur)=>{if(sel===document.activeElement)return;sel.innerHTML="";
    for(const it of items){const o=document.createElement("option");o.value=it.idx;
      o.textContent=it.name+(it.def?"  [сис]":"");if(it.idx===cur)o.selected=true;sel.appendChild(o);}};
  fill($("capSelect"),d.capture,d.curCapture);
  fill($("playSelect"),d.playback,d.curPlayback);
  fill($("origSelect"),d.playback,d.curMonitor>=0?d.curMonitor:d.savedMonitor);
  if(document.activeElement!==$("monitorChk"))$("monitorChk").checked=!!S.voiceMonitorOn;
  $("bypassChk").checked=S.bypassOn;$("duckingChk").checked=S.duckingOn;
  if(S.latencyMode!==undefined)$("latencyMode").value=String(S.latencyMode);
  if(document.activeElement!==$("autoStartChk"))$("autoStartChk").checked=!!S.autoStart;
  if(document.activeElement!==$("runAdminChk"))$("runAdminChk").checked=!!S.runAsAdmin;
  if(document.activeElement!==$("hideConsoleChk"))$("hideConsoleChk").checked=!!S.hideConsole;
  $("runNote").textContent=S.elevated
    ? "Приложение сейчас запущено от имени администратора."
    : "При включении «от имени админа» приложение перезапустится с запросом UAC. Настройка действует при каждом запуске.";
  $("routeNote").textContent=d.running?`${d.capture[d.curCapture]?.name||"?"} → ${d.playback[d.curPlayback]?.name||"?"}`:"не запущено";
}

// Meters
const LED_N=22;
function buildLed(){const led=$("micLed");for(let i=0;i<LED_N;i++)led.appendChild(document.createElement("i"));}
function renderMeters(m){
  if(!S||document.hidden)return;
  setSwitch($("powerBtn"),m.dspOn&&m.bypassOn);setSwitch($("aiPowerBtn"),m.aiOn);
  const lit=Math.round(Math.sqrt(Math.min(1,m.mic*6))*LED_N);
  const segs=$("micLed").children;
  for(let i=0;i<LED_N;i++){const seg=segs[i];seg.className=i<lit?"on":"";
    if(i<lit){if(i>=LED_N*0.85)seg.classList.add("hot");else if(i>=LED_N*0.6)seg.classList.add("warn");}}
  $("micNum").textContent=m.mic>0.0005?(20*Math.log10(m.mic)).toFixed(0)+" dB":"−∞";
  if(m.vadThresholdDb!=null)S.vadThresholdDb=m.vadThresholdDb;
  if(m.vadSleeping!=null)S.vadSleeping=m.vadSleeping;
  if(m.e2eLatency!=null)S.e2eLatency=m.e2eLatency;
  if(m.voiceLatency!=null)S.voiceLatency=m.voiceLatency;
  if(m.dspOn!=null)S.dspOn=m.dspOn;
  if(m.preset!=null&&S.preset!==m.preset&&performance.now()>=presetClickGuardUntil){
    S.preset=m.preset;renderPresets();
    $("fxPresetLabel").textContent=S.presets[m.preset]?S.presets[m.preset].name:"";
    if(editingPreset>=0)openPresetEditor(m.preset);
  }
  if(m.aiOn!=null){S.ai.enabled=m.aiOn;if(m.aiLatency!=null)S.ai.latency=m.aiLatency;}
  const thr=Math.pow(10,(S.vadThresholdDb!=null?S.vadThresholdDb:-48)/20);
  const mark=$("vadMark");
  if(mark)mark.style.left=(Math.sqrt(Math.min(1,thr*6))*100).toFixed(1)+"%";
  $("micLed").classList.toggle("sleeping",!!S.vadSleeping);
  playingIds.clear();pausedIds.clear();
  for(const p of(m.playing||[])){const id=typeof p==="object"?p.id:p;playingIds.add(id);
    if(typeof p==="object"&&p.paused)pausedIds.add(id);
    updateWaveProgress(id,typeof p==="object"?(p.pos||0):0);}
  updatePlayingRows();updateTopLive();
  $("latInfo").textContent="Тракт "+(m.e2eLatency!=null?Math.round(m.e2eLatency):"—")+" мс"
    +(m.dspOn?" · DSP "+Math.round(m.voiceLatency):" · DSP off")
    +(m.aiOn?" · AI "+Math.round(m.aiLatency):"");
  $("glitchInfo").textContent="ur "+m.micUnderruns;
}

// Nav
function selectPane(name){
  tab=name;
  document.querySelectorAll(".nav-item").forEach(b=>b.classList.toggle("active",b.dataset.tab===name));
  document.querySelectorAll(".pane").forEach(p=>p.classList.toggle("active",p.id==="tab-"+name));
  $("paneTitle").textContent=TITLES[name]||"";
}
document.querySelectorAll(".nav-item").forEach(b=>b.addEventListener("click",()=>selectPane(b.dataset.tab)));

// Power
$("powerBtn").addEventListener("click",()=>{
  const next=!S.dspOn;send({cmd:"dspOn",on:next});S.dspOn=next;
  setSwitch($("powerBtn"),next&&S.bypassOn);updateTopLive();});
$("aiPowerBtn").addEventListener("click",()=>{
  if(!S.ai.ready){toast("Загрузи AI-голос");return;}
  send({cmd:"aiOn",on:!S.ai.enabled});S.ai.enabled=!S.ai.enabled;
  setSwitch($("aiPowerBtn"),S.ai.enabled);updateTopLive();});
$("ambientPowerBtn").addEventListener("click",()=>{
  if(!S)return;const on=!S.ambientOn;send({cmd:"ambientOn",on});S.ambientOn=on;
  setSwitch($("ambientPowerBtn"),on);updateTopLive();});
$("fxSendBtn").addEventListener("click",()=>{
  if(!S)return;const on=!S.soundFxSend;send({cmd:"soundFxSend",on});S.soundFxSend=on;
  setSwitch($("fxSendBtn"),on);updateTopLive();
  if(on)toast("Саундборд идёт через текущий пресет"+(S.ai.enabled?" и RVC":""));});
$("addPresetBtn").addEventListener("click",()=>{
  if(!S)return;openPresetEditor(S.preset>=0?S.preset:0);
  $("peName").focus();$("peName").select();toast("Задай новое имя и нажми «Сохранить как новый»");});

// Overrides
let ovTimer=null;
function ovSend(){
  const p=+$("pitchOv").value,f=+$("formantOv").value;
  $("pitchOvVal").textContent=p===0?"off":(p>0?"+":"")+p;
  $("formantOvVal").textContent=f===0?"off":(f>0?"+":"")+f;
  clearTimeout(ovTimer);
  ovTimer=setTimeout(()=>{send({cmd:"pitch",semi:p,on:p!==0});send({cmd:"formant",semi:f,on:f!==0});},180);
}
$("pitchOv").addEventListener("input",ovSend);
$("formantOv").addEventListener("input",ovSend);
$("ovReset").addEventListener("click",()=>{$("pitchOv").value=0;$("formantOv").value=0;ovSend();});
// VAD threshold (dBFS): below it for ~300 ms the voice branch sleeps.
let vadTimer=null;
$("vadThreshold").addEventListener("input",e=>{
  const db=+e.target.value;
  $("vadThresholdVal").textContent=db+" dB";
  if(S)S.vadThresholdDb=db;
  clearTimeout(vadTimer);
  vadTimer=setTimeout(()=>send({cmd:"vadThreshold",db:db}),150);
});

// Soundboard
$("addSoundBtn").addEventListener("click",()=>send({cmd:"addFileDialog"}));
$("emptyAddBtn").addEventListener("click",()=>send({cmd:"addFileDialog"}));
$("refreshSoundsBtn").addEventListener("click",()=>{
  send({cmd:"refreshSounds"});toast("Обновляю библиотеку из data\\sounds…");
});
$("stopAllBtn").addEventListener("click",()=>send({cmd:"stopall"}));

// AI
let aiPitchT=null,aiMarginT=null,aiFadeT=null;
$("aiPitch").addEventListener("input",e=>{
  $("aiPitchVal").textContent=(+e.target.value>0?"+":"")+e.target.value;
  clearTimeout(aiPitchT);aiPitchT=setTimeout(()=>send({cmd:"aipitch",semi:+e.target.value}),200);});
$("aiProvider").addEventListener("change",e=>{send({cmd:"aiProvider",name:e.target.value});toast("Провайдер: "+e.target.value);});
$("aiImportBtn").addEventListener("click",()=>send({cmd:"aivoiceImport"}));
$("aiModelsHelpBtn").addEventListener("click",()=>send({cmd:"openModelsHelp"}));

// In-app RVC .pth -> ONNX converter.
let pthCandidate=null;
function openConvertDialog(m){
  pthCandidate=m.path;
  $("convertPath").textContent=m.path||"";
  $("cvName").value=m.name||"";
  $("cvGender").value="other";
  $("cvPitch").value=0;$("cvPitchVal").textContent="0";
  $("cvFp16").checked=false;
  $("convertOverlay").classList.remove("hidden");
}
$("aiConvertPthBtn").addEventListener("click",()=>send({cmd:"aivoiceConvertPth"}));
$("cvCancel").addEventListener("click",()=>{
  $("convertOverlay").classList.add("hidden");pthCandidate=null;
});
$("cvPitch").addEventListener("input",e=>{
  $("cvPitchVal").textContent=(+e.target.value>0?"+":"")+e.target.value;
});
$("cvGo").addEventListener("click",()=>{
  if(!pthCandidate)return;
  const name=($("cvName").value||"").trim()||"My voice";
  send({cmd:"convertPth",path:pthCandidate,name:name,
        gender:$("cvGender").value,pitch:+$("cvPitch").value,
        fp16:$("cvFp16").checked});
  $("convertOverlay").classList.add("hidden");pthCandidate=null;
  toast("Конвертация запущена — это может занять несколько минут");
});
$("aiMargin").addEventListener("input",e=>{$("aiMarginVal").textContent=e.target.value+" мс";
  clearTimeout(aiMarginT);aiMarginT=setTimeout(()=>send({cmd:"aiMargin",ms:+e.target.value}),250);});
$("aiFade").addEventListener("input",e=>{$("aiFadeVal").textContent=e.target.value+" мс";
  clearTimeout(aiFadeT);aiFadeT=setTimeout(()=>send({cmd:"aiFade",ms:+e.target.value}),250);});
$("aiAutolimit").addEventListener("change",e=>send({cmd:"aiAutolimit",on:e.target.checked,n:+$("aiFlap").value}));
$("aiFlap").addEventListener("change",()=>send({cmd:"aiAutolimit",on:$("aiAutolimit").checked,n:+$("aiFlap").value}));

// Settings
$("uiLanguage").addEventListener("change",e=>{
  const lang=e.target.value==="en"?"en":"ru";
  vdSetLocale(lang);if(S)S.language=lang;send({cmd:"setLanguage",language:lang});
});
$("latencyMode").addEventListener("change",e=>{send({cmd:"setLatencyMode",mode:+e.target.value});
  toast("Задержка изменена");settingsRenderedOnce=false;});
$("origSelect").addEventListener("change",e=>{
  if(!settingsRenderedOnce)return;
  send({cmd:"monitorOutput",idx:+e.target.value});
});
$("monitorChk").addEventListener("change",e=>{
  // WebView2 restores form state on restart; ignore the resulting synthetic
  // change so the monitor never turns itself on unexpectedly.
  if(!settingsRenderedOnce)return;
  send({cmd:"monitor",on:e.target.checked,idx:+$("origSelect").value});
  setSwitch($("monitorBtn"),e.target.checked);
});
// Dedicated, always-visible self-monitoring ("voice preview") toggle.
$("monitorBtn").addEventListener("click",()=>{
  const on=!$("monitorBtn").classList.contains("on");
  send({cmd:"monitor",on,idx:+$("origSelect").value});
  setSwitch($("monitorBtn"),on);
  $("monitorChk").checked=on;
  toast(on?"Предпрослушивание голоса: ВКЛ":"Предпрослушивание голоса: ВЫКЛ");
});
$("soundboardMonitorBtn").addEventListener("click",()=>{
  if(!S)return;
  const on=!S.soundboardMonitor;send({cmd:"soundboardMonitor",on,idx:+$("origSelect").value});S.soundboardMonitor=on;
  setSwitch($("soundboardMonitorBtn"),on);
  toast(on?"Основной саундборд: слышно на оригинальном выходе":"Основной саундборд: только в канал");
});
$("applyRoute").addEventListener("click",()=>{send({cmd:"select",cap:+$("capSelect").value,play:+$("playSelect").value});
  toast("Маршрут применён");settingsRenderedOnce=false;});
$("bypassChk").addEventListener("change",e=>send({cmd:"bypassOn",on:e.target.checked}));
$("duckingChk").addEventListener("change",e=>send({cmd:"duckingOn",on:e.target.checked}));
$("autoStartChk").addEventListener("change",e=>send({cmd:"autoStart",on:e.target.checked}));
$("runAdminChk").addEventListener("change",e=>send({cmd:"runAsAdmin",on:e.target.checked}));
$("hideConsoleChk").addEventListener("change",e=>send({cmd:"hideConsole",on:e.target.checked}));
$("hkVoice").addEventListener("click",()=>startHkCapture({target:"voice"}));
$("hkAi").addEventListener("click",()=>startHkCapture({target:"ai"}));
$("hkStopAll").addEventListener("click",()=>startHkCapture({target:"stopall"}));

// Voice EQ (main panel)
let vEqTimer=null;
function vEqSend(){clearTimeout(vEqTimer);
  vEqTimer=setTimeout(()=>{
    if(S&&S.preset>=0)send({cmd:"setPresetEq",idx:S.preset,eq:readEqFaders(voiceFaders)});
  },150);}
function initVoiceEq(){
  const c=$("voiceEq");
  buildEq(c,voiceFaders,()=>vEqSend());
  const rc=document.createElement("div");rc.className="eq-col";
  rc.innerHTML='<output>&nbsp;</output><button class="btn" id="vEqReset" style="font-size:10px;padding:5px 8px">flat</button><span class="eq-freq">Hz</span>';
  c.appendChild(rc);
  $("vEqReset").addEventListener("click",()=>{
    setEqFaders(voiceFaders,[0,0,0,0,0,0,0,0]);
    vEqSend();});
}
function loadVoiceEq(){
  if(!S||S.preset<0||!S.presets[S.preset])return;
  setEqFaders(voiceFaders,S.presets[S.preset].eq);
}

// Confirm dialog
function uiConfirm(text){
  return new Promise(resolve=>{
    $("confirmText").textContent=text;$("confirmOverlay").classList.remove("hidden");
    const done=v=>{$("confirmOverlay").classList.add("hidden");resolve(v);};
    $("confirmYes").onclick=()=>done(true);$("confirmNo").onclick=()=>done(false);
    $("confirmYes").focus();});
}

// Hotkey capture
function startHkCapture(t){hkTarget=t;$("hkOverlay").classList.remove("hidden");$("hkComboPreview").textContent="…";}
function endHkCapture(){hkTarget=null;$("hkOverlay").classList.add("hidden");}
function clearCurrentHotkey(){
  if(!hkTarget)return;
  const msg={cmd:"hotkeyOff",target:hkTarget.target};
  if(hkTarget.id!=null)msg.id=hkTarget.id;
  if(hkTarget.idx!=null)msg.idx=hkTarget.idx;
  send(msg);endHkCapture();toast("Хоткей удалён");
}
$("hkClear").addEventListener("click",clearCurrentHotkey);
$("hkCancel").addEventListener("click",endHkCapture);
document.addEventListener("keydown",e=>{
  if(!hkTarget){
    if(e.key==="Escape"){
      if(!$("editOverlay").classList.contains("hidden"))closeEditDialog();
      else if(!$("tagOverlay").classList.contains("hidden"))closeTagEditor();
      else if(!$("presetEditor").classList.contains("hidden"))closePresetEditor();
      else if(!$("confirmOverlay").classList.contains("hidden"))$("confirmNo").click();
      else if(!$("convertOverlay").classList.contains("hidden")){
        $("convertOverlay").classList.add("hidden");pthCandidate=null;}
      else if(previewIds.size)stopPreviewNow();}
    return;}
  // Let Tab move focus and Enter/Space activate the dialog buttons. Other
  // keys are captured as shortcut input (or as explicit removal/cancel keys).
  if(e.key==="Tab")return;
  if((e.key==="Enter"||e.key===" ")&&e.target.closest&&e.target.closest("#hkOverlay button"))return;
  e.preventDefault();e.stopPropagation();
  if(e.key==="Escape"){endHkCapture();return;}
  if(e.key==="Delete"||e.key==="Backspace"){clearCurrentHotkey();return;}
  const mod=[];if(e.ctrlKey)mod.push("Ctrl");if(e.altKey)mod.push("Alt");
  if(e.shiftKey)mod.push("Shift");if(e.metaKey)mod.push("Win");
  const k=e.key.length===1?e.key.toUpperCase():/^F\d{1,2}$/.test(e.key)?e.key:null;
  if(!k||!mod.length)return;
  const combo=mod.join("+")+"+"+k;$("hkComboPreview").textContent=combo;
  const msg={cmd:"hotkey",target:hkTarget.target,combo};
  if(hkTarget.id!=null)msg.id=hkTarget.id;if(hkTarget.idx!=null)msg.idx=hkTarget.idx;
  send(msg);endHkCapture();toast("Хоткей: "+combo);
});

// Drag & drop — WebView2 accepts external drops and exposes dropped DOM File
// objects to the host via postMessageWithAdditionalObjects().
let dragDepth=0;
document.addEventListener("dragenter",e=>{
  if(e.target.closest&&e.target.closest(".chain-row"))return;
  e.preventDefault();
  dragDepth++;
  document.body.classList.add("dropzone-active");
});
document.addEventListener("dragover",e=>{
  if(e.target.closest&&e.target.closest(".chain-row"))return;
  // preventDefault is required to make the element a valid drop target.
  e.preventDefault();
  if(e.dataTransfer)e.dataTransfer.dropEffect="copy";
  document.body.classList.add("dropzone-active");
});
document.addEventListener("dragleave",e=>{
  if(e.target.closest&&e.target.closest(".chain-row"))return;
  e.preventDefault();
  dragDepth=Math.max(0,dragDepth-1);
  if(dragDepth===0)document.body.classList.remove("dropzone-active");
});
document.addEventListener("drop",e=>{
  if(e.target.closest&&e.target.closest(".chain-row"))return;
  e.preventDefault();
  dragDepth=0;
  document.body.classList.remove("dropzone-active");
  const files=e.dataTransfer&&e.dataTransfer.files?e.dataTransfer.files:null;
  if(!files||!files.length||!host)return;
  toast("Добавляю звуков: "+files.length);
  if(host.postMessageWithAdditionalObjects)
    host.postMessageWithAdditionalObjects({cmd:"dropFiles",count:files.length},files);
  else
    send({cmd:"dropFiles",count:files.length});
});

// Title bar
$("tbMin").addEventListener("click",()=>send({cmd:"winMin"}));
$("tbMax").addEventListener("click",()=>send({cmd:"winMax"}));
$("tbClose").addEventListener("click",()=>send({cmd:"exit"}));
// Native WebView2 draggable regions handle dragging and double-click maximize
// over the whole titlebar; no JS drag/maximize command is needed.

// Init
buildLed();initVoiceEq();
// #voice / #sounds / #ai / #settings deep-link (handy for docs/screenshots).
{
  const h=(location.hash||"").replace("#","");
  if(TITLES[h])selectPane(h);
}

if(host){
  let uiReadySent=false;
  host.addEventListener("message",e=>{
    let m=e.data;if(typeof m==="string"){try{m=JSON.parse(m);}catch{return;}}
    if(!m||typeof m!=="object")return;
    if(m.type==="state"){
      const first=!S;
      const oldIds=new Set((S&&S.sounds||[]).map(s=>s.id));
      const newIds=new Set((m.sounds||[]).map(s=>s.id));
      if(m.language)vdSetLocale(m.language);
      S=m;renderState();loadVoiceEq();
      if(editingPreset>=0&&S.presets[editingPreset])setAmbientEditor(S.presets[editingPreset]);
      if(!first){
        let added=0,removed=0;
        newIds.forEach(id=>{if(!oldIds.has(id))added++;});
        oldIds.forEach(id=>{if(!newIds.has(id))removed++;});
        if(added||removed){
          const parts=[];
          if(added)parts.push("добавлено: "+added);
          if(removed)parts.push("удалено: "+removed);
          toast("Библиотека обновлена — "+parts.join(", "));
        }
      }
      if(first&&!uiReadySent){uiReadySent=true;send({cmd:"uiReady"});}
    }
    else if(m.type==="meters")renderMeters(m);
    else if(m.type==="refresh")send({cmd:"refresh"});
    else if(m.type==="pthSelected")openConvertDialog(m);
    else if(m.type==="voiceConvertDone"){
      $("convertOverlay").classList.add("hidden");
      pthCandidate=null;
      if(m.ok){toast("Голос сконвертирован и добавлен в библиотеку");send({cmd:"refresh"});}
      else {
        const lines=String(m.text||"").trim().split("\n").filter(s=>s.trim()&&!/^VD:exit/.test(s));
        toast("Ошибка конвертации: "+(lines.pop()||"неизвестная ошибка"));
      }
    }
    else if(m.type==="notice")toast(m.text);
    else if(m.type==="aiLoaded"){m.ok?toast("AI загружен"):toast("Ошибка: "+(m.err||"?"));}
  });
  send({cmd:"init"});
  let tries=0;const it=setInterval(()=>{if(S){clearInterval(it);return;}
    if(++tries>30){clearInterval(it);return;}send({cmd:"init"});},400);
}else{
  S=demoState();renderState();loadVoiceEq();
  const fp=[];for(let i=0;i<256;i++)fp.push(0.3+0.6*Math.abs(Math.sin(i*0.08)*Math.cos(i*0.03)));
  for(const s of S.sounds)s.peaks=fp;
  renderSounds();
  renderMeters({mic:0.02+Math.random()*0.06,dspOn:true,bypassOn:true,preset:S.preset,
    voiceLatency:45,micUnderruns:4,aiOn:true,aiReady:true,aiLatency:290,aiUnderruns:1,
    playing:[{id:1,pos:0.4,paused:false}]});
  setInterval(()=>{renderMeters({mic:0.02+Math.random()*0.06,dspOn:true,bypassOn:true,preset:S.preset,
    voiceLatency:45,micUnderruns:4,aiOn:true,aiReady:true,aiLatency:290,aiUnderruns:1,
    playing:[{id:1,pos:Math.random(),paused:false}]});},120);
}
