import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';
const core=path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const require=createRequire(path.join(core,'web-ui/package.json'));
const babel=require('@babel/core');
const compiled={};
for(const file of ['Scanner.tsx','types.ts','values.ts']) {
 const filename=path.join(core,'web-ui/esp32m/ui/src/scanners/modbus',file);
 compiled[file]=(await babel.transformAsync(readFileSync(filename,'utf8'),{
  filename,configFile:false,babelrc:false,
  presets:[[require.resolve('@babel/preset-typescript'),{onlyRemoveTypeImports:false}],
    [require.resolve('@babel/preset-react'),{runtime:'automatic',development:false}]],
  plugins:[require.resolve('@babel/plugin-transform-modules-commonjs')]
 })).code;
}
const normal={configured:true,sharedBus:true,mode:'rtu',uart:2,baud:9600,parity:0,
 from:1,to:247,addr:1,cmd:3,regs:0,regc:1,busy:false,job:0,operation:'scan',
 scanned:0,total:0,progress:0,elapsedMs:0,cancelRequested:false,cancelled:false,error:0,results:[]};
const plain=value=>JSON.parse(JSON.stringify(value));
function harness(initial={}) {
 let state={...normal,...initial}, config={...normal}, now=10000, live=true;
 let cursor=0, dirty=false, hooks=[], effects=[], handler;
 const timers=[],calls=[],modules={};
 const slot=fn=>{const i=cursor++;if(!(i in hooks))hooks[i]=fn();return i;};
 const react={
  useState(value){const i=slot(()=>typeof value==='function'?value():value);
   return [hooks[i],v=>{const n=typeof v==='function'?v(hooks[i]):v;
    if(!Object.is(n,hooks[i])){hooks[i]=n;dirty=true;}}];},
  useRef(value){return hooks[slot(()=>({current:value}))];},
  useEffect(effect,deps){const i=slot(()=>undefined);
   if(!hooks[i]||deps.some((v,n)=>!Object.is(v,hooks[i][n]))){hooks[i]=deps;effects.push(effect);}}
 };
 const api={async request(name,command,data){
  calls.push({name,command,data:plain(data??{})});
  if(handler)return handler(command,data);
  if(command==='cancel'){state={...state,cancelRequested:true};return {data:{}};}
  state={...normal,...data,job:state.job+1,operation:command==='scan'?'scan':command==='write'?'write':'read',
   scanned:command==='scan'?data.to-data.from+1:1,total:command==='scan'?data.to-data.from+1:1,
   progress:100,elapsedMs:1200,results:command==='scan'?[[data.from,0,8908]]:[],
   values:command==='scan'?[]:[8908],verified:command==='write',writeError:0,verifyError:0};
  return {data:state};
 }};
 const jsx=(type,props)=>({type,props:props??{}});
 const context=vm.createContext({console,Date:class extends Date{static now(){return now;}},
  setInterval:fn=>{timers.push(fn);return timers.length;},clearInterval:()=>{}});
 const load=file=>{
  if(modules[file])return modules[file];
  const exports=modules[file]={};
  const wrapper='(function(exports, require) {'+compiled[file]+'\n})';
  vm.runInContext(wrapper,context)(exports,id=>{
   if(id==='react')return react;
   if(id==='react/jsx-runtime')return {jsx,jsxs:jsx,Fragment:'Fragment'};
   if(id==='@mui/material')return new Proxy({},{get:(_,key)=>key});
   if(id==='@ts-libs/ui-app')return {CardBox:'CardBox'};
   if(id==='@ts-libs/tools')return {errorToString:e=>e.message};
   if(id==='../../backend')return {useBackendApi:()=>api,useSubscribedState:()=>[state,async()=>state,live],useRequest:(name,command)=>{
    assert.equal(name,'modbus');
    return [command==='state-get'?state:config,async()=>state];
   }};
   if(id==='./types')return load('types.ts');
   if(id==='./values')return load('values.ts');
   throw new Error('Unexpected import '+id);
  });return exports;
 };
 const text=node=>{
  if(Array.isArray(node))return node.map(text).join(' ');
  if(node==null||typeof node==='boolean')return '';
  if(typeof node!=='object')return String(node);
  return text(node.props.children);
 };
 const render=()=>{
  for(let n=0;n<10;++n) {
   dirty=false;cursor=0;const tree=load('Scanner.tsx').default();
   for(const effect of effects.splice(0))effect();
   if(dirty)continue;
   const nodes=[];const walk=node=>{
    if(Array.isArray(node))return node.forEach(walk);
    if(!node||typeof node!=='object')return;
    nodes.push(node);walk(node.props.children);walk(node.props.control);
   };walk(tree);return {tree,nodes,text:text(tree)};
  }throw new Error('Render loop');
 };
 const find=(type,label)=>render().nodes.find(n=>n.type===type&&(n.props.label===label||text(n)===label));
 return {render,find,calls,load,api,
  edit:(label,value)=>{const f=find('TextField',label);assert(f,label);f.props.onChange({target:{value}});},
  click:async label=>{const b=find('Button',label);assert(b,label);assert(!b.props.disabled,label+' disabled');await b.props.onClick();},
  tab:value=>find('Tabs',undefined).props.onChange(null,value),
  confirm:()=>{const f=find('FormControlLabel','I confirm writing these values to this slave address.');f.props.control.props.onChange(null,true);},
  advance:ms=>{now+=ms;timers.forEach(fn=>fn());},
  setState:value=>{state={...state,...value};},
  setLive:value=>{live=value;},
  setHandler:value=>{handler=value;}
 };
}
{
 const h=harness();
 assert.doesNotMatch(h.render().text,/Flomarvel|actual angle/i);
 assert.equal(h.find('TextField','From address').props.value,'1');
 assert.equal(h.find('TextField','To address').props.value,'247');
 h.edit('To address','1');
 h.edit('Start register / coil','3');
 assert.equal(h.find('TextField','Start register / coil').props.value,'3');
 await h.click('Scan');
 assert.deepEqual(h.calls[0],{name:'modbus',command:'scan',data:{from:1,to:1,cmd:3,regs:3,regc:1}});
 assert.match(h.render().text,/Reply received/);assert.match(h.render().text,/8908/);
 h.edit('From address','3');h.edit('To address','3');await h.click('Scan');
 assert.equal(h.calls[1].data.from,3);assert.equal(h.calls[1].data.to,3);
 h.edit('Start register / coil','99');
 assert.match(h.render().text,/start\s+3\s/); // Results retain actual request parameters.
}
{
 const h=harness({job:1,busy:true,from:1,to:3,total:3,scanned:1,progress:33,results:[[1,0,8908]]});
 assert(h.find('Button','Scan').props.disabled);
 await h.click('Stop scan');assert.equal(h.calls[0].command,'cancel');
 assert(h.find('Button','Stop scan').props.disabled);
 h.setState({busy:false,cancelled:true});
 assert.match(h.render().text,/cancelled/);
}
{
 const h=harness();h.edit('To address','1');h.edit('Start register / coil','3');
 let release;const waiting=new Promise(resolve=>{release=resolve;});
 h.setHandler(async()=>{await waiting;h.setState({job:1,total:1,scanned:1,progress:100});return {data:{}};});
 const button=h.find('Button','Scan');
 const first=button.props.onClick();await button.props.onClick();
 assert.equal(h.calls.length,1);release();await first;
 h.setState({job:0,results:[],scanned:0,total:0,progress:0});
 assert(!h.render().text.includes('Operation 1')); // Firmware reboot clears retained status.
}
{
 const h=harness();h.render();h.advance(6000);
 assert(!h.render().text.includes('status is stale')); // Stable subscribed state is current.
 h.setLive(false);
 assert.match(h.render().text,/status is stale/);assert(h.find('Button','Scan').props.disabled);
 const absent=harness({configured:false});assert(absent.find('Button','Scan').props.disabled);
}
{
 const h=harness();h.tab(1);
 h.edit('Slave address','1');h.edit('Start register / coil','0x3');h.edit('Quantity','2');
 await h.click('Read values');
 assert.deepEqual(h.calls[0].data,{addr:1,cmd:3,regs:3,regc:2});
 assert.equal(h.calls[0].command,'request');
 h.tab(2);h.edit('Quantity','1');h.edit('Values','9000');
 assert(h.find('Button','Write once and verify').props.disabled);
 h.confirm();await h.click('Write once and verify');
 assert.deepEqual(h.calls[1].data,{addr:1,cmd:6,regs:3,regc:1,data:[9000],confirm:true});
 assert(h.find('Button','Write once and verify').props.disabled); // Confirmation consumed.
 assert.match(h.render().text,/Requested values observed/);
}
{
 const h=harness();h.tab(2);h.edit('Values','1');h.confirm();
 h.setHandler(async()=>{throw new Error('Lost reply');});
 await h.click('Write once and verify');
 assert.equal(h.calls.length,1);assert.match(h.render().text,/before retrying/);
 assert(h.find('Button','Write once and verify').props.disabled);
}
{
 const h=harness({job:1,scanned:1,total:1,progress:100,results:[[1,263]],busy:false});
 assert.match(h.render().text,/does not prove/);assert(!h.render().text.includes('Reply received'));
 const helpers=h.load('values.ts');
 const input={from:'1',to:'1',addr:'1',regs:'0',regc:'1',data:'1'};
 assert.equal(helpers.requestFor(1,{...input,regs:'65535',regc:'2'},3,false),undefined);
 assert.equal(helpers.requestFor(1,input,6,false),undefined);
 assert.equal(helpers.requestFor(2,input,6,false),undefined);
 assert.equal(helpers.requestFor(2,{...input,data:'2'},5,true),undefined);
 assert.equal(helpers.requestFor(0,{...input,from:'2'},3,false),undefined);
 assert.equal(helpers.requestFor(1,{...input,regc:''},3,false),undefined);
 assert.equal(helpers.requestFor(1,{...input,addr:'0'},3,false),undefined);
 const types=h.load('types.ts');
 assert.equal(types.WriteCommands.find(([,label])=>label==='Write registers (16)')[0],16);
}
console.log('Modbus scanner UI: generic scans, retained results, cancel, reload, stale status, reads, confirmed writes and failure handling passed');
