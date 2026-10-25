'use strict';
/* =====================================================================
   02-chat —— 消息流、流式渲染、等待指示
   ===================================================================== */

const LIVE = {
  node: null, body: null, raw: '', reasoning: '', rnode: null,
  started: false, streaming: false, pendingAct: '',
};
let pinned = true; // 视口是否贴着底部

function nearBottom() {
  const s = $('#stream');
  return s.scrollHeight - s.scrollTop - s.clientHeight < 80;
}
function stick(force) {
  if (!force && !pinned) return;
  const s = $('#stream');
  s.scrollTop = s.scrollHeight;
}

$('#stream').addEventListener('scroll', () => {
  pinned = nearBottom();
});

/* ---------------- 历史 ---------------- */
function avatarNode() {
  const a = el('div', 'ava');
  const p = S.persona || {};
  if (p.avatar_url) a.innerHTML = `<img src="${esc(p.avatar_url)}" alt="">`;
  else a.textContent = (p.name || '·').slice(0, 1);
  return a;
}

function msgAi(text, opts = {}) {
  const wrap = el('div', 'msg ai');
  wrap.appendChild(avatarNode());
  const body = el('div', 'body');
  const meta = el('div', 'meta');
  meta.appendChild(el('span', 'who', (S.persona && S.persona.name) || ''));
  meta.appendChild(el('span', 'time', opts.time || fmtClock()));
  body.appendChild(meta);
  const md = el('div', 'text md');
  md.innerHTML = mdToHtml(text || '');
  body.appendChild(md);
  const actions = el('div', 'actions');
  const replay = el('button', 'act-btn');
  replay.innerHTML = icon('play') + '<span>重播</span>';
  replay.onclick = () => speakText(text);
  const copy = el('button', 'act-btn');
  copy.innerHTML = '<span>复制</span>';
  copy.onclick = () => {
    navigator.clipboard?.writeText(text).then(() => toast('已复制', 'ok'), () => toast('复制失败', 'err'));
  };
  actions.append(replay, copy);
  body.appendChild(actions);
  wrap.appendChild(body);
  return wrap;
}

function msgUser(text) {
  const wrap = el('div', 'msg user');
  const body = el('div', 'body');
  const bubble = el('div', 'bubble', text);
  body.appendChild(bubble);
  wrap.appendChild(body);
  return wrap;
}

function paintHistory() {
  const stream = $('#stream');
  stream.innerHTML = '';
  LIVE.node = null; LIVE.body = null; LIVE.raw = '';

  if (!S.history.length) {
    const w = el('div', 'welcome');
    if (!S.personas.length) {
      // 默认空角色：欢迎页就是「建一个」
      w.appendChild(el('div', 'hi', 'a companion, not an assistant'));
      w.appendChild(el('div', 'say', '还没有角色。建一个，她才能住进来。'));
      w.appendChild(el('div', 'btn-row',
        actionBtn('创建第一个角色', () => openPersonaEditor(true), 'btn primary')));
    } else {
      w.appendChild(el('div', 'hi', 'a companion, not an assistant'));
      w.appendChild(el('div', 'say', (S.persona && S.persona.greeting) || '……在的。'));
      w.appendChild(el('div', 'hint', S.tts.enabled
        ? '开口说话，或者按麦克风；她会用声音回你。'
        : '开口说话；在设置里接上语音服务，她就能读出回复。'));
    }
    stream.appendChild(w);
    pinned = true;
    return;
  }
  let lastDay = '';
  for (const m of S.history) {
    if (m.kind === 'reasoning') continue; // 历史里的思考默认折叠在后面
    if (m.role === 'user') stream.appendChild(msgUser(m.text));
    else stream.appendChild(msgAi(m.text));
  }
  pinned = true;
  stick(true);
}

