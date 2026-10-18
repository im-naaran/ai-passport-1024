// Real isolated Chrome + production page, backed by a localhost device simulation.
// This checks rendering/interaction; it does not emulate ESP Flash, RF or device timing.
import assert from "node:assert/strict";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import http from "node:http";
import { spawn } from "node:child_process";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
const require = createRequire(import.meta.url);
const protocol = require("../main/web/badge/lightboard_protocol.js");
const layout = require("../main/web/badge/lightboard_layout.js");
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const artifacts = path.resolve(process.argv[3] || path.join(os.tmpdir(), "lightboard-browser-results"));
const executable = process.argv[2] || process.env.CHROME_PATH || (process.platform === "darwin"
    ? "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" : "/usr/bin/chromium");
const profile = await fs.mkdtemp(path.join(os.tmpdir(), "lightboard-chrome-"));
await fs.mkdir(artifacts, { recursive: true });
let revision = 0, texts = [...layout.DEFAULT_TEXTS], postMode = "ok", fontFails = false;
const posts = [], requests = [], errors = [];
const token = "0000000012345678";
const payload = (extra = {}) => ({schema:1,revision,max_items:6,max_chars:20,font_version:1,layout_version:1,token,texts,...extra});
const server = http.createServer(async (req, res) => {
    try {
        const url = new URL(req.url, "http://localhost");
        res.setHeader("Cache-Control", "no-store");
        const json = (value, code = 200) => { res.writeHead(code,{"Content-Type":"application/json"}); res.end(JSON.stringify(value)); };
        if (url.pathname === "/api/lightboard") {
            if (req.method === "GET") return json(payload());
            const chunks = []; for await (const chunk of req) chunks.push(chunk);
            const bytes = Buffer.concat(chunks);
            const decoded = protocol.decode(new Uint8Array(bytes));
            posts.push({revision:decoded.expectedRevision,texts:decoded.items.map(i=>i.text),bytes:bytes.length});
            if (postMode === "conflict") { revision++; texts=["另一页面的文案"]; return json({error:"revision_conflict"},409); }
            if (postMode === "storage") return json({error:"storage_error"},500);
            if (decoded.expectedRevision !== revision) return json({error:"revision_conflict"},409);
            assert.equal(req.headers["x-passport-session"],token);
            texts=decoded.items.map(i=>i.text); revision=((revision+1)>>>0)||1;
            if(postMode==="disconnect") {
                // Lose a partially received response after commit. Sending headers
                // first also prevents a browser's transparent stale-socket retry
                // from turning this transport scenario into a revision conflict.
                res.writeHead(200,{"Content-Type":"application/json","Content-Length":"512"});
                res.write('{"ok":true');setTimeout(()=>res.destroy(),10);return;
            }
            await new Promise(resolve=>setTimeout(resolve,100));
            return json(payload({ok:true}));
        }
        if(url.pathname==="/api/status") return json({token,connected:true});
        if(url.pathname==="/api/profile") {
            if(req.method==="POST") {for await(const _ of req){} return json({ok:true});}
            return json({name:"测试工牌",bio:"保持好奇",shape:"square"});
        }
        if(url.pathname==="/api/photo") {res.writeHead(200,{"Content-Type":"application/octet-stream"});return res.end(Buffer.alloc(80000));}
        if(url.pathname==="/favicon.ico") {res.writeHead(204);return res.end();}
        const name=url.pathname==="/"?"index.html":url.pathname.slice(1);
        if(!/^[a-z0-9_.]+$/.test(name)) {res.writeHead(404);return res.end();}
        if(name==="lightboard.woff2"&&fontFails) {res.writeHead(500);return res.end();}
        const data=await fs.readFile(path.join(root,"main/web/badge",name));
        const type=name.endsWith(".js")?"application/javascript":name.endsWith(".css")?"text/css":name.endsWith(".woff2")?"font/woff2":"text/html";
        res.writeHead(200,{"Content-Type":type});res.end(data);
    } catch(error) { errors.push(error.stack); if(!res.headersSent)res.writeHead(500);res.end(); }
});
await new Promise(resolve=>server.listen(0,"127.0.0.1",resolve));
const origin=`http://127.0.0.1:${server.address().port}`;
const chrome=spawn(executable,["--headless=new",`--user-data-dir=${profile}`,"--remote-debugging-pipe",
    "--no-first-run","--no-default-browser-check","--disable-background-networking","--disable-component-update",
    "--disable-sync","--metrics-recording-only"],{stdio:["ignore","ignore","pipe","pipe","pipe"]});
