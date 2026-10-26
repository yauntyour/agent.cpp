'use strict';
/* =====================================================================
   02-chat —— 消息流、流式渲染、等待指示
   ===================================================================== */

const LIVE = {
  node: null, body: null, raw: '', reasoning: '', rnode: null, rbody: null, rsum: null,
  started: false, streaming: false, pendingAct: '', reasonTouched: false,
  toolPending: [],
};
let pinned = true;  // 视口是否贴着底部
let autoScroll = 0; // >0：这一次滚动是我们自己做的，别当成「用户翻走了」

function nearBottom() {
  const s = $('#stream');
  return s.scrollHeight - s.scrollTop - s.clientHeight < 80;
}
/* 贴底必须是瞬时滚动：CSS 的 scroll-behavior:smooth 会让每次赋值都重启动画，
   token 一个接一个来的时候永远追不上，滚不到底就把 pinned 弄丢，之后就再也不跟了。 */
function stick(force) {
  if (!force && !pinned) return;
  const s = $('#stream');
  autoScroll += 1;
  s.scrollTop = s.scrollHeight;
  requestAnimationFrame(() => { autoScroll = Math.max(0, autoScroll - 1); });
}

$('#stream').addEventListener('scroll', () => {
  if (autoScroll > 0) { pinned = true; return; } // 自己滚的不算用户操作
  pinned = nearBottom();
});

/* ---------------- 历史 ---------------- */
/* 字幕轨里没有头像和时钟：说话的人用名字标出来，时间交给会话记录 */
function subWho() {
  return el('div', 'who', (S.persona && S.persona.name) || '');
}

/* 只有思考、没有正文的回合：别只留一个空壳，把「没说出来」讲清楚，并给一次重说 */
function reasonOnlyNote(body, reasoning) {
  body.appendChild(el('div', 'noanswer', '这次她想了很久，最后没有说出口。'));
  // pin：这条消息没有正文，唯一能做的两件事别藏在 hover 后面
  const actions = el('div', 'actions pin');
  const retry = el('button', 'act-btn');
  retry.innerHTML = '<span>重说</span>';
  retry.onclick = () => stageSend('（再说一次）');
  const copy = el('button', 'act-btn');
  copy.innerHTML = '<span>复制思考</span>';
  copy.onclick = () => {
    const text = reasoning || '';
    if (!text) return;
    navigator.clipboard?.writeText(text).then(() => toast('已复制', 'ok'), () => toast('复制失败', 'err'));
  };
  actions.append(retry, copy);
  body.appendChild(actions);
}

/* 思考块的标题：默认收起来，但把字数摆出来——收着也能看出它在长 */
function reasonLabel(chars, streaming) {
  if (!chars) return streaming ? '思考中' : '思考';
  return `${streaming ? '思考中' : '思考'} · ${chars} 字`;
}

function msgAi(text, opts = {}) {
  const wrap = el('div', 'msg ai');
  wrap.appendChild(subWho());
  const body = el('div', 'body');
  const said = String(text || '').trim();
  const atts = Array.isArray(opts.attachments) ? opts.attachments : [];
  // 历史里的思考：默认收纳，想看再点开（之前是直接丢掉）
  if (opts.reasoning) {
    const det = el('details', 'reason');
    const sum = el('summary', null, reasonLabel(opts.reasoning.length, false));
    const rb = el('div', 'rbody', opts.reasoning);
    det.append(sum, rb);
    body.appendChild(det);
  }
  if (said) {
    const md = el('div', 'text md');
    md.innerHTML = mdToHtml(text || '');
    body.appendChild(md);
  }
  // 贴纸 / 图片 / 视频 / 文件：附件渲染器在 12-attach.js
  const gallery = typeof attachGallery === 'function' ? attachGallery(atts) : null;
  if (gallery) body.appendChild(gallery);
  if (!said && opts.reasoning && !gallery) {
    reasonOnlyNote(body, opts.reasoning);
    wrap.appendChild(body);
    return wrap;
  }
  if (said) {
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
  }
  wrap.appendChild(body);
  return wrap;
}

