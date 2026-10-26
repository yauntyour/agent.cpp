'use strict';
/* =====================================================================
   05-settings —— 设置：模型服务、模型、语音服务、IM 接入、偏好
   ===================================================================== */

/* 设置是一列模块：点哪一个进哪一个的子页（pushDrawer / drawerBack 在 04-panels）。
   加一个模块 = 在 SETTINGS_MODULES 里加一行，别的地方不用动。 */
const SETTINGS_MODULES = [
  { key: 'providers', name: '模型服务', sub: () => S.providers.length ? `${S.providers.length} 个 · ${S.provider || '未选'}` : '还没有配置', build: () => providerSection() },
  { key: 'model', name: '模型', sub: () => (S.model && S.model !== '(none)') ? S.model : '未选择', build: () => modelSection() },
  { key: 'tts', name: '语音合成', sub: () => voiceStateText(S.tts), build: () => serviceSection('tts', S.tts) },
  { key: 'asr', name: '语音识别', sub: () => voiceStateText(S.asr), build: () => serviceSection('asr', S.asr) },
  { key: 'im', name: 'IM 接入', sub: () => imStatusText(), build: () => imSection() },
  { key: 'stickers', name: '表情包', sub: () => stickerStatusText(), build: () => stickerSection() },
  { key: 'pref', name: '偏好', sub: () => `${S.voice_reply ? '自动朗读' : '手动朗读'} · ${S.autoallow ? '工具自动执行' : '工具逐次确认'}`, build: () => prefSection() },
];

/* 表情包列表由服务端随 state 一起下发；打开设置时再拉一次，防止别处加过没刷新 */
let STICKERS_LOADED = false;
const STICKER_EXTS = ['png', 'jpg', 'jpeg', 'gif', 'webp'];
const STICKER_MAX_BYTES = 8 * 1024 * 1024; // 和 app-core 的 kMaxSticker 一致

function stickerStatusText() {
  const n = (S.stickers || []).length;
  return n ? `${n} 个` : '还没有';
}

function voiceStateText(svc) {
  const kind = (svc && svc.kind) || 'off';
  return kind === 'off' ? '未开启' : kind;
}

function openSettings() {
  openDrawer('设置', 'settings', (body) => {
    const list = el('div', 'mod-list');
    for (const m of SETTINGS_MODULES) {
      const row = el('button', 'mod-row');
      const left = el('div', 'mod-main');
      left.appendChild(el('div', 'mod-name', m.name));
      left.appendChild(el('div', 'mod-sub', m.sub()));
      row.append(left, el('span', 'mod-go', '›'));
      row.onclick = () => pushDrawer(m.name, 'settings/' + m.key, (b) => {
        b.appendChild(drawerBack('设置'));
        b.appendChild(m.build());
      });
      list.appendChild(row);
    }
    body.appendChild(list);
  });
}

