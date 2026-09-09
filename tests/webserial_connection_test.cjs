const assert=require('node:assert/strict');
const fs=require('fs'),vm=require('vm'),{ReadableStream,WritableStream}=require('node:stream/web');
const prefix=fs.readFileSync('tests/webui_test.cjs','utf8').split('const nodes={};')[0].split('class Element')[1];
(async()=>{for(const dropped of [0,1,3]) for(const hello of ['OK DEVICE=MAKER_MINI_SUMO VERSION=1','OK DEVICE=MAKER_MINI_SUMO FW=RC VERSION=1.2.0 PROTOCOL=2','OK DEVICE=MAKER_MINI_SUMO FW=AutoRC VERSION=1.3.0 PROTOCOL=5']){
 const commands=[];let controller;let helloCount=0;
 const port={getInfo:()=>({usbVendorId:0x1A86,usbProductId:0x7523}),open:async()=>{},close:async()=>{},readable:new ReadableStream({start(c){controller=c;}}),writable:new WritableStream({write(bytes){const command=new TextDecoder().decode(bytes).trim();commands.push(command);if(command==='HELLO' && ++helloCount <= dropped)return;const response=command==='HELLO'?hello:command==='CONFIG ON'?'OK CONFIG':command==='GET RC'?'RC MAP=0':'CONFIG F=0 B=0';controller.enqueue(new TextEncoder().encode(response+'\r\n'));}})};
 const context=vm.createContext({console,TextDecoder,TextEncoder,clearTimeout,port,navigator:{serial:{requestPort:async()=>port,addEventListener(){}}},window:{setInterval(){},setTimeout:(f,ms)=>setTimeout(f,ms===2000?0:10)}});
 vm.runInContext('class Element'+prefix+`const nodes={};const document={documentElement:new Element('html'),querySelector:s=>nodes[s]??=new Element(),createElement:t=>new Element(t)};`,context);
 vm.runInContext(fs.readFileSync('WebSerialConfigurator/app.js','utf8'),context);await new Promise(r=>setImmediate(r));
 if(dropped===3) {
  await assert.rejects(vm.runInContext('connect()',context), /no reply to HELLO after 3 attempts/);
  assert.equal(vm.runInContext('ready',context),false);
  assert.equal(commands.length,3);
 } else {
  await vm.runInContext('connect()',context);
  assert.equal(vm.runInContext('ready',context),true);
  assert.equal(vm.runInContext('document.querySelector("#firmwareInfo").textContent',context),`MakerMiniSumo_${hello.includes('FW=AutoRC')?'AutoRC':'RC'} · Version ${hello.match(/VERSION=([^ ]+)/)[1]}`);
  assert.equal(helloCount,dropped+1);
  assert.equal(commands.filter(c=>c==='GET CONFIG').length,1);
 }
 await vm.runInContext('disconnect()',context);
}console.log('Connection tests passed: 3 firmware handshakes, delayed startup recovery and bounded timeout.');})().catch(error=>{console.error(error);process.exitCode=1;});
