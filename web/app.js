// VoiceDeck front-end — broadcast console aesthetic.
"use strict";
const host = (window.chrome && window.chrome.webview) || null;
const $ = (id) => document.getElementById(id);
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
let liveTimer=null; // debounce for live preset updates
const editorFaders=[], voiceFaders=[];

const TITLES={voice:"Голосовые эффекты",sounds:"Саундборд",ai:"AI-голос",settings:"Устройства"};
const EQ_FREQS=[200,400,800,1600,3200,6400,10000,12000];
const SPACE_OPTS=["none","room","hall","cave","helmet","radio","megaphone"];
const EFFECT_OPTS=["none","robot","alien","atari","clone","silence"];

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
    .map((n,i)=>({idx:i,name:n,transpose:i===2?4:0,formant:i===2?3:0,space:"none",effect:i>=5&&i<=9?n.toLowerCase():"none",
      custom:i===5,hotkey:i===5?"Ctrl+Alt+F1":"",eq:[0,0,0,0,0,0,0,0],chain:[]}));
  const pk=[];for(let i=0;i<256;i++)pk.push(0.3+0.6*Math.abs(Math.sin(i*0.08)*Math.cos(i*0.03)));
  return {type:"state",dspOn:true,bypassOn:true,duckingOn:false,monitorOn:false,preset:2,presets:P,
    sounds:[{id:1,name:"airhorn",dur:2.1,gain:1,loop:false,hotkey:"Ctrl+Alt+1",eq:[0,0,0,0,0],peaks:pk},
      {id:2,name:"bell",dur:1.2,gain:0.8,loop:false,hotkey:"",eq:[0,0,0,0,0],peaks:pk},
      {id:3,name:"gong",dur:4.0,gain:1,loop:false,hotkey:"",eq:[0,0,0,0,0],peaks:pk}],
    ai:{enabled:true,ready:true,voice:"Narrator (male)",device:"GTX 1080",usesGpu:true,provider:"dml",
      modelsInstalled:true,latency:290,underruns:0,pitch:0,margin:150,fade:30,autolimit:true,flapLimit:8,
      voices:[{name:"Narrator (male)",gender:"male",pitch:0},{name:"Presenter (female)",gender:"female",pitch:12}]},
    devices:{capture:[{idx:0,name:"Микрофон (USB)",def:true}],
      playback:[{idx:0,name:"Line 1 (VAC)",def:true},{idx:1,name:"Динамики",def:false}],
      curCapture:0,curPlayback:0,curMonitor:-1,running:true},
    mic:0.03,voiceLatency:45,micUnderruns:4,latencyMode:1,hotkeys:{voice:"Ctrl+Alt+V",ai:""}};
}

function setSwitch(el,on){el.classList.toggle("on",!!on);}
function updateTopLive(){if(!S)return;
  const p=[];if(S.dspOn)p.push("DSP "+Math.round(S.voiceLatency)+" ms");
  if(S.ai.ready&&S.ai.enabled)p.push("AI "+Math.round(S.ai.latency)+" ms");
  $("topLive").textContent=p.join("   ·   ");}

function renderState(){
  if(!S)return;
  setSwitch($("powerBtn"),S.dspOn&&S.bypassOn);setSwitch($("aiPowerBtn"),S.ai.enabled);
  setSwitch($("monitorBtn"),S.devices.curMonitor>=0);
  renderPresets();renderSounds();renderAi();
  if(!settingsRenderedOnce){renderSettings();settingsRenderedOnce=true;}
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
function renderPresets(){
  const grid=$("presetGrid");grid.innerHTML="";
  for(const p of S.presets){
    const tile=document.createElement("div");
    tile.className="tile"+(p.idx===S.preset?" active":"");
    const sub=[];
    if(p.transpose)sub.push((p.transpose>0?"+":"")+p.transpose);
    if(p.chain&&p.chain.length)sub.push(p.chain.join("→"));
    else{if(p.space!=="none")sub.push(p.space);if(p.effect!=="none")sub.push(p.effect);}
    tile.innerHTML=presetIcon(p.name)+`<div class="t-name">${esc(p.name)}</div><div class="t-sub">${esc(sub.join(" "))}</div>`+
      (p.hotkey?`<span class="keycap" data-hk="preset" data-idx="${p.idx}">${esc(p.hotkey)}</span>`:"");
    // Click = apply + open editor
    tile.addEventListener("click",(e)=>{
      if(e.target.classList.contains("keycap")){startHkCapture({target:"preset",idx:p.idx,combo:p.hotkey});return;}
      send({cmd:"preset",idx:p.idx});S.preset=p.idx;renderPresets();
      openPresetEditor(p.idx); // auto-open editor with this preset
    });
    // Edit button
    const edt=document.createElement("button");edt.className="t-del t-edit";edt.innerHTML=ICONS.edit;
    edt.title="Редактировать";edt.addEventListener("click",(e)=>{e.stopPropagation();openPresetEditor(p.idx);});
    tile.appendChild(edt);
    grid.appendChild(tile);
  }
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
    space:$("peSpace").value,
    effect:$("peEffect").value,
    chain:(p&&p.chain)?[...p.chain]:[],
    eq:eq,
  };
}

