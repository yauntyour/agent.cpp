'use strict';
/* =====================================================================
   07-live2d —— 舞台：Live2D 模型
   =====================================================================
   三件事，都只在页面里做，服务端只管发文件：

   1. 运行时：从 .agent/live2d/runtime/ 按 manifest.json 的清单加载
      （默认 pixi.min.js → live2dcubismcore.min.js → cubism4.min.js）。
      也可以让它走 CDN（面板里的「从 CDN 加载」），适合先看效果。
   2. 模型：.agent/live2d/models/ 下面任意 model3.json，路径写在角色卡
      live2d.model 里。加载完把动作组 / 表情名回报给角色卡，供提示词引用。
   3. 演出：动作、表情、参数、口型。表情与动作的对应关系是数据（live2d.emo），
      不是代码 —— 换模型不用改 JS。

   没有模型时整个舞台退回光球，情绪 / 动作 / 触发器照常工作。
   ===================================================================== */

const L2D = {
  mode: 'orb',            // orb | l2d
  status: 'idle',         // idle | loading | ready | error
  app: null, model: null, canvas: null,
  scan: null,             // live2d.scan 的结果
  runtimeFrom: '',        // local | cdn
  groups: [],             // 动作组名
  expressions: [],        // 表情名
  params: [],             // 正在补间的参数
  cfg: {},                // 角色卡的 live2d 对象
  mouth: 0, speaking: false,
  hitFired: false,
  tick: 0,
  _loading: null,
};

/* 面板里可以改这两个地址；只影响「从 CDN 加载」这一条路 */
const L2D_CDN = [
  'https://unpkg.com/pixi.js@6.5.10/dist/browser/pixi.min.js',
  'https://unpkg.com/live2dcubismcore@1.0.2/live2dcubismcore.min.js',
  'https://unpkg.com/pixi-live2d-display@0.4.0/dist/cubism4.min.js',
];

/* 情绪 → 表情 / 动作。expr 可以是序号（0 起）也可以是表情名，
   具体含义由模型决定，所以这一份只是「常见样本模型的合理默认」，
   面板里可以逐条改写，写进角色卡 live2d.emo。 */
const L2D_EMO_DEFAULT = {
  neutral: { expr: 0 },
  happy: { expr: 1 },
  sad: { expr: 2 },
  angry: { expr: 3 },
  shy: { expr: 4 },
  surprised: { expr: 5 },
  thinking: { expr: 6 },
  affection: { expr: 7 },
};

function l2dCfg() {
  const c = (S.persona && S.persona.live2d) || {};
  return c && typeof c === 'object' ? c : {};
}

function l2dSetStatus(kind, text) {
  const wrap = $('#l2d-wrap');
  const st = $('#l2d-status');
  wrap.classList.toggle('loading', kind === 'loading');
  wrap.classList.toggle('error', kind === 'error');
  wrap.classList.toggle('ready', kind === 'ready');
  if (st) st.textContent = text || '';
  const hint = $('#stage-hint');
  if (hint && kind !== 'ready') hint.textContent = text || 'Live2D 未接入 · 点这里接入';
}

function l2dSetMode(mode) {
  L2D.mode = mode;
  $('#stage-frame').dataset.mode = mode;
  if (mode === 'l2d') {
    $('#stage-hint').textContent = '';
    $('#stage-hint').classList.add('hidden');
  } else {
    $('#stage-hint').classList.remove('hidden');
  }
}

/* ---------------- 运行时 ---------------- */
function loadScript(url) {
  return new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = url;
    s.async = false; // 顺序就是依赖顺序：pixi → core → display
    s.onload = () => resolve(url);
    s.onerror = () => reject(new Error('脚本加载失败：' + url));
    document.head.appendChild(s);
  });
}

