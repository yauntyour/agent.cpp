'use strict';
/* =====================================================================
   11-im —— IM 接入

   设置 → IM 接入 是一列渠道；点一个进它自己的子页，里面是：
   状态、扫码登录（二维码怎么来的见各 kind 的说明）、凭据、白名单。
   新增一个渠道也是一层子页（添加表单）。
   ===================================================================== */

const IM_KINDS = [
  ['off', '关闭'],
  ['wechat', '微信（iLink / ClawBot）'],
  ['telegram', 'Telegram'],
  ['onebot', 'QQ（OneBot）'],
  ['wecom', '企业微信'],
  ['bridge', 'HTTP 桥'],
];

/* 渠道媒体能力的统一说明；真正的解析/下载/发送在服务端（agent-im）做 */
const IM_MEDIA_NOTE = '支持图片 / 语音 / 视频 / 文件 / 表情包收发（由服务端处理）。';

const IM_HELP = {
  wechat: '腾讯官方个人号 Bot API（iLink / ClawBot，2026-03 开放）。保存后点上面的二维码，'
    + '用手机微信扫码并在手机上确认；凭证只进本地加密库，之后走 35 秒长轮询收消息。',
  telegram: 'Telegram Bot API。BotFather 给的 token 填在下面；国内直连 api.telegram.org 不通时'
    + '必须挂代理（或填自建反代地址）。消息走长轮询，图片、语音、视频、文件、贴纸都由服务端接送。',
  onebot: 'NapCat / go-cqhttp / Lagrange 的 HTTP API 地址。事件用「反向 HTTP」推到这里：'
    + 'http://127.0.0.1:<服务端口>/api/im/<渠道 id>。登录在 NapCat 自己的界面里完成，'
    + '这里只查一次 get_login_info 报告「已登录」，不轮询二维码。',
  wecom: '企业微信自建应用：回调要公网地址（frp / ngrok），后台填 Token 与 EncodingAESKey；'
    + '发送走 cgi-bin/message/send，长回合不会被 5 秒限制卡住。',
  bridge: '任何本地桥（个人微信 hook / wechaty / 自己的脚本）POST 到 /api/im/<渠道 id>，'
    + '回复 POST 回这里填的地址；桥可以把登录二维码 POST 上来（{"qr": "<base64>"}）。',
};

const QR_TEXT = {
  wait: '等手机扫码',
  scaned: '扫到了，请在手机上确认',
  online: '已登录',
  expired: '二维码过期了',
  error: '取二维码失败',
  unsupported: '这个渠道不需要扫码',
};

/* 私聊 / 群聊分开的白名单策略（字段含义见 src/agent-im.hpp 的 channel） */
const PRIVATE_MODES = [
  ['open', '谁都能私聊'],
  ['allow', '仅白名单用户'],
];
const GROUP_MODES = [
  ['off', '关闭（群里不响应）'],
  ['mention', '任何人 @ 她就回（不用白名单）'],
  ['allow_mention', '白名单用户 @ 她才回'],
  ['allow', '白名单用户任意消息都回（不用 @）'],
];
function imModeText(table, v) {
  const hit = table.find(([k]) => k === v);
  return hit ? hit[1] : v;
}
function imPolicyText(c) {
  const pm = c.private_mode === 'allow'
    ? '私聊白名单 ' + ((c.private_allow || []).length || 0)
    : '私聊开放';
  let gm = c.group_mode === 'mention' ? '群聊 @ 即回'
    : c.group_mode === 'allow_mention' ? '群聊白名单 @'
    : c.group_mode === 'allow' ? '群聊白名单'
    : '群聊关闭';
  const gids = (c.group_ids || []).length ? ' · ' + c.group_ids.length + ' 个群' : '';
  return pm + ' · ' + gm + gids;
}

function imKindName(kind) {
  const hit = IM_KINDS.find(([v]) => v === kind);
  return hit ? hit[1] : (kind || '未配置');
}

function imStatusText() {
  const live = (S.im || []).filter(c => c.enabled && c.kind && c.kind !== 'off');
  return live.length ? `${live.length} 个已开启` : '未接入';
}