function openPresetEditor(idx){
  if(idx<0||idx>=S.presets.length)return;
  editingPreset=idx;editorDismissed=false;
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
  if(!p.chain)p.chain=[];
  renderChainList(p.chain);
  setEqFaders(editorFaders,p.eq);
}
function closePresetEditor(){
  $("presetEditor").classList.add("hidden");
  editingPreset=-1;editorDismissed=true;
}

function sendLiveUpdate(){
  if(editingPreset<0)return;
  clearTimeout(liveTimer);
  liveTimer=setTimeout(()=>{
    const def=collectEditorDef();
    send({cmd:"livePreset",idx:editingPreset,...def});
  },150);
}

function renderChainList(chain){
  const el=$("peChainList");el.innerHTML="";
  if(!chain||!chain.length){el.innerHTML=`<span class="dim">пусто</span>`;return;}
  chain.forEach((c,i)=>{
    const row=document.createElement("div");row.className="chain-row";
    row.innerHTML=`<span class="mono">${i+1}.</span> <b>${esc(c)}</b>`;
    const up=document.createElement("button");up.className="s-ctl";up.textContent="↑";
    up.addEventListener("click",()=>{if(i>0){chain.splice(i-1,0,chain.splice(i,1)[0]);renderChainList(chain);sendLiveUpdate();}});
    const dn=document.createElement("button");dn.className="s-ctl";dn.textContent="↓";
    dn.addEventListener("click",()=>{if(i<chain.length-1){chain.splice(i+1,0,chain.splice(i,1)[0]);renderChainList(chain);sendLiveUpdate();}});
    const rm=document.createElement("button");rm.className="s-ctl del";rm.textContent="✕";
    rm.addEventListener("click",()=>{chain.splice(i,1);renderChainList(chain);sendLiveUpdate();});
    row.appendChild(up);row.appendChild(dn);row.appendChild(rm);
    el.appendChild(row);
  });
}

