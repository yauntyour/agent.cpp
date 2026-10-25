'use strict';
/* =====================================================================
   08-triggers —— 触发器：事件 → 动作序列
   =====================================================================
   一条规则长这样（存在角色卡 persona.json 的 triggers 数组里）：

     { "id": "poke-head", "on": "click", "when": { "zone": "head" },
       "do": [ {"a":"motion","group":"TapHead"}, {"a":"say","text":"别摸头…"} ],
       "cd": 5, "once": false, "off": false }

   on    事件：boot / turn_begin / turn_end / reply / user / emotion / act /
              motion / expr / click / idle
   when  条件（全部满足才触发）：zone / emotion / act / group / expr /
              text / min_idle；字符串或数组都行，text 支持 /正则/
   do    动作：motion expr param say speak send idle toast fx sound wait emo
   cd    冷却（秒）；once 只触发一次（本次页面会话）

   两个方向都是通的：
   - AI → 舞台：回复里的 [[emo:]] / [[act:]] / [[motion:]] 事件直接进 fire()
   - 舞台 → AI：动作里的 send / idle 会把一条消息交回回合管线，她真的回你
   ===================================================================== */

const TRIG = {
  list: [],           // 当前生效的规则
  builtin: false,     // 是否在用内置默认（角色卡里没写）
  last: {},           // id -> 上次触发时间
  onceDone: {},       // id -> true
  depth: 0,           // 防止 emo 动作自己触发自己
  lastKind: {},
  idleTimer: null,
  lastInput: Date.now(),
  input: 0,
};

/* 内置默认规则：没有配过触发器的角色先用这一套，戳一下就有反应 */
const TRIG_DEFAULT = [
  {
    id: 'touch-head', on: 'click', when: { zone: ['head', 'touchhead'] }, cd: 5,
    note: '摸头会躲',
    do: [{ a: 'motion', group: 'TapHead' }, { a: 'say', text: '别、别摸头…' }, { a: 'emo', name: 'shy' }],
  },
  {
    id: 'poke-body', on: 'click', when: { zone: ['body', 'touchbody'] }, cd: 12,
    note: '戳一下，她会回你',
    do: [{ a: 'motion', group: 'TapBody' }, { a: 'send', text: '（你戳了戳她的手臂）' }],
  },
  {
    id: 'act-respond', on: 'act', when: { act: ['hug', 'headpat', 'lean', 'wave'] }, cd: 4,
    note: '星号里的动作 → 模型动作',
    do: [{ a: 'motion', group: 'TapBody', index: 0 }],
  },
  {
    id: 'emo-happy', on: 'emotion', when: { emotion: ['happy', 'affection'] }, cd: 4,
    note: '开心时换姿势',
    do: [{ a: 'motion', group: 'Idle' }],
  },
  {
    id: 'idle-shift', on: 'idle', when: { min_idle: 75 }, cd: 80,
    note: '安静久了动一下',
    do: [{ a: 'motion', group: 'Idle' }, { a: 'fx', name: 'pulse', ms: 700 }],
  },
  {
    id: 'goodnight', on: 'reply', when: { text: '晚安' }, cd: 30,
    note: '说到晚安就困了',
    do: [{ a: 'emo', name: 'affection' }, { a: 'say', text: '晚安…做个好梦' }],
  },
];

function trigClone(x) { return JSON.parse(JSON.stringify(x)); }

function trigRefresh() {
  const mine = (S.persona && Array.isArray(S.persona.triggers)) ? S.persona.triggers : [];
  TRIG.builtin = mine.length === 0;
  const list = mine.length ? mine : trigClone(TRIG_DEFAULT);
  const sig = JSON.stringify(list);
  if (sig === TRIG._sig) return;   // 状态广播很频繁，规则没变就别清冷却
  TRIG._sig = sig;
  TRIG.list = list;
  TRIG.last = {};
  TRIG.onceDone = {};
}