/* ---------------- IM 接入：渠道列表 ---------------- */
function imSection() {
  const sect = el('div', 'sect');
  const list = S.im || [];
  if (!list.length) {
    const empty = el('div', 'empty',
      '还没有接入。她只会待在桌面窗口里；接一个渠道，就能在手机上跟她说话 —— '
      + '同一个人、同一段会话、同一份记忆。');
    empty.style.lineHeight = '1.7';
    sect.appendChild(empty);
  }
  for (const c of list) sect.appendChild(imChannelRow(c));
  sect.appendChild(el('div', 'btn-row', actionBtn('＋ 添加渠道', openImNew, 'btn')));
  return sect;
}

function imChannelRow(c) {
  const live = c.enabled && c.kind && c.kind !== 'off';
  const row = el('button', 'mod-row');
  const left = el('div', 'mod-main');
  left.appendChild(el('div', 'mod-name', `${c.id} · ${imKindName(c.kind)}`));
  const bits = [];
  if (!live) bits.push('未启用');
  else if (c.state === 'error') bits.push('出错');
  else if (c.state === 'running') bits.push('运行中');
  else bits.push('已停止');
  if ((c.kind === 'wechat' || c.kind === 'onebot' || c.kind === 'bridge') && c.has_bot) {
    bits.push('已登录' + (c.bot_id ? ' ' + c.bot_id : ''));
  }
  if (c.persona) {
    const p = (S.personas || []).find(x => x.id === c.persona);
    bits.push('角色 ' + (p ? (p.name || p.id) : c.persona));
  }
  bits.push(`收 ${c['in'] || 0} · 发 ${c.out || 0}`);
  bits.push(imPolicyText(c));
  left.appendChild(el('div', 'mod-sub', bits.join(' · ')));
  row.append(left, el('span', 'mod-go', '›'));
  row.onclick = () => pushDrawer(c.id, 'settings/im/' + c.id, (body) => {
    body.appendChild(drawerBack('IM 接入'));
    body.appendChild(imDetail((S.im || []).find(x => x.id === c.id) || c));
  });
  return row;
}

function openImNew() {
  pushDrawer('添加渠道', 'settings/im/new', (body) => {
    body.appendChild(drawerBack('IM 接入'));
    body.appendChild(imForm({ id: '', kind: 'wechat', enabled: true, allow: [], limit: 0 }, '添加', async (state) => {
      const id = (state.id || '').trim();
      if (!/^[A-Za-z0-9_-]{1,32}$/.test(id)) throw new Error('渠道 id 只能用字母、数字、- 和 _');
      await cmd('im.set', { id, patch: state });
      await refreshState();
      toast(state.kind === 'wechat' ? '已添加，接下来扫码登录' : '已添加', 'ok');
      popDrawer();
    }));
  });
}

/* ---------------- 渠道详情 ---------------- */
function imDetail(c) {
  const box = el('div');

  const live = c.enabled && c.kind && c.kind !== 'off';
  const head = el('div', 'card' + (live ? ' on' : ''));
  const top = el('div', 'card-top');
  const left = el('div');
  left.appendChild(el('div', 'card-name', imKindName(c.kind)));
  left.appendChild(el('div', 'card-sub', c.webhook ? `回调 ${c.webhook}` : (c.api_base || c.bot_base || '默认地址')));
  top.appendChild(left);
  const stateText = !live ? '未启用' : (c.state === 'error' ? '出错' : (c.state === 'running' ? '运行中' : '已停止'));
  top.appendChild(el('span', 'badge' + (live && c.state !== 'error' ? ' ok' : (live ? ' warn' : '')), stateText));
  head.appendChild(top);
  head.appendChild(el('div', 'card-sub', `收 ${c['in'] || 0} · 发 ${c.out || 0}`));
  if (c.last_error) head.appendChild(el('div', 'tip', c.last_error));
  box.appendChild(head);

  if (c.kind === 'wechat' || c.kind === 'onebot' || c.kind === 'bridge') box.appendChild(qrPanel(c));

  box.appendChild(imForm(Object.assign({}, c), '保存', async (state) => {
    state.id = c.id;
    await cmd('im.set', { id: c.id, patch: state });
    await refreshState();
    toast('已保存', 'ok');
  }));

  const row = el('div', 'btn-row');
  if (live) {
    row.appendChild(actionBtn('测试连接', async () => {
      const r = await cmd('im.test', { id: c.id });
      toast('通了：' + JSON.stringify(r.info || {}), 'ok', 5000);
      await refreshState();
    }, 'btn sm'));
  }
  row.appendChild(actionBtn(c.enabled ? '停用' : '启用', async () => {
    await cmd('im.set', { id: c.id, patch: { enabled: !c.enabled } });
    await refreshState();
  }, 'btn sm ghost'));
  row.appendChild(actionBtn('删除', async () => {
    if (!confirm(`删除渠道「${c.id}」？配对的凭据会一起删掉。`)) return;
    await cmd('im.del', { id: c.id });
    await refreshState();
    popDrawer();
  }, 'btn sm danger'));
  box.appendChild(row);
  return box;
}