/* ---------------- 模型服务 ---------------- */
function providerSection() {
  const sect = el('div', 'sect');
  sect.appendChild(el('div', 'sect-h', '模型服务'));
  if (!S.providers.length) {
    sect.appendChild(el('div', 'empty', '还没有配置模型服务。填一个 OpenAI 兼容的地址就能开始。'));
  }
  for (const p of S.providers) {
    const card = el('div', 'card' + (p.current ? ' on' : ''));
    const top = el('div', 'card-top');
    const left = el('div');
    left.appendChild(el('div', 'card-name', p.name));
    left.appendChild(el('div', 'card-sub', `${p.api_style} · ${p.base}`));
    top.appendChild(left);
    const badges = el('div');
    badges.style.display = 'flex';
    badges.style.gap = '6px';
    badges.appendChild(el('span', 'badge' + (p.has_key ? ' ok' : ' warn'), p.has_key ? '已配密钥' : '缺密钥'));
    if (p.current) badges.appendChild(el('span', 'badge ok', '使用中'));
    top.appendChild(badges);
    card.appendChild(top);

    const row = el('div', 'btn-row');
    if (!p.current) row.appendChild(actionBtn('设为当前', async () => {
      await cmd('providers.select', { name: p.name });
      await refreshState();
    }, 'btn sm'));
    row.appendChild(actionBtn('换密钥', async () => {
      const k = prompt('输入新的 API Key');
      if (!k) return;
      await cmd('provider.set_key', { name: p.name, key: k });
      await refreshState();
      toast('密钥已更新', 'ok');
    }, 'btn sm'));
    row.appendChild(actionBtn('删除', async () => {
      if (!confirm(`删除供应商「${p.name}」？`)) return;
      await cmd('providers.rm', { name: p.name });
      await refreshState();
    }, 'btn sm danger'));
    card.appendChild(row);
    sect.appendChild(card);
  }

  const det = document.createElement('details');
  det.style.marginTop = '10px';
  const sum = el('summary', null, '＋ 添加模型服务');
  sum.style.cssText = 'cursor:pointer;font-size:12.5px;color:var(--muted);padding:6px 0';
  det.appendChild(sum);
  const form = el('div');
  form.style.marginTop = '8px';
  const name = textInput('', '名字，例如 deepseek');
  const style = document.createElement('select');
  for (const [v, t] of [['openai', 'OpenAI Chat Completions'], ['openai-responses', 'OpenAI Responses'], ['anthropic', 'Anthropic Messages']]) {
    const o = document.createElement('option');
    o.value = v; o.textContent = t;
    style.appendChild(o);
  }
  const base = textInput('', 'https://api.deepseek.com/v1');
  const key = textInput('', 'sk-…', 'password');
  const proxy = textInput('', '可选：http://127.0.0.1:7890');
  form.append(
    field('名字', name),
    field('协议', style),
    field('Base URL', base, '写到 /v1 为止；模型列表会去 <base>/models 拉。'),
    field('API Key', key, '加密保存在 .agent/.crypt 里，不落配置文件。'),
    field('代理', proxy),
  );
  form.appendChild(el('div', 'btn-row', actionBtn('添加', async () => {
    if (!base.value.trim()) throw new Error('需要 Base URL');
    await cmd('providers.add', {
      name: name.value.trim(), api_style: style.value,
      base: base.value.trim(), key: key.value, proxy: proxy.value.trim(),
    });
    await refreshState();
    toast('已添加', 'ok');
  }, 'btn primary')));
  det.appendChild(form);
  sect.appendChild(det);
  return sect;
}

/* ---------------- 模型 ---------------- */
function modelSection() {
  const sect = el('div', 'sect');
  sect.appendChild(el('div', 'sect-h', '模型'));
  const cur = el('div', 'card');
  const curLeft = el('div');
  curLeft.appendChild(el('div', 'card-name', S.model || '未选择模型'));
  curLeft.appendChild(el('div', 'card-sub', S.model ? '当前生效' : '选择供应商后拉取模型列表'));
  cur.appendChild(el('div', 'card-top', curLeft));
  sect.appendChild(cur);

  const listBox = el('div');
  listBox.style.marginTop = '8px';
  sect.appendChild(listBox);

  const fetchBtn = actionBtn('拉取模型列表', async () => {
    listBox.innerHTML = '';
    listBox.appendChild(el('div', 'empty', '正在拉取…'));
    const r = await cmd('models.fetch');
    const models = r.models || [];
    listBox.innerHTML = '';
    if (!models.length) { listBox.appendChild(el('div', 'empty', '没有拉到模型')); return; }
    const sel = document.createElement('select');
    for (const m of models) {
      const o = document.createElement('option');
      o.value = m; o.textContent = m;
      if (m === S.model) o.selected = true;
      sel.appendChild(o);
    }
    listBox.appendChild(field('', sel));
    listBox.appendChild(el('div', 'btn-row', actionBtn('使用这个模型', async () => {
      await cmd('model.set', { model: sel.value });
      await refreshState();
      toast('模型已切换', 'ok');
    }, 'btn primary sm')));
  }, 'btn');
  sect.appendChild(el('div', 'btn-row', fetchBtn));

  const seg = el('div', 'seg');
  const LABELS = ['关', '低', '中', '高', '最高'];
  LABELS.forEach((t, i) => {
    const b = el('button', i === S.thinking ? 'on' : '', t);
    b.onclick = async () => {
      try {
        await cmd('think.set', { level: i });
        await refreshState();
      } catch (e) { toast(e.message, 'err'); }
    };
    seg.appendChild(b);
  });
  sect.appendChild(field('思考强度', seg, '推理型模型会给出一段思考过程；越高越慢。'));
  return sect;
}

