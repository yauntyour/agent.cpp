'use strict';
/* =====================================================================
   04-panels —— 抽屉框架、记忆、角色编辑、会话、全局按钮
   ===================================================================== */

const DRAWER = { current: null, title: '' };

function openDrawer(title, name, build) {
  DRAWER.current = name;
  DRAWER.title = title;
  $('#drawer-title').textContent = title;
  $('#drawer').classList.remove('hidden');
  $('#scrim').classList.remove('hidden');
  DRAWER.build = build;
  buildDrawer();
}
function buildDrawer() {
  if (!DRAWER.build) return;
  const body = $('#drawer-body');
  body.innerHTML = '';
  DRAWER.build(body);
}
function paintDrawer() {
  if ($('#drawer').classList.contains('hidden')) return;
  // 舞台面板的规则编辑器里正在填表：状态广播来了也别把它冲掉
  if (DRAWER.current === 'stage' && typeof STAGE_VIEW !== 'undefined' && STAGE_VIEW.kind !== 'list') return;
  buildDrawer();
}
function closeDrawer() {
  $('#drawer').classList.add('hidden');
  $('#scrim').classList.add('hidden');
  DRAWER.current = null;
  DRAWER.build = null;
}
$('#btn-drawer-close').onclick = closeDrawer;
$('#scrim').onclick = closeDrawer;
document.addEventListener('keydown', (e) => {
  if (e.key !== 'Escape') return;
  // 抽屉开着先关抽屉；没有抽屉又在回应，Esc 就是「停止生成」
  if (!$('#drawer').classList.contains('hidden')) { closeDrawer(); return; }
  if (S.busy) stopTurn();
});

/* ---------------- 表单小件 ---------------- */
function field(label, input, tip) {
  const f = el('div', 'field');
  if (label) f.appendChild(el('label', null, label));
  f.appendChild(input);
  if (tip) f.appendChild(el('div', 'tip', tip));
  return f;
}
function textInput(value, placeholder, type = 'text') {
  const i = document.createElement('input');
  i.type = type;
  i.value = value == null ? '' : value;
  if (placeholder) i.placeholder = placeholder;
  return i;
}
function textArea(value, rows = 3, placeholder) {
  const t = document.createElement('textarea');
  t.rows = rows;
  t.value = value == null ? '' : value;
  if (placeholder) t.placeholder = placeholder;
  return t;
}
function switchRow(k, d, on, onToggle) {
  const row = el('div', 'switch-row');
  const left = el('div');
  left.appendChild(el('div', 'k', k));
  if (d) left.appendChild(el('div', 'd', d));
  const sw = el('button', 'switch' + (on ? ' on' : ''));
  sw.onclick = async () => {
    const next = !sw.classList.contains('on');
    sw.classList.toggle('on', next);
    try { await onToggle(next); } catch (e) { sw.classList.toggle('on', !next); toast(e.message, 'err'); }
  };
  row.append(left, sw);
  return row;
}
function actionBtn(label, fn, cls = 'btn') {
  const b = el('button', cls, label);
  b.onclick = async () => {
    b.disabled = true;
    try { await fn(); } catch (e) { toast(e.message, 'err'); }
    b.disabled = false;
  };
  return b;
}

/* ---------------- 记忆 ---------------- */
function openMemory() {
  openDrawer('记忆', 'memory', (body) => {
    const sect = el('div', 'sect');
    sect.appendChild(el('div', 'sect-h', `她记得的 ${S.memory.length} 件事`));
    if (!S.memory.length) {
      sect.appendChild(el('div', 'empty', '还什么都没记住。聊得久了，或者直接写一条。'));
    }
    for (const m of S.memory.slice().reverse()) {
      const row = el('div', 'list-row');
      const grow = el('div', 'grow', m.text);
      const when = el('div', 'when', fmtDay(m.t));
      const del = el('button', 'del', '');
      del.innerHTML = icon('trash');
      del.title = '忘掉这条';
      del.onclick = async () => {
        await cmd('memory.forget', { text: m.text });
        await refreshState();
      };
      row.append(grow, when, del);
      sect.appendChild(row);
    }
    body.appendChild(sect);

    const add = el('div', 'sect');
    add.appendChild(el('div', 'sect-h', '手动添加'));
    const input = textArea('', 2, '例如：她喜欢在晚上十点以后聊天');
    add.appendChild(field('', input, '一行一条；写进来的内容每次对话都会带上。'));
    add.appendChild(el('div', 'btn-row',
      actionBtn('记住', async () => {
        const text = input.value.trim();
        if (!text) return;
        await cmd('memory.add', { text });
        input.value = '';
        await refreshState();
      }, 'btn primary')
    ));
    const clr = actionBtn('清空全部记忆', async () => {
      if (!confirm('清空她的全部长期记忆？')) return;
      await cmd('memory.clear');
      await refreshState();
    }, 'btn danger');
    add.appendChild(el('div', 'btn-row', clr));
    body.appendChild(add);
  });
}

