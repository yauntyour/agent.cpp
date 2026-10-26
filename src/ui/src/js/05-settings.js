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
  { key: 'prompt', name: '提示词', sub: () => promptSummary(), build: () => promptSection() },
  { key: 'status', name: '运行状态', sub: () => statusSummary(), build: () => statusSection() },
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
/* ASR 上传格式：服务端认什么就发什么。
   wav 由页面自己转（WebAudio 解码 + 重采样，不需要宿主装东西），其余格式
   宿主会用 ffmpeg 转 —— 所以只有服务端非要 mp3/m4a 时才需要它。 */
const ASR_UPLOAD_FORMATS = [
  ['auto', '跟随文件（原样上传）'],
  ['wav', 'WAV 16k 单声道（页面转码）'],
  ['mp3', 'MP3（需要 ffmpeg）'],
  ['m4a', 'M4A（需要 ffmpeg）'],
  ['ogg', 'OGG（需要 ffmpeg）'],
  ['flac', 'FLAC（需要 ffmpeg）'],
];

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
        // 上传形态：以前这三项是写死的（file / 按扩展名 / 无额外字段），
        // 服务端不认就只能改代码。现在都摆出来。
        const uf = document.createElement('select');
        for (const [v, t] of ASR_UPLOAD_FORMATS) {
          const o = document.createElement('option');
          o.value = v; o.textContent = t;
          if ((state.upload_format || 'auto') === v) o.selected = true;
          uf.appendChild(o);
        }
        uf.onchange = () => { state.upload_format = uf.value; };
        fields.append(
          field('上传格式', uf,
            '录音默认是 webm/opus。服务端只认 WAV 时选 wav —— 页面会把字节转成 16k 单声道，'
            + '不是改个后缀糊过去；选 mp3/m4a/ogg/flac 则由宿主调用 ffmpeg。'),
          field('文件字段名', mk('upload_field', 'file'),
            'multipart 里音频那一段的名字。OpenAI 是 file，个别实现是 audio。'),
          field('附加表单字段', mk('upload_extra', 'format=wav&channel=1'),
            '服务端要额外的表单字段就填这里，k=v 用 & 或换行分隔。'),
          field('MIME 覆盖', mk('upload_mime', 'audio/wav'),
            '留空按格式自动推导；只有服务端认死 Content-Type 时才需要。'),
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
      + '窗口只对 127.0.0.1 的页面放行麦克风权限。'
      + '录音只在临时目录里过一遍（有的 ASR 只认文件），转写完立刻删掉 —— 本地不留档。'));
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

  // ---- 麦克风快捷键（按住说话）----
  // 键存在后端 agent.json：换端口 / 换浏览器 / 换设备都跟着走。
  const hkRow = el('div', 'hotkey-row');
  const hkMain = el('div', 'hotkey-main');
  hkMain.appendChild(el('div', 'k', '麦克风快捷键'));
  hkMain.appendChild(el('div', 'd', '按住说话、松开结束。点右边的键位重新录。'));
  const hkKey = el('button', 'hotkey-key', S.mic_hotkey || '未绑定');
  hkKey.title = '点击重新录制';
  hkKey.onclick = () => captureHotkey(hkKey);
  const hkClr = el('button', 'del hotkey-clr', '');
  hkClr.innerHTML = icon('x');
  hkClr.title = '清除绑定（麦克风按钮仍然能点）';
  hkClr.onclick = async () => {
    try {
      const r = await cmd('ui.pref', { mic_hotkey: '' });
      S.mic_hotkey = r.mic_hotkey || '';
      await refreshState();
      toast('已清除快捷键', 'ok');
    } catch (e) { toast(e.message, 'err'); }
  };
  hkRow.append(hkMain, hkKey, hkClr);
  sect.appendChild(hkRow);
  sect.appendChild(el('div', 'tip hotkey-tip',
    '默认 Alt。单独按 Alt 在部分浏览器里会被菜单栏吃掉 —— 按住没反应就换成 Alt+M / F2 这类组合。'
    + '录音时要一直按着，松开即结束并开始转写。'));

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
  // 不是「思考强度」：那个是模型的 CoT 档位，这个是给她的自由度
  sect.appendChild(switchRow('允许胡思乱想',
    '开启后她可以走神、联想、说些没用的话，不必每句都在回答问题；'
    + '关掉则每句都冲着你来。写进系统提示词，下一句就生效。',
    S.free_think, async (on) => {
      const r = await cmd('ui.pref', { free_think: on });
      S.free_think = r.free_think !== undefined ? !!r.free_think : on;
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

/* 录一个快捷键。纯修饰键（Alt / Ctrl…）也能单独绑：按下时先显示，
   松开才确认 —— 否则想绑 Alt 永远会被当成「组合键的前缀」。 */
function captureHotkey(btn) {
  if (HOTKEY.capture) return;
  HOTKEY.capture = true;
  const prev = btn.textContent;
  btn.classList.add('capturing');
  btn.textContent = '按下想要的键…';
  let pending = '';

  const stop = () => {
    document.removeEventListener('keydown', onKey, true);
    document.removeEventListener('keyup', onUp, true);
    HOTKEY.capture = false;
    btn.classList.remove('capturing');
  };
  const finish = async (label) => {
    stop();
    if (!label) { btn.textContent = prev; return; }
    try {
      const r = await cmd('ui.pref', { mic_hotkey: label });
      S.mic_hotkey = r.mic_hotkey !== undefined ? r.mic_hotkey : label;
      await refreshState(); // 抽屉会重建，键位行跟着换成新值
      toast('已绑定：' + (S.mic_hotkey || '无'), 'ok');
    } catch (e) {
      btn.textContent = prev;
      toast(e.message, 'err');
    }
  };
  const onKey = (e) => {
    e.preventDefault();
    e.stopPropagation();
    if (e.key === 'Escape') { stop(); btn.textContent = prev; return; }
    if (MOD_KEYS[e.key]) { // 纯修饰键：先摆出来，等松开确认
      pending = comboOf(e);
      btn.textContent = pending || '…';
      return;
    }
    finish(comboOf(e));
  };
  const onUp = (e) => {
    if (!MOD_KEYS[e.key] || !pending) return;
    e.preventDefault();
    e.stopPropagation();
    finish(comboOfRelease(e) || pending);
  };
  document.addEventListener('keydown', onKey, true);
  document.addEventListener('keyup', onUp, true);
}

/* ---------------- 提示词分层 ----------------
   system prompt 不是一段硬编码文本，而是若干层拼起来的（core / identity /
   relation / memory / 演出约定…）。这里读 prompt.get 的快照：每层的定义、
   当前值、以及合成后的全文；改一层就重新拉一次，字数变化一眼看得见。 */
let PROMPT_DATA = null;

function promptSummary() {
  if (!PROMPT_DATA) return '分层注入';
  const on = PROMPT_DATA.layers.filter(l => l.on).length;
  return `${on}/${PROMPT_DATA.layers.length} 层 · ${PROMPT_DATA.chars} 字`;
}

async function promptReload() {
  const pid = S.active;
  const r = await cmd('prompt.get');
  r.pid = pid;
  PROMPT_DATA = r;
  if (DRAWER.current === 'settings/prompt') paintDrawer();
}

function promptSection() {
  const wrap = el('div');
  if (PROMPT_DATA && PROMPT_DATA.pid !== S.active) PROMPT_DATA = null; // 换了角色，读数作废

  if (!PROMPT_DATA) {
    const empty = el('div', 'empty', '正在读取…');
    wrap.appendChild(empty);
    cmd('prompt.get').then((r) => {
      r.pid = S.active;
      PROMPT_DATA = r;
      if (DRAWER.current === 'settings/prompt') paintDrawer();
    }).catch((e) => { empty.textContent = '读不到提示词：' + e.message; });
    return wrap;
  }

  // ---- 合成结果 ----
  const head = el('div', 'sect');
  head.appendChild(el('div', 'sect-h', '合成结果'));
  const card = el('div', 'card on');
  card.appendChild(el('div', 'card-name', `system · ${PROMPT_DATA.chars} 字`));
  card.appendChild(el('div', 'card-sub',
    '每次对话只有这一条 system 消息（msg[0]）。改动下一个回合生效。'));
  head.appendChild(card);

  const det = document.createElement('details');
  det.className = 'pl-edit';
  det.appendChild(el('summary', null, '查看全文'));
  det.appendChild(el('pre', 'prompt-pre', PROMPT_DATA.system || '（空）'));
  head.appendChild(det);

  const row = el('div', 'btn-row');
  row.appendChild(actionBtn('改用默认（清空自定义）', async () => {
    if (!confirm('把所有层和关系记录恢复默认？')) return;
    try { await cmd('prompt.reset'); await promptReload(); toast('已恢复默认', 'ok'); }
    catch (e) { toast(e.message, 'err'); }
  }, 'btn sm ghost'));
  head.appendChild(row);
  wrap.appendChild(head);

  // ---- 每一层 ----
  const list = el('div', 'sect');
  list.appendChild(el('div', 'sect-h', '分层'));
  for (const L of PROMPT_DATA.layers) {
    const item = el('div', 'pl-item' + (L.on ? '' : ' off'));
    const top = el('div', 'pl-top');
    const main = el('div', 'pl-main');
    main.appendChild(el('div', 'pl-name', L.title));
    main.appendChild(el('div', 'pl-desc', L.desc));
    if (L.note) {
      const n = el('div', 'pl-note', L.note);
      main.appendChild(n);
    }
    const sw = el('button', 'switch' + (L.on ? ' on' : ''));
    sw.title = L.on ? '关掉这一段' : '打开这一段';
    sw.onclick = async () => {
      try { await cmd('prompt.set', { id: L.id, on: !L.on }); await promptReload(); }
      catch (e) { toast(e.message, 'err'); }
    };
    top.append(main, sw);
    item.appendChild(top);

    if (L.kind === 'identity') {
      item.appendChild(el('div', 'btn-row', actionBtn('去角色设定里改', () => {
        closeDrawer();
        openPersonaEditor(false);
      }, 'btn sm ghost')));
    } else if (L.kind === 'relation') {
      const ta = textArea(PROMPT_DATA.relation || '', 8, '她是谁、你们怎么相处、有什么约定…');
      ta.classList.add('mono');
      item.appendChild(ta);
      item.appendChild(el('div', 'btn-row', actionBtn('保存关系记录', async () => {
        await cmd('prompt.set', { relation: ta.value });
        await promptReload();
        toast('关系记录已保存', 'ok');
      }, 'btn primary sm')));
      item.appendChild(el('div', 'tip',
        '这一段每次对话都会完整注入。她也能用 settings 工具自己往这里写 —— 聊几句之后回来看看。'));
    } else if (L.kind !== 'auto') {
      const d = document.createElement('details');
      d.className = 'pl-edit';
      d.appendChild(el('summary', null, L.text ? '已改写（点开编辑）' : '用默认文案（点开改写）'));
      const ta = textArea(L.text || '', 10, (L.default || '（这一段默认是空的）').slice(0, 300));
      ta.classList.add('mono');
      d.appendChild(ta);
      const bar = el('div', 'btn-row');
      bar.appendChild(actionBtn('保存', async () => {
        await cmd('prompt.set', { id: L.id, text: ta.value.trim() });
        await promptReload();
        toast('已保存', 'ok');
      }, 'btn primary sm'));
      if (L.text) {
        bar.appendChild(actionBtn('恢复默认', async () => {
          try { await cmd('prompt.set', { id: L.id, reset: true }); await promptReload(); }
          catch (e) { toast(e.message, 'err'); }
        }, 'btn sm ghost'));
      }
      d.appendChild(bar);
      item.appendChild(d);
    }
    list.appendChild(item);
  }
  wrap.appendChild(list);
  return wrap;
}

/* ---------------- 运行状态 ----------------
   一屏看清：哪些服务真的在跑、工具是怎么暴露的、运行时落在哪。
   全部来自 state 广播里已有的字段，不额外请求，也不会因为某个服务没配就报错。 */
const SANDBOX_ZH = { 'read-only': '只读', 'edit-only': '可编辑', 'full-access': '完全访问' };

function statusSummary() {
  const svc = [S.tts, S.asr].filter(s => s && s.kind && s.kind !== 'off').length;
  return `${svc}/2 语音服务 · ${(S.toolset || []).length} 个工具`;
}

function statusKV(k, v, mono) {
  const row = el('div', 'kv');
  row.appendChild(el('span', 'kv-k', k));
  row.appendChild(el('span', 'kv-v' + (mono ? ' mono' : ''), v == null || v === '' ? '—' : String(v)));
  return row;
}

function svcLine(svc) {
  const kind = (svc && svc.kind) || 'off';
  if (kind === 'off') return '未开启';
  const bits = [kind];
  if (svc.base) bits.push(svc.base);
  if (svc.model) bits.push(svc.model);
  return bits.join(' · ');
}

function statusSection() {
  const wrap = el('div');

  const s1 = el('div', 'sect');
  s1.appendChild(el('div', 'sect-h', '服务'));
  s1.appendChild(statusKV('模型', (S.model && S.model !== '(none)') ? S.model : '未选择'));
  s1.appendChild(statusKV('供应商', S.provider || '—'));
  s1.appendChild(statusKV('语音合成', svcLine(S.tts)));
  s1.appendChild(statusKV('语音识别', svcLine(S.asr)));
  s1.appendChild(statusKV('IM 渠道', imStatusText()));
  s1.appendChild(statusKV('思考强度', THINK_ZH[S.thinking] || '关'));
  // IM 的明细跟在服务清单后面：每个渠道一行，状态 + 收发计数，出错时带原因
  const ims = (S.im || []).filter(c => c.enabled && c.kind && c.kind !== 'off');
  if (ims.length) {
    const box = el('div', 'tool-list');
    for (const c of ims) {
      const row = el('div', 'tool-row');
      row.appendChild(el('span', 'tn', c.id));
      const bits = [imKindName(c.kind)];
      bits.push(c.state === 'running' ? '运行中' : (c.state === 'error' ? '出错' : '已停止'));
      if (c.in || c.out) bits.push(`入 ${c.in || 0} / 出 ${c.out || 0}`);
      if (c.dropped) bits.push(`丢弃 ${c.dropped}`);
      if (c.state === 'error' && c.last_error) bits.push(String(c.last_error).slice(0, 70));
      row.appendChild(el('span', 'td', bits.join(' · ')));
      box.appendChild(row);
    }
    s1.appendChild(box);
  }
  wrap.appendChild(s1);

  const s2 = el('div', 'sect');
  s2.appendChild(el('div', 'sect-h', '工具'));
  s2.appendChild(statusKV('暴露', S.tools ? '全部开启' : '已关闭'));
  s2.appendChild(statusKV('沙箱', SANDBOX_ZH[S.sandbox] || S.sandbox || '—'));
  s2.appendChild(statusKV('执行审批', S.autoallow ? '自动执行（不逐条确认）' : '每条都要确认'));
  const list = S.toolset || [];
  if (list.length) {
    const box = el('div', 'tool-list');
    for (const t of list) {
      const row = el('div', 'tool-row');
      row.appendChild(el('span', 'tn', t.name));
      row.appendChild(el('span', 'td', t.desc || ''));
      box.appendChild(row);
    }
    s2.appendChild(box);
  } else {
    s2.appendChild(el('div', 'tip', '还没拿到工具清单（连上后端后会出现）。'));
  }
  wrap.appendChild(s2);

  const s3 = el('div', 'sect');
  s3.appendChild(el('div', 'sect-h', '运行时'));
  s3.appendChild(statusKV('数据根', S.root, true));
  s3.appendChild(statusKV('工作目录', S.cwd, true));
  s3.appendChild(statusKV('当前会话', S.session, true));
  s3.appendChild(statusKV('端口', S.port ? 'http://127.0.0.1:' + S.port + '/' : '—', true));
  s3.appendChild(statusKV('舞台', L2D.status === 'ready'
    ? `Live2D 运行中 · ${L2D.runtimeFrom === 'cdn' ? 'CDN' : '本地'}运行时`
    : (L2D.status === 'loading' ? '加载中' : (L2D.status === 'error' ? '出错' : '未接入'))));
  s3.appendChild(statusKV('记忆', (S.memory || []).length + ' 条'));
  s3.appendChild(statusKV('表情包', (S.stickers || []).length + ' 个'));
  s3.appendChild(statusKV('工具调用记录', (S.toolLog || []).length + ' 次（点顶栏的扳手看）'));
  wrap.appendChild(s3);

  const row = el('div', 'btn-row');
  row.appendChild(actionBtn('刷新', async () => { await refreshState(); buildDrawer(); }, 'btn sm'));
  if (S.port) row.appendChild(actionBtn('在浏览器里打开', async () => {
    window.open('http://127.0.0.1:' + S.port + '/', '_blank');
  }, 'btn sm ghost'));
  wrap.appendChild(row);
  return wrap;
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
