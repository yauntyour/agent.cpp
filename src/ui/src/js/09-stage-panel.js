'use strict';
/* =====================================================================
   09-stage-panel —— 舞台面板：模型、情绪映射、触发器
   =====================================================================
   这个面板是「演出」的控制台，三件事都能在这里改完即时生效：
   换模型（扫描 .agent/live2d/models/）、情绪→表情/动作映射、触发器规则。
   规则编辑器是表单式的，JSON 编辑是逃生舱口。
   ===================================================================== */

let STAGE_VIEW = { kind: 'list' };

function openStagePanel() {
  STAGE_VIEW = { kind: 'list' };
  openDrawer('舞台', 'stage', (body) => buildStagePanel(body));
}

function stageGo(view) {
  STAGE_VIEW = view || { kind: 'list' };
  buildDrawer();
  const body = $('#drawer-body');
  if (body) body.scrollTop = 0;   // 换视图时别把上一屏的滚动位置带过来
}

function buildStagePanel(body) {
  if (STAGE_VIEW.kind === 'rule') return buildTriggerEditor(body, STAGE_VIEW.rule, STAGE_VIEW.isNew);
  if (STAGE_VIEW.kind === 'json') return buildTriggerJson(body);
  body.appendChild(stageStatusSection());
  body.appendChild(stageModelSection());
  body.appendChild(stageEmoSection());
  body.appendChild(stageTriggerSection());
}

/* ---------------- 状态 ---------------- */
function stageStatusSection() {
  const s = el('div', 'sect');
  s.appendChild(el('div', 'sect-h', '舞台形态'));

  const card = el('div', 'card' + (L2D.status === 'ready' ? ' on' : ''));
  const top = el('div', 'card-top');
  const title = L2D.status === 'ready' ? 'Live2D' : (L2D.status === 'loading' ? 'Live2D · 加载中' : '光球');
  top.appendChild(el('div', 'card-name', title));
  const badge = el('span', 'badge ' + (L2D.status === 'ready' ? 'ok' : (L2D.status === 'error' ? 'warn' : '')),
    L2D.status === 'ready' ? '运行中' : (L2D.status === 'error' ? '出错' : '待接入'));
  top.appendChild(badge);
  card.appendChild(top);

  const model = (l2dCfg().model || '').trim();
  const sub = el('div', 'card-sub');
  if (model) sub.textContent = model;
  else sub.textContent = '还没有选模型。把模型目录放进 .agent/live2d/models/，然后在上面的列表里选一个。';
  card.appendChild(sub);

  const rt = el('div', 'card-sub');
  if (L2D.status === 'ready') {
    rt.textContent = `运行时：${L2D.runtimeFrom === 'cdn' ? 'CDN' : '本地'} · 动作组 ${L2D.groups.length} 个 · 表情 ${L2D.expressions.length} 个`;
  } else {
    const r = L2D.scan && L2D.scan.runtime;
    rt.textContent = r && r.ok ? '运行时：本地已就绪' : '运行时：缺失（' + ((r && r.missing) || []).join(', ') + '）';
  }
  card.appendChild(rt);
  s.appendChild(card);

  const row = el('div', 'btn-row');
  row.appendChild(actionBtn('重新加载', async () => { await l2dReload(); buildDrawer(); }, 'btn sm'));
  row.appendChild(actionBtn('扫描资源', async () => {
    L2D.scan = await cmd('live2d.scan');
    toast(`找到 ${(L2D.scan.models || []).length} 个模型`, 'ok');
    buildDrawer();
  }, 'btn sm'));
  row.appendChild(actionBtn('打开资源目录', async () => { await cmd('live2d.open_dir'); }, 'btn sm ghost'));
  s.appendChild(row);

  const cdnRow = el('div', 'switch-row');
  const left = el('div');
  left.appendChild(el('div', 'k', '运行时走 CDN'));
  left.appendChild(el('div', 'd', '本地没有放 Cubism Core 时，从 unpkg 现取（要联网）。'));
  const sw = el('button', 'switch' + (localStorage.getItem('agent.l2d.cdn') === '1' ? ' on' : ''));
  sw.onclick = () => {
    const on = !sw.classList.contains('on');
    sw.classList.toggle('on', on);
    l2dUseCdn(on);
    setTimeout(() => buildDrawer(), 900);
  };
  cdnRow.append(left, sw);
  s.appendChild(cdnRow);

  const tip = el('div', 'tip');
  tip.innerHTML = '目录约定：<code>.agent/live2d/runtime/</code>（pixi.min.js、live2dcubismcore.min.js、cubism4.min.js，'
    + '可用 manifest.json 指定清单）、<code>.agent/live2d/models/&lt;任意&gt;/</code>（model3.json 及其资源）。';
  s.appendChild(tip);
  return s;
}

