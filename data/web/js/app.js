'use strict';
const $ = id => document.getElementById(id);
const names = { Home:'时间与额度', Antigravity:'反重力额度', Info:'设备状态', Gallery:'潜在空间画廊', none:'尚未显示' };
let lastSeed = 0, uploading = false, saving = false;
function notice(message, error = false) { $('notice').textContent = message; $('notice').classList.toggle('error', error); $('notice').hidden = false; }
async function api(path, body) {
    let response;
    try { response = await fetch(path, {cache:'no-store', ...(body !== undefined ? {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body)} : {})}); }
    catch { throw new Error('设备连接失败，请稍后重试'); }
    let result;
    try { result = await response.json(); } catch { throw new Error('设备返回格式异常'); }
    if (!response.ok) throw new Error(result.error || '操作未完成，请重试');
    return result;
}
function fillSettings(settings) {
    for (const form of [$('network-form'), $('quota-form'), $('display-form')]) {
        for (const input of form.elements) {
            if (!input.name || settings[input.name] === undefined) continue;
            if (input.type === 'checkbox') input.checked = settings[input.name];
            else input.value = settings[input.name];
        }
    }
}
function rgb565(v) { return `rgb(${Math.round(((v>>11)&31)*255/31)},${Math.round(((v>>5)&63)*255/63)},${Math.round((v&31)*255/31)})`; }
async function preview(seed) {
    if (!seed || seed === lastSeed) return;
    const frame = await api('/api/gallery');
    const ctx = $('gallery-preview').getContext('2d');
    ctx.fillStyle = rgb565(frame.background); ctx.fillRect(0,0,240,240);
    for (const [pos,size,color,vertical] of frame.stripes) {
        ctx.fillStyle = rgb565(color);
        ctx.fillRect(vertical ? pos : 0, vertical ? 0 : pos, vertical ? size + frame.edge : 240, vertical ? 240 : size + frame.edge);
    }
    lastSeed = frame.seed;
    $('preview-hint').textContent = '设备当前画廊画面';
}
async function poll() {
    if (uploading) return;
    try {
        const s = await api('/api/state');
        $('connection').textContent = '设备已连接';
        $('wifi-state').textContent = {connected:'已连接',ap:'配网热点',disconnected:'连接中断'}[s.wifi] || '正在连接';
        for (const id of ['ssid','ip']) $(id).textContent = s[id] || '—';
        $('rssi').textContent = s.wifi === 'connected' ? `${s.rssi} 分贝毫瓦` : '—';
        const minutes = Math.floor(s.uptime_s/60);
        $('uptime').textContent = `${Math.floor(minutes/60)} 小时 ${minutes%60} 分钟`;
        $('heap').textContent = `${Math.round(s.heap/1024)} 千字节`;
        $('page-name').textContent = names[s.channel] || '—';
        $('firmware').textContent = s.fw || '—';
        await preview(s.gallery_seed);
    } catch { $('connection').textContent = '设备暂时离线'; }
}
for (const button of document.querySelectorAll('[data-tab]')) button.addEventListener('click', () => {
    for (const tab of document.querySelectorAll('.tab')) tab.hidden = tab.id !== button.dataset.tab;
    for (const nav of document.querySelectorAll('[data-tab]')) nav.classList.toggle('active', nav === button);
});
for (const toggle of document.querySelectorAll('[data-reveal]')) toggle.addEventListener('change', () => {
    for (const input of document.querySelectorAll(`#${toggle.dataset.reveal} input[autocomplete="new-password"], #${toggle.dataset.reveal} input[name$="Token"]`)) input.type = toggle.checked ? 'text' : 'password';
});
for (const form of [$('network-form'), $('quota-form'), $('display-form')]) form.addEventListener('submit', async event => {
    event.preventDefault(); if (saving || uploading) return;
    if (!form.reportValidity()) return;
    const patch = {};
    for (const input of form.elements) {
        if (!input.name) continue;
        patch[input.name] = input.type === 'checkbox' ? input.checked : input.type === 'number' || input.tagName === 'SELECT' ? Number(input.value) : input.value;
    }
    if (form.id === 'display-form') {
        const pages = ['showHome','showAntigravity','showInfo','showGallery'];
        if (!patch.autoRotate) patch[pages[patch.selectedPage]] = true;
        if (!pages.some(key => patch[key])) { notice('请至少启用一个页面', true); return; }
    }
    // Only the wireless-network save button requests a restart.
    const restart = event.submitter?.dataset.restart === 'true';
    patch._restart = restart;
    const buttons = [...form.querySelectorAll('button')]; buttons.forEach(b => b.disabled = true); saving = true;
    try {
        await api('/api/settings', patch);
        notice(restart ? '设置已保存，设备正在重启。连接新网络后重新打开设备地址。' : '设置已保存，已应用到屏幕。');
        if (!restart) { fillSettings(await api('/api/settings')); await poll(); }
    } catch (error) { notice(error.message === 'Failed to fetch' ? '设备连接失败，请稍后重试' : error.message, true); }
    finally { saving = false; buttons.forEach(b => b.disabled = false); }
});
$('refresh').addEventListener('click', async () => {
    $('refresh').disabled = true;
    try { await api('/api/refresh', {}); notice('已开始刷新额度，请稍后查看屏幕。'); }
    catch { notice('设备连接失败，未能刷新额度', true); }
    finally { $('refresh').disabled = false; }
});
$('reboot').addEventListener('click', async () => {
    try { await api('/api/reboot', {}); notice('设备正在重启，请稍后重新连接。'); }
    catch { notice('设备连接失败，未能重启', true); }
});
$('import-form').addEventListener('submit', async event => {
    event.preventDefault(); if (uploading || saving) return;
    const file = $('backup-file').files[0]; if (!file) return;
    try {
        const backup = JSON.parse(await file.text());
        if (typeof backup.wifi_ssid !== 'string') throw new Error('请使用从设备导出的设置备份');
        await api('/api/import', backup); notice('设置已恢复，设备正在重启。');
    } catch { notice('设置恢复失败，请检查备份文件和设备连接', true); }
});
for (const form of document.querySelectorAll('.upload-form')) form.addEventListener('submit', event => {
    event.preventDefault(); if (uploading || saving) return;
    const input = form.querySelector('input'), file = input.files[0];
    if (!file || !file.name.endsWith('.bin')) { notice('请选择正确的刷写文件', true); return; }
    const body = new FormData(); body.append(input.name, file);
    const xhr = new XMLHttpRequest(); xhr.open('POST','/update'); xhr.timeout = 180000;
    uploading = true; $('upload-progress').hidden = false; $('upload-progress').value = 0;
    for (const b of document.querySelectorAll('button[type=submit]')) b.disabled = true;
    $('upload-status').textContent = '正在上传，请保持设备供电与网络连接。';
    xhr.upload.onprogress = e => { if (e.lengthComputable) $('upload-progress').value = Math.floor(e.loaded*100/e.total); };
    xhr.onload = () => {
        const success = xhr.status === 200 && /Update Success|成功/.test(xhr.responseText);
        notice(success ? '刷写成功，设备正在重启。文件系统更新后请重新配网或恢复设置。' : '刷写失败，请确认文件与设备型号匹配', !success);
        finish();
    };
    xhr.onerror = xhr.ontimeout = () => { notice('上传连接中断，请检查设备状态后再重试', true); finish(); };
    function finish() { uploading = false; $('upload-status').textContent = ''; for (const b of document.querySelectorAll('button[type=submit]')) b.disabled = false; }
    xhr.send(body);
});
async function boot() {
    try { fillSettings(await api('/api/settings')); }
    catch { notice('无法读取设置，请确认设备连接后刷新页面', true); }
    await poll(); setInterval(poll,5000);
    if (location.pathname === '/update') document.querySelector('[data-tab="maintenance"]').click();
}
boot();