/* ---------------- 扫码登录面板 ----------------
   二维码不归我们造：微信走 iLink 的 get_bot_qrcode（服务端把返回的 URL 编成二维码），
   桥由桥自己 POST 上来。OneBot（NapCat）没有取码接口 —— 登录在 NapCat 里做，
   这里只查一次 get_login_info，不轮询、不显示二维码。 */
/* 扫码成功的在线状态只主动刷新一次状态：refreshState → paintDrawer 会重建
   这个面板，新面板再看到 online 又调 refreshState —— 不设闸就是无限重建，
   表现就是二维码面板一直闪「正在取二维码…」。 */
const QR_ONLINE_REFRESHED = new Set();

function qrPanel(c) {
  const wrap = el('div', 'qr-panel');
  // QQ 走 NapCat，扫码发生在 NapCat 自己的界面里：这里只是【检查一次】登录状态，
  // 不轮询、更不问二维码。否则面板会一直「取二维码」并且和 online 互相覆盖。
  const onebot = c.kind === 'onebot';
  const img = document.createElement('img');
  img.className = 'qr-img';
  img.alt = '登录二维码';
  img.style.display = 'none'; // 拿到图再显示，否则会露一个碎图标
  const state = el('div', 'qr-state', onebot ? '正在检查 NapCat 登录状态…' : '正在取二维码…');
  const hint = el('div', 'tip', '');
  wrap.append(img, state, hint);

  let stopped = false;
  let timer = null;
  const stop = () => { stopped = true; if (timer) clearTimeout(timer); };

  const tick = async (refresh) => {
    // 抽屉被重绘时这个节点会被换掉：检测到就自己收摊，别留定时器
    if (stopped || !img.isConnected) { stop(); return; }
    try {
      const r = await cmd('im.qr', { id: c.id, refresh: !!refresh });
      if (r.qr) {
        img.src = r.qr;
        img.style.display = '';
      } else {
        img.removeAttribute('src');
        img.style.display = 'none';
      }
      let text = QR_TEXT[r.state] || r.state;
      if (onebot && (r.state === 'wait' || r.state === 'scaned'))
        text = 'NapCat 已连接，等待账号登录';
      if (r.state === 'online' && r.account) text += '：' + r.account;
      state.textContent = text;
      hint.textContent = r.hint || '';
      if (r.state === 'online' || r.state === 'unsupported' || r.state === 'error') {
        stop();
        // 只有真需要扫码登录的渠道才回刷状态（让渠道行的「已登录」更新）；
        // QQ 的状态来自 NapCat，渠道卡本身没有变，不需要重建页面。
        if (r.state === 'online' && !onebot && !QR_ONLINE_REFRESHED.has(c.id)) {
          QR_ONLINE_REFRESHED.add(c.id);
          refreshState().catch(() => {});
        }
        return;
      }
    } catch (e) {
      state.textContent = e.message;
    }
    // QQ 只查一次：连接/登录状态在 NapCat 那边变，点「重新检查」才再查
    if (!stopped && !onebot) timer = setTimeout(() => tick(false), 2500);
  };

  const bar = el('div', 'btn-row');
  bar.appendChild(actionBtn(onebot ? '重新检查' : '刷新二维码', async () => {
    stop();
    stopped = false;
    QR_ONLINE_REFRESHED.delete(c.id);
    state.textContent = onebot ? '正在检查 NapCat 登录状态…' : '正在取二维码…';
    tick(true);
  }, 'btn sm ghost'));
  wrap.appendChild(bar);
  // 第一帧要等节点真的插进抽屉：isConnected 为假会直接收摊
  setTimeout(() => tick(false), 0);
  return wrap;
}