/* ---------------- 语音服务 ---------------- */
function serviceSection(which, svc) {
  const isTts = which === 'tts';
  const sect = el('div', 'sect');
  const head = el('div', 'sect-h', isTts ? '语音合成（TTS）' : '语音识别（ASR）');
  const on = (svc.kind || 'off') !== 'off';
  head.appendChild(el('span', 'badge' + (on ? ' ok' : ''), on ? '已开启' : '未开启'));
  sect.appendChild(head);

  const state = Object.assign({}, svc);
  const kinds = isTts
    ? [['off', '关闭'], ['openai', 'OpenAI 兼容'], ['http', 'JSON 模板'], ['command', '本地命令']]
    : [['off', '关闭'], ['openai', 'OpenAI 兼容'], ['command', '本地命令']];

  const seg = el('div', 'seg');
  const fields = el('div');
  const renderFields = () => {
    fields.innerHTML = '';
    if (state.kind === 'openai') {
      fields.append(
        field('Base URL', mk('base', 'https://api.openai.com/v1', '包含 /v1；本地服务写 http://127.0.0.1:9880/v1')),
        field('路径', mk('path', isTts ? '/audio/speech' : '/audio/transcriptions')),
        field('模型', mk('model', isTts ? 'tts-1' : 'whisper-1')),
      );
      if (isTts) {
        fields.append(field('默认音色', mk('voice', 'alloy')));
        const fmt = document.createElement('select');
        for (const f of ['mp3', 'wav', 'opus', 'aac', 'flac']) {
          const o = document.createElement('option');
          o.value = f; o.textContent = f;
          if ((state.format || 'mp3') === f) o.selected = true;
          fmt.appendChild(o);
        }
        fmt.onchange = () => { state.format = fmt.value; };
        fields.append(field('音频格式', fmt));
      } else {
        fields.append(
          field('语言', mk('language', 'zh')),
          field('提示词', mk('prompt', '可选：人名、专有名词，帮助识别')),
        );
      }
      fields.append(field('API Key', mk('key', 'sk-…', 'password'), state.has_key ? '已保存；留空则保持不变。' : ''));
    } else if (state.kind === 'http' && isTts) {
      fields.append(
        field('URL', mk('url', 'http://127.0.0.1:9880/tts', '完整地址')),
        field('请求体模板', mk('body', '{"text":"{text}","text_lang":"zh"}', 'JSON 模板；{text} 会被转义后填入，可再用 {voice} {model} {lang}')),
        field('音频格式', mk('format', 'wav')),
        field('API Key', mk('key', '可选', 'password')),
      );
    } else if (state.kind === 'command') {
      fields.append(
        field('命令', mk('cmd', isTts
          ? 'python tts.py --in {in} --out {out}'
          : 'whisper-cli -m model.bin -f {in} -otxt -of {out}',
          isTts ? '{in} 是 UTF-8 文本文件，{out} 是目标音频；也可用 {text} {voice}'
                : '{in} 是 WAV，{out} 是输出前缀；识别结果读 {out}.txt 或 stdout')),
        field('超时（秒）', mk('timeout_s', '60', 'number')),
      );
    }
    if (state.kind === 'off') {
      const empty = el('div', 'empty', isTts
        ? '未开启。语音合成是可选的 —— 不配置也能正常聊天，回复只显示文字。需要时在上面选一种接法，填好点「保存」。'
        : '未开启。语音识别是可选的 —— 不配置也能正常打字聊天。需要时在上面选一种接法，填好点「保存」。');
      empty.style.lineHeight = '1.7';
      fields.appendChild(empty);
    }
    if (!isTts) fields.appendChild(el('div', 'tip',
      '麦克风由页面直接采集（getUserMedia + MediaRecorder），录音不经过宿主；'
      + '窗口只对 127.0.0.1 的页面放行麦克风权限。'));
    // 保存键一直在：关掉服务也要能存下来（否则「关闭」这个选择没法生效）
    const row = el('div', 'btn-row');
    row.appendChild(actionBtn('保存', async () => {
      await cmd('service.set', { name: which, patch: state });
      await refreshState();
      toast(state.kind === 'off' ? '已关闭' : '已保存', 'ok');
    }, 'btn primary'));
    if (state.kind !== 'off') {
      row.appendChild(actionBtn('试一下', async () => {
        const r = await cmd('service.test', { name: which });
        if (isTts) { enqueueClip({ url: r.url }); toast('正在播放测试音频', 'ok'); }
        else toast('识别结果：' + (r.text || ''), 'ok', 5000);
      }, 'btn'));
      if (!isTts) row.appendChild(actionBtn('上传音频转写', () => { closeDrawer(); pickAudio(); }, 'btn ghost'));
    }
    fields.appendChild(row);
    return fields;
  };
  function mk(key, ph, type) {
    const v = state[key] == null ? '' : state[key];
    const input = textInput(type === 'password' ? '' : v, ph, type === 'password' ? 'password' : (type === 'number' ? 'number' : 'text'));
    if (type !== 'password') input.addEventListener('input', () => { state[key] = input.value; });
    else input.addEventListener('input', () => { state.key = input.value; });
    if (type === 'number') input.addEventListener('input', () => { state[key] = parseInt(input.value || '60', 10); });
    return input;
  }
  kinds.forEach(([k, t]) => {
    const b = el('button', (state.kind || 'off') === k ? 'on' : '', t);
    b.onclick = () => {
      state.kind = k;
      $$('button', seg).forEach(x => x.classList.remove('on'));
      b.classList.add('on');
      renderFields();
    };
    seg.appendChild(b);
  });
  sect.appendChild(seg);
  sect.appendChild(el('div', '', '')).style.height = '10px';
  sect.appendChild(fields);
  renderFields();
  return sect;
}