/* ---------------- 角色编辑 ---------------- */
function openPersonaEditor(creating) {
  const p = creating ? { name: '', subtitle: '', greeting: '', user_name: '', prompt: '', accent: '#8b7cff', voice: '', proactive: false, idle_minutes: 30 } : (S.persona || {});
  // 新建时的默认模型要扫描一次资源清单（空角色启动时舞台还没拿过）；
  // 等扫描回来再开抽屉，表单里才填得上模型路径
  const open = () => openDrawer(creating ? '新建角色' : '角色设定', 'persona', (body) => {
    const av = el('div', 'avatar-edit');
    const prev = el('div', 'preview');
    if (p.avatar_url) prev.innerHTML = `<img src="${esc(p.avatar_url)}">`;
    else prev.textContent = (p.name || '?').slice(0, 1);
    av.appendChild(prev);
    const pick = actionBtn('换头像', () => { $('#avatar-file').click(); }, 'btn sm');
    av.appendChild(pick);
    body.appendChild(av);

    const form = el('div', 'sect');
    const name = textInput(p.name, '她叫什么');
    const sub = textInput(p.subtitle, '一句话介绍');
    const hello = textArea(p.greeting, 2, '第一次打开时她说的话');
    const user = textInput(p.user_name, '她怎么称呼你');
    const prompt = textArea(p.prompt, 8, '人设：性格、说话方式、边界…');
    const voice = textInput(p.voice, '留空 = 用服务默认音色');
    const swatches = el('div', 'swatches');
    const ACCENTS = ['#8b7cff', '#f28ab7', '#62c9e8', '#64d6a4', '#e8bb63', '#f0777f', '#b98cff', '#7ea6ff'];
    let accent = p.accent || ACCENTS[0];
    for (const c of ACCENTS) {
      const s = el('button', 'swatch' + (c === accent ? ' on' : ''));
      s.style.background = c;
      s.onclick = () => {
        accent = c;
        $$('.swatch', swatches).forEach(x => x.classList.remove('on'));
        s.classList.add('on');
        applyAccent(c);
      };
      swatches.appendChild(s);
    }
    const idle = textInput(p.idle_minutes || 30, '30', 'number');
    const models = (L2D.scan && L2D.scan.models) || [];
    form.append(
      field('名字', name),
      field('副标题', sub),
      field('开场白', hello, '只在空会话里由界面展示，不会占用上下文。'),
      field('她怎么称呼你', user),
      field('人设', prompt, '写清楚性格、语气、边界。演出协议由程序自动追加。'),
      field('音色（可选）', voice, '对应语音服务里的 voice 参数；例如 alloy / zh-CN-XiaoxiaoNeural。'),
      field('主题色', swatches),
    );
    if (creating && models.length) {
      form.appendChild(field('舞台模型', (() => {
        const t = textInput(models[0].path, '');
        t.disabled = true;
        return t;
      })(), '新角色默认用自带的这个模型；建好后在舞台面板里可以换。'));
    }
    body.appendChild(form);

    const behavior = el('div', 'sect');
    behavior.appendChild(el('div', 'sect-h', '相处方式'));
    let proactive = !!p.proactive;
    behavior.appendChild(switchRow('主动关怀', '安静太久时，她会先开口。', proactive, async (on) => { proactive = on; }));
    behavior.appendChild(field('静默多少分钟后开口', idle));
    body.appendChild(behavior);

    const save = actionBtn(creating ? '创建' : '保存', async () => {
      const patch = {
        name: name.value.trim() || '未命名',
        subtitle: sub.value.trim(),
        greeting: hello.value.trim(),
        user_name: user.value.trim(),
        prompt: prompt.value,
        voice: voice.value.trim(),
        accent,
        proactive,
        idle_minutes: Math.max(1, parseInt(idle.value || '30', 10) || 30),
      };
      const args = creating ? { patch } : { id: p.id, patch };
      // 新角色直接挂上自带模型：建完就能在舞台上看见人
      if (creating) {
        const models = (L2D.scan && L2D.scan.models) || [];
        if (models.length) patch.live2d = { model: models[0].path };
      }
      const r = await cmd('persona.save', args);
      if (creating) await cmd('persona.select', { id: r.id });
      await refreshState();
      toast(creating ? '角色已创建' : '已保存', 'ok');
      closeDrawer();
    }, 'btn primary');
    body.appendChild(el('div', 'btn-row', save));

    if (!creating) {
      const del = actionBtn('删除这个角色', async () => {
        if (!confirm(`删除「${p.name}」以及她的会话与记忆？`)) return;
        await cmd('persona.delete', { id: p.id });
        await refreshState();
        closeDrawer();
      }, 'btn danger sm');
      body.appendChild(el('div', 'btn-row', del));
    }
  });

  if (creating && !L2D.scan && !MOCK) {
    cmd('live2d.scan').then(s => { L2D.scan = s; open(); }).catch(() => open());
  } else {
    open();
  }
}