async function l2dLoadRuntime() {
  if (window.PIXI && PIXI.live2d && window.Live2DCubismCore) return 'cached';
  const useCdn = localStorage.getItem('agent.l2d.cdn') === '1';
  let list, from;
  if (useCdn) {
    list = L2D_CDN; from = 'cdn';
  } else {
    const scripts = (L2D.scan && L2D.scan.runtime && L2D.scan.runtime.scripts) || [];
    if (!scripts.length || (L2D.scan.runtime && !L2D.scan.runtime.ok))
      throw new Error('运行时库不全：把 Cubism Core 等放进 .agent/live2d/runtime/');
    list = scripts.filter(s => s.ok !== false).map(s => '/api/live2d/runtime/' + encodeURIComponent(s.name));
    from = 'local';
  }
  for (const url of list) await loadScript(url);
  if (!window.PIXI || !PIXI.live2d || !window.Live2DCubismCore)
    throw new Error('运行时加载了但没有注册 PIXI.live2d');
  L2D.runtimeFrom = from;
  return from;
}

/* ---------------- 模型 ---------------- */
function l2dModelUrl(path) {
  return '/api/live2d/' + String(path).split('/').map(encodeURIComponent).join('/');
}

function l2dCollectInfo() {
  const defs = (L2D.model.internalModel.motionManager &&
    L2D.model.internalModel.motionManager.definitions) || {};
  L2D.groups = Object.keys(defs);
  const settings = L2D.model.internalModel.settings || {};
  L2D.expressions = (settings.expressions || []).map(e => e.Name).filter(Boolean);
}

function l2dFit() {
  const app = L2D.app, model = L2D.model;
  if (!app || !model) return;
  const res = app.renderer.resolution || 1;
  const W = app.renderer.width / res;
  const H = app.renderer.height / res;
  const cfg = L2D.cfg;
  const iw = model.internalModel.width || 1;
  const ih = model.internalModel.height || 1;
  const scale = Math.min(W / iw, H / ih) * (Number(cfg.scale) || 1) * 1.02;
  model.scale.set(scale);
  model.anchor.set(0.5, 0.5);
  model.x = W * (0.5 + (Number(cfg.x) || 0));
  model.y = H * (0.5 + (Number(cfg.y) || 0));
}

function l2dDestroy() {
  try { if (L2D.app) L2D.app.destroy(false, { children: true, texture: true, baseTexture: true }); } catch { }
  L2D.app = null;
  L2D.model = null;
  L2D.groups = [];
  L2D.expressions = [];
  L2D.params = [];
  // 一个 canvas 只能有一个 WebGL 上下文，destroy 之后它会失效：
  // 换一块干净的画布，比想办法复活旧上下文省事得多。
  const old = $('#l2d-canvas');
  if (old) {
    const fresh = document.createElement('canvas');
    fresh.id = 'l2d-canvas';
    old.replaceWith(fresh);
    L2D.canvas = fresh;
  }
}

async function l2dMount(force) {
  if (L2D._loading && !force) return L2D._loading;
  const job = l2dMountInner(force);
  L2D._loading = job;
  try {
    return await job;
  } finally {
    // 只有自己那一份还挂着时才清；换模型时可能已经换成新的加载了
    if (L2D._loading === job) L2D._loading = null;
  }
}

async function l2dMountInner(force) {
  const cfg = l2dCfg();
  L2D.cfg = cfg;
  const path = (cfg.model || '').trim();
  if (MOCK) { l2dSetStatus('error', '演示模式没有后端，接不上模型'); l2dSetMode('orb'); return; }
  if (!path) {
    l2dDestroy();
    L2D.status = 'idle';
    l2dSetStatus('idle', 'Live2D 未接入 · 点这里接入');
    l2dSetMode('orb');
    return;
  }
  try {
    l2dSetStatus('loading', '加载运行时…');
    if (L2D.mode !== 'l2d') l2dSetMode('l2d');
    await l2dLoadRuntime();
    l2dSetStatus('loading', '加载模型…');
    l2dDestroy();
    L2D.canvas = $('#l2d-canvas');
    const rect = L2D.canvas.getBoundingClientRect();
    L2D.app = new PIXI.Application({
      view: L2D.canvas,
      width: Math.max(2, Math.round(rect.width)),
      height: Math.max(2, Math.round(rect.height)),
      backgroundAlpha: 0,
      antialias: true,
      autoDensity: true,
      resolution: Math.min(2, window.devicePixelRatio || 1),
    });
    const model = await PIXI.live2d.Live2DModel.from(l2dModelUrl(path), { autoInteract: true });
    L2D.model = model;
    L2D.app.stage.addChild(model);
    l2dCollectInfo();
    l2dFit();
    L2D.status = 'ready';
    l2dSetMode('l2d');
    l2dSetStatus('ready', '');
    l2dBindPointer();
    l2dStartTicker();
    if (window.ResizeObserver && !L2D._ro) {
      L2D._ro = new ResizeObserver(() => l2dScheduleResize());
      L2D._ro.observe($('#l2d-wrap'));
    }
    // 把可用清单回报给角色卡：提示词里要写清楚能播哪些动作
    l2dReportInfo();
    if (typeof TRIG !== 'undefined') TRIG.fire('boot', {});
  } catch (e) {
    console.warn('live2d mount failed:', e);
    L2D.status = 'error';
    l2dSetMode('orb');
    l2dSetStatus('error', '模型没起来：' + (e && e.message ? e.message : e));
  }
}