/* ---------------- 条件 ---------------- */
function trigAny(cond, value) {
  if (cond == null) return true;
  if (Array.isArray(cond)) return cond.some(c => String(c).toLowerCase() === String(value).toLowerCase());
  return String(cond).toLowerCase() === String(value).toLowerCase();
}

function trigText(text, cond) {
  if (!cond) return true;
  const t = String(text || '');
  const m = /^\/(.*)\/([gimsuy]*)$/.exec(String(cond));
  if (m) {
    try { return new RegExp(m[1], m[2]).test(t); } catch { return false; }
  }
  return t.indexOf(String(cond)) >= 0;
}

function trigMatch(rule, ctx) {
  const w = rule.when || rule.match || {};
  if (w.zone != null && !trigAny(w.zone, ctx.zone)) return false;
  if (w.emotion != null && !trigAny(w.emotion, ctx.emotion || S.emotion)) return false;
  if (w.act != null && !trigAny(w.act, ctx.act)) return false;
  if (w.group != null && !trigAny(w.group, ctx.group)) return false;
  if (w.expr != null && !trigAny(w.expr, ctx.name)) return false;
  if (w.text != null && !trigText(ctx.text, w.text)) return false;
  if (w.not_text && trigText(ctx.text, w.not_text)) return false;
  if (w.min_idle != null && Number(ctx.idle || 0) < Number(w.min_idle)) return false;
  return true;
}

/* ---------------- 动作 ---------------- */
function trigFx(name, ms) {
  const frame = $('#stage-frame');
  if (!frame || !name) return;
  const cls = 'fx-' + String(name).replace(/[^a-z0-9_-]/gi, '');
  frame.classList.add(cls);
  setTimeout(() => frame.classList.remove(cls), Math.max(200, Number(ms) || 600));
}

function trigBubble(text, ms) {
  const b = $('#stage-bubble');
  if (!b) return;
  if (!text) { b.classList.remove('on'); return; }
  b.textContent = text;
  b.classList.add('on');
  if (trigBubble.t) clearTimeout(trigBubble.t);
  trigBubble.t = setTimeout(() => b.classList.remove('on'), Math.max(1200, Number(ms) || 4200));
}

/* 舞台发起的用户消息：和手打的走同一条 send 命令，只是渲染得淡一点 */
async function stageSend(text, opts = {}) {
  const t = String(text || '').trim();
  if (!t) return;
  if (S.busy) { toast('她还在回应，等一下', '', 1600); return; }
  if (!opts.hidden) {
    const stream = $('#stream');
    if ($('.welcome', stream)) stream.innerHTML = '';
    const node = msgUser(t);
    node.classList.add('from-stage');
    stream.appendChild(node);
    S.history.push({ role: 'user', kind: 'user', text: t });
    pinned = true;
    stick(true);
    LIVE.node = null; LIVE.body = null; LIVE.raw = '';
  }
  try { await cmd('send', { text: t }); }
  catch (e) { toast(e.message, 'err'); }
}

async function trigAct(act, ctx, state) {
  if (!act || typeof act !== 'object') return;
  const a = act.a || act.type;
  switch (a) {
    case 'motion':
      l2dPlayMotion(act.group, act.index);
      break;
    case 'expr':
      l2dSetExpression(act.id != null ? act.id : act.name);
      break;
    case 'param':
      l2dSetParam(act.id, act.to, act.ms);
      break;
    case 'say':
      trigBubble(act.text, act.ms);
      break;
    case 'speak':
      speakText(act.text);
      break;
    case 'send':
      if (!state.sent) { state.sent = true; await stageSend(act.text); }
      break;
    case 'idle':
      if (!state.sent) { state.sent = true; await stageSend('[[idle]]', { hidden: true }); }
      break;
    case 'emo':
      // 走一遍正常链路，光球 / 语音音色也跟着变；深度护栏防止自激
      if (TRIG.depth < 3) {
        TRIG.depth += 1;
        try { setEmotion(act.name || 'neutral'); } finally { TRIG.depth -= 1; }
      }
      break;
    case 'toast':
      toast(act.text || '');
      break;
    case 'fx':
      trigFx(act.name, act.ms);
      break;
    case 'sound':
      if (act.url) {
        const au = new Audio(act.url);
        au.volume = act.volume == null ? 0.7 : Number(act.volume);
        au.play().catch(() => { });
      }
      break;
    case 'wait':
      await new Promise(r => setTimeout(r, Math.max(0, Math.min(20000, Number(act.ms) || 200))));
      break;
    default:
      console.info('trigger: 未知动作', a);
  }
}