function msgUser(text, attachments) {
  const wrap = el('div', 'msg user');
  const body = el('div', 'body');
  const bubble = el('div', 'bubble');
  const gallery = typeof attachGallery === 'function' ? attachGallery(attachments) : null;
  if (gallery) bubble.appendChild(gallery);
  const said = String(text == null ? '' : text);
  if (said.trim()) bubble.appendChild(el('div', 'bubble-text', said));
  if (!bubble.childNodes.length) bubble.textContent = said;
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
  let lastReason = '';
  const flushReason = () => {
    // 只有思考、没有正文的回合（模型没说出话）：也渲染成一条消息，别把 CoT 弄丢
    if (lastReason) { stream.appendChild(msgAi('', { reasoning: lastReason })); lastReason = ''; }
  };
  for (const m of S.history) {
    if (m.kind === 'reasoning') { flushReason(); lastReason = m.text || ''; continue; }
    if (m.role === 'user') { flushReason(); stream.appendChild(msgUser(m.text, m.attachments)); }
    else { stream.appendChild(msgAi(m.text, { reasoning: lastReason, attachments: m.attachments })); lastReason = ''; }
  }
  flushReason();
  pinned = true;
  stick(true);
}

/* ---------------- 工具调用记录 ----------------
   两条通道，同一份数据：
   - 回合内联：每调一次就在正文边上落一条窄条（点开看参数和输出）；
   - 记录面板：S.toolLog 累积本次运行的所有调用，按会话存 localStorage，
     刷新之后还翻得到（历史正文里没有工具痕迹，所以只能靠这份日志）。 */
const TOOL_LOG_KEY = 'agent.tool.log';
const TOOL_LOG_MAX = 300;

function toolLogLoad() {
  try {
    const raw = JSON.parse(localStorage.getItem(TOOL_LOG_KEY) || '[]');
    if (Array.isArray(raw)) S.toolLog = raw.filter(x => x && x.name);
  } catch { }
}
function toolLogSave() {
  try { localStorage.setItem(TOOL_LOG_KEY, JSON.stringify(S.toolLog)); } catch { }
}
function toolLogPush(rec) {
  S.toolLog.push(rec);
  if (S.toolLog.length > TOOL_LOG_MAX) S.toolLog.splice(0, S.toolLog.length - TOOL_LOG_MAX);
  toolLogSave();
}

/* 参数里挑最有信息量的那一个当标题，没有就摊平前几个键 */
function toolArgBrief(args) {
  let a = args;
  if (typeof a === 'string') {
    try { a = JSON.parse(a); } catch { return String(a).slice(0, 90); }
  }
  if (!a || typeof a !== 'object') return '';
  for (const k of ['path', 'cmd', 'command', 'pattern', 'query', 'url', 'text', 'name', 'mode']) {
    if (typeof a[k] === 'string' && a[k]) return a[k].slice(0, 96);
  }
  return Object.keys(a).slice(0, 3).map(k => k + '=' + String(a[k]).slice(0, 28)).join(' ');
}
function toolStatusText(rec) {
  if (rec.status === 'running') return '执行中';
  if (rec.status === 'ok') return rec.sec ? rec.sec.toFixed(2) + 's' : '完成';
  return ({ failed: '失败', blocked: '被拒', unknown: '未知', exception: '异常' }[rec.status]) || '失败';
}

