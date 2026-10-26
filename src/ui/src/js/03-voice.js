'use strict';
/* =====================================================================
   03-voice —— 语音播放队列、麦克风、舞台（表情 / 动作 / 状态）
   ===================================================================== */

/* ---------------- 播放队列 ---------------- */
const AUDIO = { q: [], el: new Audio(), playing: false, seq: 0 };

AUDIO.el.addEventListener('ended', () => { AUDIO.playing = false; playNext(); });
AUDIO.el.addEventListener('error', () => { AUDIO.playing = false; playNext(); });

function setSpeaking(on) {
  $('#voice-bars').classList.toggle('on', on);
  l2dSetSpeaking(on);
}

function playNext() {
  if (!AUDIO.q.length) { setSpeaking(false); return; }
  const clip = AUDIO.q.shift();
  AUDIO.playing = true;
  setSpeaking(true);
  if (clip.emo) setEmotion(clip.emo, true);
  AUDIO.el.src = clip.url;
  AUDIO.el.play().catch(() => { AUDIO.playing = false; playNext(); });
}

function stopAudio() {
  AUDIO.q = [];
  AUDIO.playing = false;
  try { AUDIO.el.pause(); AUDIO.el.currentTime = 0; } catch { }
  setSpeaking(false);
}

function enqueueClip(clip) {
  if (!clip || !clip.url) return;
  AUDIO.q.push(clip);
  if (!AUDIO.playing) playNext();
}

async function speakText(text) {
  const clean = String(text || '').replace(/\*[^*]*\*/g, '').trim();
  if (!clean) return;
  if (!S.tts.enabled) { toast('还没有配置语音合成服务', 'err'); return; }
  try { await cmd('voice.speak', { text: clean }); }
  catch (e) { toast(e.message, 'err'); }
}

/* ---------------- 麦克风（页面直接采集） ----------------
   WebView2 / 浏览器里跑的就是 Chromium，getUserMedia + MediaRecorder 是现成的：
   录音不再经过宿主，也不用 winmm/MCI —— macOS、Linux 与浏览器里打开的页面
   走的是同一条路。宿主只在 WebView2 里负责放行麦克风权限（见 webview.h）。
   采不到（老运行时、没有设备、权限被拒）时才退回宿主的 voice.record.*。 */

const MIC = {
  stream: null, rec: null, chunks: [], ctx: null, analyser: null, raf: 0,
  t0: 0, host: false,
  hold: false, // 这次录音是「按住说话」起的：太短就静默丢弃，不弹提示
};

function micError(e) {
  const name = (e && e.name) || '';
  if (name === 'NotAllowedError' || name === 'SecurityError') return '麦克风权限被拒绝';
  if (name === 'NotFoundError' || name === 'OverconstrainedError') return '系统里没有可用的录音设备';
  if (name === 'NotReadableError') return '麦克风被别的程序占用';
  return (e && e.message) || '打不开麦克风';
}

function pickMime() {
  if (!window.MediaRecorder) return '';
  const want = ['audio/webm;codecs=opus', 'audio/webm', 'audio/ogg;codecs=opus', 'audio/mp4'];
  for (const t of want) if (MediaRecorder.isTypeSupported(t)) return t;
  return '';
}
function extForMime(m) {
  if (!m) return 'webm';
  if (m.indexOf('mp4') >= 0) return 'm4a';
  if (m.indexOf('ogg') >= 0) return 'ogg';
  return 'webm';
}

function startLevel(stream) {
  const Ctx = window.AudioContext || window.webkitAudioContext;
  if (!Ctx) return;
  try {
    MIC.ctx = new Ctx();
    const src = MIC.ctx.createMediaStreamSource(stream);
    MIC.analyser = MIC.ctx.createAnalyser();
    MIC.analyser.fftSize = 512;
    src.connect(MIC.analyser);
    const data = new Uint8Array(MIC.analyser.frequencyBinCount);
    const root = document.documentElement;
    const tick = () => {
      MIC.analyser.getByteTimeDomainData(data);
      let sum = 0;
      for (let i = 0; i < data.length; i++) {
        const d = (data[i] - 128) / 128;
        sum += d * d;
      }
      const rms = Math.sqrt(sum / data.length);
      // 语音的 RMS 通常在 0.02..0.25，乘 4.2 之后才铺得满
      root.style.setProperty('--mic-lvl', Math.min(1, rms * 4.2).toFixed(3));
      MIC.raf = requestAnimationFrame(tick);
    };
    tick();
  } catch (e) { /* 没有 Web Audio 也不影响录音本身 */ }
}

