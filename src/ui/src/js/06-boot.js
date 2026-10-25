'use strict';
/* =====================================================================
   06-boot —— 启动、连接、以及 file:// 下的一份演示数据
   ===================================================================== */

/* ---------------- 演示模式 ----------------
   直接用浏览器打开 src/ui/index.html 时没有后端；这时给一份演示数据，
   方便调界面（真实运行请用 agent.exe 或 --headless PORT）。 */
const DEMO = {
  personas: [{ id: 'xier', name: '曦儿', subtitle: '住在屏幕另一边的人', accent: '#8b7cff', avatar_url: '', active: true }],
  persona: {
    id: 'xier', name: '曦儿', subtitle: '住在屏幕另一边的人', accent: '#8b7cff',
    greeting: '……你终于来了。今天过得怎么样？', user_name: '主人',
    prompt: '', voice: '', proactive: true, idle_minutes: 40,
  },
  memory: [{ t: Date.now() / 1000 - 86400, text: '她记得你喜欢在深夜聊天' }],
  history: [],
};

function mockState() {
  return Object.assign({ ok: true, t: 'hello', active: 'xier', busy: false, emotion: 'neutral',
    model: 'demo:companion', providers: [], thinking: 0, voice_reply: false, asr_auto_send: true,
    tts: { kind: 'off' }, asr: { kind: 'off' }, voice: { tts: false, asr: false, recording: false } }, DEMO);
}

async function mockCmd(name, args) {
  const wait = (ms) => new Promise(r => setTimeout(r, ms));
  if (name === 'state') return mockState();
  if (name === 'send') {
    const text = args.text || '';
    handleEvent({ t: 'turn_begin' });
    await wait(320);
    handleEvent({ t: 'answer_begin' });
    const reply = `收到：「${text}」。\n*她把杯子往你手边推了推*\n\n这里是演示模式，接上模型以后，这句话会由她真的说出来。`;
    for (let i = 0; i < reply.length; i += 4) {
      handleEvent({ t: 'answer', d: reply.slice(i, i + 4) });
      await wait(18);
    }
    handleEvent({ t: 'emo', name: 'happy' });
    handleEvent({ t: 'turn_end', natural: true });
    return { ok: true };
  }
  if (name === 'stop') { handleEvent({ t: 'turn_end' }); return { ok: true }; }
  if (name === 'voice.record.start') { S.recording = true; return { ok: true }; }
  if (name === 'voice.record.stop') { S.recording = false; return { ok: true, path: '', seconds: 2 }; }
  if (name === 'voice.transcribe') { handleEvent({ t: 'asr', text: '演示模式听写结果', auto_send: false }); return { ok: true, text: '演示模式听写结果' }; }
  if (name === 'ui.pref') return Object.assign({ ok: true }, args);
  return { ok: true };
}

/* ---------------- 启动 ---------------- */
(async function boot() {
  wireGlobal();
  applyAccent('#8b7cff');
  if (MOCK) {
    S.connected = false;
    await refreshState();
    paintAll();
    toast('演示模式：没有后端，界面可以点，模型不会真的回话', '', 5200);
    return;
  }
  connect();
  try {
    await refreshState();
  } catch (e) {
    toast('连不上本机服务：' + e.message, 'err', 6000);
    setTimeout(() => refreshState().catch(() => { }), 1200);
  }
})();

/* 首屏没有模型时，把设置入口顶到最前面 */
onEvent((m) => {
  if (m.t === 'hello' && !MOCK && !S.model) {
    setTimeout(() => toast('还没有接入模型服务，点左下角齿轮配置', '', 5000), 600);
  }
});