/* ---------------- 偏好 ---------------- */
function prefSection() {
  const sect = el('div', 'sect');
  sect.appendChild(el('div', 'sect-h', '偏好'));
  sect.appendChild(switchRow('自动朗读回复',
    S.tts.enabled ? '关掉后仍然可以点消息下面的「重播」。' : '语音合成还没开启，这一项现在不起作用。',
    S.voice_reply, async (on) => {
    const r = await cmd('ui.pref', { voice_reply: on });
    S.voice_reply = r.voice_reply;
    if (!on) stopAudio();
    paintHead();
  }));
  sect.appendChild(switchRow('识别完直接发送', '关掉则只把文字放进输入框。', S.asr_auto_send, async (on) => {
    const r = await cmd('ui.pref', { asr_auto_send: on });
    S.asr_auto_send = r.asr_auto_send;
  }));
  // 这两个是 app-core 新加的能力：全工具常开，但要不要逐次审批由这里决定
  sect.appendChild(switchRow('允许工具自动执行（autoallow）',
    '开启后，她调用工具（读写文件、跑命令、查资料）不需要你逐次点确认；'
    + '关掉则每次都要批准，更安全但更打断。',
    S.autoallow, async (on) => {
      const r = await cmd('ui.pref', { autoallow: on });
      S.autoallow = r.autoallow !== undefined ? !!r.autoallow : on;
    }));
  sect.appendChild(switchRow('长对话自动压缩（compact）',
    '开启后上下文变长时自动把旧对话压成摘要，长聊也不容易超出模型窗口；'
    + '关掉则不再自动压缩。',
    S.compact_auto, async (on) => {
      const r = await cmd('ui.pref', { compact_auto: on });
      S.compact_auto = r.compact_auto !== undefined ? !!r.compact_auto : on;
    }));

  if (S.tools) {
    const toolTip = el('div', 'tip',
      '基础工具已全部开启：ls / read / write / edit / rg / exec / find / tw / notice / msg。'
      + '图片与音频可以用 read 直接打开成多模态内容。');
    toolTip.style.cssText = 'margin-top:12px;font-size:11px;color:var(--faint);line-height:1.7';
    sect.appendChild(toolTip);
  }

  const row = el('div', 'btn-row');
  row.appendChild(actionBtn('打开数据目录', async () => { await cmd('open.data_dir'); }, 'btn sm'));
  row.appendChild(actionBtn('刷新状态', async () => { await refreshState(); }, 'btn sm ghost'));
  sect.appendChild(row);

  const tip = el('div', 'tip');
  tip.style.cssText = 'margin-top:14px;font-size:11px;color:var(--faint);line-height:1.7';
  tip.innerHTML = '数据都在 <code>.agent/</code>：<br>'
    + 'personas/ 角色卡与记忆 · sessions/ 会话 · audio/ 语音成品<br>'
    + 'config.json 模型与密钥 · agent.json 服务与界面偏好';
  sect.appendChild(tip);
  return sect;
}

/* ---------------- 表情包管理 ----------------
   文件在 .agent/personas/<pid>/stickers/（优先）与 .agent/stickers/（全局）；
   名字 = 文件名去扩展名。这里只管上传 / 删除 / 打开目录，发不发由模型决定。 */