function stopLevel() {
  if (MIC.raf) cancelAnimationFrame(MIC.raf);
  MIC.raf = 0;
  document.documentElement.style.setProperty('--mic-lvl', '0');
  if (MIC.ctx) { try { MIC.ctx.close(); } catch { } }
  MIC.ctx = null;
  MIC.analyser = null;
}

async function startBrowserMic() {
  if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia)
    throw Object.assign(new Error('这个环境没有 getUserMedia'), { name: 'NotSupportedError' });
  if (!window.isSecureContext)
    throw Object.assign(new Error('页面不在安全上下文里'), { name: 'SecurityError' });
  const stream = await navigator.mediaDevices.getUserMedia({
    audio: { channelCount: 1, echoCancellation: true, noiseSuppression: true, autoGainControl: true },
  });
  const mime = pickMime();
  let rec;
  try {
    rec = mime ? new MediaRecorder(stream, { mimeType: mime, audioBitsPerSecond: 64000 })
      : new MediaRecorder(stream);
  } catch (e) {
    stream.getTracks().forEach((t) => t.stop()); // 拿不到编码器就把设备还回去
    throw e;
  }
  MIC.stream = stream;
  MIC.rec = rec;
  MIC.chunks = [];
  MIC.t0 = Date.now();
  rec.ondataavailable = (e) => { if (e.data && e.data.size) MIC.chunks.push(e.data); };
  rec.start(200); // 每 200ms 一片，长录音也不会攒成一个巨大的 Blob
  startLevel(stream);
}

async function stopBrowserMic() {
  const rec = MIC.rec;
  const stream = MIC.stream;
  const secs = (Date.now() - MIC.t0) / 1000;
  stopLevel();
  if (!rec) return null;
  await new Promise((resolve) => {
    rec.onstop = () => resolve();
    try { rec.stop(); } catch { resolve(); }
  });
  if (stream) stream.getTracks().forEach((t) => t.stop());
  const type = rec.mimeType || 'audio/webm';
  const blob = new Blob(MIC.chunks, { type });
  MIC.rec = null;
  MIC.stream = null;
  MIC.chunks = [];
  return { blob, secs, ext: extForMime(type) };
}

async function transcribeBlob(blob, ext) {
  ({ blob, ext } = await prepareUpload(blob, ext));
  const buf = new Uint8Array(await blob.arrayBuffer());
  let bin = '';
  for (let i = 0; i < buf.length; i += 0x8000)
    bin += String.fromCharCode.apply(null, buf.subarray(i, i + 0x8000));
  // 转写结果由服务端的 asr 事件送回（02-chat 负责填框 / 自动发送）
  await cmd('voice.transcribe', { data_b64: btoa(bin), ext });
}

/* ---------------- 上传格式（设置 → 语音识别 → 上传格式） ----------------
   「wav」时把字节真的换成 WAV，而不是只改扩展名：FunASR / SenseVoice /
   faster-whisper 这类服务端大多只认 wav，浏览器录出来的却是 webm/opus。
   解码 + 重采样都在页面里做，宿主不需要装 ffmpeg。
   转不动就按原格式上传 —— 宁可发出去，也不要因为格式偏好整条丢掉。 */
function asrUploadFormat() {
  const f = (S.asr && S.asr.upload_format) || 'auto';
  return f === 'auto' ? '' : f;
}

function encodeWavPcm16(samples, rate) {
  const n = samples.length;
  const buf = new ArrayBuffer(44 + n * 2);
  const v = new DataView(buf);
  const tag = (off, s) => { for (let i = 0; i < s.length; i++) v.setUint8(off + i, s.charCodeAt(i)); };
  tag(0, 'RIFF'); v.setUint32(4, 36 + n * 2, true); tag(8, 'WAVE');
  tag(12, 'fmt '); v.setUint32(16, 16, true);
  v.setUint16(20, 1, true);           // PCM
  v.setUint16(22, 1, true);           // 单声道
  v.setUint32(24, rate, true);
  v.setUint32(28, rate * 2, true);    // 字节率
  v.setUint16(32, 2, true);           // 块对齐
  v.setUint16(34, 16, true);          // 位深
  tag(36, 'data'); v.setUint32(40, n * 2, true);
  for (let i = 0; i < n; i++) {
    const s = Math.max(-1, Math.min(1, samples[i]));
    v.setInt16(44 + i * 2, s < 0 ? s * 0x8000 : s * 0x7fff, true);
  }
  return new Blob([buf], { type: 'audio/wav' });
}

