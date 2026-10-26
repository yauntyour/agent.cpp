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

   没有模型时画布上给一句能点的提示，情绪 / 动作 / 触发器照常工作。
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

const L2D_KINDS = ['idle', 'loading', 'ready', 'error'];

function l2dSetStatus(kind, text) {
  const wrap = $('#l2d-wrap');
  const st = $('#l2d-status');
  for (const k of L2D_KINDS) wrap.classList.toggle(k, k === kind);
  L2D.mode = kind === 'ready' ? 'l2d' : 'empty';
  if (st) st.textContent = kind === 'ready' ? '' : (text || '');
}

// 画布上的提示本身就是入口：没模型 / 没运行时，点一下打开舞台面板
$('#l2d-status').onclick = () => openStagePanel();

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
      throw new Error('运行时核心没装：在舞台面板里点「下载运行时」');
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

/* 取景：画布铺满整个窗口，所以不是「塞进一个框」，而是「让画面填满视口」。
   - 立绘（高明显大于宽）：按高度撑满，人站在画面中央，左右留白交给光晕；
   - 场景图（方形 / 横向）：铺满窗口，多出来的部分裁掉 —— 上下各裁一点，
     并且往下压 10% 的溢出量，让头顶留白多一点（下面那截反正被字幕压着）。
   cfg.scale / x / y 依然有效：在取景结果上再乘一档、再偏一点。 */