/* ---------------- 模型 ---------------- */
function stageModelSection() {
  const s = el('div', 'sect');
  s.appendChild(el('div', 'sect-h', '模型'));
  const cfg = l2dCfg();
  const models = (L2D.scan && L2D.scan.models) || [];

  const sel = document.createElement('select');
  const opt0 = document.createElement('option');
  opt0.value = '';
  opt0.textContent = models.length ? '— 选一个模型 —' : '（models/ 下还没有模型）';
  sel.appendChild(opt0);
  for (const m of models) {
    const o = document.createElement('option');
    o.value = m.path;
    o.textContent = m.name + '  ·  ' + m.dir;
    if (m.path === cfg.model) o.selected = true;
    sel.appendChild(o);
  }
  s.appendChild(field('扫描到的模型', sel, '选完点下面的「保存」；也可以直接手填路径。'));

  const path = textInput(cfg.model || '', 'models/hiyori/hiyori.model3.json');
  s.appendChild(field('模型路径', path, '相对 .agent/live2d/ 的路径，区分大小写。'));

  const scale = textInput(cfg.scale == null ? 1 : cfg.scale, '1', 'number');
  const ox = textInput(cfg.x == null ? 0 : cfg.x, '0', 'number');
  const oy = textInput(cfg.y == null ? 0 : cfg.y, '0', 'number');
  const row = el('div', 'row');
  row.append(field('缩放', scale), field('水平偏移', ox));
  s.appendChild(row);
  s.appendChild(field('垂直偏移', oy, '缩放 1 = 尽量填满画布；偏移是画布宽/高的比例（0.1 = 10%）。'));

  sel.onchange = () => { if (sel.value) path.value = sel.value; };

  s.appendChild(el('div', 'btn-row',
    actionBtn('保存并加载', async () => {
      const next = Object.assign({}, cfg, {
        model: path.value.trim(),
        scale: Number(scale.value) || 1,
        x: Number(ox.value) || 0,
        y: Number(oy.value) || 0,
      });
      await cmd('persona.save', { id: (S.persona || {}).id, patch: { live2d: next } });
      S.persona.live2d = next;
      L2D.cfg = next;
      L2D._path = '';
      await l2dReload();
      toast(next.model ? '模型已加载' : '已切回光球', 'ok');
      buildDrawer();
    }, 'btn primary sm'),
    actionBtn('切回光球', async () => {
      const next = Object.assign({}, cfg, { model: '' });
      await cmd('persona.save', { id: (S.persona || {}).id, patch: { live2d: next } });
      S.persona.live2d = next;
      L2D.cfg = next;
      L2D._path = '';
      await l2dReload();
      buildDrawer();
    }, 'btn sm ghost')
  ));
  return s;
}