/* ---------------- 表单（新建 / 编辑共用） ---------------- */
function imForm(initial, submitLabel, onSave) {
  // 新字段：私聊 / 群聊分开的白名单策略。旧渠道只有 allow 时按旧语义铺开：
  // 空 = 私聊开放 + 群聊关闭；非空 = 私聊白名单 + 群里白名单用户 @ 才回。
  const state = Object.assign({
    allow: [], limit: 0, enabled: true, proxy: '', persona: '',
    private_mode: 'open', private_allow: [], group_mode: 'off', group_allow: [], group_ids: [],
  }, initial);
  if (state.persona == null) state.persona = '';
  if (!Array.isArray(state.private_allow)) state.private_allow = [];
  if (!Array.isArray(state.group_allow)) state.group_allow = [];
  if (!Array.isArray(state.group_ids)) state.group_ids = [];
  const hasNewPolicy = Array.isArray(initial.private_allow) || Array.isArray(initial.group_allow) ||
    initial.private_mode != null || initial.group_mode != null;
  if (!hasNewPolicy && Array.isArray(initial.allow) && initial.allow.length) {
    state.private_mode = 'allow';
    state.private_allow = initial.allow.slice();
    state.group_mode = 'allow_mention';
    state.group_allow = initial.allow.slice();
  }
  const box = el('div');
  const fields = el('div');

  const mk = (key, ph, type) => {
    const v = type === 'password' ? '' : (state[key] == null ? '' : state[key]);
    const input = textInput(v, ph, type === 'number' ? 'number' : (type === 'password' ? 'password' : 'text'));
    input.addEventListener('input', () => {
      state[key] = type === 'number' ? parseInt(input.value || '0', 10) : input.value;
    });
    return input;
  };
  const mkList = (key, ph) => {
    const input = textInput((state[key] || []).join(','), ph, 'text');
    input.addEventListener('input', () => {
      state[key] = input.value.split(/[,，\s]+/).map(s => s.trim()).filter(Boolean);
    });
    return input;
  };
  const mkMode = (key, table) => {
    const sel = document.createElement('select');
    for (const [v, t] of table) {
      const o = document.createElement('option');
      o.value = v; o.textContent = t;
      if (state[key] === v) o.selected = true;
      sel.appendChild(o);
    }
    if (!table.some(([v]) => v === state[key]) && table.length) {
      state[key] = table[0][0];
      sel.value = table[0][0];
    }
    sel.onchange = () => { state[key] = sel.value; };
    return sel;
  };

  const render = () => {
    fields.innerHTML = '';
    const k = state.kind;
    const keep = '已保存；留空则保持不变';
    if (k === 'wechat') {
      fields.append(
        field('登录状态',
          el('div', 'tip', state.has_bot ? `已登录${state.bot_id ? '：' + state.bot_id : ''}` : '还没登录：点上面的二维码扫一下'),
          state.has_bot ? '会话过期（ret -14）时重新扫一次就行。' : '扫码成功后凭证写进本地加密库，不会出现在配置文件里。'),
        field('API 根', mk('bot_base', 'https://ilinkai.weixin.qq.com'), '扫码后服务端会返回 baseurl，一般不用手填。'),
      );
    } else if (k === 'telegram') {
      fields.append(
        field('Bot Token', mk('token', state.has_token ? keep : '123456:ABC-DEF…', 'password')),
        field('代理', mk('proxy', 'http://127.0.0.1:7890'), '国内直连 api.telegram.org 不通，必须挂代理。'),
        field('API 根', mk('api_base', 'https://api.telegram.org'), '只有走自建反代时才填。'),
      );
    } else if (k === 'onebot') {
      fields.append(
        field('HTTP API 地址', mk('api_base', 'http://127.0.0.1:3000')),
        field('Access Token', mk('token', state.has_token ? keep : '没设就留空', 'password'),
          '对端配置里的 access token，发消息时放在 Authorization 头。'),
      );
    } else if (k === 'wecom') {
      fields.append(
        field('回调 Token', mk('token', state.has_token ? keep : '后台「接收消息」里的 Token', 'password')),
        field('EncodingAESKey', mk('aes_key', state.has_aes_key ? keep : '43 位', 'password')),
        field('企业 ID', mk('corp_id', 'ww0123456789abcdef')),
        field('应用 AgentId', mk('agent_id', '1000002', 'number')),
        field('应用 Secret', mk('corp_secret', state.has_corp ? keep : '应用详情里的 Secret', 'password')),
        field('API 根', mk('api_base', 'https://qyapi.weixin.qq.com')),
      );
    } else if (k === 'bridge') {
      fields.append(
        field('桥的接收地址', mk('api_base', 'http://127.0.0.1:8788/send')),
        field('Secret', mk('token', state.has_token ? keep : '建议必填', 'password'),
          '桥 POST 进来时带 X-Im-Secret；不填的话只接受本机直连。'),
      );
    }
    if (k && k !== 'off') {
      const mediaTip = el('div', 'im-media-tip', IM_MEDIA_NOTE);
      fields.appendChild(mediaTip);
      const policy = el('div', 'im-policy');
      policy.appendChild(el('div', 'sect-h', '谁能说话'));
      policy.appendChild(field('私聊', mkMode('private_mode', PRIVATE_MODES),
        '默认谁都能私聊；选「仅白名单」后只回「私聊白名单」里的人。'));
      policy.appendChild(field('私聊白名单', mkList('private_allow', '平台 user id，逗号分隔'),
        'Telegram 数字 id / QQ 号 / 企业微信 userid / 桥的 user_id。'));
      // 企业微信自建应用只做 1:1，群聊策略对它没有意义
      if (k !== 'wecom') {
        policy.appendChild(field('群聊', mkMode('group_mode', GROUP_MODES),
          '群里默认不响应；「任何人 @」适合公开群，「白名单」适合只想理熟人。'));
        policy.appendChild(field('群聊用户白名单', mkList('group_allow', '平台 user id，逗号分隔'),
          '群聊模式用到「白名单」时才生效。'));
        policy.appendChild(field('只在这些群', mkList('group_ids', '群号 / 群 chat id，逗号分隔；空 = 所有群'),
          '最外层的门：填了就只在这些群里响应，不管上面的群聊模式。'));
      }
      policy.appendChild(field('每条上限', mk('limit', '0 = 平台默认', 'number'),
        '超长回复按句末自动分条，最多四条。'));
      fields.appendChild(policy);
    }
    const tip = el('div', 'tip', IM_HELP[k] || '选一种接法。凭据加密保存在 .agent/.crypt 里，不进 agent.json。');
    tip.style.lineHeight = '1.7';
    fields.appendChild(tip);
    const row = el('div', 'btn-row');
    row.appendChild(actionBtn(submitLabel, async () => { await onSave(state); }, 'btn primary'));
    fields.appendChild(row);
  };

  const idInput = textInput(state.id || '', 'wx / qq / tg', 'text');
  idInput.addEventListener('input', () => { state.id = idInput.value.trim(); });
  if (state.id) {
    idInput.disabled = true;
    box.appendChild(field('渠道 id', idInput, '回调地址 /api/im/<id> 里的那一段，建好之后不改。'));
  } else {
    box.appendChild(field('渠道 id', idInput, '回调地址 /api/im/<id> 里的那一段。'));
  }
  const kindSel = document.createElement('select');
  for (const [v, t] of IM_KINDS) {
    const o = document.createElement('option');
    o.value = v;
    o.textContent = t;
    if (state.kind === v) o.selected = true;
    kindSel.appendChild(o);
  }
  kindSel.onchange = () => { state.kind = kindSel.value; render(); };
  box.appendChild(field('类型', kindSel));

  // 绑定角色：空 = 跟随桌面当前角色；选了之后这个渠道的消息交给它处理（各角色独立并行）
  const personaSel = document.createElement('select');
  const personaChoices = [['', '跟随当前角色']].concat(
    (S.personas || []).map(p => [p.id, (p.name || p.id) + (p.id === S.active ? '（当前）' : '')]));
  for (const [v, t] of personaChoices) {
    const o = document.createElement('option');
    o.value = v;
    o.textContent = t;
    if (state.persona === v) o.selected = true;
    personaSel.appendChild(o);
  }
  personaSel.onchange = () => { state.persona = personaSel.value; };
  box.appendChild(field('绑定角色', personaSel,
    '这个渠道的消息交给哪个角色处理；留空 = 跟随桌面当前选中的角色。'));
  box.appendChild(fields);
  render();
  return box;
}