async function toWavBlob(blob, rate = 16000) {
  const Ctx = window.AudioContext || window.webkitAudioContext;
  const Off = window.OfflineAudioContext || window.webkitOfflineAudioContext;
  if (!Ctx || !Off || !blob || !blob.size) return null;
  let ctx = null;
  try {
    ctx = new Ctx();
    const pcm = await ctx.decodeAudioData(await blob.arrayBuffer());
    const frames = Math.max(1, Math.ceil(pcm.duration * rate));
    const off = new Off(1, frames, rate);
    const src = off.createBufferSource();
    src.buffer = pcm;
    src.connect(off.destination);
    src.start();
    const out = await off.startRendering();
    return encodeWavPcm16(out.getChannelData(0), rate);
  } catch (e) {
    console.warn('wav transcode failed:', e && e.message);
    return null;
  } finally {
    if (ctx) { try { ctx.close(); } catch { } }
  }
}

/* 按配置把要上传的音频准备好；返回 {blob, ext}。 */
async function prepareUpload(blob, ext) {
  if (asrUploadFormat() !== 'wav' || ext === 'wav') return { blob, ext };
  const wav = await toWavBlob(blob);
  if (!wav) { toast('转成 WAV 失败，按原格式上传', 'err', 4000); return { blob, ext }; }
  return { blob: wav, ext: 'wav' };
}

async function toggleMic() {
  if (!S.asr.enabled) { toast('还没有配置语音识别服务', 'err'); return; }
  if ($('#btn-mic').disabled) return;
  if (S.recording) return stopRecording();
  return startRecording();
}

async function startRecording(fromHold) {
  if (S.recording) return;
  stopAudio();
  MIC.hold = !!fromHold;
  try {
    await startBrowserMic();
    MIC.host = false;
  } catch (e) {
    const why = micError(e);
    try {
      await cmd('voice.record.start'); // 宿主录音（Windows MCI）兜底
      MIC.host = true;
      toast(why + '，已改用系统录音', '', 3200);
    } catch (e2) {
      // 两条路都走不通：说清楚原因，并指向不需要麦克风的通道
      console.warn('host mic fallback failed:', e2 && e2.message);
      toast(why + '。可以插上麦克风再试，或在设置 → 语音识别里用「上传音频转写」。', 'err', 6000);
      MIC.hold = false;
      return;
    }
  }
  S.recording = true;
  paintComposer();
  // 按下的瞬间就松开了（getUserMedia 比较慢）：别再让麦克风开着
  if (fromHold && HOLD.wantStop) {
    HOLD.wantStop = false;
    stopRecording();
  }
}

async function stopRecording() {
  // hold 起来的那次如果只有零点几秒，是按住说话的轻点，静默丢掉就行
  const wasHold = MIC.hold;
  MIC.hold = false;
  S.recording = false;
  paintComposer();
  if (MIC.host) {
    try {
      const r = await cmd('voice.record.stop');
      if (r.seconds && r.seconds < 1) { if (!wasHold) toast('太短了，再说一次？'); return; }
      showTyping('正在听写');
      await cmd('voice.transcribe', { path: r.path });
    } catch (e) {
      toast(e.message, 'err', 5000);
    } finally {
      hideTyping();
    }
    return;
  }
  let clip = null;
  try {
    clip = await stopBrowserMic();
  } catch (e) {
    toast(micError(e), 'err');
    return;
  }
  if (!clip || clip.secs < 0.6 || !clip.blob.size) {
    if (!wasHold) toast('太短了，再说一次？');
    return;
  }
  try {
    showTyping('正在听写');
    await transcribeBlob(clip.blob, clip.ext);
  } catch (e) {
    toast(e.message, 'err', 5000);
  } finally {
    hideTyping();
  }
}

// 关窗 / 刷新时把设备还回去，别让麦克风一直被占着
window.addEventListener('beforeunload', () => {
  if (MIC.stream) MIC.stream.getTracks().forEach((t) => t.stop());
  stopLevel();
});