async function trigRun(actions, ctx, state) {
  for (const act of actions || []) {
    try { await trigAct(act, ctx, state); }
    catch (e) { console.warn('trigger action failed', act, e); }
  }
}

/* ---------------- 触发 ---------------- */
TRIG.fire = function fire(kind, ctx) {
  if (!kind) return;
  const now = Date.now();
  const c = Object.assign({ kind }, ctx || {});
  // reply 是流式里每来一段就查一次，压一下频率，别让一条回复触发好几遍
  const minGap = kind === 'reply' ? 600 : 0;
  if (minGap && now - (TRIG.lastKind[kind] || 0) < minGap) return;
  TRIG.lastKind[kind] = now;

  const hits = TRIG.list.filter(r => r && !r.off && (r.on || r.event) === kind && trigMatch(r, c));
  if (!hits.length) return;
  const state = { sent: false };
  for (const r of hits) {
    const id = r.id || ('r' + TRIG.list.indexOf(r));
    if (TRIG.onceDone[id]) continue;
    const cd = Math.max(0, Number(r.cd != null ? r.cd : r.cooldown) || 0) * 1000;
    if (cd && now - (TRIG.last[id] || 0) < cd) continue;
    TRIG.last[id] = now;
    if (r.once) TRIG.onceDone[id] = true;
    trigRun(r.do || r.actions, c, state);
  }
};

/* 手动跑一条（面板里的「试一下」） */
TRIG.test = function test(rule) {
  if (!rule) return;
  trigRun(rule.do || rule.actions, { kind: 'test', text: '', idle: 999 }, { sent: false });
};

/* 保存：整份列表写回角色卡 */
TRIG.save = async function save(list) {
  const p = S.persona || {};
  if (!p.id) throw new Error('还没有打开角色');
  await cmd('persona.save', { id: p.id, patch: { triggers: list } });
  S.persona.triggers = list;
  trigRefresh();
};

/* ---------------- 静默计时 ----------------
   任何一个操作都算「你在」；安静到规则里的 min_idle 就触发一次 idle。 */
function trigTouch() {
  TRIG.lastInput = Date.now();
  TRIG.input += 1;
}

TRIG.startIdle = function startIdle() {
  if (TRIG.idleTimer) clearInterval(TRIG.idleTimer);
  TRIG.lastInput = Date.now();
  TRIG.idleTimer = setInterval(() => {
    const idle = (Date.now() - TRIG.lastInput) / 1000;
    if (idle < 30) return;
    TRIG.fire('idle', { idle });
  }, 5000);
};

/* ---------------- 接线 ---------------- */
onEvent((m) => {
  switch (m.t) {
    case 'turn_begin':
      trigTouch();
      TRIG.fire('turn_begin', {});
      break;
    case 'turn_end':
      TRIG.fire('turn_end', {});
      break;
    case 'emo':
      TRIG.fire('emotion', { emotion: (m.name || 'neutral').toLowerCase() });
      break;
    case 'act':
      TRIG.fire('act', { act: (m.name || '').toLowerCase(), text: m.name });
      break;
    case 'motion':
      TRIG.fire('motion', { group: m.group, index: m.index });
      break;
    case 'expr':
      TRIG.fire('expr', { name: m.name });
      break;
  }
});

document.addEventListener('pointerdown', trigTouch, true);
document.addEventListener('keydown', trigTouch, true);