function l2dFit() {
  const app = L2D.app, model = L2D.model;
  if (!app || !model) return;
  const res = app.renderer.resolution || 1;
  const W = app.renderer.width / res;
  const H = app.renderer.height / res;
  if (W < 2 || H < 2) return;
  const cfg = L2D.cfg;
  const iw = model.internalModel.width || 1;
  const ih = model.internalModel.height || 1;
  const fitW = W / iw, fitH = H / ih;
  const base = ih > iw * 1.15 ? fitH : Math.max(fitW, fitH);
  const scale = base * (Number(cfg.scale) || 1);
  model.scale.set(scale);
  model.anchor.set(0.5, 0.5);
  model.x = W * (0.5 + (Number(cfg.x) || 0));
  const overflow = Math.max(0, ih * scale - H);
  model.y = H * (0.5 + (Number(cfg.y) || 0)) + overflow * 0.1;
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

/* ---------------- 加载调度 ----------------
   一个模型只装一次：paintStage 会被状态广播反复调到，如果每次都从
   Live2DModel.from 走一遍，就会看到模型反复重建（掉帧、网络里重复请求）。
   这里把加载排成一条队：
   - 已经在装的模型，重复请求直接拿同一个 Promise；
   - 排队期间换了模型，旧的那次开跑时自己退出；
   - token 作废更早的请求，同一个模型不会被装两遍。 */
L2D._chain = Promise.resolve();
L2D._pending = null;
L2D._token = 0;
L2D._failedPath = '';

function l2dSerialize(job) {
  const run = L2D._chain.then(job, job);
  L2D._chain = run.then(() => { }, () => { });
  return run;
}

function l2dApply(force) {
  const cfg = l2dCfg();
  const path = (cfg.model || '').trim();
  L2D.cfg = cfg;
  if (!force) {
    if (L2D.status === 'ready' && L2D.model && L2D._path === path) { l2dFit(); return Promise.resolve(); }
    if (L2D._pending && L2D._path === path) return L2D._pending;   // 同一个模型正在装
    if (L2D.status === 'error' && L2D._failedPath === path) return Promise.resolve(); // 失败过别自动重试
  }
  L2D._path = path;
  const token = ++L2D._token;
  const job = l2dSerialize(() => l2dMountInner(path, token));
  const tracked = job.then(
    (r) => { if (L2D._pending === tracked) L2D._pending = null; return r; },
    (e) => { if (L2D._pending === tracked) L2D._pending = null; throw e; });
  L2D._pending = tracked;
  return tracked;
}

async function l2dMountInner(path, token) {
  if (token !== L2D._token) return;                      // 被后来的请求取代了
  if (path !== (l2dCfg().model || '').trim()) return;     // 排队期间换了模型
  // 先拿一次资源清单：面板的模型列表和「新建角色」里的默认模型都靠它，
  // 就算当前角色没有模型也要拿
  if (!L2D.scan && !MOCK) {
    try { L2D.scan = await cmd('live2d.scan'); } catch { }
    if (token !== L2D._token) return;
  }
  const cfg = l2dCfg();
  L2D.cfg = cfg;
  if (MOCK) { l2dSetStatus('error', '演示模式没有后端，接不上模型'); return; }
  if (!path) {
    l2dDestroy();
    L2D.status = 'idle';
    L2D._failedPath = '';
    l2dSetStatus('idle', '还没有选模型 · 点这里接入');
    return;
  }
  try {
    l2dSetStatus('loading', '加载运行时…');
    await l2dLoadRuntime();
    if (token !== L2D._token) return;
    l2dSetStatus('loading', '加载模型…');
    l2dDestroy();
    L2D.canvas = $('#l2d-canvas');
    // 用 offsetWidth/Height（布局尺寸）而不是 getBoundingClientRect：
    // 舞台上有呼吸缩放，rect 会带上 transform，量出来的尺寸会越滚越大
    L2D.app = new PIXI.Application({
      view: L2D.canvas,
      width: Math.max(2, L2D.canvas.offsetWidth || 2),
      height: Math.max(2, L2D.canvas.offsetHeight || 2),
      backgroundAlpha: 0,
      antialias: true,
      autoDensity: true,
      resolution: Math.min(2, window.devicePixelRatio || 1),
    });
    const model = await PIXI.live2d.Live2DModel.from(l2dModelUrl(path), { autoInteract: true });
    if (token !== L2D._token) { try { model.destroy(); } catch { } return; }
    L2D.model = model;
    L2D.app.stage.addChild(model);
    l2dCollectInfo();
    l2dFit();
    L2D.status = 'ready';
    L2D._failedPath = '';
    l2dSetStatus('ready', '');
    l2dBindPointer();
    l2dStartTicker();
    l2dStartCursor(); // 窗口外的光标也跟（桌面壳提供 host_cursor 时）
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
    L2D._failedPath = path;
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

/* 面板里拖缩放 / 偏移时先看效果，点「保存并生效」才写进角色卡 */
function l2dPreview(patch) {
  if (L2D.status !== 'ready' || !L2D.model) return;
  L2D.cfg = Object.assign({}, L2D.cfg, patch);
  l2dFit();
}

/* ---------------- 光标跟随 ----------------
   pixi-live2d 的 autoInteract 只听画布上的 pointermove：鼠标一移出窗口
   （去任务栏、去别的窗口）就再也收不到事件，视线会停在最后那一点。
   桌面壳能问系统要光标的客户区坐标（host_cursor），所以由页面自己喂 focus()，
   窗口内外都跟得上。浏览器里打开时没有这个绑定，退回库自己的交互。 */
const CURSOR = { timer: 0, busy: false };

function l2dCursorSupported() {
  return typeof window.host_cursor === 'function';
}

async function l2dCursorPoll() {
  if (CURSOR.busy || L2D.status !== 'ready' || !L2D.model) return;
  if (document.hidden || !l2dCursorSupported()) return;
  CURSOR.busy = true;
  try {
    const raw = await window.host_cursor();
    const r = typeof raw === 'string' ? JSON.parse(raw) : raw;
    if (!r || !r.ok || !(r.w > 0) || !(r.h > 0)) return;
    // 宿主给的是客户区的物理像素。按比例换成 CSS 像素，
    // 这样显示器缩放（125% / 150%）和窗口尺寸都不用各算一遍。
    const x = r.x / r.w * window.innerWidth;
    const y = r.y / r.h * window.innerHeight;
    L2D.model.focus(x, y, false);
  } catch { /* 宿主一时拿不到光标（锁屏、切换桌面）就跳过这一拍 */ }
  finally { CURSOR.busy = false; }
}

function l2dStartCursor() {
  if (!l2dCursorSupported() || CURSOR.timer) return;
  CURSOR.timer = setInterval(l2dCursorPoll, 120);
  l2dCursorPoll();
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

/* ---------------- 点哪里算哪里 ----------------
   画布铺满整屏，不能按视口切三段（那样点空白也算摸头）。
   先算出模型在屏幕上的实际方框，落在框外就不算。 */
function l2dModelBox() {
  const m = L2D.model, app = L2D.app;
  if (!m || !app) return null;
  const res = app.renderer.resolution || 1;
  const W = app.renderer.width / res, H = app.renderer.height / res;
  const b = m.getBounds();                 // 世界坐标就是 CSS 像素
  const rect = L2D.canvas.getBoundingClientRect();
  const kx = rect.width / Math.max(1, W), ky = rect.height / Math.max(1, H);
  return {
    x: rect.left + b.x * kx, y: rect.top + b.y * ky,
    w: b.width * kx, h: b.height * ky,
  };
}

function l2dZoneFromPoint(cx, cy) {
  const box = l2dModelBox();
  if (!box || box.w < 8 || box.h < 8) return 'body';
  if (cx < box.x || cx > box.x + box.w || cy < box.y || cy > box.y + box.h) return '';
  const y = (cy - box.y) / box.h;
  if (y < 0.34) return 'head';
  if (y < 0.78) return 'body';
  return 'hand';
}

function l2dBindPointer() {
  const c = L2D.canvas;
  if (!c || c.__l2dBound) return;
  c.__l2dBound = true;
  // 模型自己声明了 HitAreas 就听它的；否则按模型方框的高度切三段
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
      const zone = l2dZoneFromPoint(x, y);
      if (!zone) return;   // 点在画布上但没有模型：不算戳她
      if (typeof TRIG !== 'undefined') TRIG.fire('click', { zone });
    }, 40);
  });
}

/* 画布尺寸跟着布局走：窗口拉伸、抽屉开关都会变 */
let l2dRoTimer = null;
function l2dScheduleResize() {
  if (l2dRoTimer) clearTimeout(l2dRoTimer);
  l2dRoTimer = setTimeout(() => {
    if (!L2D.app || !L2D.model || !L2D.canvas) return;
    // 同上：布局尺寸，不带呼吸缩放的 transform
    const w = L2D.canvas.offsetWidth, h = L2D.canvas.offsetHeight;
    if (w < 2 || h < 2) return;
    L2D.app.renderer.resize(w, h);
    l2dFit();
  }, 140);
}
window.addEventListener('resize', l2dScheduleResize);

/* ---------------- 入口 ---------------- */
async function l2dReload() {
  // 不在这里同步 destroy：真正的销毁在队列里做，避免和正在跑的加载抢画布
  L2D.status = 'idle';
  L2D._failedPath = '';
  L2D._path = '';
  return l2dApply(true);
}

function l2dUseCdn(on) {
  localStorage.setItem('agent.l2d.cdn', on ? '1' : '0');
  l2dReload();
}
