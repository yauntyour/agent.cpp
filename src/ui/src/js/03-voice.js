'use strict';
/* =====================================================================
   03-voice —— 语音播放队列、麦克风、舞台（表情 / 动作 / 状态）
   ===================================================================== */

/* ---------------- 播放队列 ---------------- */
const AUDIO = { q: [], el: new Audio(), playing: false, seq: 0 };

AUDIO.el.addEventListener('ended', () => { AUDIO.playing = false; playNext(); });
AUDIO.el.addEventListener('error', () => { AUDIO.playing = false; playNext(); });

function setSpeaking(on) {
  $('#orb').classList.toggle('speaking', on);
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
  const buf = new Uint8Array(await blob.arrayBuffer());
  let bin = '';
  for (let i = 0; i < buf.length; i += 0x8000)
    bin += String.fromCharCode.apply(null, buf.subarray(i, i + 0x8000));
  // 转写结果由服务端的 asr 事件送回（02-chat 负责填框 / 自动发送）
  await cmd('voice.transcribe', { data_b64: btoa(bin), ext });
}

async function toggleMic() {
  if (!S.asr.enabled) { toast('还没有配置语音识别服务', 'err'); return; }
  if ($('#btn-mic').disabled) return;
  if (S.recording) return stopRecording();
  return startRecording();
}

async function startRecording() {
  stopAudio();
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
      return;
    }
  }
  S.recording = true;
  paintComposer();
}

async function stopRecording() {
  S.recording = false;
  paintComposer();
  if (MIC.host) {
    try {
      const r = await cmd('voice.record.stop');
      if (r.seconds && r.seconds < 1) { toast('太短了，再说一次？'); return; }
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
  if (!clip || clip.secs < 0.6 || !clip.blob.size) { toast('太短了，再说一次？'); return; }
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
  const buf = new Uint8Array(await f.arrayBuffer());
  let bin = '';
  for (let i = 0; i < buf.length; i += 0x8000)
    bin += String.fromCharCode.apply(null, buf.subarray(i, i + 0x8000));
  try {
    showTyping('正在听写');
    const r = await cmd('voice.transcribe', {
      data_b64: btoa(bin),
      ext: (f.name.split('.').pop() || 'wav').toLowerCase(),
    });
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
function setEmotion(name, fromVoice) {
  const emo = (name || 'neutral').toLowerCase();
  S.emotion = emo;
  $('#orb').dataset.emo = emo;
  $('#mood-text').textContent = EMO_ZH[emo] || emo;
  // 光球和 Live2D 是同一个情绪的两张脸：表情映射写在角色卡 live2d.emo 里
  l2dSetEmotion(emo);
  if (!fromVoice) {
    if (moodTimer) clearTimeout(moodTimer);
    // 情绪是回合内的状态：过一会儿回到平静，但保留当前值到下一句
    moodTimer = setTimeout(() => {
      if (!AUDIO.playing) {
        $('#orb').dataset.emo = 'neutral';
        $('#mood-text').textContent = EMO_ZH.neutral;
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
  $('#orb').dataset.emo = S.emotion || 'neutral';
  $('#stage-mem').textContent = `记忆 ${S.memory.length} 条`;
  const av = $('#avatar');
  if (p.avatar_url) {
    av.src = p.avatar_url;
    av.classList.remove('hidden');
    $('#face').classList.add('hidden');
  } else {
    av.classList.add('hidden');
    $('#face').classList.remove('hidden');
  }
  const pro = $('#btn-proactive');
  pro.classList.toggle('on', !!p.proactive);
  $('#proactive-v').textContent = p.proactive ? `每 ${p.idle_minutes || 30} 分钟` : '关';
  if (!document.querySelector('#orb').classList.contains('speaking')) setSpeaking(false);
  // 舞台形态跟着角色卡走：配了模型就换 Live2D，没配就是光球
  trigRefresh();
  l2dApply();
}

/* ---------------- 事件 ---------------- */
onEvent((m) => {
  if (m.t === 'voice') enqueueClip({ url: m.url, text: m.text, emo: m.emo });
  else if (m.t === 'voice_stop') stopAudio();
  else if (m.t === 'voice_error') toast('语音合成失败：' + (m.d || ''), 'err', 5000);
  else if (m.t === 'turn_begin') {
    setEmotion('thinking', true);
  }
});