function stickerSection() {
  const sect = el('div', 'sect');
  const head = el('div', 'sect-h', '表情包');
  if (S.active) head.appendChild(el('span', 'badge', (S.stickers || []).length + ' 个'));
  sect.appendChild(head);

  if (!S.active) {
    const empty = el('div', 'empty', '先创建一个角色，再给她准备表情包。');
    empty.style.lineHeight = '1.7';
    sect.appendChild(empty);
    return sect;
  }

  // 第一次打开时拉一次（state 里的列表可能还没包含别处刚加的）
  if (!STICKERS_LOADED) {
    STICKERS_LOADED = true;
    cmd('sticker.list').then((r) => {
      const arr = Array.isArray(r.stickers) ? r.stickers : (Array.isArray(r.list) ? r.list : null);
      if (arr) S.stickers = arr;
      if (!$('#drawer').classList.contains('hidden')) paintDrawer();
    }).catch(() => { STICKERS_LOADED = false; });
  }

  const list = S.stickers || [];
  if (!list.length) {
    const empty = el('div', 'empty', '还没有表情包。上传几张 png / gif / webp，聊天时她会在合适的时候发给你。');
    empty.style.lineHeight = '1.7';
    sect.appendChild(empty);
  } else {
    const grid = el('div', 'sticker-grid');
    for (const s of list) {
      const cell = el('div', 'sticker-cell');
      const img = document.createElement('img');
      img.src = s.url || '';
      img.alt = s.name || '表情包';
      img.loading = 'lazy';
      img.title = (s.name || '') + '（点开看大图）';
      img.onclick = () => openLightbox(s.url, s.name);
      const name = el('div', 'sticker-name', s.name || '');
      const del = el('button', 'sticker-del');
      del.type = 'button';
      del.title = '删除「' + (s.name || '') + '」';
      del.innerHTML = icon('trash');
      del.onclick = async () => {
        if (!confirm('删除表情包「' + (s.name || '') + '」？')) return;
        try {
          await cmd('sticker.del', { name: s.name });
          await refreshState();
          toast('已删除', 'ok');
        } catch (e) { toast(e.message, 'err'); }
      };
      cell.append(img, name, del);
      grid.appendChild(cell);
    }
    sect.appendChild(grid);
  }

  const row = el('div', 'btn-row');
  row.appendChild(actionBtn('上传表情包', pickSticker, 'btn primary sm'));
  row.appendChild(actionBtn('打开表情包目录', async () => { await cmd('open.stickers_dir'); }, 'btn sm ghost'));
  row.appendChild(actionBtn('刷新', async () => { await refreshState(); }, 'btn sm ghost'));
  sect.appendChild(row);

  const tip = el('div', 'tip');
  tip.style.cssText = 'margin-top:12px;font-size:11px;color:var(--faint);line-height:1.7';
  tip.innerHTML = '支持 png / jpg / gif / webp，单个不超过 ' + fmtSize(STICKER_MAX_BYTES) + '；名字就是文件名。'
    + '<br>当前角色的在 <code>.agent/personas/&lt;角色&gt;/stickers/</code>，'
    + '全局的 <code>.agent/stickers/</code> 对所有角色可见。';
  sect.appendChild(tip);
  return sect;
}

function pickSticker() {
  if (!S.active) { toast('先创建一个角色', 'err'); return; }
  $('#sticker-file').click();
}

$('#sticker-file').addEventListener('change', async (e) => {
  const files = Array.from(e.target.files || []);
  e.target.value = '';
  if (!files.length) return;
  let ok = 0;
  for (const f of files) {
    const ext = extOf(f.name);
    if (STICKER_EXTS.indexOf(ext) < 0) {
      toast('「' + (f.name || '文件') + '」不是支持的图片格式', 'err');
      continue;
    }
    if (f.size > STICKER_MAX_BYTES) {
      toast('「' + (f.name || '文件') + '」太大：单张上限 ' + fmtSize(STICKER_MAX_BYTES), 'err');
      continue;
    }
    try {
      const dataUrl = await fileToDataUrl(f);
      const name = String(f.name || 'sticker').replace(/\.[^.]+$/, '');
      await cmd('sticker.add', { name, ext, data_b64: b64OfDataUrl(dataUrl) });
      ok++;
    } catch (err) {
      toast(err.message || '上传失败', 'err');
    }
  }
  if (ok) {
    await refreshState();
    toast('已添加 ' + ok + ' 个表情包', 'ok');
  }
});