/* 一条工具调用：details 展开看参数 + 输出 */
function toolCard(rec, live) {
  const det = el('details', 'tool-call');
  const sum = el('summary');
  const status = el('span', 'tc-status ' + (rec.status === 'running' ? 'run' : (rec.status === 'ok' ? 'ok' : 'fail')));
  sum.appendChild(el('span', 'tc-name', rec.name || 'tool'));
  sum.appendChild(el('span', 'tc-arg', rec.brief || ''));
  sum.appendChild(el('span', 'tc-meta', ''));
  sum.appendChild(status);
  const body = el('div', 'tc-body');
  det.append(sum, body);
  const view = { det, sum, body, status, meta: sum.querySelector('.tc-meta'), rec };
  toolCardPaint(view);
  if (live) det.open = false;
  return view;
}
function toolCardPaint(view) {
  const rec = view.rec;
  view.status.className = 'tc-status ' + (rec.status === 'running' ? 'run' : (rec.status === 'ok' ? 'ok' : 'fail'));
  view.status.textContent = toolStatusText(rec);
  view.meta.textContent = rec.chars ? rec.chars + ' 字' : '';
  const bits = [];
  if (rec.args) bits.push('参数：' + rec.args);
  if (rec.output) bits.push('输出：' + rec.output);
  view.body.textContent = bits.join('\n\n') || '（没有输出）';
}
/* 记录面板开着时实时跟着更新，但别把滚动位置弄丢 */
function toolPanelRedraw() {
  if (typeof paintDrawer !== 'function' || DRAWER.current !== 'tools') return;
  const body = $('#drawer-body');
  const top = body ? body.scrollTop : 0;
  paintDrawer();
  if (body) body.scrollTop = top;
}

/* 工具开始：日志里落一条 running，正文边上落一张卡片 */
function toolBegin(m) {
  const rec = {
    id: 't' + Date.now().toString(36) + '-' + Math.random().toString(36).slice(2, 6),
    ts: Date.now(),
    session: S.session || '',
    name: m.name || 'tool',
    args: String(m.args || '').slice(0, 400),
    brief: toolArgBrief(m.args),
    status: 'running',
    sec: 0, chars: 0, output: '',
  };
  toolLogPush(rec);
  if (!LIVE.node) ensureLiveNode();
  const host = LIVE.node && LIVE.node.querySelector('.body');
  if (host) {
    const view = toolCard(rec, true);
    host.appendChild(view.det);
    LIVE.toolPending.push(view);
  }
  stick();
  toolPanelRedraw();
}

/* 工具结束：认领最近一个同名的 running 卡片（seq 每轮从 1 数起，不能当唯一键） */
function toolEnd(m) {
  const name = m.name || 'tool';
  let idx = -1;
  for (let i = LIVE.toolPending.length - 1; i >= 0; i--) {
    if (LIVE.toolPending[i].rec.name === name) { idx = i; break; }
  }
  if (idx < 0 && LIVE.toolPending.length) idx = LIVE.toolPending.length - 1;
  let rec;
  if (idx >= 0) {
    const view = LIVE.toolPending.splice(idx, 1)[0];
    rec = view.rec;
    rec.status = m.status || 'unknown';
    rec.sec = m.sec || 0;
    rec.chars = m.chars || 0;
    rec.output = String(m.output || '').slice(0, 800);
    toolCardPaint(view);
  } else {
    // 没收到 begin（中途刷新之类）：补一条收尾记录，记录面板里不至于丢掉
    rec = {
      id: 't' + Date.now().toString(36), ts: Date.now(), session: S.session || '',
      name, args: '', brief: toolArgBrief(m.args), status: m.status || 'unknown',
      sec: m.sec || 0, chars: m.chars || 0, output: String(m.output || '').slice(0, 800),
    };
    toolLogPush(rec);
  }
  toolLogSave(); // begin 时 push 的就是这个对象，改完就地落盘
  toolPanelRedraw();
}