/* 模型里有什么，角色卡里就记什么：下次拼提示词时能列出动作组 */
let l2dReportTimer = null;
function l2dReportInfo() {
  if (l2dReportTimer) clearTimeout(l2dReportTimer);
  l2dReportTimer = setTimeout(async () => {
    const p = S.persona || {};
    if (!p.id) return;
    if (S.busy) { l2dReportInfo(); return; }   // 回合中写角色卡会打断状态，等它说完
    const cur = l2dCfg();
    const same = JSON.stringify(cur.groups || []) === JSON.stringify(L2D.groups) &&
      JSON.stringify(cur.expressions || []) === JSON.stringify(L2D.expressions);
    if (same) return;
    try {
      const next = Object.assign({}, cur, { groups: L2D.groups, expressions: L2D.expressions });
      await cmd('persona.save', { id: p.id, patch: { live2d: next } });
      S.persona.live2d = next;
      L2D.cfg = next;
    } catch (e) { console.warn('report live2d info failed', e); }
  }, 1200);
}

/* ---------------- 演出 ---------------- */
function l2dPlayMotion(group, index) {
  const m = L2D.model;
  if (!m || !group) return false;
  const idx = Number.isFinite(Number(index)) && index !== '' && index != null ? Number(index) : undefined;
  if (L2D.groups.length && !L2D.groups.includes(group)) {
    console.info('live2d: 模型里没有动作组', group, '可用：', L2D.groups);
    return false;
  }
  try {
    m.motion(group, idx, 3); // 3 = FORCE，触发器的意图比待机动作优先
    return true;
  } catch (e) { console.warn('motion failed', e); return false; }
}

function l2dSetExpression(id) {
  const m = L2D.model;
  if (!m || id === undefined || id === null || id === '') return false;
  try {
    if (typeof id === 'number' || /^\d+$/.test(String(id))) {
      const list = (m.internalModel.settings && m.internalModel.settings.expressions) || [];
      const i = Number(id);
      if (i < 0 || i >= list.length) return false;
      m.expression(i);
    } else {
      m.expression(String(id));
    }
    return true;
  } catch (e) { console.warn('expression failed', e); return false; }
}

function l2dSetParam(id, to, ms) {
  if (!L2D.model || !id) return;
  const core = L2D.model.internalModel.coreModel;
  let from = Number(to);
  try { from = core.getParameterValueById(id); } catch { }
  L2D.params = L2D.params.filter(p => p.id !== id);
  L2D.params.push({ id, from, to: Number(to), t0: performance.now(), ms: Math.max(0, Number(ms) || 0) });
}

function l2dEmotionMap(name) {
  const map = l2dCfg().emo;
  const table = map && typeof map === 'object' ? map : L2D_EMO_DEFAULT;
  return table[name] || (table === L2D_EMO_DEFAULT ? L2D_EMO_DEFAULT.neutral : null);
}

function l2dSetEmotion(name) {
  if (L2D.status !== 'ready') return;
  const hit = l2dEmotionMap((name || 'neutral').toLowerCase());
  if (!hit) return;
  if (hit.expr !== undefined && hit.expr !== null && hit.expr !== '') l2dSetExpression(hit.expr);
  if (hit.motion) {
    const g = Array.isArray(hit.motion) ? hit.motion[0] : hit.motion;
    const i = Array.isArray(hit.motion) ? hit.motion[1] : undefined;
    l2dPlayMotion(g, i);
  }
}

function l2dSetSpeaking(on) {
  L2D.speaking = !!on;
}

