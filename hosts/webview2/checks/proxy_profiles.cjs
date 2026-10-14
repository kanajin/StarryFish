// Original pinned PAC compiler; synthetic worker/proxy APIs and names only.
// No browser, network, actual configuration or credentials.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const OmegaPac=require('../.deps/zeroomega/js/omega_pac.min.js');
const read=file=>fs.readFileSync(path.join(__dirname,file),'utf8');
const helperSource=read('../src/proxy_profiles.js'),menuSource=read('../src/proxy_menu.js');
const actionSource=read('../../../app/src/proxy_initialization.js');
const url='chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/options.html';
const clone=x=>JSON.parse(JSON.stringify(x));
const fixed={...OmegaPac.Profiles.create('自用 "节点" & 💙','FixedProfile'),fallbackProxy:{scheme:'socks5',host:'127.0.0.1',port:1080},bypassList:[]};
const auto={...OmegaPac.Profiles.create('自动切换','SwitchProfile'),defaultProfileName:fixed.name,rules:[]};
const pac={...OmegaPac.Profiles.create('本地PAC','PacProfile'),pacScript:'function FindProxyForURL(url,host){return "DIRECT";}',pacUrl:''};
const options={'schemaVersion':2,['+'+fixed.name]:fixed,['+'+auto.name]:auto,['+'+pac.name]:pac};
const tempRules={'example.invalid':{condition:{conditionType:'HostWildcardCondition',pattern:'*.example.invalid'},profileName:'direct',isTempRule:true}};
const deletedRules={'example.invalid':{...tempRules['example.invalid'],profileName:'已删除'}};
const rules={...OmegaPac.Profiles.create('规则列表','RuleListProfile'),ruleList:'[SwitchyOmega Conditions]\n@with result\n*.example.invalid +direct\n* +direct',defaultProfileName:'direct',matchProfileName:'direct'};
const alias={...OmegaPac.Profiles.create('规则别名','VirtualProfile'),defaultProfileName:rules.name};
options['+'+rules.name]=rules;options['+'+alias.name]=alias;
function context(mode='ok',name=fixed.name) {
  const events=[],intervals=new Map();let sequence=0,samples=0,applied=0;
  const c={window:{},location:{href:url},chrome:{runtime:{id:'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'}},
    OmegaPac,TextEncoder,TextDecoder,Uint8Array,URL,setTimeout,clearTimeout,Date,
    setInterval:fn=>{const id=++sequence;intervals.set(id,fn);return id;},clearInterval:id=>intervals.delete(id)};
  vm.runInNewContext('('+helperSource+')('+JSON.stringify(url)+')',c);
  const initial=c.window.__starryfishProfiles.prepare(clone(options),name,mode==='temp'?tempRules:mode==='deleted-temp'?deletedRules:{});
  let effective={levelOfControl:'controlled_by_this_extension',value:clone(mode==='cleared-temp'||mode==='deleted-temp'?initial.alternative:initial.config)};
  c.window.gbrowserProxyQuery=packet=>{
    const verb=packet.request.split(':')[1];events.push(verb);
    if (mode==='deny-apply' && verb==='apply') packet.onFailure();
    else packet.onSuccess(['begin','apply'].includes(verb)?'authorized':'received');
  };
  c.chrome.runtime.sendMessage=(packet,reply)=>{
    events.push(packet.method);
    if (packet.method==='getAll') {
      ++samples;const value=clone(options);
      if (mode==='profile-changed' && samples>1) value['+'+fixed.name].fallbackProxy.port=9999;
      reply({result:value});
    } else if (packet.method==='applyProfile') {
      ++applied;assert.equal(applied,1);assert.equal(events.includes('apply'),true);
      assert.deepEqual(clone(packet.args),[name,{update:false}]);
      reply(mode==='apply-error'?{error:{}}:{result:null});
    } else if (packet.method==='getState') reply({result:{currentProfileName:name,isSystemProfile:false}});
    else if (packet.method==='getTempRules') {
      const rules=mode==='deleted-temp'?clone(deletedRules):mode==='temp'||(mode==='temp-changed' && samples>1)?clone(tempRules):{};
      if (mode==='deleted-temp' && applied) rules['example.invalid'].profileName=null;
      reply({result:rules});
    }
    else throw new Error('unexpected mutation');
  };
  c.chrome.proxy={settings:{get:(_,reply)=>reply(effective),onChange:{addListener(){},removeListener(){}}}};
  return {c,events,intervals,helper:c.window.__starryfishProfiles,get applied(){return applied;},
    replace:value=>{effective=value;}};
}
(async()=>{
  const base=context();const h=base.helper;
  assert.equal(h.decode(h.hex(fixed.name)),fixed.name);
  for (const bad of ['00','0','gg','ff','0a','41'.repeat(129)]) assert.throws(()=>h.decode(bad));
  for (const name of [fixed.name,auto.name,pac.name,rules.name,alias.name,'direct']) {
    const prepared=h.prepare(clone(options),name);
    assert.equal(h.matches(prepared,{levelOfControl:'controlled_by_this_extension',value:clone(prepared.config)}),true);
    assert.equal(h.matches(prepared,{levelOfControl:'controlled_by_other_extensions',value:clone(prepared.config)}),false);
  }
  const aliasPrepared=h.prepare(clone(options),alias.name);
  const originalAlias=OmegaPac.PacGenerator.ascii(OmegaPac.PacGenerator.compress(OmegaPac.PacGenerator.script(clone(options),alias)).print_to_string());
  const label=OmegaPac.PacGenerator.ascii(JSON.stringify(alias.name)).replace(/\*/g,'\\u002a').replace(/\\/g,'\\u002f');
  assert.equal(h.matches(aliasPrepared,{levelOfControl:'controlled_by_this_extension',value:{mode:'pac_script',pacScript:{mandatory:true,
    data:'/*OmegaProfile*'+label+'*'+alias.revision+'*/'+originalAlias}}}),true,'alias must compile as the original selected profile');
  const temporary=tempRules;
  const wrapped=h.prepare(clone(options),fixed.name,temporary);
  const tempProfile={...OmegaPac.Profiles.create('','SwitchProfile'),defaultProfileName:fixed.name,rules:Object.values(temporary)};
  const tempAst=OmegaPac.PacGenerator.compress(OmegaPac.PacGenerator.script(clone(options),tempProfile));
  const fixedLabel=OmegaPac.PacGenerator.ascii(JSON.stringify(fixed.name)).replace(/\*/g,'\\u002a').replace(/\\/g,'\\u002f');
  const tempConfig={mode:'pac_script',pacScript:{mandatory:true,data:'/*OmegaProfile*'+fixedLabel+'*'+fixed.revision+'*/'+OmegaPac.PacGenerator.ascii(tempAst.print_to_string())}};
  assert.equal(h.matches(wrapped,{levelOfControl:'controlled_by_this_extension',value:tempConfig}),true,'original temporary wrapper must match');
  assert.equal(h.matches(wrapped,{levelOfControl:'controlled_by_this_extension',value:clone(h.prepare(clone(options),fixed.name).config)}),false,'temporary routes cannot be ignored');
  const emptied=h.prepare(clone(options),fixed.name,{});
  assert.equal(h.matches(emptied,{levelOfControl:'controlled_by_this_extension',value:clone(emptied.alternative)}),true,'emptied private wrapper must match');
  const deleted=h.prepare(clone(options),fixed.name,deletedRules);
  assert.equal(deleted.temporaryRules.length,0);
  assert.equal(h.matches(deleted,{levelOfControl:'controlled_by_this_extension',value:clone(emptied.alternative)}),true,'deleted temporary target must use original cleanup result');
  assert.throws(()=>h.prepare(clone(options),fixed.name,{bad:{profileName:'direct'}}));
  const prepared=h.prepare(clone(options),auto.name),wrong=clone(prepared.config);
  wrong.pacScript.data=wrong.pacScript.data.replace('127.0.0.1','127.0.0.2');
  assert.equal(h.matches(prepared,{levelOfControl:'controlled_by_this_extension',value:wrong}),false);
  const changed=clone(options);delete changed['+'+fixed.name];assert.throws(()=>h.prepare(changed,auto.name));
  const overridden=clone(options);overridden['+direct']={name:'direct'};assert.throws(()=>h.prepare(overridden,fixed.name));
  for (const [mode,expected] of [['ok','matched'],['temp','matched'],['cleared-temp','matched'],['deleted-temp','matched'],['deny-apply','unconfirmed'],['apply-error','unconfirmed'],['profile-changed','different'],['temp-changed','different']]) {
    const test=context(mode);
    await vm.runInNewContext('('+actionSource+')('+['1',JSON.stringify(url),'{}','"custom"','false',JSON.stringify(test.helper.hex(fixed.name))].join(',')+')',test.c);
    assert.equal(test.events.includes(expected),true,mode);
    assert.equal(test.applied,['deny-apply','profile-changed','temp-changed'].includes(mode)?0:1,mode);
    if (mode==='ok') {
      test.replace({levelOfControl:'controlled_by_other_extensions',value:{mode:'direct'}});
      await [...test.intervals.values()][0]();await Promise.resolve();
      assert.equal(test.events.includes('changed'),true);assert.equal(test.applied,1);
    }
  }
  const menu=context();
  vm.runInNewContext('('+menuSource+')(7,'+JSON.stringify(url)+')',menu.c);
  for(let i=0;i<40;++i) await Promise.resolve();
  const value=menu.c.window.__starryfishProxyMenuTake(7);
  assert.equal(value.startsWith('SFPM1|'+menu.helper.hex(fixed.name)+'|'),true);
  assert.equal(value.includes(menu.helper.hex(auto.name)),true);
  assert.equal(value.includes('127.0.0.1')||value.includes('socks5'),false);
  assert.equal(menu.c.window.__starryfishProxyMenuTake(8),null);
  console.log('PASS custom proxy names/menu, exact readback, original PAC compilation, pre-write drift, denial, unknown/no replay and revocation; browser NOT_STARTED');
})().catch(error=>{console.error(error);process.exitCode=1;});