/* ---------------- 流式 ---------------- */
function ensureLiveNode() {
  if (LIVE.node) return;
  const stream = $('#stream');
  if ($('.welcome', stream)) stream.innerHTML = '';
  LIVE.node = el('div', 'msg ai');
  LIVE.node.appendChild(subWho());
  const body = el('div', 'body');
  LIVE.rnode = null;
  LIVE.body = el('div', 'text md');
  body.appendChild(LIVE.body);
  LIVE.node.appendChild(body);
  stream.appendChild(LIVE.node);
  LIVE.body.classList.add('streaming');
  LIVE.streaming = true;
  LIVE.raw = '';
  LIVE.reasoning = '';
  LIVE.rnode = null;
  LIVE.rbody = null;
  LIVE.rsum = null;
  LIVE.reasonTouched = false;
  LIVE.started = false;
  LIVE.toolPending = [];
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
    const sum = el('summary', null, reasonLabel(0, true));
    const rb = el('div', 'rbody');
    det.append(sum, rb);
    // 只有真的点了 summary 才算「用户手动折过」：程序自己设 open 也会派发 toggle，
    // 用 toggle 监听会把自动展开误判成用户操作，收尾时就再也折不回去了
    sum.addEventListener('click', () => { LIVE.reasonTouched = true; });
    LIVE.node.querySelector('.body').insertBefore(det, LIVE.body);
    LIVE.rnode = det;
    LIVE.rsum = sum;
    LIVE.rbody = rb;
    LIVE.reasonTouched = false;
    det.open = false; // 默认收纳：思考不占版面，想看自己点开
  }
  const rb = LIVE.rbody;
  if (rb) {
    const follow = !LIVE.reasonTouched && LIVE.rnode.open &&
      (rb.scrollHeight - rb.scrollTop - rb.clientHeight < 24);
    rb.textContent = LIVE.reasoning;
    // 展开着看的时候跟上最新一行；收起来就不用管内层滚动
    if (follow) rb.scrollTop = rb.scrollHeight;
  }
  if (LIVE.rsum) LIVE.rsum.textContent = reasonLabel(LIVE.reasoning.length, true);
  stick(); // 消息在长，自动滚动跟上（用户翻上去看历史时不打扰）
}