/* ---------------- 流式 ---------------- */
function ensureLiveNode() {
  if (LIVE.node) return;
  const stream = $('#stream');
  if ($('.welcome', stream)) stream.innerHTML = '';
  LIVE.node = el('div', 'msg ai');
  LIVE.node.appendChild(avatarNode());
  const body = el('div', 'body');
  const meta = el('div', 'meta');
  meta.appendChild(el('span', 'who', (S.persona && S.persona.name) || ''));
  meta.appendChild(el('span', 'time', fmtClock()));
  body.appendChild(meta);
  LIVE.rnode = null;
  LIVE.body = el('div', 'text md');
  body.appendChild(LIVE.body);
  LIVE.node.appendChild(body);
  stream.appendChild(LIVE.node);
  LIVE.body.classList.add('streaming');
  LIVE.streaming = true;
  LIVE.raw = '';
  LIVE.reasoning = '';
  LIVE.started = false;
  pinned = true;
  stick(true);
}

function renderLive() {
  if (!LIVE.body) return;
  LIVE.body.innerHTML = mdToHtml(LIVE.raw) || '<span style="opacity:.4">…</span>';
  stick();
}

function appendReason(delta) {
  if (!LIVE.body) return;
  LIVE.reasoning += delta;
  if (!LIVE.rnode) {
    const det = el('details', 'reason');
    det.open = false;
    const sum = el('summary', null, '思考');
    const body = el('div', 'rbody');
    det.append(sum, body);
    LIVE.node.querySelector('.body').insertBefore(det, LIVE.body);
    LIVE.rnode = body;
  }
  LIVE.rnode.textContent = LIVE.reasoning;
}

function finishLive() {
  if (!LIVE.node) return;
  if (LIVE.body) {
    LIVE.body.classList.remove('streaming');
    LIVE.body.innerHTML = mdToHtml(LIVE.raw);
  }
  if (LIVE.raw.trim()) {
    // 补上重播/复制
    const body = LIVE.node.querySelector('.body');
    const actions = el('div', 'actions');
    const replay = el('button', 'act-btn');
    replay.innerHTML = icon('play') + '<span>重播</span>';
    replay.onclick = () => speakText(LIVE.raw);
    const copy = el('button', 'act-btn');
    copy.innerHTML = '<span>复制</span>';
    copy.onclick = () => navigator.clipboard?.writeText(LIVE.raw).then(() => toast('已复制', 'ok'));
    actions.append(replay, copy);
    body.appendChild(actions);
  } else {
    LIVE.node.remove();
  }
  LIVE.node = null; LIVE.body = null; LIVE.rnode = null;
  LIVE.streaming = false;
  stick();
}

/* ---------------- 等待指示 ---------------- */
let waitTimer = null;
function showTyping(label) {
  const t = $('#typing');
  t.classList.remove('hidden');
  $('#typing-label').textContent = label || '正在想';
  if (waitTimer) clearInterval(waitTimer);
  const t0 = Date.now();
  waitTimer = setInterval(() => {
    const s = Math.floor((Date.now() - t0) / 1000);
    $('#typing-label').textContent = `${label || '正在想'} · ${s}s`;
  }, 1000);
}
function hideTyping() {
  $('#typing').classList.add('hidden');
  if (waitTimer) { clearInterval(waitTimer); waitTimer = null; }
}