// Editor events — live apply on every change
$("peClose").addEventListener("click",closePresetEditor);
[["pePitch","pePitchVal","semi"],["peFormant","peFormantVal","semi"],
 ["peTimbre","peTimbreVal","dec"],["peStrength","peStrengthVal","dec"]].forEach(([id,val,fmt])=>{
  $(id).addEventListener("input",(e)=>{
    const v=+e.target.value;
    $(val).textContent=fmt==="dec"?v.toFixed(2):(v>0?"+":"")+v;
    sendLiveUpdate(); // live apply
  });
});
$("peSpace").addEventListener("change",()=>sendLiveUpdate());
$("peEffect").addEventListener("change",()=>sendLiveUpdate());
$("peAddChain").addEventListener("click",()=>{
  const sel=$("peChainSel").value;if(!sel)return;
  const p=S.presets[editingPreset];
  if(!p.chain)p.chain=[];
  if(p.chain.length>=6){toast("Максимум 6 эффектов");return;}
  p.chain.push(sel);renderChainList(p.chain);sendLiveUpdate();
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
  if(!searchQuery)return S.sounds;
  const q=searchQuery.toLowerCase();
  return S.sounds.filter(s=>s.name.toLowerCase().includes(q));
}

// Sound preview auto-restores voice self-monitoring when the clip ends.
let previewTimer=null;
function stopPreviewNow(){
  clearTimeout(previewTimer);
  if(host)send({cmd:"stopPreview"});
  previewIds.clear();
  toast("Предпрослушивание остановлено");
}
function doPreview(s){
  send({cmd:"preview",id:s.id});
  previewIds.add(s.id);
  clearTimeout(previewTimer);
  previewTimer=setTimeout(()=>{
    send({cmd:"stopPreview"});
    previewIds.clear();
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
  if(vol)vol.addEventListener("input",e=>send({cmd:"volume",id:s.id,pct:+e.target.value}));
  const loop=container.querySelector("[data-loop]");
  if(loop)loop.addEventListener("click",()=>{send({cmd:"loop",id:s.id,on:!s.loop});s.loop=!s.loop;renderSounds();});
  const del=container.querySelector("[data-del]");
  if(del)del.addEventListener("click",async()=>{if(await uiConfirm("Удалить «"+s.name+"»?"))send({cmd:"remove",id:s.id});});
  const hk=container.querySelector("[data-hk]");
  if(hk)hk.addEventListener("click",e=>{e.stopPropagation();startHkCapture({target:"sound",id:s.id,combo:s.hotkey});});
  // Waveform seek
  const wave=container.querySelector("[data-wave]");
  if(wave)wave.addEventListener("click",(e)=>{
    if(!playingIds.has(s.id))return;
    const rect=wave.getBoundingClientRect();
    send({cmd:"seek",id:s.id,frac:Math.max(0,Math.min(1,(e.clientX-rect.left)/rect.width))});
  });
}

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
    if(isGrid){
      const card=document.createElement("div");
      card.className="scard"+(playingIds.has(s.id)?" playing":"");
      card.dataset.id=s.id;
      card.innerHTML=`
        <div class="scard-head">
          <button class="play-btn" data-play title="ЛКМ — играть, ПКМ — предпрослушивание">
            ${(playingIds.has(s.id)&&!pausedIds.has(s.id))?ICONS.pause:ICONS.play}
          </button>
          <div class="s-name"><b>${esc(s.name)}</b><span class="mono">${fmtSec(s.dur)}</span></div>
        </div>
        <div class="wave-canvas" data-wave id="wave-${s.id}" title="Клик — перемотать"></div>
        <div class="scard-ctrls">
          <button class="s-ctl" data-stop title="Стоп">■</button>
          <button class="s-ctl preview" data-preview title="Предпрослушивание (на динамики)">${ICONS.preview}</button>
          <input type="range" min="0" max="200" step="5" value="${Math.round(s.gain*100)}" data-vol style="width:60px">
          <button class="s-ctl${s.loop?" on":""}" data-loop title="Повтор">${ICONS.loop}</button>
          <button class="keycap" data-hk>${s.hotkey?esc(s.hotkey):"hotkey"}</button>
          <button class="s-ctl del" data-del title="Удалить">${ICONS.trash}</button>
        </div>`;
      drawWaveform(card.querySelector("[data-wave]"),s);
      makeSoundControls(card,s);
      container.appendChild(card);
    } else {
      const row=document.createElement("div");
      row.className="srow"+(playingIds.has(s.id)?" playing":"");
      row.dataset.id=s.id;
      row.innerHTML=`
        <button class="play-btn" data-play title="ЛКМ — играть, ПКМ — предпрослушивание">
          ${(playingIds.has(s.id)&&!pausedIds.has(s.id))?ICONS.pause:ICONS.play}
        </button>
        <div class="s-name"><b>${esc(s.name)}</b><span class="mono">${fmtSec(s.dur)}${s.loop?" · loop":""}</span></div>
        <div class="wave-canvas" data-wave id="wave-${s.id}" title="Клик — перемотать" style="flex:1;max-width:200px"></div>
        <button class="s-ctl" data-stop title="Стоп">■</button>
        <button class="s-ctl preview" data-preview title="Предпрослушивание (на динамики)">${ICONS.preview}</button>
        <input type="range" class="s-vol" min="0" max="200" step="5" value="${Math.round(s.gain*100)}" data-vol>
        <button class="s-ctl${s.loop?" on":""}" data-loop title="Повтор">${ICONS.loop}</button>
        <button class="keycap" data-hk>${s.hotkey?esc(s.hotkey):"hotkey"}</button>
        <button class="s-ctl del" data-del title="Удалить">${ICONS.trash}</button>`;
      drawWaveform(row.querySelector("[data-wave]"),s);
      makeSoundControls(row,s);
      container.appendChild(row);
    }
  }
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
    if(btn)btn.innerHTML=(on&&!paused)?ICONS.pause:ICONS.play;
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
      `<div class="v-info"><b>${esc(v.name)}</b><span>${v.pitch?(v.pitch>0?"+":"")+v.pitch+"s":""}</span></div>`+
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
  fill($("origSelect"),d.playback,d.curMonitor);
  if(document.activeElement!==$("monitorChk"))$("monitorChk").checked=d.curMonitor>=0;
  $("bypassChk").checked=S.bypassOn;$("duckingChk").checked=S.duckingOn;
  if(S.latencyMode!==undefined)$("latencyMode").value=String(S.latencyMode);
  $("hkVoice").textContent=S.hotkeys.voice||"— клик —";
  $("hkAi").textContent=S.hotkeys.ai||"— клик —";
  $("routeNote").textContent=d.running?`${d.capture[d.curCapture]?.name||"?"} → ${d.playback[d.curPlayback]?.name||"?"}`:"не запущено";
}

// Meters
const LED_N=22;
function buildLed(){const led=$("micLed");for(let i=0;i<LED_N;i++)led.appendChild(document.createElement("i"));}
function renderMeters(m){
  if(!S)return;
  setSwitch($("powerBtn"),m.dspOn&&m.bypassOn);setSwitch($("aiPowerBtn"),m.aiOn);
  const lit=Math.round(Math.sqrt(Math.min(1,m.mic*6))*LED_N);
  const segs=$("micLed").children;
  for(let i=0;i<LED_N;i++){const seg=segs[i];seg.className=i<lit?"on":"";
    if(i<lit){if(i>=LED_N*0.85)seg.classList.add("hot");else if(i>=LED_N*0.6)seg.classList.add("warn");}}
  $("micNum").textContent=m.mic>0.0005?(20*Math.log10(m.mic)).toFixed(0)+" dB":"−∞";
  playingIds.clear();pausedIds.clear();
  for(const p of(m.playing||[])){const id=typeof p==="object"?p.id:p;playingIds.add(id);
    if(typeof p==="object"&&p.paused)pausedIds.add(id);
    updateWaveProgress(id,typeof p==="object"?(p.pos||0):0);}
  updatePlayingRows();updateTopLive();
  $("latInfo").textContent="DSP "+(m.dspOn?Math.round(m.voiceLatency)+"ms":"off")+(m.aiOn?" · AI "+Math.round(m.aiLatency)+"ms":"");
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

// Soundboard
$("addSoundBtn").addEventListener("click",()=>send({cmd:"addFileDialog"}));
$("emptyAddBtn").addEventListener("click",()=>send({cmd:"addFileDialog"}));
$("refreshSoundsBtn").addEventListener("click",()=>{
  send({cmd:"refreshSounds"});toast("Обновляю библиотеку из data\\sounds…");
});
$("stopPreviewBtn").addEventListener("click",stopPreviewNow);
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
        gender:$("cvGender").value,pitch:+$("cvPitch").value});
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
$("latencyMode").addEventListener("change",e=>{send({cmd:"setLatencyMode",mode:+e.target.value});
  toast("Задержка изменена");settingsRenderedOnce=false;});