/* ---------------- 情绪映射 ---------------- */
function stageEmoSection() {
  const s = el('div', 'sect');
  s.appendChild(el('div', 'sect-h', '情绪 → 表情 / 动作'));
  const cfg = l2dCfg();
  const table = cfg.emo && typeof cfg.emo === 'object' ? cfg.emo : trigClone(L2D_EMO_DEFAULT);
  const exprs = L2D.expressions || [];
  const groups = L2D.groups || [];

  const draft = {};
  for (const name of Object.keys(EMO_ZH)) {
    const cur = table[name] || {};
    draft[name] = { expr: cur.expr == null ? '' : String(cur.expr), motion: Array.isArray(cur.motion) ? cur.motion[0] : (cur.motion || '') };
    const row = el('div', 'map-row');
    row.appendChild(el('span', 'map-k', EMO_ZH[name]));

    const exprSel = document.createElement('select');
    const e0 = document.createElement('option');
    e0.value = '';
    e0.textContent = '表情：不改';
    exprSel.appendChild(e0);
    exprs.forEach((n, i) => {
      const o = document.createElement('option');
      o.value = String(i);
      o.textContent = '表情：' + n;
      if (String(draft[name].expr) === String(i) || String(draft[name].expr) === n) o.selected = true;
      exprSel.appendChild(o);
    });
    if (!exprs.length) exprSel.disabled = true;
    exprSel.onchange = () => { draft[name].expr = exprSel.value; };

    const moSel = document.createElement('select');
    const m0 = document.createElement('option');
    m0.value = '';
    m0.textContent = '动作：不改';
    moSel.appendChild(m0);
    for (const g of groups) {
      const o = document.createElement('option');
      o.value = g;
      o.textContent = '动作：' + g;
      if (g === draft[name].motion) o.selected = true;
      moSel.appendChild(o);
    }
    moSel.onchange = () => { draft[name].motion = moSel.value; };

    row.append(exprSel, moSel);
    s.appendChild(row);
  }

  s.appendChild(el('div', 'btn-row',
    actionBtn('保存映射', async () => {
      const emo = {};
      for (const k of Object.keys(draft)) {
        const one = {};
        if (draft[k].expr !== '') one.expr = /^\d+$/.test(draft[k].expr) ? Number(draft[k].expr) : draft[k].expr;
        if (draft[k].motion) one.motion = draft[k].motion;
        if (Object.keys(one).length) emo[k] = one;
      }
      const next = Object.assign({}, cfg, { emo });
      await cmd('persona.save', { id: (S.persona || {}).id, patch: { live2d: next } });
      S.persona.live2d = next;
      L2D.cfg = next;
      toast('映射已保存', 'ok');
    }, 'btn primary sm'),
    actionBtn('逐个试', async () => {
      for (const k of Object.keys(EMO_ZH)) {
        setEmotion(k, true);
        await new Promise(r => setTimeout(r, 900));
      }
    }, 'btn sm ghost')
  ));
  if (!L2D.expressions.length)
    s.appendChild(el('div', 'tip', L2D.status === 'ready'
      ? '这个模型没有表情数据（有些样本模型只有动作组），那就只挂动作。'
      : '模型还没加载，加载后这里会列出真实的表情名。'));
  return s;
}

/* ---------------- 触发器 ---------------- */
function trigSummary(rule) {
  const w = rule.when || rule.match || {};
  const bits = [];
  if (w.zone != null) bits.push('区域=' + [].concat(w.zone).join('/'));
  if (w.emotion != null) bits.push('情绪=' + [].concat(w.emotion).join('/'));
  if (w.act != null) bits.push('动作=' + [].concat(w.act).join('/'));
  if (w.group != null) bits.push('组=' + [].concat(w.group).join('/'));
  if (w.text != null) bits.push('文字≈' + w.text);
  if (w.min_idle != null) bits.push('静默>' + w.min_idle + 's');
  return bits.join(' · ') || '无条件';
}

function trigDoSummary(rule) {
  const acts = rule.do || rule.actions || [];
  return acts.map(a => {
    const t = a.a || a.type;
    if (t === 'motion') return '动作 ' + (a.group || '?');
    if (t === 'expr') return '表情 ' + (a.id != null ? a.id : a.name);
    if (t === 'say') return '台词「' + String(a.text || '').slice(0, 10) + '」';
    if (t === 'send') return '让她回话';
    if (t === 'idle') return '主动开口';
    if (t === 'speak') return '朗读';
    if (t === 'emo') return '情绪 ' + (a.name || '');
    if (t === 'wait') return '等 ' + (a.ms || 0) + 'ms';
    return t || '?';
  }).join(' → ');
}