const chromeClosed = new Promise(resolve => chrome.once("exit", resolve));
let stderr="", sequence=0, buffer=Buffer.alloc(0), session, browserVersion;
const pending=new Map();
chrome.stderr.on("data",data=>{stderr=(stderr+data).slice(-6000);});
chrome.on("error",error=>{for(const p of pending.values())p.reject(error);pending.clear();});
chrome.stdio[4].on("data",data=>{
    buffer=Buffer.concat([buffer,data]);let end;
    while((end=buffer.indexOf(0))>=0){
        const value=JSON.parse(buffer.subarray(0,end).toString());buffer=buffer.subarray(end+1);
        if(value.id){const p=pending.get(value.id);if(!p)continue;pending.delete(value.id);clearTimeout(p.timer);
            if(value.error)p.reject(Error(JSON.stringify(value.error)));else p.resolve(value.result);
        } else if(value.method==="Network.requestWillBeSent")requests.push(value.params.request.url);
        else if(value.method==="Runtime.exceptionThrown")errors.push(value.params.exceptionDetails.exception?.description||value.params.exceptionDetails.text);
    }
});
function command(method,params={},target=session){
    return new Promise((resolve,reject)=>{
        const id=++sequence,timer=setTimeout(()=>{pending.delete(id);reject(Error(`CDP timeout: ${method}`));},20000);
        pending.set(id,{resolve,reject,timer});
        chrome.stdio[3].write(JSON.stringify({id,method,params,...(target?{sessionId:target}:{})})+"\0");
    });
}
async function evaluate(expression){
    const value=await command("Runtime.evaluate",{expression,awaitPromise:true,returnByValue:true});
    if(value.exceptionDetails)throw Error(value.exceptionDetails.exception?.description||value.exceptionDetails.text);
    return value.result.value;
}
async function waitFor(expression){
    return evaluate(`(async()=>{const end=performance.now()+18000;while(performance.now()<end){if(${expression})return true;await new Promise(r=>setTimeout(r,25));}throw Error(${JSON.stringify(`Wait: ${expression}`)});})()`);
}
async function edit(value){
    await evaluate(`(()=>{const e=document.getElementById('lightboard-texts');e.value=${JSON.stringify(value)};e.dispatchEvent(new Event('input',{bubbles:true}));})()`);
}
const save=()=>evaluate("document.getElementById('lightboard-form').requestSubmit()");
const message=()=>evaluate("document.getElementById('lightboard-message').textContent");
async function ready(){await waitFor("document.getElementById('lightboard-save') && !document.getElementById('lightboard-save').disabled");}
const checks=[];
try {
    browserVersion=await command("Browser.getVersion",{},null);
    const target=await command("Target.createTarget",{url:"about:blank"},null);
    session=(await command("Target.attachToTarget",{targetId:target.targetId,flatten:true},null)).sessionId;
    await command("Page.enable");await command("Runtime.enable");await command("Network.enable");
    await command("Emulation.setDeviceMetricsOverride",{width:390,height:844,deviceScaleFactor:1,mobile:true});
    await command("Page.navigate",{url:origin});await ready();
    assert.equal(await evaluate("document.getElementById('lightboard-texts').value"),texts.join("\n"));
    assert.equal(await evaluate("document.fonts.check('64px PassportLightboard')"),true);
    assert.equal(await evaluate("document.documentElement.scrollWidth <= innerWidth"),true);
    assert.equal(await evaluate("document.getElementById('save').disabled"),false);
    checks.push("390px page, offline font, six defaults and original badge initialized");
    for(const invalid of ["", "中".repeat(21),Array(7).fill("A").join("\n"),"😀"]){
        await edit(invalid);await waitFor("document.getElementById('lightboard-message').dataset.kind === 'error'");
        assert.equal(await evaluate("document.getElementById('lightboard-save').disabled"),true);
    }
    checks.push("empty, 21-character, seven-line and unsupported input rejected");
    await edit("  Hello 1024  \n今天不修 BUG");await ready();
    const before=posts.length;await save();
    assert.equal(await evaluate("document.getElementById('lightboard-save').disabled"),true);
    await save();await waitFor("document.getElementById('lightboard-message').dataset.kind === 'success'");
    assert.equal(posts.length,before+1);assert.deepEqual(posts.at(-1).texts,["Hello 1024","今天不修 BUG"]);
    checks.push("canonical two-plane POST, duplicate submission locked, successful echo");
    await waitFor("Array.from(document.getElementById('lightboard-preview').getContext('2d').getImageData(0,0,240,320).data).some((v,i)=>i%4!==3&&v>0)");
    const clip=await evaluate("(()=>{const r=document.querySelector('.lightboard-preview-card').getBoundingClientRect();return {x:r.x+scrollX,y:r.y+scrollY,width:r.width,height:r.height,scale:1};})()");
    const screenshot=await command("Page.captureScreenshot",{format:"png",captureBeyondViewport:true,clip});
    await fs.writeFile(path.join(artifacts,"browser-preview.png"),Buffer.from(screenshot.data,"base64"));
    postMode="conflict";await edit("冲突后保留的编辑");await ready();await save();
    await waitFor("document.getElementById('lightboard-message').textContent.includes('设备文案已更新')");
    assert.equal(await evaluate("document.getElementById('lightboard-texts').value"),"冲突后保留的编辑");
    assert.equal(await evaluate("document.getElementById('lightboard-save').disabled"),true);
    const afterConflict=posts.length;
    await evaluate("document.getElementById('lightboard-reload').click()");await ready();
    assert.equal(posts.length,afterConflict);assert.equal(await evaluate("document.getElementById('lightboard-texts').value"),"冲突后保留的编辑");
    postMode="ok";await save();await waitFor("document.getElementById('lightboard-message').dataset.kind === 'success'");
    checks.push("409 retains edits; reread refreshes revision without automatically saving");
    postMode="storage";await edit("存储失败仍保留");await ready();await save();
    await waitFor("document.getElementById('lightboard-message').textContent.includes('设备保存失败')");
    assert.equal(await evaluate("document.getElementById('lightboard-texts').value"),"存储失败仍保留");
    postMode="disconnect";await save();
    await waitFor("document.getElementById('lightboard-message').textContent.includes('保存结果未知')");
    assert.equal(await evaluate("document.getElementById('lightboard-save').disabled"),true);
    const afterDisconnect=posts.length;postMode="ok";
    await evaluate("document.getElementById('lightboard-reload').click()");await ready();
    assert.equal(posts.length,afterDisconnect);
    checks.push("500 and lost-response paths retain input; unknown result requires reconciliation");
    fontFails=true;await command("Page.navigate",{url:origin});
    await waitFor("document.getElementById('lightboard-message')?.textContent.includes('离线字体加载失败')");
    assert.equal(await evaluate("document.getElementById('lightboard-save').disabled"),true);
    assert.equal(await evaluate("document.getElementById('save').disabled"),false);
    fontFails=false;await evaluate("document.getElementById('lightboard-reload').click()");await ready();
    checks.push("font failure blocks lamp save, preserves badge operation, and reread retries locally");
    // Exercise browsers without Canvas filter using the same production Gaussian fallback.
    await command("Page.addScriptToEvaluateOnNewDocument",{source:"delete CanvasRenderingContext2D.prototype.filter;"});
    await command("Page.navigate",{url:origin});await ready();
    assert.equal(await evaluate("'filter' in document.createElement('canvas').getContext('2d')"),false);
    const fallback=await evaluate("(()=>{const i=LightboardLayout.render((w,h)=>{const c=document.createElement('canvas');c.width=w;c.height=h;return c;},'Hello 1024',0);return {width:i.width,height:i.height,different:i.layers[0].some((v,n)=>v!==i.layers[1][n])};})()");
    assert.equal(fallback.different,true);assert.ok(fallback.width<=240&&fallback.height<=320);
    checks.push("real Canvas with filter removed still produces bounded distinct core/halo planes");
    const external=requests.filter(url=>/^https?:/.test(url)&&!url.startsWith(origin+"/"));
    assert.deepEqual(external,[]);assert.deepEqual(errors,[]);
    const report={browser:browserVersion.product,viewport:"390x844",checks,requestCount:requests.length,
        externalRequests:external,posts:posts.map(p=>({revision:p.revision,bytes:p.bytes})),errors,
        note:"Local device API simulation; no ESP32, radio, ROM/MMU or phone-device acceptance."};
    await fs.writeFile(path.join(artifacts,"browser-check.json"),JSON.stringify(report,null,2)+"\n");
    console.log(JSON.stringify(report,null,2));
} catch(error) {
    try {
        console.error(JSON.stringify({page:await evaluate("({ready:document.readyState,hidden:document.hidden,message:document.getElementById('lightboard-message')?.textContent,input:document.getElementById('lightboard-texts')?.value,fontStatus:document.fonts.status})"),errors,requests},null,2));
    } catch (_) { /* preserve the original failure if the renderer already exited */ }
    console.error(error.stack,stderr);process.exitCode=1;
} finally {
    for(const p of pending.values()){clearTimeout(p.timer);p.reject(Error("browser closed"));}pending.clear();
    try {await command("Browser.close",{},null);} catch {chrome.kill();}
    await chromeClosed;
    server.closeAllConnections();await new Promise(resolve=>server.close(resolve));
    await fs.rm(profile,{recursive:true,force:true,maxRetries:10,retryDelay:100});
}