/* ---------------- 按住说话 ----------------
   绑定的键（默认 Alt）按住开始录、松开结束 —— 对讲机的手感，也不怕忘了关。
   键在「设置 → 偏好 → 麦克风快捷键」里录，存后端 agent.json（换端口 / 换浏览器都跟着走）。

   两个必须处理的坑：
   1. 单独按 Alt 会被系统抢（Windows 菜单、浏览器焦点环）。所以按下后先等 180ms
      再真开录：这期间又按了别的键（Alt+Tab / Alt+F4 这类组合）就撤销，不录音。
   2. 窗口失焦收不到 keyup。blur / visibilitychange 时主动收尾，别把麦克风留在开着的状态。
*/
const MOD_KEYS = { Alt: 'Alt', Control: 'Ctrl', Shift: 'Shift', Meta: 'Cmd' };
const HOTKEY = { capture: false }; // 设置里正在录快捷键时，这里的监听让路

function keyLabel(e) {
  const k = e.key;
  if (MOD_KEYS[k]) return MOD_KEYS[k];
  if (k === ' ') return 'Space';
  if (k === 'Escape') return 'Esc';
  if (k && k.length === 1) return k.toUpperCase();
  return k || '';
}

/* 从键盘事件算「完整组合」：修饰键在前、主键在后，顺序固定，两边比较才可靠。
   修饰键自己按下时（e.key=Alt）不算主键，只算修饰键 —— 于是得到单键 "Alt"。 */
function comboOf(e) {
  const parts = [];
  if (e.ctrlKey) parts.push('Ctrl');
  if (e.shiftKey) parts.push('Shift');
  if (e.altKey) parts.push('Alt');
  if (e.metaKey) parts.push('Cmd');
  const isMod = Object.hasOwn(MOD_KEYS, e.key);
  if (!isMod) {
    const main = keyLabel(e);
    if (main) parts.push(main);
  }
  return parts.join('+');
}

/* keyup 时修饰键标志已经复位（松开 Alt 的那一刻 altKey 已经是 false），
   不补回去就永远匹配不上「Alt」这类单键绑定 */
function comboOfRelease(e) {
  const fake = { key: e.key, ctrlKey: e.ctrlKey, shiftKey: e.shiftKey, altKey: e.altKey, metaKey: e.metaKey };
  if (e.key === 'Alt') fake.altKey = true;
  else if (e.key === 'Control') fake.ctrlKey = true;
  else if (e.key === 'Shift') fake.shiftKey = true;
  else if (e.key === 'Meta') fake.metaKey = true;
  return comboOf(fake);
}

const HOLD = { down: false, started: false, pending: 0, wantStop: false };

function holdCancel() {
  if (HOLD.pending) { clearTimeout(HOLD.pending); HOLD.pending = 0; }
  HOLD.down = false;
  HOLD.started = false;
  HOLD.wantStop = false;
}

function holdRelease() {
  if (HOLD.pending) { // 还没到 180ms：这次只是轻点，什么都不做
    clearTimeout(HOLD.pending);
    HOLD.pending = 0;
    HOLD.down = false;
    HOLD.started = false;
    return;
  }
  HOLD.down = false;
  if (!HOLD.started) return;
  HOLD.started = false;
  HOLD.wantStop = true;
  if (S.recording) stopRecording();
  // 还在等 getUserMedia：startRecording 收尾时会看到 wantStop
}

document.addEventListener('keydown', (e) => {
  if (HOTKEY.capture) return;
  const hk = S.mic_hotkey;
  if (!hk || !S.asr.enabled) return;   // 没配语音识别就别抢键
  if (comboOf(e) !== hk) {
    if (HOLD.down && !HOLD.started) holdCancel(); // 按的是组合键（Alt+Tab…），撤销
    return;
  }
  if (HOLD.down) return;               // 长按的自动重复
  e.preventDefault();
  HOLD.down = true;
  HOLD.started = false;
  HOLD.wantStop = false;
  HOLD.pending = setTimeout(() => {
    HOLD.pending = 0;
    if (!HOLD.down) return;
    HOLD.started = true;
    if (!S.recording) startRecording(true);
  }, 180);
}, true);

document.addEventListener('keyup', (e) => {
  if (HOTKEY.capture || !HOLD.down) return;
  const hk = S.mic_hotkey;
  if (!hk || comboOfRelease(e) !== hk) return;
  e.preventDefault();
  holdRelease();
}, true);

// 切窗口 / 最小化：keyup 收不到，主动收尾
window.addEventListener('blur', () => { if (HOLD.down) holdRelease(); });
document.addEventListener('visibilitychange', () => {
  if (document.hidden && HOLD.down) holdRelease();
});