const TRIG_EVENTS = [
  ['click', '戳一下（舞台点击）'],
  ['emotion', '情绪变化'],
  ['act', '星号动作 [[act:]]'],
  ['motion', '模型动作 [[motion:]]'],
  ['expr', '模型表情 [[expr:]]'],
  ['reply', '回复正文命中文字'],
  ['user', '你发出的消息'],
  ['idle', '安静太久'],
  ['turn_begin', '回合开始'],
  ['turn_end', '回合结束'],
  ['boot', '舞台就绪'],
];

function stageTriggerSection() {
  const s = el('div', 'sect');
  const head = el('div', 'sect-h', '触发器');
  s.appendChild(head);
  const list = TRIG.list;
  if (TRIG.builtin) {
    s.appendChild(el('div', 'tip', '现在用的是内置默认规则（角色卡里还没有存过触发器）。改任何一条都会写进角色卡。'));
  }
  if (!list.length) s.appendChild(el('div', 'empty', '还没有规则'));

  list.forEach((rule, i) => {
    const row = el('div', 'trig-row' + (rule.off ? ' off' : ''));
    const main = el('div', 'trig-main');
    const t1 = el('div', 'trig-t');
    t1.appendChild(el('span', 'trig-on', (TRIG_EVENTS.find(e => e[0] === rule.on) || [rule.on, rule.on])[1]));
    t1.appendChild(el('span', 'trig-name', rule.note || rule.id || ''));
    main.appendChild(t1);
    main.appendChild(el('div', 'trig-sub', trigSummary(rule)));
    main.appendChild(el('div', 'trig-sub2', trigDoSummary(rule)));
    row.appendChild(main);

    const tools = el('div', 'trig-tools');
    const run = el('button', 'del', '试');
    run.title = '试一下这条规则';
    run.onclick = () => { TRIG.test(rule); toast('试了一次：' + (rule.note || rule.id || ''), '', 1600); };
    const edit = el('button', 'del', '改');
    edit.title = '编辑';
    edit.onclick = () => stageGo({ kind: 'rule', rule: trigClone(rule), isNew: false, index: i });
    const del = el('button', 'del', '×');
    del.title = '删除';
    del.onclick = async () => {
      const next = trigClone(TRIG.list);
      next.splice(i, 1);
      try { await TRIG.save(next); toast('已删除', 'ok'); buildDrawer(); }
      catch (e) { toast(e.message, 'err'); }
    };
    tools.append(run, edit, del);
    row.appendChild(tools);
    s.appendChild(row);
  });

  s.appendChild(el('div', 'btn-row',
    actionBtn('新建规则', () => stageGo({ kind: 'rule', rule: trigNewRule(), isNew: true }), 'btn primary sm'),
    actionBtn('JSON 编辑', () => stageGo({ kind: 'json' }), 'btn sm ghost'),
    actionBtn('恢复内置默认', async () => {
      if (!confirm('清掉角色卡里的触发器，回到内置默认？')) return;
      try { await TRIG.save([]); toast('已恢复默认', 'ok'); buildDrawer(); }
      catch (e) { toast(e.message, 'err'); }
    }, 'btn sm danger')
  ));
  return s;
}

/* ---------------- 规则编辑器 ---------------- */
function trigNewRule() {
  return {
    id: 'rule' + Date.now().toString(36).slice(-4),
    on: 'click',
    when: { zone: 'head' },
    do: [{ a: 'motion', group: (L2D.groups[0] || 'TapHead') }, { a: 'say', text: '' }],
    cd: 5,
  };
}

