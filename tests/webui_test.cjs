const vm = require('node:vm');
const fs = require('node:fs');
const assert = require('node:assert/strict');
class Element {
 constructor(tag='div'){this.tag=tag;this.children=[];this.dataset={};this.value='0';this.listeners={};this.disabled=false;}
 append(...nodes){this.children.push(...nodes);}
 replaceChildren(...nodes){this.children=nodes;}
 setAttribute(k,v){this[k]=v;}
 addEventListener(k,v){this.listeners[k]=v;}
 querySelector(selector){for(const c of this.children){if(c.tag===selector)return c;const found=c.querySelector?.(selector);if(found)return found;}return null;}
 reportValidity(){return true;}
}
const nodes={};
const document={documentElement:new Element('html'),querySelector:s=>nodes[s]??=new Element(),createElement:t=>new Element(t)};
const context=vm.createContext({document,navigator:{},window:{setInterval(){},setTimeout,clearTimeout},clearTimeout,TextEncoder,TextDecoder,console});
vm.runInContext(fs.readFileSync('WebSerialConfigurator/app.js','utf8'),context);
assert.equal(vm.runInContext('WEBUI_VERSION', context), '1.4.0');
assert.equal(nodes['#pageNav'].children.length,9);
assert(nodes['#pageNav'].children.slice(1).every(n=>n.disabled));
assert(vm.runInContext('responseMatcher("SAVE F 8")("OK SAVED F=8")',context));
assert(vm.runInContext('responseMatcher("GET RC")("RC MAP=1")',context));
assert(vm.runInContext('responseMatcher("SAVE RC 1")("OK SAVED RC=1")',context));
assert(!vm.runInContext('responseMatcher("SAVE RC 1")("OK SAVED RC=0")',context));
assert(vm.runInContext('responseMatcher("GET SENSOR")("SENSOR 0 800 800 512 1500 1520 1 7 99 7.4")',context));
assert(!vm.runInContext('responseMatcher("GET SENSOR")("SENSOR 0 800 800 512 1 7 99 7.4")',context));
assert(!vm.runInContext('responseMatcher("SAVE F 8")("SENSOR 0 800 800 512 1500 1520 1 7 99 7.4")',context));
assert(!vm.runInContext('responseMatcher("SAVE F 8")("OK SAVED B=8")',context));
vm.runInContext('var resolved = false; pendingResponse = {matches:responseMatcher("SAVE DEF 2"),resolve:()=>resolved=true,reject:()=>{},timeout:0};handleSerialLine("SENSOR 0 800 800 512 1500 1520 1 5 99 7.4")',context);
assert(!vm.runInContext('resolved',context));
vm.runInContext('handleSerialLine("OK SAVED DEF")',context);assert(vm.runInContext('resolved',context));
(async()=>{
 vm.runInContext('ready=true; firmware={type:"AutoRC",sensor:true,rcMapping:true};sendCommand=async command => command==="GET AUTO" ? "AUTO 35 35 100 100 100 120 50 300 100" : command==="GET DEF" ? "DEF 3 2000 50 50 50" : command.slice(4)+" "+Array(5).fill("0 0 0 100").join(" ")',context);
 await vm.runInContext('showPage("0")',context);
 const form=nodes['#editorContent'].querySelector('form');
 assert.equal(form.children.filter(n=>n.tag==='fieldset').length,5);
 const row=form.children[0];const toggle=row.querySelector('input');
 assert(!toggle.checked);assert(row.children[2].querySelector('input').disabled);
 toggle.checked=true;toggle.listeners.change();assert(!row.children[2].querySelector('input').disabled);
 await vm.runInContext('showPage("5")',context);
 assert.equal(nodes['#editorContent'].querySelector('form').children[0].children.length,5);
 await vm.runInContext('showPage("auto")',context);
 const routineForm=nodes['#editorContent'].querySelector('form');
 assert.equal(routineForm.children.filter(n=>n.tag==='fieldset').length,3);
 assert.equal(routineForm.children.slice(0,3).reduce((n,g)=>n+g.children[1].children.length,0),9);
 await vm.runInContext('showPage("home")',context);assert(nodes['#editorPage'].hidden);
 console.log('WebUI navigation, fields and response isolation tests passed');
})().catch(e=>{console.error(e);process.exitCode=1});