function finishLive() {
  if (!LIVE.node) return;
  if (LIVE.body) {
    LIVE.body.classList.remove('streaming');
    LIVE.body.innerHTML = mdToHtml(LIVE.raw);
  }
  const hasText = !!LIVE.raw.trim();
  const reason = LIVE.reasoning.trim();
  // 默认收纳：想看的自己点开；用户已经点开过就别替他收回去
  if (LIVE.rnode && !LIVE.reasonTouched) LIVE.rnode.open = false;
  if (LIVE.rsum) LIVE.rsum.textContent = reasonLabel(reason.length, false);
  if (LIVE.rbody) LIVE.rbody.scrollTop = 0; // 收尾了，展开时从头看
  const body = LIVE.node.querySelector('.body');
  if (hasText) {
    // 补上重播/复制
    const actions = el('div', 'actions');
    const replay = el('button', 'act-btn');
    replay.innerHTML = icon('play') + '<span>重播</span>';
    replay.onclick = () => speakText(LIVE.raw);
    const copy = el('button', 'act-btn');
    copy.innerHTML = '<span>复制</span>';
    copy.onclick = () => navigator.clipboard?.writeText(LIVE.raw).then(() => toast('已复制', 'ok'));
    actions.append(replay, copy);
    body.appendChild(actions);
  } else if (reason) {
    // 只思考没正文：留着 CoT 并把「没说出来」讲明白，别让人对着一个空壳等结果
    reasonOnlyNote(body, reason);
  } else {
    // 什么都没产生（没有正文也没有思考）：不留空消息，跟以前一样收掉
    LIVE.node.remove();
  }
  LIVE.node = null; LIVE.body = null; LIVE.rnode = null; LIVE.rbody = null; LIVE.rsum = null;
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

/* ---------------- 列表里的一条贴纸 ----------------
   app-core 在桌面回合里发 {"t":"sticker","persona":pid,"name":"happy","url":...}，
   这里在消息流里画一条只有贴纸的消息；刷新后同一张贴纸会以附件形式回到历史里。 */
function appendSticker(ev) {
  if (!ev || !ev.url) return;
  if (ev.persona && S.active && ev.persona !== S.active) return;
  const stream = $('#stream');
  if ($('.welcome', stream)) stream.innerHTML = '';
  const wrap = el('div', 'msg ai');
  wrap.appendChild(subWho());
  const body = el('div', 'body');
  const img = document.createElement('img');
  img.className = 'sticker-msg';
  img.src = ev.url;
  img.alt = ev.name || '表情包';
  img.loading = 'lazy';
  img.onclick = () => { if (typeof openLightbox === 'function') openLightbox(ev.url, ev.name); };
  body.appendChild(img);
  wrap.appendChild(body);
  // 贴纸事件在回合收尾时到：接在正文后面，和刷新后历史里的顺序一致
  stream.appendChild(wrap);
  stick();
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
      // 思考阶段就把消息骨架建好：CoT 有地方渲染，也顺便自动滚动跟上
      ensureLiveNode();
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
    case 'tool_begin':
      toolBegin(m);
      break;
    case 'tool_end':
      toolEnd(m);
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
    // ---- act 工具推过来的三条（标记法没有对应物） ----
    case 'stage_say':
      // 她说的一句“舞台台词”：只飘在画面上，不进正文、不朗读
      trigBubble(m.text || '', m.ms);
      break;
    case 'param':
      l2dSetParam(m.id || '', m.to, m.ms);
      break;
    case 'fx':
      trigFx(m.name || '', m.ms);
      break;
    case 'sticker':
      appendSticker(m);
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
  const hasAttach = ATTACH.items.length > 0;
  if (!text && !hasAttach) return; // 文字和附件都空：什么都不发
  if (!S.active) { toast('先创建一个角色', 'err'); openPersonaEditor(true); return; }
  if (S.busy) { toast('她还在说，先等一下'); return; }
  // 先把附件读成 base64：读失败就原地报错，别把输入框和托盘清空
  let attachments = [];
  let previews = [];
  if (hasAttach) {
    try {
      attachments = await attachPayload();
    } catch (e) {
      toast('读取附件失败：' + (e.message || e), 'err');
      return;
    }
    if (!attachments.length) attachments = [];
    previews = attachPreviews();
  }
  input.value = '';
  autoGrow();
  if (hasAttach) clearAttach();
  stopAudio();
  if ($('.welcome', $('#stream'))) $('#stream').innerHTML = '';
  // 立刻画出这条用户消息 + 本地预览，不等服务端回 history
  $('#stream').appendChild(msgUser(text, previews));
  const entry = { role: 'user', kind: 'user', text };
  if (previews.length) entry.attachments = previews;
  S.history.push(entry);
  LIVE.node = null;
  LIVE.body = null;
  LIVE.raw = '';
  pinned = true;
  stick(true);
  TRIG.fire('user', { text });
  try {
    await cmd('send', attachments.length ? { text, attachments } : { text });
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
  paintComposer(); // 文字和附件都空的时候把发送键置灰
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
  $('#btn-mic').title = S.asr.enabled
    ? `点击开始 / 结束录音${S.mic_hotkey ? `；按住 ${S.mic_hotkey} 说话` : ''}`
    : '还没有配置语音识别服务';
  $('#btn-mic').classList.toggle('rec', S.recording);
  if (typeof paintThink === 'function') paintThink();
  // 发送键：回合中它永远是「停止」；平时文字与附件都空才置灰
  const hasText = !!input.value.trim();
  btn.disabled = !S.busy && (none || (!hasText && !ATTACH.items.length));
  const hint = $('#composer-hint');
  if (S.recording) {
    hint.textContent = `正在听…${S.mic_hotkey ? `松开 ${S.mic_hotkey}` : '再点一次'}结束，随后自动转写`;
    hint.classList.add('rec');
  } else if (ATTACH.items.length) {
    hint.textContent = '已选 ' + ATTACH.items.length + ' / ' + ATTACH.max + ' 个附件 · Enter 发送';
    hint.classList.remove('rec');
  } else if (!S.asr.enabled && !S.tts.enabled) {
    hint.textContent = '语音服务未配置：设置 → 服务，可以接入任何 OpenAI 兼容的 TTS / ASR';
    hint.classList.remove('rec');
  } else {
    hint.textContent = S.tts.enabled ? 'Enter 发送 · Shift+Enter 换行 · 回复会自动朗读' : 'Enter 发送 · Shift+Enter 换行';
    hint.classList.remove('rec');
  }
}