function buildTriggerEditor(body, rule, isNew) {
  const r = trigClone(rule || trigNewRule());
  r.when = r.when || r.match || {};
  r.do = r.do || r.actions || [];
  if (!r.do.length) r.do = [{ a: 'motion' }];

  const back = el('div', 'btn-row');
  back.appendChild(actionBtn('← 回到列表', () => stageGo({ kind: 'list' }), 'btn sm ghost'));
  body.appendChild(back);

  // 动作 / 表情的候选：从模型里读到的真实名字，输入框里直接补全
  const dlG = document.createElement('datalist');
  dlG.id = 'l2d-groups';
  for (const g of (L2D.groups || [])) {
    const o = document.createElement('option');
    o.value = g;
    dlG.appendChild(o);
  }
  const dlE = document.createElement('datalist');
  dlE.id = 'l2d-exprs';
  for (const e of (L2D.expressions || [])) {
    const o = document.createElement('option');
    o.value = e;
    dlE.appendChild(o);
  }
  body.append(dlG, dlE);

  const s1 = el('div', 'sect');
  s1.appendChild(el('div', 'sect-h', isNew ? '新规则' : '编辑规则'));
  const note = textInput(r.note || '', '例如：摸头会躲');
  s1.appendChild(field('名字（给自己看的）', note));
  const onSel = document.createElement('select');
  for (const [v, label] of TRIG_EVENTS) {
    const o = document.createElement('option');
    o.value = v;
    o.textContent = label;
    if (v === r.on) o.selected = true;
    onSel.appendChild(o);
  }
  onSel.onchange = () => {
    r.on = onSel.value;
    // 换事件时把这条事件用不上的条件清掉：留着「点击区域=head」的回复规则
    // 永远不会命中，比清空更让人困惑
    const keep = {
      click: ['zone'], emotion: ['emotion'], act: ['act'], motion: ['group'],
      expr: ['expr'], reply: ['text'], user: ['text'], idle: ['min_idle'],
    }[r.on] || [];
    const drop = (key, input) => { if (!keep.includes(key)) input.value = ''; };
    drop('zone', zone); drop('emotion', emotion); drop('act', act);
    drop('group', group); drop('text', text); drop('min_idle', minIdle);
  };
  s1.appendChild(field('什么时候触发', onSel));
  body.appendChild(s1);

  const s2 = el('div', 'sect');
  s2.appendChild(el('div', 'sect-h', '条件（留空 = 不限制）'));
  const zone = textInput(joinCond(r.when.zone), 'head, body, hand');
  const emotion = textInput(joinCond(r.when.emotion), 'happy, shy');
  const act = textInput(joinCond(r.when.act), 'hug, headpat');
  const group = textInput(joinCond(r.when.group), 'TapHead');
  const text = textInput(r.when.text || '', '晚安 或 /晚安|睡了/');
  const minIdle = textInput(r.when.min_idle == null ? '' : r.when.min_idle, '60', 'number');
  s2.append(
    field('点击区域 zone', zone, '模型声明了 HitAreas 时是区域名；否则 head / body / hand。'),
    field('情绪 emotion', emotion, '命中 [[emo:…]] 的名字，逗号分隔。'),
    field('星号动作 act', act, '命中 *抱住你* 这类动作，逗号分隔。'),
    field('动作组 group', group, '命中模型播放的动作组，逗号分隔。'),
    field('正文 text', text, '子串，或写成 /正则/ 形式。'),
    field('静默秒数 min_idle', minIdle, '只对「安静太久」有意义：安静超过这么多秒才触发。')
  );
  body.appendChild(s2);

  const s3 = el('div', 'sect');
  s3.appendChild(el('div', 'sect-h', '动作（按顺序执行）'));
  const actsBox = el('div', 'acts');
  const rows = [];
  const addActRow = (act) => {
    const row = el('div', 'act-row');
    const head = el('div', 'act-head');
    const sel = document.createElement('select');
    for (const t of ['motion', 'expr', 'param', 'emo', 'say', 'speak', 'send', 'idle', 'fx', 'sound', 'toast', 'wait']) {
      const o = document.createElement('option');
      o.value = t;
      o.textContent = t;
      if ((act.a || act.type) === t) o.selected = true;
      sel.appendChild(o);
    }
    const rm = el('button', 'del', '×');
    rm.title = '删掉这个动作';
    rm.onclick = () => {
      const i = rows.indexOf(entry);
      if (i >= 0) { rows.splice(i, 1); row.remove(); }
    };
    head.append(sel, rm);
    const bodyBox = el('div', 'act-body');
    const entry = { act, sel, body: bodyBox, row };
    rows.push(entry);

    const render = () => {
      const t = sel.value;
      act.a = t;
      delete act.type;
      bodyBox.innerHTML = '';
      if (t === 'motion') {
        const g = textInput(act.group || '', 'TapHead');
        g.setAttribute('list', 'l2d-groups');
        const idx = textInput(act.index == null ? '' : act.index, '0', 'number');
        g.oninput = () => { act.group = g.value; };
        idx.oninput = () => { act.index = idx.value === '' ? undefined : Number(idx.value); };
        bodyBox.append(field('动作组', g), field('第几个（可空）', idx));
      } else if (t === 'expr') {
        const id = textInput(act.id != null ? act.id : (act.name || ''), '0 或 F01');
        id.setAttribute('list', 'l2d-exprs');
        id.oninput = () => { act.id = id.value; delete act.name; };
        bodyBox.append(field('表情（序号或名字）', id));
      } else if (t === 'param') {
        const id = textInput(act.id || '', 'ParamAngleZ');
        const to = textInput(act.to == null ? 0 : act.to, '15', 'number');
        const ms = textInput(act.ms == null ? 400 : act.ms, '400', 'number');
        id.oninput = () => { act.id = id.value; };
        to.oninput = () => { act.to = Number(to.value) || 0; };
        ms.oninput = () => { act.ms = Number(ms.value) || 0; };
        bodyBox.append(field('参数 id', id), field('目标值', to), field('毫秒', ms));
      } else if (t === 'emo') {
        const sel2 = document.createElement('select');
        for (const k of Object.keys(EMO_ZH)) {
          const o = document.createElement('option');
          o.value = k;
          o.textContent = k + ' · ' + EMO_ZH[k];
          if (act.name === k) o.selected = true;
          sel2.appendChild(o);
        }
        sel2.onchange = () => { act.name = sel2.value; };
        act.name = act.name || 'happy';
        bodyBox.append(field('情绪', sel2));
      } else if (t === 'say' || t === 'speak' || t === 'send' || t === 'toast') {
        const tx = textArea(act.text || '', 2, t === 'send' ? '（你戳了戳她的手臂）' : '');
        tx.oninput = () => { act.text = tx.value; };
        bodyBox.append(field(t === 'send' ? '发给她的话' : '文字', tx,
          t === 'send' ? '会作为一条用户消息进入回合，她真的会回。' : ''));
      } else if (t === 'idle') {
        bodyBox.append(el('div', 'tip', '让她主动开口一次（等同于「主动关怀」那条 [[idle]]）。'));
      } else if (t === 'fx') {
        const sel2 = document.createElement('select');
        for (const v of ['pulse', 'shake', 'flash', 'dim']) {
          const o = document.createElement('option');
          o.value = v;
          o.textContent = v;
          if (act.name === v) o.selected = true;
          sel2.appendChild(o);
        }
        sel2.onchange = () => { act.name = sel2.value; };
        act.name = act.name || 'pulse';
        const ms = textInput(act.ms == null ? 600 : act.ms, '600', 'number');
        ms.oninput = () => { act.ms = Number(ms.value) || 600; };
        bodyBox.append(field('效果', sel2), field('毫秒', ms));
      } else if (t === 'sound') {
        const url = textInput(act.url || '', '/api/live2d/models/x/sound/hi.wav');
        const vol = textInput(act.volume == null ? 0.7 : act.volume, '0.7', 'number');
        url.oninput = () => { act.url = url.value; };
        vol.oninput = () => { act.volume = Number(vol.value) || 0.7; };
        bodyBox.append(field('音频地址', url, '可以是 /api/live2d/ 下的文件，也可以是外链。'), field('音量', vol));
      } else if (t === 'wait') {
        const ms = textInput(act.ms == null ? 300 : act.ms, '300', 'number');
        ms.oninput = () => { act.ms = Number(ms.value) || 0; };
        bodyBox.append(field('毫秒', ms));
      }
    };
    sel.onchange = render;
    render();
    row.append(head, bodyBox);
    actsBox.appendChild(row);
  };
  for (const a of r.do) addActRow(a);
  s3.appendChild(actsBox);
  s3.appendChild(el('div', 'btn-row', actionBtn('+ 加一个动作', () => addActRow({ a: 'say', text: '' }), 'btn sm')));

  const cd = textInput(r.cd == null ? 3 : r.cd, '3', 'number');
  const once = el('button', 'switch' + (r.once ? ' on' : ''));
  once.onclick = () => { r.once = !once.classList.contains('on'); once.classList.toggle('on', r.once); };
  const onceRow = el('div', 'switch-row');
  const onceLeft = el('div');
  onceLeft.append(el('div', 'k', '只触发一次'), el('div', 'd', '本次页面会话内只算一次（刷新后重置）。'));
  onceRow.append(onceLeft, once);
  s3.append(field('冷却秒数 cd', cd, '同一规则两次触发之间至少隔这么久。'), onceRow);
  body.appendChild(s3);

  body.appendChild(el('div', 'btn-row',
    actionBtn(isNew ? '创建' : '保存', async () => {
      r.note = note.value.trim();
      r.when = {};
      const w = {};
      const put = (k, v) => { if (v) w[k] = v; };
      put('zone', splitCond(zone.value));
      put('emotion', splitCond(emotion.value));
      put('act', splitCond(act.value));
      put('group', splitCond(group.value));
      put('text', text.value.trim());
      if (minIdle.value !== '') w.min_idle = Number(minIdle.value) || 0;
      r.when = w;
      r.do = rows.map(x => x.act).filter(a => a && a.a);
      r.cd = Math.max(0, Number(cd.value) || 0);
      if (!r.do.length) { toast('至少要有 1 个动作', 'err'); return; }
      const next = trigClone(TRIG.list);
      const at = next.findIndex(x => x.id === r.id);
      if (at >= 0) next[at] = r; else next.push(r);
      try { await TRIG.save(next); toast(isNew ? '规则已创建' : '已保存', 'ok'); stageGo({ kind: 'list' }); }
      catch (e) { toast(e.message, 'err'); }
    }, 'btn primary'),
    actionBtn('试一下', () => TRIG.test(r), 'btn ghost'),
    actionBtn('取消', () => stageGo({ kind: 'list' }), 'btn ghost')
  ));
  return body;
}