$("origSelect").addEventListener("change",e=>{if($("monitorChk").checked&&settingsRenderedOnce)send({cmd:"monitor",on:true,idx:+e.target.value});});
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
  send({cmd:"monitor",on:on}); // no idx → engine uses the saved/default original output
  setSwitch($("monitorBtn"),on);
  $("monitorChk").checked=on;
  toast(on?"Предпрослушивание голоса: ВКЛ":"Предпрослушивание голоса: ВЫКЛ");
});
$("applyRoute").addEventListener("click",()=>{send({cmd:"select",cap:+$("capSelect").value,play:+$("playSelect").value});
  toast("Маршрут применён");settingsRenderedOnce=false;});
$("bypassChk").addEventListener("change",e=>send({cmd:"bypassOn",on:e.target.checked}));
$("duckingChk").addEventListener("change",e=>send({cmd:"duckingOn",on:e.target.checked}));
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
$("hkCancel").addEventListener("click",endHkCapture);
document.addEventListener("keydown",e=>{
  if(!hkTarget){
    if(e.key==="Escape"){
      if(!$("presetEditor").classList.contains("hidden"))closePresetEditor();
      else if(!$("confirmOverlay").classList.contains("hidden"))$("confirmNo").click();
      else if(!$("convertOverlay").classList.contains("hidden")){
        $("convertOverlay").classList.add("hidden");pthCandidate=null;}
      else if(previewIds.size)stopPreviewNow();}
    return;}
  e.preventDefault();e.stopPropagation();
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
  e.preventDefault();
  dragDepth++;
  document.body.classList.add("dropzone-active");
});
document.addEventListener("dragover",e=>{
  // preventDefault is required to make the element a valid drop target.
  e.preventDefault();
  if(e.dataTransfer)e.dataTransfer.dropEffect="copy";
  document.body.classList.add("dropzone-active");
});
document.addEventListener("dragleave",e=>{
  e.preventDefault();
  dragDepth=Math.max(0,dragDepth-1);
  if(dragDepth===0)document.body.classList.remove("dropzone-active");
});
document.addEventListener("drop",e=>{
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
$("tbDrag").addEventListener("mousedown",()=>send({cmd:"winDrag"}));
$("tbDrag").addEventListener("dblclick",()=>send({cmd:"winMax"}));

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
      S=m;renderState();loadVoiceEq();
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
