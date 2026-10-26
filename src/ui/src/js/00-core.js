'use strict';
/* =====================================================================
   00-core —— 常量、工具、传输层、状态仓库
   页面是一个纯粹的 HTTP/SSE 客户端：桌面窗口和浏览器走同一条通道。
   ===================================================================== */

const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));

const MOCK = location.protocol === 'file:' || /[?&]mock=1\b/.test(location.search);

/* ---------------- 状态仓库 ---------------- */
const S = {
  connected: false,
  personas: [],
  active: '',
  persona: {},
  history: [],
  busy: false,
  emotion: 'neutral',
  memory: [],
  im: [],
  tts: { enabled: false, kind: 'off' },
  asr: { enabled: false, kind: 'off' },
  voice_reply: true,
  asr_auto_send: true,
  providers: [],
  model: '',
  thinking: 0,
  recording: false,
  sessions: [],
  lastAct: '',
  // 表情包 / 附件 / 工具偏好（服务端 state 的新字段，见 team-contract 2.5）
  stickers: [],
  autoallow: true,
  compact_auto: true,
  tools: true,
};

/* 附件托盘的状态。放在核心层：paintComposer 只读它，真正的读取 / 上传在 12-attach。 */
const ATTACH = {
  items: [],     // { id, name, mime, size, kind, url, dataUrl, file }
  seq: 0,
  max: 8,        // 服务端 attach.max_files，state 回来会覆盖
  maxBytes: 48 * 1024 * 1024,
};

const EMO_ZH = {
  neutral: '平静', happy: '开心', sad: '难过', angry: '生气',
  shy: '害羞', surprised: '惊讶', thinking: '思索', affection: '亲昵',
};