function joinCond(v) {
  if (v == null) return '';
  return Array.isArray(v) ? v.join(', ') : String(v);
}
function splitCond(v) {
  const s = String(v || '').trim();
  if (!s) return '';
  const parts = s.split(/[,，\s]+/).filter(Boolean);
  return parts.length > 1 ? parts : parts[0] || '';
}

/* ---------------- JSON 编辑 ---------------- */
function buildTriggerJson(body) {
  const s = el('div', 'sect');
  s.appendChild(el('div', 'sect-h', '触发器 JSON'));
  const ta = textArea(JSON.stringify(TRIG.list, null, 2), 22, '');
  ta.classList.add('mono');
  s.appendChild(field('', ta, '整份数组直接改；保存前会做一次语法检查。字段说明见 README「演出协议」。'));
  body.appendChild(s);
  body.appendChild(el('div', 'btn-row',
    actionBtn('保存', async () => {
      let list;
      try { list = JSON.parse(ta.value); } catch (e) { toast('JSON 语法错误：' + e.message, 'err', 5000); return; }
      if (!Array.isArray(list)) { toast('最外层必须是数组', 'err'); return; }
      try { await TRIG.save(list); toast('已保存', 'ok'); stageGo({ kind: 'list' }); }
      catch (e) { toast(e.message, 'err'); }
    }, 'btn primary'),
    actionBtn('← 回到列表', () => stageGo({ kind: 'list' }), 'btn ghost')
  ));
  return body;
}