/* ---------------- 上传音频转写（没有麦克风时的备用通道） ---------------- */
function pickAudio() {
  if (!S.asr.enabled) { toast('还没有配置语音识别服务', 'err'); return; }
  $('#audio-file').click();
}
$('#audio-file').addEventListener('change', async (e) => {
  const f = e.target.files && e.target.files[0];
  e.target.value = '';
  if (!f) return;
  if (f.size > 24 * 1024 * 1024) { toast('音频太大（上限 24MB）', 'err'); return; }
  const ext0 = (f.name.split('.').pop() || 'wav').toLowerCase();
  const { blob, ext } = await prepareUpload(f, ext0);
  const buf = new Uint8Array(await blob.arrayBuffer());
  let bin = '';
  for (let i = 0; i < buf.length; i += 0x8000)
    bin += String.fromCharCode.apply(null, buf.subarray(i, i + 0x8000));
  try {
    showTyping('正在听写');
    const r = await cmd('voice.transcribe', { data_b64: btoa(bin), ext });
    hideTyping();
    if (r.text) {
      $('#input').value = r.text;
      autoGrow();
      if (r.auto_send) sendCurrent();
      else $('#input').focus();
    }
  } catch (err) { hideTyping(); toast(err.message, 'err'); }
});

/* ---------------- 舞台 ---------------- */
const ACT_ZH = {
  hug: '紧紧抱住了你', headpat: '伸手摸了摸你的头', wave: '朝你挥了挥手',
  nod: '轻轻点了点头', smile: '弯起眼睛笑了', blush: '别过脸去',
  lean: '凑近了一点', think: '托着下巴想了想', yawn: '打了个小小的哈欠',
  sleep: '靠在你肩上睡着了',
};

let actTimer = null;
function showAct(name) {
  if (!name) return;
  const line = $('#stage-act');
  line.textContent = (ACT_ZH[name] || name) + '…';
  line.classList.add('on');
  S.lastAct = name;
  if (actTimer) clearTimeout(actTimer);
  actTimer = setTimeout(() => line.classList.remove('on'), 5200);
}

let moodTimer = null;

/* 情绪 → 舞台打光的颜色。角色卡的主色管界面，这一层只管「光」：
   开心偏暖、难过偏冷、生气偏红，换情绪时整块背景的柔光跟着换。 */
const MOOD_LIGHT = {
  neutral: '#8b7cff', happy: '#ffc46b', sad: '#6b9cff', angry: '#ff6b6b',
  shy: '#ff8fb8', surprised: '#ffd166', thinking: '#7fd3ff', affection: '#ff7fb0',
};

function setMoodLight(name) {
  const c = MOOD_LIGHT[name] || MOOD_LIGHT.neutral;
  document.documentElement.style.setProperty('--mood', c);
}

function setEmotion(name, fromVoice) {
  const emo = (name || 'neutral').toLowerCase();
  S.emotion = emo;
  $('#mood-text').textContent = EMO_ZH[emo] || emo;
  setMoodLight(emo);
  // 表情交给模型：映射写在角色卡 live2d.emo 里
  l2dSetEmotion(emo);
  if (!fromVoice) {
    if (moodTimer) clearTimeout(moodTimer);
    // 情绪是回合内的状态：过一会儿回到平静，但保留当前值到下一句
    moodTimer = setTimeout(() => {
      if (!AUDIO.playing) {
        $('#mood-text').textContent = EMO_ZH.neutral;
        setMoodLight('neutral');
        l2dSetEmotion('neutral');
      }
    }, 14000);
  }
}

function paintStage() {
  const p = S.persona || {};
  $('#stage-name').textContent = p.name || '—';
  $('#stage-sub').textContent = p.subtitle || '';
  $('#mood-text').textContent = EMO_ZH[S.emotion] || '平静';
  setMoodLight(S.emotion);
  $('#stage-mem').textContent = `记忆 ${S.memory.length} 条`;
  const pro = $('#btn-proactive');
  pro.classList.toggle('on', !!p.proactive);
  $('#proactive-v').textContent = p.proactive ? `每 ${p.idle_minutes || 30} 分钟` : '关';
  // 舞台就是 Live2D：角色卡里配了模型就加载，没配就让画布上的提示说话
  trigRefresh();
  l2dApply();
}

/* ---------------- 事件 ---------------- */
onEvent((m) => {
  if (m.t === 'voice') enqueueClip({ url: m.url, text: m.text, emo: m.emo });
  else if (m.t === 'voice_stop') stopAudio();
  else if (m.t === 'voice_error')
    toast('语音合成失败：' + (m.d || '') + '（设置 → 语音合成 里可以改，或者直接关掉）', 'err', 6000);
  else if (m.t === 'turn_begin') {
    setEmotion('thinking', true);
  }
});