/* ---------------- 小工具 ---------------- */
function esc(s) {
  return String(s == null ? '' : s).replace(/[&<>"']/g, c =>
    ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}
function el(tag, cls, child) {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (child instanceof Node) n.appendChild(child);
  else if (child != null) n.textContent = child;
  return n;
}
function fmtClock(ts) {
  const d = new Date((ts || Date.now() / 1000) * 1000);
  return d.toTimeString().slice(0, 5);
}
function fmtDay(ts) {
  if (!ts) return '';
  const d = new Date(ts * 1000);
  return `${d.getMonth() + 1}月${d.getDate()}日`;
}
function icon(name) {
  const p = {
    trash: '<path d="M3.5 6.5h17"/><path d="M8.8 6.5V5a1.8 1.8 0 0 1 1.8-1.8h2.8A1.8 1.8 0 0 1 15.2 5v1.5"/><path d="M6.3 6.5l.9 12.3a2 2 0 0 0 2 1.9h5.6a2 2 0 0 0 2-1.9l.9-12.3"/>',
    play: '<path d="m8 5 11 7-11 7V5Z"/>',
    stop: '<rect x="6" y="6" width="12" height="12" rx="2"/>',
    plus: '<path d="M12 5v14M5 12h14"/>',
    x: '<path d="M18 6 6 18M6 6l12 12"/>',
    refresh: '<path d="M21 12a8 8 0 1 1-2.3-5.7M21 4v4h-4"/>',
    check: '<path d="m20 6-11 11-5-5"/>',
    paperclip: '<path d="m21.4 11.6-8.9 8.9a6 6 0 0 1-8.5-8.5l8.9-8.9a4 4 0 0 1 5.7 5.7l-8.9 8.9a2 2 0 0 1-2.8-2.8l8.2-8.2"/>',
    image: '<rect x="3" y="4" width="18" height="16" rx="2"/><circle cx="8.5" cy="9.5" r="1.5"/><path d="m21 15-5-5L5 20"/>',
    video: '<rect x="3" y="5" width="13" height="14" rx="2"/><path d="m16 10 5-3v10l-5-3"/>',
    audio: '<path d="M9 18V6l10-2v12"/><circle cx="6.5" cy="18" r="2.5"/><circle cx="16.5" cy="16" r="2.5"/>',
    file: '<path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8Z"/><path d="M14 3v5h5"/>',
    download: '<path d="M12 4v11M7.5 10.5 12 15l4.5-4.5M5 19h14"/>',
  }[name] || '';
  return `<svg viewBox="0 0 24 24">${p}</svg>`;
}

function toast(msg, kind = '', ms = 2600) {
  const t = el('div', 'toast ' + kind, msg);
  $('#toasts').appendChild(t);
  setTimeout(() => {
    t.style.transition = 'opacity .25s, transform .25s';
    t.style.opacity = '0';
    t.style.transform = 'translateY(6px)';
    setTimeout(() => t.remove(), 260);
  }, ms);
}

/* 角色主题色 */
function applyAccent(color) {
  if (!color) return;
  document.documentElement.style.setProperty('--accent', color);
  // 主色的深一档，供阴影用
  const c = color.replace('#', '');
  if (c.length === 6) {
    const n = parseInt(c, 16);
    const r = Math.round(((n >> 16) & 255) * .62);
    const g = Math.round(((n >> 8) & 255) * .62);
    const b = Math.round((n & 255) * .62);
    document.documentElement.style.setProperty('--accent-deep',
      `rgb(${r},${g},${b})`);
  }
}

/* ---------------- 传输层 ---------------- */
async function cmd(name, args = {}) {
  if (MOCK) return mockCmd(name, args);
  const r = await fetch('/api/cmd/' + encodeURIComponent(name), {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(args || {}),
  });
  if (!r.ok) throw new Error('HTTP ' + r.status);
  const j = await r.json();
  if (!j.ok) throw new Error(j.error || '命令失败');
  return j;
}

let es = null;
function connect() {
  if (MOCK) { setTimeout(() => { S.connected = true; paintAll(); }, 200); return; }
  if (es) es.close();
  es = new EventSource('/api/events');
  es.onopen = () => { S.connected = true; paintHead(); };
  es.onerror = () => { S.connected = false; paintHead(); };
  es.onmessage = (ev) => {
    let m;
    try { m = JSON.parse(ev.data); } catch { return; }
    handleEvent(m);
  };
}

/* 命令 + 状态刷新 */
async function refreshState() {
  const st = await cmd('state');
  applyState(st, true);
}

function applyState(st, rebuild) {
  if (!st) return;
  S.connected = true;
  if (Array.isArray(st.personas)) S.personas = st.personas;
  if (st.active !== undefined) S.active = st.active;
  if (st.persona) S.persona = st.persona;
  if (st.emotion) S.emotion = st.emotion;
  if (Array.isArray(st.memory)) S.memory = st.memory;
  if (Array.isArray(st.history)) S.history = st.history;
  if (Array.isArray(st.providers)) S.providers = st.providers;
  if (st.busy !== undefined) S.busy = !!st.busy;
  if (Array.isArray(st.im)) S.im = st.im;
  if (st.tts) S.tts = Object.assign({}, S.tts, st.tts, { enabled: !!(st.voice && st.voice.tts) });
  if (st.asr) S.asr = Object.assign({}, S.asr, st.asr, { enabled: !!(st.voice && st.voice.asr) });
  if (st.voice && st.voice.recording !== undefined) S.recording = !!st.voice.recording;
  if (st.voice_reply !== undefined) S.voice_reply = !!st.voice_reply;
  if (st.asr_auto_send !== undefined) S.asr_auto_send = !!st.asr_auto_send;
  if (st.model !== undefined) S.model = st.model;
  if (st.thinking !== undefined) S.thinking = st.thinking;
  if (Array.isArray(st.stickers)) S.stickers = st.stickers;
  if (st.attach) {
    if (st.attach.max_files > 0) ATTACH.max = st.attach.max_files;
    if (st.attach.max_bytes > 0) ATTACH.maxBytes = st.attach.max_bytes;
  }
  if (st.autoallow !== undefined) S.autoallow = !!st.autoallow;
  if (st.compact_auto !== undefined) S.compact_auto = !!st.compact_auto;
  if (st.tools !== undefined) S.tools = !!st.tools;
  applyAccent(S.persona && S.persona.accent);
  if (rebuild) paintAll();
}

/* ---------------- 事件 ---------------- */
/* 事件由 02-chat / 03-voice / 04-panels 分别实现；这里只做分发。 */
const EVENT_HANDLERS = [];
function onEvent(fn) { EVENT_HANDLERS.push(fn); }
function handleEvent(m) {
  if (m.t === 'hello') applyState(m, true); // 继续分发：各模块可能也要看一眼
  for (const fn of EVENT_HANDLERS) {
    try { fn(m); } catch (e) { console.warn('event handler', m.t, e); }
  }
}

/* ---------------- 通用 UI ---------------- */
function paintHead() {
  // 名字在 #stage-name（paintStage 负责），这里只管连接状态和朗读开关
  const st = $('#head-status');
  st.className = 'head-status' + (S.busy ? ' busy' : (S.connected ? '' : ' off'));
  st.textContent = !S.connected ? '未连接' : (S.busy ? '正在回应' : '在线');
  $('#btn-voice').classList.toggle('on', S.voice_reply);
  $('#btn-voice').title = S.voice_reply ? '朗读回复：开' : '朗读回复：关';
}

function paintAll() {
  paintHead();
  paintPersonas();
  paintStage();
  // 回合进行中别重建历史：服务端这时返回的是空历史，重建会把正在流式渲染的
  // 那一段抹掉（正在说的字会跳回开头）。回合结束后由 finishLive 收尾。
  if (typeof paintHistory === 'function' && !S.busy) paintHistory();
  if (typeof paintComposer === 'function') paintComposer();
  if (typeof paintDrawer === 'function' && !$('#drawer').classList.contains('hidden')) paintDrawer();
}

function paintPersonas() {
  const box = $('#persona-list');
  box.innerHTML = '';
  for (const p of S.personas) {
    const b = el('button', 'persona-dot' + (p.id === S.active ? ' on' : ''));
    b.title = p.name;
    b.dataset.id = p.id;
    if (p.avatar_url) b.innerHTML = `<img src="${esc(p.avatar_url)}" alt="">`;
    else b.textContent = (p.name || '?').slice(0, 1);
    if (p.id === S.active && S.busy) b.appendChild(el('span', 'busy-dot'));
    b.onclick = async () => {
      if (p.id === S.active) return;
      try {
        await cmd('persona.select', { id: p.id });
        stopAudio();
        await refreshState();
      } catch (e) { toast(e.message, 'err'); }
    };
    box.appendChild(b);
  }
}
