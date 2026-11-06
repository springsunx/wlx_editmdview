// Actual Chromium layout/hit-testing regression, without third-party Node modules.
// Usage: node tests/preview_outline_test.cjs [path-to-msedge.exe]
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
const browser = process.argv[2] || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe';
const profile = fs.mkdtempSync(path.join(root, 'build', 'outline-browser-'));
const child = spawn(browser, ['--headless=new', '--no-first-run', '--disable-gpu',
    '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
    '--remote-debugging-port=0', '--user-data-dir=' + profile, 'about:blank'],
    { windowsHide: true, stdio: ['ignore', 'ignore', 'pipe'] });
let socket;
let sequence = 0;
let session;
const pending = new Map();
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const send = (method, params = {}, sessionId = session) => new Promise((resolve, reject) => {
    const id = ++sequence;
    pending.set(id, { resolve, reject });
    socket.send(JSON.stringify({ id, method, params, sessionId }));
});
const evaluate = async expression => {
    const response = await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
    if (response.exceptionDetails) throw Error(JSON.stringify(response.exceptionDetails));
    return response.result.value;
};
const waitFor = async (expression, message) => {
    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
        if (await evaluate(expression)) return;
        await delay(50);
    }
    throw Error(message + ': ' + JSON.stringify(await evaluate(`({scrollY, height:innerHeight,
        target:document.getElementById('heading-239').getBoundingClientRect().toJSON()})`)));
};
(async () => {
    const endpoint = await new Promise((resolve, reject) => {
        let output = '';
        const timer = setTimeout(() => reject(Error('Browser startup timed out: ' + output)), 20000);
        child.on('error', reject);
        child.stderr.on('data', chunk => {
            output += chunk;
            const match = /DevTools listening on (ws:\/\/[^\s]+)/.exec(output);
            if (match) { clearTimeout(timer); resolve(match[1]); }
        });
    });
    socket = new WebSocket(endpoint);
    await new Promise((resolve, reject) => { socket.onopen = resolve; socket.onerror = reject; });
    socket.onmessage = event => {
        const message = JSON.parse(event.data);
        const request = pending.get(message.id);
        if (!request) return;
        pending.delete(message.id);
        if (message.error) request.reject(Error(JSON.stringify(message.error)));
        else request.resolve(message.result);
    };
    const { targetId } = await send('Target.createTarget', { url: 'about:blank' });
    session = (await send('Target.attachToTarget', { targetId, flatten: true })).sessionId;
    await send('Page.enable');
    await send('Emulation.setDeviceMetricsOverride', { width: 900, height: 600, deviceScaleFactor: 1, mobile: false });
    const markdown = fs.readFileSync(path.join(root, 'src/markdown.cpp'), 'utf8');
    const railCss = [...markdown.matchAll(/R"css\(([\s\S]*?)\)css"/g)]
        .map(match => match[1]).find(css => css.startsWith('.editmdview-outline'));
    const css = fs.readFileSync(path.join(root, 'src/assets/preview.css'), 'utf8');
    const script = fs.readFileSync(path.join(root, 'src/assets/preview.js'), 'utf8');
    const fixture = count => '<style>*{box-sizing:border-box}body{margin:0}h2{height:90px;margin:20px}' +
        railCss + css + '</style><main>' + Array.from({ length: count }, (_, i) =>
            `<h2 id="heading-${i}">Heading ${i}</h2>`).join('') +
        '</main><nav class="editmdview-outline" style="--outline-height:420px">' +
        Array.from({ length: count }, (_, i) => `<a href="#heading-${i}"><span class="outline-mark"></span>` +
            `<span class="outline-label">Heading ${i}</span></a>`).join('') + '</nav>';
    const { frameTree } = await send('Page.getFrameTree');
    await send('Page.setDocumentContent', { frameId: frameTree.frame.id, html: fixture(240) });
    await evaluate(script);
    await evaluate(`window.chrome = {webview:{addEventListener(){},postMessage(){}}};
        EditMdViewPreview.install({}); document.documentElement.style.scrollBehavior='auto'; true`);
    assert.equal(await evaluate(`document.querySelector('.editmdview-outline').classList.contains('is-overflowing')`), true);
    const railPoint = await evaluate(`(() => {const r=document.querySelector('.editmdview-outline').getBoundingClientRect();return {x:r.right-4,y:r.top+30}})()`);
    await send('Input.dispatchMouseEvent', { type: 'mouseWheel', ...railPoint, deltaY: 100000, deltaX: 0 });
    await delay(100);
    assert.equal(await evaluate('window.scrollY'), 0, 'outline wheel must not move the document');
    assert.equal(await evaluate(`(() => {const n=document.querySelector('.editmdview-outline');return n.scrollTop+n.clientHeight>=n.scrollHeight-1})()`), true);
    const lastPoint = await evaluate(`(() => {const r=document.querySelector('.editmdview-outline a:last-child').getBoundingClientRect();return {x:r.right-4,y:r.top+r.height/2}})()`);
    assert.equal(await evaluate(`document.elementFromPoint(${lastPoint.x},${lastPoint.y}).closest('a').hash`), '#heading-239', 'last visible mark must hit last heading');
    await send('Input.dispatchMouseEvent', { type: 'mouseMoved', ...lastPoint });
    assert.equal(await evaluate(`document.querySelector('.editmdview-outline-tooltip').textContent`), 'Heading 239');
    assert.equal(await evaluate(`document.querySelector('.editmdview-outline-tooltip').hidden`), false);
    await send('Input.dispatchMouseEvent', { type: 'mousePressed', ...lastPoint, button: 'left', clickCount: 1 });
    await send('Input.dispatchMouseEvent', { type: 'mouseReleased', ...lastPoint, button: 'left', clickCount: 1 });
    await waitFor(`(() => {const r=document.getElementById('heading-239').getBoundingClientRect();return r.top<innerHeight&&r.bottom>0})()`,
        'last heading must be visible after click');
    assert.equal(await evaluate('location.hash'), '#heading-239');
    assert.equal(await evaluate(`(() => {const r=document.getElementById('heading-239').getBoundingClientRect();return r.top<innerHeight&&r.bottom>0})()`), true, 'last heading must be visible after click');
    await evaluate(`const first=document.querySelector('.editmdview-outline a'); first.focus({preventScroll:true});
        first.dispatchEvent(new KeyboardEvent('keydown',{key:'End',bubbles:true})); true`);
    assert.equal(await evaluate('document.activeElement.hash'), '#heading-239');
    await evaluate(`document.activeElement.dispatchEvent(new KeyboardEvent('keydown',{key:'Home',bubbles:true})); true`);
    assert.equal(await evaluate('document.activeElement.hash'), '#heading-0');
    await evaluate('document.activeElement.blur(); window.scrollTo(0,0); true');
    await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: 100, y: 50 });
    await delay(100);
    await evaluate('window.scrollTo(0,document.documentElement.scrollHeight); true');
    await waitFor(`document.querySelector('.editmdview-outline a[aria-current]')?.hash === '#heading-239'`,
        'document bottom activates last heading');
    assert.equal(await evaluate(`document.querySelector('.editmdview-outline a[aria-current]').hash`), '#heading-239', 'document bottom activates last heading');
    assert.equal(await evaluate(`(() => {const n=document.querySelector('.editmdview-outline');return n.scrollTop+n.clientHeight>=n.scrollHeight-1})()`), true, 'outline follows the current heading');
    await send('Emulation.setDeviceMetricsOverride', { width: 500, height: 180, deviceScaleFactor: 1, mobile: false });
    await delay(100);
    assert.equal(await evaluate(`(() => {const r=document.querySelector('.editmdview-outline').getBoundingClientRect();return r.top>=0&&r.bottom<=innerHeight})()`), true, 'rail stays within short viewport');
    await evaluate('EditMdViewPreview.install({}); true');
    assert.equal(await evaluate(`document.querySelectorAll('.editmdview-outline-tooltip').length`), 1, 'reinstall must not duplicate title cards');
    await send('Page.setDocumentContent', { frameId: frameTree.frame.id, html: fixture(8) });
    await evaluate('EditMdViewPreview.install({}); true');
    assert.equal(await evaluate(`document.querySelector('.editmdview-outline').classList.contains('is-overflowing')`), false, 'short outlines keep evenly spaced marks');
    console.log('PASS: 240 headings, wheel isolation, last-mark hit test and click, tooltip, keyboard, auto-follow, resize, reinstall, short outline');
})().catch(error => { console.error(error); process.exitCode = 1; }).finally(async () => {
    if (socket && socket.readyState === WebSocket.OPEN) {
        await send('Browser.close', {}, undefined).catch(() => {});
        socket.close();
    }
    child.kill();
});