function l2dStartTicker() {
  const app = L2D.app;
  if (!app || app.__l2dTick) return;
  app.__l2dTick = true;
  app.ticker.add(() => {
    const m = L2D.model;
    if (!m) return;
    L2D.tick += 1 / 60;
    // 口型：说话时给一个 3~5Hz 的包络，比随机数像人
    const t = L2D.tick;
    const target = L2D.speaking
      ? 0.18 + 0.55 * Math.abs(Math.sin(t * 7.4) * Math.sin(t * 2.1 + 0.7))
      : 0;
    L2D.mouth += (target - L2D.mouth) * 0.28;
    try {
      const core = m.internalModel.coreModel;
      core.setParameterValueById('ParamMouthOpenY', L2D.mouth);
    } catch { }
    // 参数补间（触发器里的 param 动作用得上）
    if (L2D.params.length) {
      const now = performance.now();
      L2D.params = L2D.params.filter(p => {
        const k = p.ms <= 0 ? 1 : Math.min(1, (now - p.t0) / p.ms);
        const e = 1 - Math.pow(1 - k, 3);
        const v = p.from + (p.to - p.from) * e;
        try { m.internalModel.coreModel.setParameterValueById(p.id, v); } catch { return false; }
        return k < 1;
      });
    }
  });
}

/* ---------------- 点哪里算哪里 ---------------- */
function l2dZoneFromPoint(cx, cy) {
  const rect = L2D.canvas.getBoundingClientRect();
  const y = (cy - rect.top) / Math.max(1, rect.height);
  if (y < 0.34) return 'head';
  if (y < 0.78) return 'body';
  return 'hand';
}

function l2dBindPointer() {
  const c = L2D.canvas;
  if (!c || c.__l2dBound) return;
  c.__l2dBound = true;
  // 模型自己声明了 HitAreas 就听它的；否则按画布高度切三段
  if (L2D.model) {
    try {
      L2D.model.on('hit', (names) => {
        L2D.hitFired = true;
        const zone = (names && names[0] ? String(names[0]) : 'body').toLowerCase();
        if (typeof TRIG !== 'undefined') TRIG.fire('click', { zone, names });
      });
    } catch (e) { console.warn('hit binding failed', e); }
  }
  c.addEventListener('pointerdown', () => { L2D.hitFired = false; });
  c.addEventListener('pointerup', (e) => {
    const x = e.clientX, y = e.clientY;
    // 库的 hit 走的是 pointertap（pointerup 上发），等它先出手再兜底
    setTimeout(() => {
      if (L2D.hitFired) return;
      if (typeof TRIG !== 'undefined') TRIG.fire('click', { zone: l2dZoneFromPoint(x, y) });
    }, 40);
  });
}

/* 画布尺寸跟着布局走：窗口拉伸、抽屉开关都会变 */
let l2dRoTimer = null;
function l2dScheduleResize() {
  if (l2dRoTimer) clearTimeout(l2dRoTimer);
  l2dRoTimer = setTimeout(() => {
    if (!L2D.app || !L2D.model || !L2D.canvas) return;
    const rect = L2D.canvas.getBoundingClientRect();
    if (rect.width < 2 || rect.height < 2) return;
    L2D.app.renderer.resize(Math.round(rect.width), Math.round(rect.height));
    l2dFit();
  }, 140);
}
window.addEventListener('resize', l2dScheduleResize);

/* ---------------- 入口 ---------------- */
async function l2dApply(force) {
  const cfg = l2dCfg();
  const path = (cfg.model || '').trim();
  // 同一个模型不重复加载；换角色 / 换模型才重建
  if (!force && L2D.status === 'ready' && L2D.model && L2D._path === path) {
    L2D.cfg = cfg;
    l2dFit();
    return;
  }
  L2D._path = path;
  if (!L2D.scan && !MOCK) {
    try { L2D.scan = await cmd('live2d.scan'); } catch { }
  }
  await l2dMount(force);
}

async function l2dReload() {
  l2dDestroy();
  L2D.status = 'idle';
  L2D._path = '';
  await l2dApply(true);
}

function l2dUseCdn(on) {
  localStorage.setItem('agent.l2d.cdn', on ? '1' : '0');
  l2dReload();
}
