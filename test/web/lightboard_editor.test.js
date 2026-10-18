"use strict";
const test = require("node:test"), assert = require("node:assert/strict");
const editor = require("../../main/web/badge/lightboard_app.js");
const protocol = require("../../main/web/badge/lightboard_protocol.js");
function payload(texts = ["Hello 1024"], revision = 0, extra = {}) {
    return { schema: 1, font_version: 1, layout_version: 1, max_items: 6, max_chars: 20,
        revision, token: "0000000012345678", texts, ...extra };
}
function response(data, status = 200) { return { ok: status === 200, status, json: async () => data }; }
function rendered(text) { return { text, mode: "poster", width: 8, height: 4, stride: 8, x: 12, y: 100,
    fontSize: 64, layers: [new Uint8Array(32).fill(128), new Uint8Array(32).fill(64)] }; }
function deferred() { let resolve, reject; const promise = new Promise((a,b) => { resolve=a; reject=b; }); return {promise,resolve,reject}; }
function fixture(overrides = {}) {
    let current = payload(), posts = [];
    const deps = { loadFont: async () => {}, render: rendered, fetch: async (url, options) => {
        assert.equal(url, "/api/lightboard");
        if (options.method !== "POST") { assert.equal(options.cache, "no-store"); return response(current); }
        posts.push(options);
        const parsed = protocol.decode(options.body);
        assert.equal(parsed.expectedRevision, current.revision);
        current = payload(parsed.items.map(i => i.text), current.revision + 1, {ok:true});
        return response(current);
    }, ...overrides };
    const changes = [];
    const model = editor.createEditor(deps, s => changes.push(s.items.map(i=>i.text).join("|")));
    return {model, deps, posts, changes, setCurrent: value => {current = value;} };
}
test("device read, canonical edit, two-plane envelope and saved echo use the same resources", async () => {
    const f=fixture(); await f.model.start();
    assert.equal(f.model.state.input, "Hello 1024"); assert.equal(f.model.canSave(), true);
    await f.model.edit("  中A1！ \r\n\n 中A1！  ");
    assert.equal(await f.model.save(), true);
    assert.equal(f.model.state.input, "中A1！\n中A1！"); assert.equal(f.model.state.revision, 1);
    assert.equal(f.posts[0].headers["X-Passport-Session"], "0000000012345678");
    assert.deepEqual(protocol.decode(f.posts[0].body).items.map(i=>i.text), ["中A1！","中A1！"]);
    assert.equal(f.model.canSave(), true); assert.equal(f.model.state.kind, "success");
});
test("six lines of twenty Unicode characters are allowed; empty, 21, 7 and unsupported input cannot submit", async () => {
    const f=fixture(); await f.model.start();
    for (const value of ["", " \n ", "中".repeat(21), Array(7).fill("A").join("\n"), "emoji😀", "A\u0000B"]) {
        await f.model.edit(value); assert.equal(f.model.canSave(), false, value);
        assert.equal(await f.model.save(), false);
    }
    await f.model.edit(Array(6).fill("中".repeat(20)).join("\n"));
    assert.equal(f.model.canSave(), true); assert.equal(f.model.state.items.length, 6);
    assert.equal(f.posts.length, 0);
});
test("duplicate save and edits are locked while a POST is outstanding", async () => {
    const gate=deferred(); let posts=0;
    const f=fixture({fetch: async (url, options) => {
        if (options.method !== "POST") return response(payload());
        posts++; return gate.promise;
    }}); await f.model.start(); await f.model.edit("新文案");
    const first=f.model.save(); assert.equal(f.model.state.uploading,true);
    assert.equal(await f.model.save(),false); await f.model.edit("不能覆盖");
    assert.equal(f.model.state.input,"新文案"); assert.equal(posts,1);
    gate.resolve(response(payload(["新文案"],1,{ok:true}))); assert.equal(await first,true);
    assert.equal(f.model.state.uploading,false);
});
test("pending offline font is shared and a later edit wins after the font arrives", async () => {
    const font=deferred(); let loads=0;
    const f=fixture({loadFont:()=>{loads++;return font.promise;}});
    const start=f.model.start(); await new Promise(setImmediate);
    const edit=f.model.edit("最新文案"); assert.equal(f.model.canSave(),false);
    font.resolve(); await Promise.all([start,edit]);
    assert.equal(loads,1); assert.deepEqual(f.model.state.items.map(i=>i.text),["最新文案"]);
    assert.equal(f.changes.includes("Hello 1024"),false);
});
test("font failure disables saving and explicit reread retries the offline font", async () => {
    let fail=true;
    const f=fixture({loadFont:async()=>{if(fail)throw Error("font");}}); await f.model.start();
    assert.match(f.model.state.validation,/离线字体加载失败/); assert.equal(await f.model.save(),false);
    fail=false; await f.model.reload(); assert.equal(f.model.canSave(),true);
    assert.equal(f.posts.length,0);
});
test("an older asynchronous render cannot overwrite a newer edit", async () => {
    const old=deferred();
    const f=fixture({render:(text)=>text==="旧文案"?old.promise:rendered(text)}); await f.model.start();
    const stale=f.model.edit("旧文案"); await new Promise(setImmediate);
    await f.model.edit("新文案"); old.resolve(rendered("旧文案")); await stale;
    assert.deepEqual(f.model.state.items.map(i=>i.text),["新文案"]);
    assert.equal(f.changes.includes("旧文案"),false);
});
test("a delayed GET preserves edits made after reading began", async () => {
    const get=deferred(); const f=fixture({fetch:()=>get.promise});
    const start=f.model.start(); await f.model.edit("先写好的文案");
    get.resolve(response(payload(["设备里的文案"],9))); await start;
    assert.equal(f.model.state.input,"先写好的文案"); assert.equal(f.model.state.revision,9);
    assert.equal(f.model.canSave(),true);
});
test("409 revision conflict retains text and requires reread plus a separate save", async () => {
    let posts=0, revision=0;
    const f=fixture({fetch:async(url,options)=>{
        if(options.method!=="POST")return response(payload(["设备文案"],revision));
        posts++; if(posts===1)return response({error:"revision_conflict"},409);
        const body=protocol.decode(options.body); assert.equal(body.expectedRevision,4);
        return response(payload(body.items.map(i=>i.text),5,{ok:true}));
    }}); await f.model.start(); await f.model.edit("保留编辑");
    assert.equal(await f.model.save(),false); assert.equal(f.model.canSave(),false);
    assert.equal(f.model.state.input,"保留编辑"); revision=4;
    await f.model.reload(); assert.equal(posts,1); assert.equal(f.model.state.input,"保留编辑");
    assert.equal(f.model.canSave(),true); assert.equal(await f.model.save(),true); assert.equal(posts,2);
});
test("busy and 500 errors retain editable input and never report success", async () => {
    for(const [code,status] of [["busy",409],["storage_error",500]]) {
        const f=fixture({fetch:async(url,options)=> options.method==="POST"?response({error:code},status):response(payload())});
        await f.model.start(); await f.model.edit("保留这条");
        assert.equal(await f.model.save(),false); assert.equal(f.model.state.input,"保留这条");
        assert.equal(f.model.state.kind,"error"); assert.equal(f.model.canSave(),true);
    }
});
test("lost response blocks automatic retries until GET reconciles the committed list", async () => {
    let saved=false, posts=0;
    const f=fixture({fetch:async(url,options)=>{
        if(options.method!=="POST")return response(payload(saved?["网络中断前的编辑"]:["Hello 1024"],saved?1:0));
        posts++;saved=true;throw Error("disconnect");
    }}); await f.model.start(); await f.model.edit("网络中断前的编辑");
    assert.equal(await f.model.save(),false); assert.equal(f.model.state.uncertain,true);
    assert.equal(await f.model.save(),false); assert.equal(posts,1);
    await f.model.reload(); assert.equal(f.model.state.revision,1); assert.equal(posts,1);
    assert.equal(f.model.state.input,"网络中断前的编辑"); assert.equal(f.model.canSave(),true);
});
test("bad device versions and failed initial reads never enable saving", async () => {
    for(const value of [payload(["A"],0,{font_version:2}),payload(["😀"]),payload(["A"],-1),payload(["A"],0,{token:"bad"})]){
        const f=fixture({fetch:async()=>response(value)}); assert.equal(await f.model.start(),false);
        assert.equal(f.model.canSave(),false); assert.equal(f.model.state.kind,"error");
    }
});
test("preview timing mirrors static duration, breathing bounds and complete 80px per second scroll",()=>{
    const short={mode:"poster",x:20,width:200}, long={mode:"scroll",x:0,width:780};
    assert.equal(editor.frameAt([],0),null); assert.equal(editor.frameAt([short,long],3000).index,1);
    assert.equal(editor.frameAt([short],750).core,255); assert.equal(editor.frameAt([short],1500).core,204);
    assert.equal(editor.frameAt([short],3000).core,0);
    assert.equal(editor.frameAt([long],0).x,240); assert.equal(editor.frameAt([long],1120).x,160);
    assert.equal(editor.frameAt([long],12870).x,-780); assert.equal(editor.frameAt([long],12990).x,240);
});