/* 头像上传 */
$('#avatar-file').addEventListener('change', async (e) => {
  const f = e.target.files && e.target.files[0];
  e.target.value = '';
  if (!f) return;
  if (f.size > 8 * 1024 * 1024) { toast('图片太大（上限 8MB）', 'err'); return; }
  const buf = await f.arrayBuffer();
  let bin = '';
  const bytes = new Uint8Array(buf);
  for (let i = 0; i < bytes.length; i += 0x8000)
    bin += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  const b64 = btoa(bin);
  const ext = (f.name.split('.').pop() || 'png').toLowerCase();
  try {
    await cmd('persona.avatar', { id: (S.persona || {}).id, data_b64: b64, ext });
    await refreshState();
    toast('头像已更新', 'ok');
  } catch (err) { toast(err.message, 'err'); }
});

/* ---------------- 会话 ---------------- */
async function openSessions() {
  try {
    const r = await cmd('sessions.list');
    S.sessions = r.sessions || [];
  } catch (e) { toast(e.message, 'err'); }
  openDrawer('会话记录', 'sessions', (body) => {
    const sect = el('div', 'sect');
    sect.appendChild(el('div', 'sect-h', '这个角色的会话'));
    if (!S.sessions.length) sect.appendChild(el('div', 'empty', '还没有记录'));
    for (const s of S.sessions) {
      const row = el('div', 'list-row');
      const grow = el('div', 'grow');
      grow.appendChild(el('div', null, s.current ? '当前会话' : '历史会话'));
      grow.appendChild(el('div', 'when', `${fmtDay(s.mtime)} · ${s.id.slice(0, 18)}…`));
      if (s.current) grow.style.color = 'var(--accent)';
      const del = el('button', 'del');
      del.innerHTML = icon('trash');
      del.onclick = async () => {
        if (!confirm('删除这条会话记录？')) return;
        await cmd('session.delete', { id: s.id });
        await refreshState();
        openSessions();
      };
      row.append(grow, del);
      if (!s.current) {
        row.style.cursor = 'pointer';
        row.onclick = async () => {
          try {
            await cmd('session.select', { id: s.id });
            await refreshState();
            closeDrawer();
          } catch (e) { toast(e.message, 'err'); }
        };
      }
      sect.appendChild(row);
    }
    body.appendChild(sect);
    body.appendChild(el('div', 'btn-row', actionBtn('开一条新会话', async () => {
      await cmd('session.new');
      await refreshState();
      closeDrawer();
    }, 'btn primary')));
  });
}

/* ---------------- 全局按钮 ---------------- */
function wireGlobal() {
  $('#btn-send').onclick = () => (S.busy ? stopTurn() : sendCurrent());
  $('#btn-mic').onclick = toggleMic;
  $('#btn-voice').onclick = async () => {
    try {
      const r = await cmd('ui.pref', { voice_reply: !S.voice_reply });
      S.voice_reply = r.voice_reply;
      if (!S.voice_reply) stopAudio();
      paintHead();
    } catch (e) { toast(e.message, 'err'); }
  };
  $('#btn-new-session').onclick = async () => {
    stopAudio();
    try {
      await cmd('session.new');
      await refreshState();
    } catch (e) { toast(e.message, 'err'); }
  };
  $('#btn-sessions').onclick = openSessions;
  $('#btn-memory').onclick = openMemory;
  $('#btn-settings').onclick = openSettings;
  $('#btn-edit-persona').onclick = () => openPersonaEditor(false);
  $('#btn-new-persona').onclick = () => openPersonaEditor(true);
  $('#btn-stage').onclick = () => openStagePanel();
  $('#btn-proactive').onclick = async () => {
    const p = S.persona || {};
    try {
      await cmd('persona.save', { id: p.id, patch: { proactive: !p.proactive } });
      await refreshState();
    } catch (e) { toast(e.message, 'err'); }
  };
  const input = $('#input');
  input.addEventListener('input', autoGrow);
  input.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && !e.shiftKey && !e.isComposing) {
      e.preventDefault();
      sendCurrent();
    }
  });
}
