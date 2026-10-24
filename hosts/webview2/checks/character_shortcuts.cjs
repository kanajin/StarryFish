 'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const script=fs.readFileSync(require('node:path').join(__dirname,'../src/character_shortcuts.js'),'utf8');
const url='https://game.granbluefantasy.jp/#raid_multi/123';
const bit=i=>1<<i, number=i=>(i<=4?bit(i-1):0)|(i<=3?bit(i+11):bit(i+16));
function fixture(){
 const nodes=new Map();let hit=null;
 const node=(x=10,y=20)=>({isConnected:true,parentElement:null,style:{display:'block',visibility:'visible',opacity:'1'},
   getBoundingClientRect:()=>({left:x,top:y,width:80,height:100}),matches:()=>false,contains:()=>false,closest:()=>null,
   getAttribute:()=> '1',querySelectorAll:s=>nodes.get(s)||[]});
 const body=node(),document={body,activeElement:body,hasFocus:()=>true,visibilityState:'visible',
   querySelectorAll:s=>nodes.get(s)||[],elementFromPoint:()=>hit};
 const window={};window.top=window;
 const state={window,document,location:{origin:'https://game.granbluefantasy.jp',href:url,hash:'#raid_multi/123'},innerWidth:800,innerHeight:1000,devicePixelRatio:1.5,getComputedStyle:n=>n.style};
 vm.createContext(state);
 const set=(s,n=node())=>{nodes.set(s,[n]);hit=n;return n;};
 return {nodes,node,state,set,hit:n=>hit=n,run:(mask,phase=0,locked='')=>vm.runInContext(script+`(${JSON.stringify(url)},${mask},${phase},${JSON.stringify(locked)})`,state),
  panel:()=>{const p=set('.prt-command-chara');set('.btn-command-back');return p;},
  picker:()=>{const p=set('.pop-raid-item');nodes.set('.pop-show,[role="dialog"],[aria-modal="true"],#ready,#loading',[p]);set('.prt-select-item');return p;}};
}
let count=0;function check(name,f){f();console.log('PASS '+name);count++;}
check('digits switch characters from main and skill panels; six potions have stable selectors',()=>{
 for(let n=1;n<=6;n++){
  const f=fixture();if(n<=4){f.set(`.btn-command-character[pos="${n-1}"]`);assert.equal(f.run(number(n)).action,3);
  f.nodes.clear();f.panel();assert.equal(f.run(number(n)).action,n===1?4:2);}
  f.nodes.clear();f.picker();f.set(n<=3?['.item-small','.item-large','.item-potion'][n-1]:`.prt-event-item .btn-event-item[item-id="${n-3}"]`);assert.equal(f.run(number(n)).action,3);
 }
});
check('skill keys select only the current character skills',()=>{for(let n=1;n<=4;n++){const f=fixture();f.panel();f.set(`.prt-ability-list [data-ability-index="${n}"]`);assert.equal(f.run(bit(n+7)).action,3);}});
check('Elixir is found in item popup even outside the small/large picker container',()=>{
 const f=fixture();const popup=f.picker(),potion=f.set('.item-potion');
 const picker=f.nodes.get('.prt-select-item')[0];picker.querySelectorAll=()=>[];
 popup.querySelectorAll=s=>s==='.item-potion'?[potion]:s==='.prt-select-item'?[picker]:[];
 assert.equal(f.run(number(3)).action,3);
});
check('skill lookup stays within the current character panel',()=>{
 const f=fixture(),p=f.panel();f.set('.prt-ability-list [data-ability-index="1"]');p.querySelectorAll=()=>[];
 assert.equal(f.run(bit(8)).action,0);
});
check('missing skill never selects another skill or backs out',()=>{const f=fixture();f.panel();f.set('.prt-ability-list [data-ability-index="1"]');assert.equal(f.run(bit(11)).action,0);});
check('context restrictions and skill character identity remain stable',()=>{
 const f=fixture(),p=f.panel();f.set('.prt-ability-list [data-ability-index="1"]');
 assert.equal(f.run(bit(12)).action,0);const skill=f.run(bit(8));p.getAttribute=()=> '2';assert.equal(f.run(bit(8),0,skill.context).action,0);
 f.nodes.clear();f.set('.btn-command-character[pos="0"]');assert.equal(f.run(bit(8)).action,0);
});
check('character switch backs once and then selects requested portrait',()=>{const f=fixture();f.panel();const a=f.run(number(3));assert.equal(a.action,2);assert.equal(f.run(number(3),1,a.context).action,1);f.nodes.clear();f.set('.btn-command-character[pos="2"]');assert.equal(f.run(number(3),1,a.context).action,3);});
check('Back remains usable after the game retains display-off alongside display-on',()=>{
 const f=fixture();f.panel();const back=f.nodes.get('.btn-command-back')[0];
 back.matches=s=>s==='.display-off';assert.equal(f.run(number(3)).action,2);
 back.style.display='none';assert.equal(f.run(number(3)).action,1);
});
check('queued skill waits until the requested character panel has opened',()=>{
 const f=fixture();f.set('.btn-command-character[pos="2"]');assert.equal(f.run(number(3),2,'2').action,1);
 const p=f.panel();assert.equal(f.run(number(3),2,'2').action,1);p.getAttribute=()=> '3';assert.equal(f.run(number(3),2,'2').action,1);
 const ability=f.set('.prt-ability-list [data-ability-index]');ability.style.opacity='0';assert.equal(f.run(number(3),2,'2').action,1);
 ability.style.opacity='1';assert.equal(f.run(number(3),2,'2').action,4);
 f.set('.prt-ability-list [data-ability-index="1"]');assert.equal(f.run(bit(8)).context,'8:2:3');
});
check('queued potion waits for recovery picker; Back and summon wait for panel transition',()=>{
 const f=fixture();f.set('.btn-temporary');assert.equal(f.run(bit(15),2,'15').action,1);f.picker();assert.equal(f.run(bit(15),2,'15').action,4);
 const g=fixture();g.panel();assert.equal(g.run(bit(5),2,'5').action,1);g.nodes.clear();assert.equal(g.run(bit(5),2,'5').action,4);
 assert.equal(g.run(bit(7),2,'7').action,1);g.set('.prt-command-summon.summon-show .prt-summon-list.opened');assert.equal(g.run(bit(7),2,'7').action,4);
});
check('attack cancellation takes priority and cannot replace attack mid-motion',()=>{const f=fixture();f.set('.btn-attack-start');const a=f.run(bit(16));f.set('.btn-attack-cancel.display-on');assert.equal(f.run(bit(16)).action,3);assert.equal(f.run(bit(16),0,a.context).action,0);});
check('offscreen display-off cancellation cannot mask attack; menu lookup excludes other overlays',()=>{
 const f=fixture();f.set('.btn-attack-cancel');f.set('.btn-attack-start');assert.equal(f.run(bit(16)).action,3);
 const g=fixture(),hidden=g.set('.btn-attack-cancel.display-on');hidden.style.display='none';assert.equal(g.run(bit(16)).action,0);
 const h=fixture();const painted=h.node(),overlay=h.node();h.nodes.set('.btn-raid-menu.menu',[painted,overlay]);h.hit(overlay);assert.equal(h.run(bit(19)).action,3);
});
check('FC confirms only its own OK and never repeats gauge or confirmation',()=>{const f=fixture();f.set('#prt-advanced-command .btn-cb-gauge');const a=f.run(bit(17));assert.equal(a.action,3);assert.equal(f.run(bit(17),2,a.context).action,1);const popup=f.set('.pop-confirm-fatal-chain');f.nodes.set('.pop-show,[role="dialog"],[aria-modal="true"],#ready,#loading',[popup]);assert.equal(f.run(bit(17),2,a.context).action,0);f.set('.btn-usual-ok');assert.equal(f.run(bit(17),2,a.context).action,6);assert.equal(f.run(bit(17),3,a.context).action,4);assert.equal(f.run(bit(16)).action,0);});
check('attack backs once then clicks attack, never replays',()=>{const f=fixture();f.panel();const first=f.run(bit(16));assert.equal(first.action,2);assert.equal(f.run(bit(16),1,first.context).action,1);f.nodes.clear();f.set('.btn-attack-start');assert.equal(f.run(bit(16),1,first.context).action,3);assert.equal(f.run(bit(16),2,first.context).action,4);});
check('B F V Escape C Z use only verified visible targets',()=>{
 for(const [i,s] of [[15,'.btn-temporary'],[17,'#prt-advanced-command .btn-cb-gauge'],[18,'.btn-lock'],[19,'.btn-raid-menu.menu'],[6,'#js-btn-quick-summon.qs-ready'],[7,'.prt-summon-list .prt-list-top']]){
  const f=fixture();assert.equal(f.run(bit(i)).action,0);f.set(s);assert.equal(f.run(bit(i)).action,3);
 }
});
check('back is battle-only; summon already open is no-op',()=>{const f=fixture();f.set('.btn-back');assert.equal(f.run(bit(5)).action,0);f.set('.btn-command-back');assert.equal(f.run(bit(5)).action,3);f.set('.prt-command-summon.summon-show .prt-summon-list.opened');assert.equal(f.run(bit(7)).action,4);});
check('item confirmation and unrelated popup never auto-confirm',()=>{const f=fixture();f.picker();f.set('.item-potion');assert.equal(f.run(number(3)).action,3);f.nodes.delete('.prt-select-item');assert.equal(f.run(number(3)).action,0);f.nodes.set('.pop-show,[role="dialog"],[aria-modal="true"],#ready,#loading',[f.node()]);assert.equal(f.run(bit(16)).action,0);});
check('one potion selection never becomes a target character click',()=>{const f=fixture();f.picker();f.set('.item-small');const a=f.run(number(1));f.nodes.clear();f.set('.btn-command-character[pos="0"]');assert.equal(f.run(number(1),2,a.context).action,4);});
check('hidden disabled occluded and offscreen elements rejected',()=>{
 for(const mutate of [(f,n)=>n.style.display='none',(f,n)=>n.matches=()=>true,(f,n)=>f.hit({}),(f,n)=>n.getBoundingClientRect=()=>({left:-200,top:0,width:80,height:40}),(f,n)=>{n.parentElement=f.node();n.parentElement.style.opacity='0';}]){const f=fixture(),n=f.set('.btn-attack-start');mutate(f,n);assert.equal(f.run(bit(16)).action,0);}
});
check('origins routes stale documents frames editable and focus guards',()=>{
 for(const mutate of [f=>f.state.location.origin='https://evil.test',f=>f.state.location.hash='#mypage',f=>f.state.location.href+='4',f=>f.state.window.top={},f=>f.state.document.hasFocus=()=>false,f=>f.state.document.visibilityState='hidden',f=>f.state.document.activeElement.isContentEditable=true,f=>f.state.document.activeElement.shadowRoot={activeElement:{isContentEditable:true}},f=>f.state.document.activeElement.closest=()=>({})]){const f=fixture();f.set('.btn-attack-start');mutate(f);assert.equal(f.run(bit(16)).action,0);}
});
check('refresh works outside battle but not inside editable input',()=>{const f=fixture();f.state.location.hash='#mypage';assert.equal(f.run(bit(4)).action,5);f.state.document.activeElement.isContentEditable=true;assert.equal(f.run(bit(4)).action,0);});
check('duplicates and invalid masks are rejected',()=>{const f=fixture();f.nodes.set('.btn-attack-start',[f.node(),f.node()]);assert.equal(f.run(bit(16)).action,0);for(const mask of [0,-1,1.2,1<<23])assert.equal(f.run(mask).action,0);assert.equal(f.run(bit(16),4).action,0);});
console.log(`${count} contextual shortcut checks passed; isolated fixtures, not live game acceptance`);
