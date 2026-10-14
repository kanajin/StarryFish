// Read-only helpers inside the pinned original options document. Only names
// leave this document; complete profiles/PAC/auth stay here, never in logs.
(function installProxyProfiles(expectedUrl) {
  const sameDocument = () => location.href.split('#')[0] === expectedUrl &&
    'chrome-extension://' + chrome.runtime.id + '/options.html' === expectedUrl;
  const nameValid = name => typeof name === 'string' && name.length > 0 && name.length <= 128 &&
    !/[\u0000-\u001f\u007f]/.test(name) && new TextEncoder().encode(name).length <= 512 &&
    new TextDecoder('utf-8',{fatal:true}).decode(new TextEncoder().encode(name))===name;
  const hex = name => [...new TextEncoder().encode(name)].map(n => n.toString(16).padStart(2,'0')).join('');
  const decode = text => {
    if (typeof text !== 'string' || !/^(?:[0-9a-f]{2}){1,512}$/.test(text)) throw new Error('invalid name');
    const name = new TextDecoder('utf-8',{fatal:true}).decode(Uint8Array.from(text.match(/../g),n => parseInt(n,16)));
    if (!nameValid(name)) throw new Error('invalid name');
    return name;
  };
  const equal = (a,b) => {
    let budget=100000;
    const visit=(x,y,depth=0) => {
      if (--budget<0 || depth>64) return false;
      if (x===y) return true;
      if (!x || !y || typeof x!=='object' || typeof y!=='object' || Array.isArray(x)!==Array.isArray(y)) return false;
      const keys=Object.keys(x);
      return keys.length===Object.keys(y).length && keys.every(k => Object.prototype.hasOwnProperty.call(y,k) && visit(x[k],y[k],depth+1));
    };
    return visit(a,b);
  };
  const prepare = (options,name,tempRules={}) => {
    if (!sameDocument() || !nameValid(name) || !options || Array.isArray(options) ||
        Object.prototype.hasOwnProperty.call(options,'+direct') || Object.prototype.hasOwnProperty.call(options,'-builtinProfiles') ||
        typeof OmegaPac==='undefined') throw new Error('profile unavailable');
    const profiles={};
    const copy=(key,depth=0) => {
      if (!nameValid(key) || depth>64) throw new Error('invalid reference');
      if (key==='direct' || Object.prototype.hasOwnProperty.call(profiles,'+'+key)) return;
      const p=options['+'+key];
      if (!p || p.name!==key || typeof p.profileType!=='string' || Object.keys(profiles).length>=64) throw new Error('missing profile');
      profiles['+'+key]=JSON.parse(JSON.stringify(p));
      const refs=OmegaPac.Profiles.directReferenceSet(p);
      for (const ref of Object.values(refs)) copy(ref,depth+1);
    };
    copy(name);
    if (!tempRules || typeof tempRules!=='object' || Array.isArray(tempRules) ||
        Object.keys(tempRules).length>4096 || JSON.stringify(tempRules).length>2*1024*1024) throw new Error('invalid temporary rules');
    const temporaryRules=[];
    for (const rule of Object.values(tempRules)) {
      if (!rule || typeof rule!=='object' || Array.isArray(rule) || rule.isTempRule!==true ||
          !rule.condition || typeof rule.condition!=='object' || Array.isArray(rule.condition) ||
          rule.condition.conditionType!=='HostWildcardCondition' || typeof rule.condition.pattern!=='string' ||
          rule.condition.pattern.length>2048 || (rule.profileName!==null && !nameValid(rule.profileName)))
        throw new Error('invalid temporary rule');
      // Options.applyProfile clears rules whose target was deleted, before it
      // builds the PAC. Normalize both the pre-cleanup and null-target readback.
      if (rule.profileName===null || !OmegaPac.Profiles.byName(rule.profileName,options)) continue;
      temporaryRules.push(JSON.parse(JSON.stringify(rule)));
    }
    for (const rule of temporaryRules) copy(rule.profileName);
    if (JSON.stringify(profiles).length>4*1024*1024) throw new Error('profile too large');
    // The original Options.applyProfile passes the selected alias itself to
    // ProxyImpl. Compiling only its destination changes the generated PAC.
    const profile=OmegaPac.Profiles.byName(name,profiles),meta=profile;
    if (!profile) throw new Error('missing alias');
    const generate=selected => {
      for (const p of Object.values(profiles)) if (p.profileType.endsWith('RuleListProfile') && p.pacScript) {
        const temp=OmegaPac.Profiles.create('__tempZeroRuleListPac',p.profileType);
        for (const key of ['defaultProfileName','format','matchProfileName','ruleList']) temp[key]=p[key];
        temp.isTempPacProfile=true;
        const ast=OmegaPac.PacGenerator.script({['+'+temp.name]:temp},temp.name,{profileNotFound:()=> 'ignore'});
        if (p.pacScript!==OmegaPac.PacGenerator.ascii(ast.print_to_string({beautify:true,comments:true})))
          throw new Error('derived PAC differs');
      }
      const ast=OmegaPac.PacGenerator.compress(OmegaPac.PacGenerator.script(profiles,selected,
        {profileNotFound:()=> {throw new Error('missing PAC reference');}}));
      const label=OmegaPac.PacGenerator.ascii(JSON.stringify(meta.name)).replace(/\*/g,'\\u002a').replace(/\\/g,'\\u002f');
      return {mode:'pac_script',pacScript:{mandatory:true,data:'/*OmegaProfile*'+label+'*'+meta.revision+'*/'+OmegaPac.PacGenerator.ascii(ast.print_to_string())}};
    };
    let config;
    if (profile.profileType==='DirectProfile') config={mode:'direct'};
    else if (profile.profileType==='FixedProfile') {
      const rules={};
      const protocols=['proxyForHttp','proxyForHttps','proxyForFtp'];
      let specified=false;
      for (const key of protocols) if (profile[key]!=null) {rules[key]=profile[key];specified=true;}
      if (profile.fallbackProxy) {
        if (profile.fallbackProxy.scheme==='http') {
          if (!specified) rules.singleProxy=profile.fallbackProxy;
          else for (const key of protocols) if (rules[key]==null) rules[key]=profile.fallbackProxy;
        } else rules.fallbackProxy=profile.fallbackProxy;
      }
      if (!specified && !profile.fallbackProxy) config={mode:'direct'};
      else {
        if (!Array.isArray(profile.bypassList) || profile.bypassList.length>4096) throw new Error('invalid bypass rules');
        rules.bypassList=profile.bypassList.map(c => {const text=OmegaPac.Conditions.str(c);return text.substr(text.indexOf(' ')+1);});
        config={mode:'fixed_servers',rules};
      }
    } else if (profile.profileType==='PacProfile') {
      if (OmegaPac.Profiles.isFileUrl(profile.pacUrl)) throw new Error('unsupported PAC source');
      if (profile.pacScript && (typeof profile.pacScript!=='string' || profile.pacScript.length>2*1024*1024)) throw new Error('invalid PAC');
      if (profile.pacScript && profile.pacScript.length<=2*1024*1024)
        config={mode:'pac_script',pacScript:{data:OmegaPac.PacGenerator.ascii(profile.pacScript),mandatory:true}};
      else {
        const url=new URL(profile.pacUrl);
        if (!['http:','https:'].includes(url.protocol) || url.username || url.password || url.href.length>8192)
          throw new Error('unsupported PAC source');
        config={mode:'pac_script',pacScript:{url:profile.pacUrl,mandatory:true}};
      }
    } else if (['SwitchProfile','VirtualProfile','RuleListProfile','SwitchyRuleListProfile','AutoProxyRuleListProfile'].includes(profile.profileType)) {
      config=generate(profile);
    } else throw new Error('unsupported profile');
    let alternative=null;
    if (OmegaPac.Profiles.isIncludable(profile)) {
      const temp={...OmegaPac.Profiles.create('','SwitchProfile'),defaultProfileName:name,rules:temporaryRules};
      const wrapped=generate(temp);
      // getTempRules cannot distinguish never-created from an emptied private
      // wrapper. Both exact compiler outputs express the same empty-rule intent.
      if (temporaryRules.length) config=wrapped;
      else alternative=wrapped;
    } else if (temporaryRules.length) throw new Error('temporary rules unsupported');
    return {profiles,config,alternative,temporaryRules,name};
  };
  const canonical = text => {
    const factory = /^(\/\*OmegaProfile\*[^\r\n]*?\*\/var FindProxyForURL=)function\(([a-zA-Z_$]),([a-zA-Z_$])\)\{return function\(([a-zA-Z_$]),([a-zA-Z_$])\)\{"use strict";var ([a-zA-Z_$])=\2,([a-zA-Z_$])=\4\.substr\(0,\4\.indexOf\(":"\)\);do\{if\(!\3\[\6\]\)return \6;\6=\3\[\6\];if\("function"===typeof \6\)\6=\6\(\4,\5,\7\)\}while\("string"!==typeof \6\|\|43===\6\.charCodeAt\(0\)\);return \6\}\}\(/;
    const match=factory.exec(text);
    return match && new Set(match.slice(2)).size===6 ? match[1]+'<validated Omega factory>('+text.slice(match[0].length) : null;
  };
  const matchesConfig = (config,effective) => {
    if (!effective || effective.levelOfControl!=='controlled_by_this_extension' || !effective.value) return false;
    if (equal(config,effective.value)) return true;
    const reference=config.pacScript,actual=effective.value.pacScript;
    if (config.mode!=='pac_script' || !reference || !actual ||
        typeof reference.data!=='string' || typeof actual.data!=='string') return false;
    const normalized=canonical(reference.data),other=canonical(actual.data);
    return normalized!==null && other!==null && equal(
      {...config,pacScript:{...reference,data:normalized}},
      {...effective.value,pacScript:{...actual,data:other}});
  };
  const matches=(prepared,effective)=>matchesConfig(prepared.config,effective) ||
    (prepared.alternative!==null && matchesConfig(prepared.alternative,effective));
  const sameIntent=(a,b)=>equal(a.profiles,b.profiles) && equal(a.temporaryRules,b.temporaryRules);
  window.__starryfishProfiles={decode,hex,nameValid,prepare,equal,matches,sameIntent,sameDocument};
})