/* ---------------- 事件 ---------------- */
onEvent((m) => {
  switch (m.t) {
    case 'turn_begin':
      S.busy = true;
      paintHead();
      paintComposer(); // 发送键变成「停止」，不然回合里没地方按
      if (m.idle) toast('她主动开口了');
      showTyping('正在想');
      break;
    case 'answer_begin':
      hideTyping();
      ensureLiveNode();
      break;
    case 'answer':
      if (!LIVE.node) ensureLiveNode();
      hideTyping();
      LIVE.raw += m.d || '';
      renderLive();
      // 正文末尾一小段交给触发器匹配关键词（「晚安」「抱抱」之类）
      TRIG.fire('reply', { text: LIVE.raw.slice(-160) });
      break;
    case 'reason':
      if (!LIVE.node) ensureLiveNode();
      appendReason(m.d || '');
      break;
    case 'progress':
      if (!LIVE.node) showTyping('正在想');
      break;
    case 'status':
      if (m.d && /compact|summar/i.test(m.d)) showTyping('整理记忆');
      break;
    case 'error':
      hideTyping();
      if (LIVE.node && !LIVE.raw) { finishLive(); }
      toast(m.d || '出错了', 'err', 6000);
      break;
    case 'turn_end': {
      hideTyping();
      S.busy = false;
      finishLive();
      paintHead();
      paintComposer();
      break;
    }
    case 'memory':
      refreshState().catch(() => {});
      toast('记住了一件事', 'ok', 1800);
      break;
    case 'asr':
      if (m.text) {
        $('#input').value = m.text;
        autoGrow();
        if (m.auto_send) sendCurrent();
        else $('#input').focus();
      }
      break;
    case 'voice_state':
      S.recording = !!m.recording;
      paintComposer();
      break;
    case 'emo':
      setEmotion(m.name || 'neutral');
      break;
    case 'act':
      showAct(m.name || '');
      break;
    case 'motion':
      l2dPlayMotion(m.group || m.name || '', m.index);
      break;
    case 'expr':
      l2dSetExpression(m.name || '');
      break;
    case 'quit':
      window.close();
      break;
  }
});

/* ---------------- 发送 ---------------- */
async function sendCurrent() {
  const input = $('#input');
  const text = input.value.trim();
  if (!text) return;
  if (!S.active) { toast('先创建一个角色', 'err'); openPersonaEditor(true); return; }
  if (S.busy) { toast('她还在说，先等一下'); return; }
  input.value = '';
  autoGrow();
  stopAudio();
  if ($('.welcome', $('#stream'))) $('#stream').innerHTML = '';
  $('#stream').appendChild(msgUser(text));
  S.history.push({ role: 'user', kind: 'user', text });
  LIVE.node = null;
  LIVE.body = null;
  LIVE.raw = '';
  pinned = true;
  stick(true);
  TRIG.fire('user', { text });
  try {
    await cmd('send', { text });
  } catch (e) {
    toast(e.message, 'err');
  }
}

async function stopTurn() {
  try { await cmd('stop'); } catch { }
  stopAudio();
  hideTyping();
}

function autoGrow() {
  const t = $('#input');
  t.style.height = 'auto';
  t.style.height = Math.min(t.scrollHeight, 168) + 'px';
}

function paintComposer() {
  const btn = $('#btn-send');
  btn.classList.toggle('stop', S.busy);
  btn.title = S.busy ? '停止生成（Esc 也可以）' : '发送';
  const input = $('#input');
  const none = !S.active;
  input.disabled = none;
  input.placeholder = none
    ? '先创建一个角色…'
    : (S.busy ? '正在回应… 点右边停止' : '说点什么…（Enter 发送，Shift+Enter 换行）');
  $('#btn-mic').disabled = !S.asr.enabled || none;
  $('#btn-mic').title = S.asr.enabled ? '点击开始 / 结束录音' : '还没有配置语音识别服务';
  $('#btn-mic').classList.toggle('rec', S.recording);
  const hint = $('#composer-hint');
  if (S.recording) {
    hint.textContent = '正在听…再点一次结束，随后自动转写';
    hint.classList.add('rec');
  } else if (!S.asr.enabled && !S.tts.enabled) {
    hint.textContent = '语音服务未配置：设置 → 服务，可以接入任何 OpenAI 兼容的 TTS / ASR';
    hint.classList.remove('rec');
  } else {
    hint.textContent = S.tts.enabled ? 'Enter 发送 · Shift+Enter 换行 · 回复会自动朗读' : 'Enter 发送 · Shift+Enter 换行';
    hint.classList.remove('rec');
  }
}
