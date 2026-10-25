// WebUI 前端逻辑 —— 从原单文件 webui.html 拆出
        // ============ 全局状态 ============
        const state = {
            user: "User", agentName: "assistant",
            sessions: [], currentSessionId: null, messages: [],
            settings: {}, models: [], currentModel: null,
            thinkMode: false, tools: [], todos: [], channels: [],
            isTyping: false, pendingImages: [],
            assistantMsgId: null,
            streamBuffers: { content: '', thinking: '', thinkings: [], toolOutputs: [] },
            contextUsage: { total: 0, max: 128000 },
            memoryActive: false,
            memoryCreatedAt: null,
            _scrollAtBottom: true,
            providers: [],
            currentProviderId: null,
            channelDrivers: [], channelStatus: [],
            theme: localStorage.getItem('theme') || 'dark',
            // 工具权限系统
            permRules: [], permDefaults: {}, permPending: [], permQueue: [],
            permModalId: null, permTimeoutSec: 120
        };
        const API = '/api';

        // ---- 极简 HTTP 封装（替代 axios，界面不依赖任何外部库） ----
        // 用法与 axios 一致：axios.get(url) / axios.post(url, obj) → { data }
        // 失败时抛出的 Error 带 response.{status,data}，调用方读 e.response?.data?.error
        const axios = {
            async request(method, url, body) {
                const init = { method, headers: { 'Accept': 'application/json' } };
                if (body !== undefined) {
                    init.headers['Content-Type'] = 'application/json';
                    init.body = JSON.stringify(body);
                }
                const res = await fetch(url, init);
                const text = await res.text();
                let data = null;
                if (text) { try { data = JSON.parse(text); } catch (e) { data = text; } }
                if (!res.ok) {
                    const message = (data && data.error) ? data.error : ('HTTP ' + res.status);
                    const err = new Error(message);
                    err.response = { status: res.status, data };
                    throw err;
                }
                return { data, status: res.status };
            },
            get(url) { return this.request('GET', url); },
            post(url, body) { return this.request('POST', url, body); },
        };

        // ============ DOM 缓存 ============
        const $ = (sel) => document.querySelector(sel);
        const $$ = (sel) => document.querySelectorAll(sel);
        const els = {
            sessionList: $('#session-list'), newSessionBtn: $('#new-session-btn'),
            clearSessionBtn: $('#clear-session-btn'), summarySessionBtn: $('#summary-session-btn'),
            messagesContainer: $('#messages-container'), userInput: $('#user-input'),
            sendBtn: $('#send-btn'), thinkToggle: $('#think-toggle'),
            currentUserDisplay: $('#current-user-display'),
            todosList: $('#todos-list'), newTodoInput: $('#new-todo-input'), addTodoBtn: $('#add-todo-btn'),
            channelCards: $('#channel-cards'), channelEditor: $('#channel-editor'),
            channelCount: $('#channel-count'), channelNewBtn: $('#channel-new-btn'),
            currentSessionIdDisplay: $('#current-session-id-display'),
            modelDropdownBtn: $('#model-dropdown-btn'), modelDropdownMenu: $('#model-dropdown-menu'),
            modelDropdownList: $('#model-dropdown-list'), modelNameDisplay: $('#model-name-display'),
            connectionStatusDot: $('#connection-status-dot'),
            providerDropdownBtn: $('#provider-dropdown-btn'), providerDropdownMenu: $('#provider-dropdown-menu'),
            providerNameDisplay: $('#provider-name-display'), providerDropdownList: $('#provider-dropdown-list'),
            providersList: $('#providers-list'),
            attachBtn: $('#attach-btn'), fileUpload: $('#file-upload'),
            imagePreviewContainer: $('#image-preview-container'),
            settingsFormContainer: $('#settings-form-container'), saveSettingsBtn: $('#save-settings-btn'),
            dataStatsBtn: $('#data-stats-btn'), toolsToggleList: $('#tools-toggle-list'),
            permTabBadge: $('#perm-tab-badge'), permPendingList: $('#perm-pending-list'),
            permRulesList: $('#perm-rules-list'), permDefaultsList: $('#perm-defaults-list'),
            permToolList: $('#perm-tool-list'),
            permApproveOverlay: $('#perm-approve-overlay'), permApproveBody: $('#perm-approve-body'),
            permApproveCloseBtn: $('#close-perm-approve-btn'),
            settingsGearBtn: $('#settings-gear-btn'), closeSettingsBtn: $('#close-settings-btn'),
            settingsOverlay: $('#settings-overlay'), overlayBackdrop: $('#overlay-backdrop'),
            statsOverlay: $('#stats-overlay'), statsOverlayBody: $('#stats-overlay-body'),
            channelQrOverlay: $('#channel-qr-overlay'), channelQrOverlayBody: $('#channel-qr-overlay-body'),
            closeChannelQrBtn: $('#close-channel-qr-btn'),
            closeStatsBtn: $('#close-stats-btn'),
            fileBrowserBtn: $('#file-browser-btn'),
            themeToggleBtn: $('#theme-toggle-btn'),
            fbPanel: $('#file-browser-panel'),
            fbBreadcrumb: $('#fb-breadcrumb'),
            fbListing: $('#fb-listing'),
            fbRefresh: $('#fb-refresh'),
            fbCollapse: $('#fb-collapse'),
            loadingOverlay: $('#loading-overlay'),
            scrollBottomBtn: $('#scroll-bottom-btn'),
            contextUsage: $('#context-usage'),
            contextBarFill: $('#context-bar-fill'),
            contextText: $('#context-text'),
        };

        // ============ 工具函数 ============
        function toast(msg, type) {
            const icons = { success: 'fa-circle-check', error: 'fa-circle-exclamation', info: 'fa-circle-info' };
            const t = document.createElement('div');
            t.className = 'toast toast-' + (type || 'info');
            t.innerHTML = `<i class="fa-solid ${icons[type] || icons.info}"></i> ${msg}`;
            $('#toast-container').appendChild(t);
            setTimeout(() => { t.style.opacity = '0'; t.style.transition = 'opacity 0.3s'; setTimeout(() => t.remove(), 300); }, 3000);
        }
        // 排障信息只写浏览器控制台：界面不再暴露日志终端
        function log(msg, type) {
            (type === 'error' ? console.warn : console.log)('[agent.cpp] ' + msg);
        }

        // ============ 桌面窗口外壳 ============
        // 页面跑在 WebView2 里时，无边框窗口的拖拽 / 缩放 / 最小化 / 最大化 /
        // 关闭都交给原生处理（命令在 src/desktop/webview2.cpp 的 handle_ipc 里）。
        const wv = (window.chrome && window.chrome.webview) ? window.chrome.webview : null;
        function winPost(cmd) { if (wv) wv.postMessage(cmd); }
        function initWindowShell() {
            if (!wv) return; // 浏览器里打开时保持普通网页的样子
            document.body.classList.add('is-webview');
            $('#win-min').onclick = () => winPost('win:min');
            $('#win-max').onclick = () => winPost('win:max');
            $('#win-close').onclick = () => winPost('win:close');

            const bar = $('#titlebar');
            bar.addEventListener('mousedown', (e) => {
                if (e.button !== 0 || e.target.closest('.tb-controls')) return;
                winPost('win:drag');
            });
            bar.addEventListener('dblclick', (e) => {
                if (e.target.closest('.tb-controls')) return;
                winPost('win:max');
            });
            $$('#resize-layer .rs').forEach(el => {
                el.addEventListener('mousedown', (e) => {
                    if (e.button !== 0) return;
                    e.preventDefault();
                    winPost('win:resize:' + el.dataset.dir);
                });
            });
            if (wv.addEventListener) {
                wv.addEventListener('message', (e) => {
                    const maxed = e.data === 'win:state:max';
                    const icon = document.getElementById('win-max-icon');
                    if (icon) {
                        icon.setAttribute('x', maxed ? '2.5' : '0.5');
                        icon.setAttribute('y', maxed ? '2.5' : '0.5');
                        icon.setAttribute('width', maxed ? '6' : '9');
                        icon.setAttribute('height', maxed ? '6' : '9');
                    }
                    const btn = $('#win-max');
                    if (btn) btn.title = maxed ? '还原' : '最大化';
                });
            }
        }
        function esc(s) { if (!s) return ''; return s.replace(/[&<>"']/g, m => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[m]); }
        function fixToolTags(text) {
            if (!text) return text;
            let r = text.replace(/<system_output>([\s\S]*?)<\/system_output>/g, (_, content) => {
                const trimmed = content.trim();
                const hasErr = trimmed.includes('[TOOL_ERR]');
                const hasDone = trimmed.includes('[TOOL_DONE]') || trimmed.includes('[CS_DONE]');
                let statusIcon = '', statusClass = '';
                if (hasErr) { statusIcon = '✗'; statusClass = 'err'; }
                else if (hasDone) { statusIcon = '✓'; statusClass = 'ok'; }
                const cleanContent = trimmed
                    .replace(/\[TOOL_DONE\]\n?/g, '')
                    .replace(/\[TOOL_ERR\]\n?/g, '')
                    .replace(/\[CS_DONE\]\n?/g, '')
                    .trim();
                return `\n<details class="sys-output-block">\n<summary>\n<i class="fa-solid fa-chevron-right"></i>\n<i class="fa-solid fa-terminal" style="font-size:11px;"></i>\n<span style="margin-left:4px;font-weight:500;font-size:12px;">系统输出</span>\n<span style="margin-left:auto;font-size:12px;" class="sysout-status ${statusClass}">${statusIcon}</span>\n</summary>\n<div class="sysout-inner">\n<pre><code>${esc(cleanContent)}</code></pre>\n</div>\n</details>\n`;
            });
            r = r.replace(/<cs>([\s\S]*?)<\/cs>/g, (_, content) => {
                const trimmed = content.trim();
                const idx = trimmed.indexOf(':');
                let name = trimmed, args = '';
                if (idx !== -1) { name = trimmed.slice(0, idx); args = trimmed.slice(idx + 1); }
                return `\n<details class="cs-block">\n<summary>\n<i class="fa-solid fa-chevron-right"></i>\n<span class="cs-icon"><i class="fa-solid fa-terminal"></i></span>\n<span class="cs-name">${esc(name)}</span>\n<span class="cs-args">${esc(args)}</span>\n</summary>\n<div class="cs-inner">${esc(trimmed)}</div>\n</details>\n`;
            });
            r = r.replace(/<tool>([\s\S]*?)<\/tool>/g, (_, content) => {
                const trimmed = content.trim();
                const idx = trimmed.indexOf(':');
                let name = trimmed, args = '';
                if (idx !== -1) { name = trimmed.slice(0, idx); args = trimmed.slice(idx + 1); }
                return `\n<details class="tool-inline-block">\n<summary>\n<i class="fa-solid fa-chevron-right"></i>\n<span class="tool-inline-icon"><i class="fa-solid fa-screwdriver-wrench"></i></span>\n<span class="tool-inline-name">${esc(name)}</span>\n<span class="tool-inline-args">${esc(args)}</span>\n</summary>\n<div class="tool-inline-inner">${esc(trimmed)}</div>\n</details>\n`;
            });
            return r;
        }

        // ============ 覆盖层管理 ============
        function openOverlay() { els.settingsOverlay.classList.add('open'); els.overlayBackdrop.classList.add('show'); }
        function closeOverlay() {
            els.settingsOverlay.classList.remove('open');
            els.statsOverlay.classList.remove('open');
            els.overlayBackdrop.classList.remove('show');
            closeChannelQrLogin();
            stopPermPoll();
            stopChannelPoll();
        }
        els.settingsGearBtn.onclick = openOverlay;
        els.closeSettingsBtn.onclick = () => {
            els.settingsOverlay.classList.remove('open');
            stopPermPoll();
            stopChannelPoll();
            if (!els.statsOverlay.classList.contains('open')) {
                els.overlayBackdrop.classList.remove('show');
            }
        };
        els.closeStatsBtn.onclick = () => {
            els.statsOverlay.classList.remove('open');
            if (!els.settingsOverlay.classList.contains('open')) {
                els.overlayBackdrop.classList.remove('show');
            }
        };
        els.closeChannelQrBtn.onclick = closeChannelQrLogin;
        els.overlayBackdrop.onclick = closeOverlay;

        // 覆盖层标签切换
        $$('.overlay-tab').forEach(btn => {
            btn.onclick = () => {
                $$('.overlay-tab').forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
                $$('.overlay-panel').forEach(p => p.classList.remove('active'));
                $(`#opanel-${btn.dataset.otab}`).classList.add('active');
                // 权限面板：打开时加载数据并轮询待审批队列
                if (btn.dataset.otab === 'permissions') {
                    loadPermissions();
                    startPermPoll();
                } else {
                    stopPermPoll();
                }
                // 频道面板：打开时拉一次配置与状态，并持续轮询运行状态
                if (btn.dataset.otab === 'channels') {
                    loadChannels();
                    startChannelPoll();
                } else {
                    stopChannelPoll();
                }
            };
        });

        // ============ 统计弹窗 ============
        function openStatsOverlay() {
            els.statsOverlay.classList.add('open');
            els.overlayBackdrop.classList.add('show');
            loadStatsOverlay();
        }
        async function loadStatsOverlay() {
            const container = els.statsOverlayBody;
            container.innerHTML = '<div style="padding:40px 20px;text-align:center;color:var(--text-dim);font-size:13px;"><i class="fa-solid fa-spinner fa-spin" style="margin-right:6px;"></i>加载中...</div>';
            try {
                const r = await axios.get(API + '/data');
                const usages = r.data?.usages || {};
                const entries = Object.entries(usages);
                if (!entries.length) {
                    container.innerHTML = '<div style="padding:40px 20px;text-align:center;color:var(--text-dim);font-size:13px;">暂无使用数据</div>';
                    return;
                }
                let totalPrompt = 0, totalCompletion = 0, totalOverall = 0;
                const cards = entries.map(([k, v]) => {
                    const pc = v.prompt_cost || 0, cc = v.completion_cost || 0, tc = v.total_cost || (pc + cc);
                    totalPrompt += pc; totalCompletion += cc; totalOverall += tc;
                    return { name: k, pc, cc, tc };
                }).sort((a, b) => b.tc - a.tc);

                let html = `
          <div class="stat-card" style="background:rgba(99,102,241,0.08);border-color:rgba(99,102,241,0.3);">
            <div class="stat-card-header">
              <span class="stat-card-title" style="color:var(--accent-soft-text);">总消耗</span>
              <span class="stat-card-total" style="color:#c7d2fe;">${totalOverall.toLocaleString()}</span>
            </div>
            <div class="stat-detail-row"><span>Prompt</span><span class="stat-detail-mono">${totalPrompt.toLocaleString()}</span></div>
            <div class="stat-detail-row"><span>Completion</span><span class="stat-detail-mono">${totalCompletion.toLocaleString()}</span></div>
          </div>
          <div class="stat-section-title">各会话详情</div>
        `;
                const maxVal = cards[0]?.tc || 1;
                cards.forEach(item => {
                    const pct = (item.tc / maxVal) * 100;
                    html += `
            <div class="stat-card">
              <div class="stat-card-header">
                <span class="stat-card-title">${esc(item.name)}</span>
                <span class="stat-card-total">${item.tc.toLocaleString()}</span>
              </div>
              <div class="stat-bar" style="margin-bottom:8px;"><div class="stat-bar-fill" style="width:${pct}%;background:linear-gradient(90deg,var(--accent-color),var(--accent-color));"></div></div>
              <div class="stat-detail-row"><span style="color:var(--text-dim);">Prompt</span><span class="stat-detail-mono">${item.pc.toLocaleString()}</span></div>
              <div class="stat-detail-row"><span style="color:var(--text-dim);">Completion</span><span class="stat-detail-mono">${item.cc.toLocaleString()}</span></div>
            </div>
          `;
                });
                container.innerHTML = html;
            } catch (e) {
                container.innerHTML = `<div style="padding:40px 20px;text-align:center;color:var(--err);font-size:13px;">加载失败: ${esc(e.message)}</div>`;
            }
        }
        els.dataStatsBtn.onclick = openStatsOverlay;

        // ============ 扫码登录弹窗 ============
        let qrLoginChannel = null;
        let qrLoginTimer = null;
        let qrAutoCloseTimer = null;

        // 请求启动频道 Bot（若未运行）并弹出扫码登录窗口
        async function requestChannelQr(name) {
            try {
                await axios.post(API + '/channel/start', { name });
            } catch (e) { /* 忽略：未启动时弹窗也会显示状态 */ }
            openChannelQrLogin(name);
        }
        function openChannelQrLogin(name) {
            qrLoginChannel = name;
            const title = document.getElementById('channel-qr-title');
            if (title) title.textContent = name + ' 扫码登录';
            els.channelQrOverlay.classList.add('open');
            els.overlayBackdrop.classList.add('show');
            stopQrLoginPoll();
            qrLoginTimer = setInterval(refreshQrLoginPanel, 2000);
            refreshQrLoginPanel();
        }
        function closeChannelQrLogin() {
            qrLoginChannel = null;
            stopQrLoginPoll();
            if (qrAutoCloseTimer) { clearTimeout(qrAutoCloseTimer); qrAutoCloseTimer = null; }
            els.channelQrOverlay.classList.remove('open');
            if (!els.settingsOverlay.classList.contains('open') && !els.statsOverlay.classList.contains('open')) {
                els.overlayBackdrop.classList.remove('show');
            }
        }
        function stopQrLoginPoll() {
            if (qrLoginTimer) { clearInterval(qrLoginTimer); qrLoginTimer = null; }
        }
        async function refreshQrLoginPanel() {
            const body = els.channelQrOverlayBody;
            if (!qrLoginChannel || !body) return;
            try {
                const d = (await axios.get(API + '/channel/qr/' + encodeURIComponent(qrLoginChannel))).data;
                const stateText = d.state || 'idle';
                let html = '';
                html += `<div class="channel-hint" style="margin-top:6px;"><i class="fa-solid fa-qrcode" style="margin-right:4px;"></i>机器人状态：<b>${esc(stateText)}</b>${d.running ? '（运行中）' : '（未运行）'}</div>`;
                if (d.qr_svg && stateText === 'waiting_qr') {
                    html += `<div style="text-align:center;margin-top:10px;"><img src="data:image/svg+xml;base64,${d.qr_svg}" style="width:240px;height:240px;background:#fff;padding:8px;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,.25);" alt="登录二维码"><div style="font-size:11px;color:var(--text-dim);margin-top:6px;">请使用微信扫码登录</div></div>`;
                }
                if (d.detail) html += `<div style="font-size:12px;color:var(--text-muted);margin-top:8px;">${esc(d.detail)}</div>`;
                if (d.qr_url && stateText === 'waiting_qr') html += `<div style="font-size:11px;color:var(--text-dim);word-break:break-all;margin-top:4px;">备用链接：${esc(d.qr_url)}</div>`;
                if (stateText === 'running') {
                    html = `<div style="text-align:center;padding:20px 10px;"><div style="font-size:38px;color:var(--ok);"><i class="fa-solid fa-circle-check"></i></div><div style="font-size:14px;color:var(--text-main);margin-top:8px;">登录成功，机器人已运行</div><div style="font-size:11px;color:var(--text-dim);margin-top:4px;">窗口即将自动关闭</div></div>`;
                    if (!qrAutoCloseTimer) qrAutoCloseTimer = setTimeout(() => { if (qrLoginChannel) closeChannelQrLogin(); }, 1500);
                }
                body.innerHTML = html;
            } catch (e) { /* 忽略轮询错误 */ }
        }
        // ============ 频道管理 ============
        // 频道名同时是 token 的存储键，因此创建后不可改名（后端也不接受改名）。
        const CHANNEL_STATE_LABEL = {
            running: '运行中', waiting_qr: '等待扫码', stopping: '正在停止', stopped: '已停止',
            error: '异常', no_token: '缺少 Token', idle: '未启动'
        };
        let channelPollTimer = null;

        function channelStatusOf(name) {
            return (state.channelStatus || []).find(s => s.name === name) || null;
        }
        function channelCardEl(name) {
            if (!els.channelCards) return null;
            return [...els.channelCards.querySelectorAll('.ch-card')].find(el => el.dataset.name === name) || null;
        }
        function channelBadge(ch) {
            const st = channelStatusOf(ch.name);
            const active = ch.status === 'active';
            let cls = 'off', text = '未启用';
            if (active) {
                const s = st ? st.state : 'idle';
                text = CHANNEL_STATE_LABEL[s] || s;
                cls = s === 'running' ? 'ok'
                    : (s === 'error' || s === 'no_token') ? 'err'
                        : (s === 'waiting_qr' || s === 'stopping') ? 'wait' : 'idle';
            }
            return `<span class="ch-pill ${cls}">${esc(text)}</span>`;
        }
        function channelDetailHtml(ch) {
            const st = channelStatusOf(ch.name);
            if (!st) return '';
            const bits = [];
            if (st.detail) bits.push(st.detail);
            if (st.last_error) bits.push(st.last_error);
            if (!bits.length) return '';
            return `<div class="ch-detail">${esc(bits.join(' · '))}</div>`;
        }
        function channelFoot(ch, running) {
            const name = esc(ch.name);
            const startOrStop = running
                ? `<button class="ch-btn" onclick="channelStop('${name}')"><i class="fa-solid fa-stop"></i> 停止</button>`
                : `<button class="ch-btn primary" onclick="channelStart('${name}')"><i class="fa-solid fa-play"></i> 启动</button>`;
            const qr = ch.type === 'wechat'
                ? `<button class="ch-btn" onclick="channelQrLogin('${name}')"><i class="fa-solid fa-qrcode"></i> 扫码</button>`
                : '';
            return `${startOrStop}${qr}
                <button class="ch-btn" onclick="channelSave('${name}')"><i class="fa-solid fa-floppy-disk"></i> 保存</button>
                <button class="ch-btn danger" onclick="channelDelete('${name}')" title="删除频道"><i class="fa-solid fa-trash-can"></i></button>`;
        }

        // channel.config 是自由字段，按值的类型渲染成输入控件
        function channelConfigFields(ch) {
            const cfg = ch.config || {};
            const keys = Object.keys(cfg);
            if (!keys.length) return '';
            return keys.map(k => {
                const v = cfg[k];
                let input;
                if (typeof v === 'boolean')
                    input = `<select class="ch-cfg" data-key="${esc(k)}"><option value="true" ${v ? 'selected' : ''}>true</option><option value="false" ${!v ? 'selected' : ''}>false</option></select>`;
                else if (typeof v === 'number')
                    input = `<input class="ch-cfg" data-key="${esc(k)}" type="number" value="${v}">`;
                else
                    input = `<input class="ch-cfg" data-key="${esc(k)}" value="${esc(String(v))}" spellcheck="false">`;
                return `<div class="ch-field"><label>${esc(k)}</label>${input}</div>`;
            }).join('');
        }

        function renderChannelPanel() {
            if (!els.channelCards) return;
            if (els.channelCount) els.channelCount.textContent = String((state.channels || []).length);
            const list = state.channels || [];
            // 重渲染时保留已展开的卡片
            const expanded = new Set([...els.channelCards.querySelectorAll('.ch-card.expanded')].map(el => el.dataset.name));
            if (!list.length) {
                els.channelCards.innerHTML = `<div class="ch-empty">
                    <i class="fa-solid fa-tower-broadcast"></i>
                    <div>还没有频道</div>
                    <div class="ch-empty-sub">接入 Telegram / 微信 / QQ，助手就能在 IM 里继续工作</div>
                </div>`;
                return;
            }
            els.channelCards.innerHTML = list.map(ch => {
                const st = channelStatusOf(ch.name);
                const running = !!(st && st.running);
                const active = ch.status === 'active';
                const isOpen = expanded.has(ch.name);
                return `<div class="ch-card${isOpen ? ' expanded' : ''}" data-name="${esc(ch.name)}">
                    <div class="ch-card-head">
                        <span class="ch-avatar">${esc((ch.type || '?').slice(0, 1).toUpperCase())}</span>
                        <div class="ch-head-text">
                            <div class="ch-name">${esc(ch.name)}</div>
                            <div class="ch-type">${esc(ch.type || 'unknown')}</div>
                        </div>
                        ${channelBadge(ch)}
                        <label class="ch-switch" title="${active ? '启用中，点击停用' : '未启用，点击启用'}">
                            <input type="checkbox" ${active ? 'checked' : ''} onchange="channelSetActive('${esc(ch.name)}', this.checked)">
                            <span></span>
                        </label>
                        <button class="ch-icon-btn" title="展开/收起配置" onclick="channelToggleBody(this)"><i class="fa-solid fa-chevron-${isOpen ? 'up' : 'down'}"></i></button>
                    </div>
                    ${channelDetailHtml(ch)}
                    <div class="ch-card-body">
                        ${channelConfigFields(ch) || '<div class="ch-muted">该频道没有额外参数</div>'}
                        <div class="ch-field"><label>Bot Token</label>
                            <input type="password" class="ch-token" placeholder="留空 = 保持不变（已加密存储）" autocomplete="off" spellcheck="false">
                        </div>
                    </div>
                    <div class="ch-card-foot" data-running="${running ? 1 : 0}">${channelFoot(ch, running)}</div>
                </div>`;
            }).join('');
        }

        // 轮询只刷新状态，不重建 DOM —— 否则会把正在编辑的输入框冲掉
        function refreshChannelStatusDom() {
            (state.channels || []).forEach(ch => {
                const card = channelCardEl(ch.name);
                if (!card) return;
                const st = channelStatusOf(ch.name);
                const running = !!(st && st.running);

                const badge = card.querySelector('.ch-pill');
                const badgeHtml = channelBadge(ch);
                if (badge && badge.outerHTML !== badgeHtml) badge.outerHTML = badgeHtml;

                const oldDetail = card.querySelector('.ch-detail');
                const detail = channelDetailHtml(ch);
                if (oldDetail && detail) oldDetail.outerHTML = detail;
                else if (oldDetail && !detail) oldDetail.remove();
                else if (!oldDetail && detail) card.querySelector('.ch-card-head').insertAdjacentHTML('afterend', detail);

                // 只在运行态变化时重建按钮，避免轮询打断点击/悬停
                const foot = card.querySelector('.ch-card-foot');
                if (foot && foot.dataset.running !== String(running ? 1 : 0)) {
                    foot.dataset.running = running ? '1' : '0';
                    foot.innerHTML = channelFoot(ch, running);
                }
            });
        }

        function channelToggleBody(btn) {
            const card = btn.closest('.ch-card');
            if (!card) return;
            card.classList.toggle('expanded');
            const icon = btn.querySelector('i');
            if (icon) icon.className = 'fa-solid fa-chevron-' + (card.classList.contains('expanded') ? 'up' : 'down');
        }

        async function loadChannelDrivers() {
            try {
                const r = await axios.get(API + '/channels/drivers');
                state.channelDrivers = r.data?.drivers || [];
            } catch (e) {
                state.channelDrivers = [];
                log('加载频道驱动失败: ' + (e.message || e), 'error');
            }
        }
        async function loadChannelStatus() {
            try {
                const r = await axios.get(API + '/channel/status');
                state.channelStatus = r.data || [];
            } catch (e) { /* 状态轮询失败保持上一次结果 */ }
        }
        function startChannelPoll() {
            stopChannelPoll();
            channelPollTimer = setInterval(async () => {
                await loadChannelStatus();
                refreshChannelStatusDom();
            }, 3000);
        }
        function stopChannelPoll() {
            if (channelPollTimer) { clearInterval(channelPollTimer); channelPollTimer = null; }
        }

        async function loadChannels() {
            try {
                const r = await axios.get(API + '/channels');
                state.channels = r.data || [];
                // settings 里也存着 channels：保持同步，避免「应用配置」把删掉的频道又写回去
                if (state.settings) state.settings.channels = state.channels;
                if (!state.channelDrivers || !state.channelDrivers.length) await loadChannelDrivers();
                await loadChannelStatus();
                renderChannelPanel();
            } catch (e) {
                log('加载频道列表失败: ' + (e.message || e), 'error');
            }
        }

        // ---- 新建 ----
        function channelNewForm() {
            const drivers = state.channelDrivers || [];
            if (!drivers.length) { toast('频道驱动未加载，请重启应用', 'error'); return; }
            els.channelEditor.innerHTML = `<div class="ch-editor">
                <div class="ch-editor-title"><i class="fa-solid fa-plug-circle-plus"></i> 新建频道</div>
                <div class="ch-field"><label>名称</label><input id="ch-new-name" placeholder="例如 Telegram / 工作 QQ" spellcheck="false"></div>
                <div class="ch-field"><label>类型</label>
                    <select id="ch-new-type" onchange="channelNewHint()">
                        ${drivers.map(d => `<option value="${esc(d.type)}">${esc(d.display)}</option>`).join('')}
                    </select>
                </div>
                <div class="ch-field"><label>创建后立即启用</label>
                    <select id="ch-new-status"><option value="active">是</option><option value="inactive">否</option></select>
                </div>
                <div class="ch-editor-hint" id="ch-new-hint"></div>
                <div class="ch-editor-actions">
                    <button class="ch-btn primary" onclick="channelCreate()"><i class="fa-solid fa-check"></i> 创建</button>
                    <button class="ch-btn" onclick="channelCancelNew()">取消</button>
                </div>
            </div>`;
            channelNewHint();
            const nameInput = document.getElementById('ch-new-name');
            if (nameInput) nameInput.focus();
        }
        function channelNewHint() {
            const type = document.getElementById('ch-new-type')?.value;
            const d = (state.channelDrivers || []).find(x => x.type === type);
            const hint = document.getElementById('ch-new-hint');
            if (hint) hint.innerHTML = d ? `<i class="fa-solid fa-circle-info"></i> ${esc(d.hint || '')}<br>名称同时是 Token 存储键，创建后不可修改。` : '';
        }
        function channelCancelNew() { if (els.channelEditor) els.channelEditor.innerHTML = ''; }

        async function channelCreate() {
            const name = document.getElementById('ch-new-name')?.value.trim();
            const type = document.getElementById('ch-new-type')?.value;
            const status = document.getElementById('ch-new-status')?.value || 'inactive';
            if (!name) { toast('请填写频道名称', 'error'); return; }
            if ((state.channels || []).some(c => c.name === name)) { toast('频道名称已存在', 'error'); return; }
            const driver = (state.channelDrivers || []).find(d => d.type === type);
            const config = driver && driver.config ? JSON.parse(JSON.stringify(driver.config)) : {};
            try {
                await axios.post(API + '/channels/add', { name, type, status, config });
                toast(name + ' 已创建', 'success');
                channelCancelNew();
                await loadChannels();
                if (status === 'active' && type === 'wechat') {
                    const st = channelStatusOf(name);
                    if (st && (st.state === 'waiting_qr' || st.state === 'no_token')) openChannelQrLogin(name);
                }
            } catch (e) {
                toast('创建失败: ' + ((e.response && e.response.data && e.response.data.error) || e.message), 'error');
            }
        }

        // ---- 启停 / 保存 / 删除 ----
        async function channelStart(name) {
            const ch = (state.channels || []).find(c => c.name === name);
            try {
                // 未启用的频道：先落配置再拉起（后端会顺手 start）
                if (ch && ch.status !== 'active') {
                    await axios.post(API + '/channels/update', { name, status: 'active' });
                    await loadChannels();
                    toast(name + ' 已启用', 'success');
                } else {
                    const d = (await axios.post(API + '/channel/start', { name })).data || {};
                    if (d.started === false && d.state !== 'running') {
                        toast('启动失败: ' + (d.detail || d.state || '请检查配置'), 'error');
                    } else {
                        toast(name + ' 已启动', 'success');
                    }
                    await loadChannelStatus();
                    refreshChannelStatusDom();
                }
                const st = channelStatusOf(name);
                if (ch && ch.type === 'wechat' && st && (st.state === 'waiting_qr' || st.state === 'no_token')) {
                    openChannelQrLogin(name);
                }
            } catch (e) {
                toast('启动失败: ' + ((e.response && e.response.data && e.response.data.error) || e.message), 'error');
            }
        }
        async function channelStop(name) {
            try {
                const d = (await axios.post(API + '/channel/stop', { name })).data || {};
                toast(d.stopped ? name + ' 正在停止（长轮询需等待当前请求返回）' : name + ' 当前未在运行', 'info');
                await loadChannelStatus();
                refreshChannelStatusDom();
            } catch (e) {
                toast('停止失败: ' + ((e.response && e.response.data && e.response.data.error) || e.message), 'error');
            }
        }
        async function channelSetActive(name, active) {
            try {
                await axios.post(API + '/channels/update', { name, status: active ? 'active' : 'inactive' });
                toast(name + (active ? ' 已启用' : ' 已停用'), 'success');
                await loadChannels();
            } catch (e) {
                toast('更新失败: ' + ((e.response && e.response.data && e.response.data.error) || e.message), 'error');
            }
        }
        async function channelSave(name) {
            const card = channelCardEl(name);
            if (!card) return;
            const cfg = {};
            card.querySelectorAll('.ch-cfg').forEach(inp => {
                const key = inp.dataset.key;
                let v = inp.value;
                if (inp.tagName === 'SELECT') v = inp.value === 'true';
                else if (inp.type === 'number') v = Number(v);
                cfg[key] = v;
            });
            try {
                await axios.post(API + '/channels/update', { name, config: cfg });
                const tokenInput = card.querySelector('.ch-token');
                const token = tokenInput ? tokenInput.value.trim() : '';
                if (token) {
                    await axios.post(API + '/channel/token', { name, token });
                    tokenInput.value = '';
                    tokenInput.placeholder = '已加密保存（留空则保持不变）';
                }
                toast(name + ' 配置已保存', 'success');
                await loadChannels();
            } catch (e) {
                toast('保存失败: ' + ((e.response && e.response.data && e.response.data.error) || e.message), 'error');
            }
        }
        async function channelDelete(name) {
            if (!confirm(`删除频道「${name}」？\n运行中的 Bot 会被停止，已保存的 Token 保留在 workspace/tokens/ 下。`)) return;
            try {
                await axios.post(API + '/channels/delete', { name });
                toast(name + ' 已删除', 'success');
                await loadChannels();
            } catch (e) {
                toast('删除失败: ' + ((e.response && e.response.data && e.response.data.error) || e.message), 'error');
            }
        }
        function channelQrLogin(name) { requestChannelQr(name); }

        // ============ 设置表单 ============
        function renderSettingsForm(s) {
            const d = (s.permissions && s.permissions.defaults) || {};
            const labels = { exec: 'exec 执行命令', write: 'write 写入文件', edit: 'edit 编辑文件', wget: 'wget 网络下载', read: 'read 读取文件', Image: 'Image 图像', '*': '* 全部工具' };
            const permKeys = ['exec', 'write', 'edit', 'wget', 'read', 'Image', '*'];
            const extraPermKeys = Object.keys(d).filter(k => !permKeys.includes(k));
            els.settingsFormContainer.innerHTML = `
    <div class="setting-group">
      <div class="setting-group-title">身份与工作区</div>
      <div class="setting-row"><label>你的昵称</label><input type="text" id="s-user_name" value="${esc(s.user_name || '')}"></div>
      <div class="setting-row"><label>助手昵称</label><input type="text" id="s-agent_name" value="${esc(s.agent_name || '')}"></div>
      <div class="setting-row"><label>默认模型</label><input type="text" id="s-model" value="${esc(s.model || '')}"></div>
      <div class="setting-row"><label>系统提示词文件</label><input type="text" id="s-prompt" value="${esc(s.prompt || '')}" placeholder="agent.txt"></div>
      <div class="setting-row"><label>工作区目录</label><input type="text" id="s-workspace" value="${esc(s.workspace || '')}" placeholder="workspace"></div>
      <div class="setting-row"><label>数据根目录</label><input type="text" id="s-root" value="${esc(s.root || '')}" placeholder="留空 = &lt;exeDir&gt;/.agent"></div>
    </div>
    <div class="setting-group">
      <div class="setting-group-title">模型与运行</div>
      <div class="setting-row check-row"><input type="checkbox" id="s-stream" ${s.stream ? 'checked' : ''}><label for="s-stream">启用流式输出</label></div>
      <div class="setting-row"><label>上下文窗口</label><input type="number" id="s-max_context" value="${s.max_context || 128000}" min="1000"></div>
      <div class="setting-row"><label>单轮最多工具调用次数</label><input type="number" id="s-max_mpc_rounds" value="${s.max_mpc_rounds || 5}" min="1" max="50"></div>
      <div class="setting-row"><label>自动记忆摘要阈值 (%)</label><input type="number" id="s-auto_summary_threshold" value="${s.auto_summary_threshold || 0}" min="0" max="100"></div>
      <div class="setting-hint">上下文占用超过该百分比时自动生成记忆摘要，填 0 关闭。</div>
      <div class="setting-row check-row"><input type="checkbox" id="s-auto_expand" ${s.filesystem?.auto_expand !== false ? 'checked' : ''}><label for="s-auto_expand">工具调用时自动展开文件浏览器</label></div>
    </div>
    <div class="setting-group">
      <div class="setting-group-title">权限策略</div>
      <div class="setting-row"><label>审批超时（秒）</label><input type="number" id="s-perm-timeout" value="${s.permissions?.timeout_sec || 120}" min="5" max="3600"></div>
      <div class="setting-row"><label>频道内审批策略</label>
        <select id="s-perm-channel-ask">
          ${['deny', 'allow'].map(v => `<option value="${v}" ${(s.permissions?.channel_ask || 'deny') === v ? 'selected' : ''}>${v === 'allow' ? 'allow（自动放行）' : 'deny（自动拒绝，推荐）'}</option>`).join('')}
        </select>
      </div>
      <div class="setting-hint">频道里没有交互界面，遇到「询问」类操作时按这里处理。</div>
      <div class="setting-row"><label>工具默认策略</label>
        <div style="width:100%;display:grid;gap:6px;">
          ${[...permKeys, ...extraPermKeys].map(k => `
          <div style="display:flex;align-items:center;gap:8px;">
            <span style="flex:1;font-size:12px;color:var(--text-main);font-family:'JetBrains Mono',monospace;">${esc(labels[k] || k)}</span>
            <select data-perm-default="${esc(k)}" class="setting-select" style="width:110px;">
              ${['allow', 'ask', 'deny'].map(v => `<option value="${v}" ${(d[k] || 'ask') === v ? 'selected' : ''}>${v}</option>`).join('')}
            </select>
          </div>`).join('')}
        </div>
      </div>
      <div class="setting-hint">用户规则（权限面板里配置）优先于这里的默认策略。</div>
    </div>
    <details class="setting-group adv-group">
      <summary>高级：内置服务端 / WebUI</summary>
      <div class="setting-row"><label>监听地址</label><input type="text" id="s-host" value="${esc((s.server && s.server.host) || '127.0.0.1')}"></div>
      <div class="setting-row"><label>端口</label><input type="number" id="s-port" value="${(s.server && s.server.port) || 8080}" min="1" max="65535"></div>
      <div class="setting-hint">桌面模式下服务端只监听 127.0.0.1，是界面与 QQ webhook 的内部通道；端口被占用时自动改用系统临时端口。</div>
      <div class="setting-row"><label>WebUI 路径</label><input type="text" id="s-webui-path" value="${esc((s.webui && s.webui.path) || 'webui')}"></div>
      <div class="setting-row"><label>WebUI 首页</label><input type="text" id="s-webui-index" value="${esc((s.webui && s.webui.index) || 'index.html')}"></div>
      <div class="setting-row"><label>界面来源</label>
        <select id="s-webui-source" class="setting-select" style="width:100%;">
          ${[['embedded', 'embedded（内嵌界面）'], ['auto', 'auto（磁盘有文件就用磁盘）'], ['disk', 'disk（强制用磁盘文件）']]
            .map(([v, label]) => `<option value="${v}" ${((s.webui && s.webui.source) || 'embedded') === v ? 'selected' : ''}>${label}</option>`).join('')}
        </select>
      </div>
      <div class="setting-hint">root / server / webui 的修改在保存并重启应用后生效。</div>
    </details>
  `;
        }
        async function loadSettings() {
            try { const r = await axios.post(API + '/settings', { updata: false }); state.settings = r.data; state.channels = state.settings.channels || []; state.providers = state.settings.providers || []; } catch (e) { toast('加载设置失败', 'error'); log('加载设置失败: ' + (e.message || e), 'error'); }
            renderSettingsForm(state.settings);
            await loadProviders();
        }
        async function saveSettings() {
            // 以当前配置为基底，只覆盖表单里出现的字段：
            // 这样 channels / providers / webui 里没在表单展示的子键都不会被抹掉
            const ns = JSON.parse(JSON.stringify(state.settings || {}));
            ns.user_name = $('#s-user_name')?.value || '';
            ns.agent_name = $('#s-agent_name')?.value || '';
            ns.workspace = $('#s-workspace')?.value || '';
            ns.root = $('#s-root')?.value || '';
            ns.model = $('#s-model')?.value || '';
            ns.prompt = $('#s-prompt')?.value || '';
            ns.stream = $('#s-stream')?.checked || false;
            ns.max_mpc_rounds = parseInt($('#s-max_mpc_rounds')?.value) || 5;
            ns.max_context = parseInt($('#s-max_context')?.value) || 128000;
            ns.auto_summary_threshold = parseInt($('#s-auto_summary_threshold')?.value) || 0;
            ns.filesystem = Object.assign({}, ns.filesystem || {}, {
                auto_expand: $('#s-auto_expand')?.checked !== false
            });
            ns.server = Object.assign({}, ns.server || {}, {
                host: $('#s-host')?.value || '127.0.0.1',
                port: parseInt($('#s-port')?.value) || 8080
            });
            ns.webui = Object.assign({}, ns.webui || {}, {
                path: $('#s-webui-path')?.value || 'webui',
                index: $('#s-webui-index')?.value || 'index.html',
                source: $('#s-webui-source')?.value || 'embedded'
            });
            ns.permissions = {
                timeout_sec: parseInt($('#s-perm-timeout')?.value) || 120,
                channel_ask: $('#s-perm-channel-ask')?.value || 'deny',
                defaults: (() => {
                    const d = {};
                    document.querySelectorAll('[data-perm-default]').forEach(el => { d[el.dataset.permDefault] = el.value; });
                    return d;
                })()
            };
            state.settings = ns;
            try {
                const r = await axios.post(API + '/settings', { updata: true, settings: ns });
                state.settings = r.data; state.channels = state.settings.channels || []; toast('设置已保存', 'success'); log('配置更新成功', 'success');
                await getModels();
            } catch (e) { toast('保存失败: ' + ((e.response && e.response.data && e.response.data.error) || e.message || 'unknown'), 'error'); log('保存设置失败: ' + ((e.response && e.response.data && e.response.data.error) || e.message || e), 'error'); }
        }
        els.saveSettingsBtn.onclick = saveSettings;

        // ============ 文件上传 ============
        els.attachBtn.onclick = () => els.fileUpload.click();
        function compressImage(file, maxRes = 1024, quality = 0.8) {
            return new Promise((resolve, reject) => {
                const r = new FileReader();
                r.onload = (e) => {
                    const img = new Image();
                    img.onload = () => {
                        let w = img.width, h = img.height;
                        if (w > maxRes || h > maxRes) { if (w > h) { h = Math.round(h * maxRes / w); w = maxRes; } else { w = Math.round(w * maxRes / h); h = maxRes; } }
                        const c = document.createElement('canvas'); c.width = w; c.height = h;
                        c.getContext('2d').drawImage(img, 0, 0, w, h);
                        resolve(c.toDataURL('image/jpeg', quality));
                    };
                    img.onerror = reject; img.src = e.target.result;
                };
                r.readAsDataURL(file);
            });
        }
        els.fileUpload.onchange = async (e) => {
            for (const f of e.target.files || []) {
                if (f.type.startsWith('image/')) { try { state.pendingImages.push(await compressImage(f)); renderImagePreviews(); } catch (e) { log('图片压缩失败: ' + (e.message || e), 'error'); } }
                else { const fr = new FileReader(); fr.onload = () => { els.userInput.value += (els.userInput.value.trim() ? '\n\n' : '') + `[${f.name}]\n${fr.result}`; els.userInput.dispatchEvent(new Event('input')); }; fr.readAsText(f); }
            }
            els.fileUpload.value = '';
        };
        function renderImagePreviews() {
            els.imagePreviewContainer.innerHTML = state.pendingImages.map((img, i) =>
                `<div class="img-preview"><img src="${img}"><button class="rm-btn" onclick="removePending(${i})"><i class="fa-solid fa-times"></i></button></div>`
            ).join('');
        }
        window.removePending = (i) => { state.pendingImages.splice(i, 1); renderImagePreviews(); };

        // ============ 加载动画 ============
        function showLoading(text) {
            const overlay = els.loadingOverlay;
            if (text) overlay.querySelector('.loading-text').textContent = text;
            overlay.classList.add('active');
        }
        function hideLoading() {
            els.loadingOverlay.classList.remove('active');
            els.loadingOverlay.querySelector('.loading-text').textContent = '加载中...';
        }

        // ============ 滚动冻结控制 ============
        function isScrolledToBottom() {
            const c = els.messagesContainer;
            return c.scrollHeight - c.scrollTop - c.clientHeight < 80;
        }
        function scrollToBottom() {
            els.messagesContainer.scrollTop = els.messagesContainer.scrollHeight;
        }
        function updateScrollState() {
            const atBottom = isScrolledToBottom();
            state._scrollAtBottom = atBottom;
            els.scrollBottomBtn.classList.toggle('show', !atBottom);
        }
        els.messagesContainer.addEventListener('scroll', updateScrollState);
        els.scrollBottomBtn.onclick = () => { scrollToBottom(); els.scrollBottomBtn.classList.remove('show'); };

        // ============ 上下文使用显示 ============
        function updateContextUsage(usage) {
            if (!els.contextUsage || !els.contextBarFill) return;
            if (!usage || !state.settings.max_context) {
                els.contextUsage.style.display = 'none';
                return;
            }
            els.contextUsage.style.display = 'flex';
            const total = usage.total_cost || usage.prompt_cost + usage.completion_cost || 0;
            const max = state.settings.max_context || 128000;
            const pct = Math.min(100, (total / max) * 100);
            els.contextBarFill.style.width = pct + '%';
            els.contextText.textContent = `${total.toLocaleString()} / ${max.toLocaleString()}`;
        }
        function updateMemoryIndicator(active, createdAt) {
            state.memoryActive = active;
            if (createdAt !== undefined) state.memoryCreatedAt = createdAt;
            const indicator = document.getElementById('memory-time-indicator');
            const timeText = document.getElementById('memory-time-text');
            if (!indicator || !timeText) return;
            if (active && state.memoryCreatedAt) {
                const date = new Date(state.memoryCreatedAt * 1000);
                timeText.textContent = 'Latest:' + date.toLocaleString();
                indicator.classList.add('visible');
            } else {
                indicator.classList.remove('visible');
                timeText.textContent = '';
            }
        }

        // ============ 会话管理 ============
        async function loadSessions() {
            try {
                const r = await axios.get(API + '/session'); state.sessions = r.data.session_list || []; renderSessions();
                state.currentSessionId = null;
                els.currentSessionIdDisplay.textContent = 'ID: -';
            } catch (e) { log('加载会话列表失败', 'error'); }
        }
        function renderSessions() {
            els.sessionList.innerHTML = '';
            state.sessions.forEach(id => {
                const isActive = id === state.currentSessionId;
                const isChannel = state.channels.some(c => c.name === id);
                const div = document.createElement('div');
                div.className = `session-item${isActive ? ' active' : ''}`;
                div.innerHTML = `<span class="sess-name" title="${esc(id)}">${esc(id)}</span>` + (isChannel ? '' : `<button class="del-btn" title="删除"><i class="fa-solid fa-trash-can"></i></button>`);
                div.querySelector('.sess-name')?.addEventListener('click', () => switchSession(id));
                const delBtn = div.querySelector('.del-btn');
                if (delBtn) delBtn.addEventListener('click', (ev) => { ev.stopPropagation(); deleteSession(id); });
                els.sessionList.appendChild(div);
            });
        }
        async function switchSession(sid) {
            if (state.isTyping) { toast('请等待当前生成完成', 'error'); return; }
            showLoading('切换会话中...');
            try {
                const r = await axios.post(API + '/session/msg', { session_id: sid });
                state.currentSessionId = sid;
                state.messages = parseMessages(r.data.messages || []);
                renderMessages(); renderSessions(); log(`已切换到: ${sid}`, 'success');
                // 检查是否有记忆摘要
                const hasMemory = r.data.memory;
                const memCreatedAt = r.data.memory_created_at;
                updateMemoryIndicator(hasMemory, memCreatedAt);
                els.currentSessionIdDisplay.textContent = `ID: ${sid}${hasMemory ? '[🧠]' : '[📃]'}`;
                // 重置自动摘要追踪
                _lastSummaryTotal = 0;
                _autoSummaryRunning = false;
                // 按消息字符数估算当前上下文用量
                const totalChars = JSON.stringify(r.data.messages || []).length;
                state.contextUsage.total = totalChars;
                state.contextUsage.max = state.settings.max_context || 128000;
                updateContextUsage({ total_cost: state.contextUsage.total });
            } catch (e) { toast('无法加载会话', 'error'); log('切换会话失败: ' + (e.message || e), 'error'); }
            finally { hideLoading(); }
        }
        async function newSession() {
            if (state.isTyping) { toast('请等待当前生成完成', 'error'); return; }
            try {
                const r = await axios.post(API + '/session/new');
                if (r.data.status === 'OK') { const nid = r.data.session_id; if (!state.sessions.includes(nid)) state.sessions.push(nid); await switchSession(nid); renderSessions(); toast('新会话已创建', 'success'); }
            } catch (e) { toast('创建失败', 'error'); log('创建会话失败: ' + (e.message || e), 'error'); }
        }
        async function deleteSession(sid) {
            if (!confirm(`确认删除会话 ${sid}？此操作不可撤销。`)) return;
            try {
                const r = await axios.post(API + '/session/delete', { session_id: sid });
                if (r.data.status === 'deleted') { state.sessions = state.sessions.filter(s => s !== sid); renderSessions(); if (state.currentSessionId === sid && state.sessions.length > 0) await switchSession(state.sessions[0]); else if (state.sessions.length === 0) await newSession(); toast('会话已删除', 'success'); }
            } catch (e) { toast('删除失败: ' + (e.response?.data?.error || e.message), 'error'); log('删除会话失败: ' + (e.response?.data?.error || e.message), 'error'); }
        }
        async function clearSession() {
            if (!state.currentSessionId || !confirm('清空当前会话的全部上下文？')) return;
            try { await axios.post(API + '/session/clear'); await switchSession(state.currentSessionId); toast('上下文已清空', 'success'); } catch (e) { toast('清空失败', 'error'); log('清空会话失败: ' + (e.message || e), 'error'); }
            updateContextUsage(null);
            updateMemoryIndicator(false);
        }
        async function genMemory() {
            if (!state.currentSessionId || !confirm('为当前会话生成记忆摘要？')) return;
            const btn = els.summarySessionBtn, orig = btn.innerHTML; btn.innerHTML = '<i class="fa-solid fa-spinner fa-spin"></i>'; btn.disabled = true;
            try { const r = await axios.post(API + '/session/memory'); if (r.data.status === 'done') { toast('记忆已更新', 'success'); updateMemoryIndicator(true, r.data.memory_created_at); els.currentSessionIdDisplay.textContent = `ID: ${state.currentSessionId} [🧠]`; } } catch (e) { toast('记忆生成失败', 'error'); log('记忆生成失败: ' + (e.message || e), 'error'); }
            finally { btn.innerHTML = orig; btn.disabled = false; }
        }

        // ============ 自动记忆摘要 ============
        let _autoSummaryRunning = false;
        let _lastSummaryTotal = 0;
        async function checkAutoSummary() {
            const threshold = state.settings.auto_summary_threshold || 0;
            if (threshold <= 0) return;
            if (_autoSummaryRunning) return;
            if (!state.currentSessionId) return;

            const total = state.contextUsage.total || 0;
            const max = state.contextUsage.max || 128000;
            if (max <= 0) return;

            const pct = (total / max) * 100;
            if (pct < threshold) return;

            // 已有记忆且总量未显著增长，跳过
            if (state.memoryActive && _lastSummaryTotal > 0 && total - _lastSummaryTotal < max * 0.05) return;

            _autoSummaryRunning = true;
            try {
                const r = await axios.post(API + '/session/memory');
                if (r.data.status === 'done') {
                    updateMemoryIndicator(true, r.data.memory_created_at);
                    els.currentSessionIdDisplay.textContent = `ID: ${state.currentSessionId} [🧠]`;
                    log(`自动记忆摘要已生成 (使用率: ${pct.toFixed(0)}%)`, 'success');
                    _lastSummaryTotal = total;
                }
            } catch (e) {
                log('自动记忆摘要失败: ' + (e.message || e), 'error');
            } finally {
                _autoSummaryRunning = false;
            }
        }

        // ============ 供应商选择 ============
        const providerTypeIcons = {
            openai: { icon: 'fa-bolt', color: 'var(--ok)' },
            ollama: { icon: 'fa-brain', color: '#60a5fa' },
            llama: { icon: 'fa-microchip', color: '#f59e0b' }
        };

        async function loadProviders() {
            try {
                const r = await axios.get(API + '/providers');
                state.providers = r.data.providers || [];
                state.currentProviderId = r.data.current_provider || '';
            } catch (e) {
                log('加载供应商列表失败: ' + (e.message || e), 'error');
            }
            initProvider();
            renderProviderDropdown();
            renderProviders();
        }

        function initProvider() {
            const currentId = state.currentProviderId || state.settings.current_provider || '';
            const provider = state.providers.find(p => p.id === currentId);
            if (provider) {
                els.providerNameDisplay.textContent = provider.name || provider.id;
                state.currentProviderId = provider.id;
            } else if (state.providers.length > 0) {
                const first = state.providers[0];
                els.providerNameDisplay.textContent = first.name || first.id;
                state.currentProviderId = first.id;
            } else {
                els.providerNameDisplay.textContent = '未配置';
            }
        }

        function renderProviderDropdown() {
            els.providerDropdownList.innerHTML = state.providers.map(p => {
                const typeInfo = providerTypeIcons[p.type] || providerTypeIcons.openai;
                const isActive = p.id === state.currentProviderId;
                return `<button class="provider-opt ${isActive ? 'active' : ''}" data-provider-id="${esc(p.id)}" onclick="selectProvider('${esc(p.id)}')">
                    <i class="fa-solid ${typeInfo.icon}" style="color:${typeInfo.color};"></i> ${esc(p.name || p.id)}
                </button>`;
            }).join('');
        }

        async function selectProvider(providerId) {
            if (providerId === state.currentProviderId) return;
            try {
                const r = await axios.post(API + '/provider', { provider_id: providerId });
                if (r.data.status === 'OK') {
                    state.currentProviderId = providerId;
                    state.settings.current_provider = providerId;
                    const provider = state.providers.find(p => p.id === providerId);
                    els.providerNameDisplay.textContent = provider ? (provider.name || provider.id) : providerId;
                    renderProviderDropdown();
                    els.providerDropdownMenu.classList.remove('show');
                    toast('已切换到 ' + (provider ? provider.name : providerId), 'success');
                    log('供应商已切换: ' + providerId, 'success');
                    state.currentModel = null;
                    await getModels();
                }
            } catch (e) {
                toast('切换供应商失败: ' + (e.response?.data?.error || e.message), 'error');
                log('切换供应商失败: ' + (e.message || e), 'error');
            }
        }

        els.providerDropdownBtn.onclick = (e) => { e.stopPropagation(); els.providerDropdownMenu.classList.toggle('show'); };
        document.addEventListener('click', () => els.providerDropdownMenu.classList.remove('show'));

        // ============ 供应商管理 ============
        function renderProviders() {
            if (!els.providersList) return;
            if (!state.providers.length) {
                els.providersList.innerHTML = '<div style="color:var(--text-dim);font-size:13px;text-align:center;padding:24px;">暂无供应商，请添加</div>';
                return;
            }
            els.providersList.innerHTML = state.providers.map(p => {
                const typeInfo = providerTypeIcons[p.type] || providerTypeIcons.openai;
                const isActive = p.id === state.currentProviderId;
                const hasKey = p.has_key !== false;
                return `<div class="channel-item" style="border-color:${isActive ? '#6366f1' : 'var(--border-color)'};" data-provider-id="${esc(p.id)}">
                    <div class="channel-item-header">
                        <span style="display:flex;align-items:center;gap:8px;">
                            <i class="fa-solid ${typeInfo.icon}" style="color:${typeInfo.color};font-size:14px;"></i>
                            <span class="channel-item-name">${esc(p.name || p.id)}</span>
                            ${isActive ? '<span style="font-size:10px;color:var(--ok);background:var(--ok-soft);padding:2px 6px;border-radius:4px;">当前使用</span>' : ''}
                        </span>
                        <div style="display:flex;gap:4px;">
                            ${!isActive ? `<button class="channel-btn" onclick="selectProvider('${esc(p.id)}')" title="切换到此供应商"><i class="fa-solid fa-check"></i></button>` : ''}
                            <button class="channel-btn" onclick="editProviderForm('${esc(p.id)}')" title="编辑"><i class="fa-solid fa-pen"></i></button>
                            <button class="channel-btn danger" onclick="deleteProvider('${esc(p.id)}')" title="删除"><i class="fa-solid fa-trash"></i></button>
                        </div>
                    </div>
                    <div style="font-size:11px;color:var(--text-dim);margin-top:6px;">
                        <div>类型: ${esc(p.type)} | 地址: ${esc(p.server_address || '未设置')} | API Key: ${hasKey ? '<span style="color:var(--ok);">已配置</span>' : '<span style="color:var(--text-dim);">未配置</span>'}</div>
                    </div>
                </div>`;
            }).join('');
        }

        function addProviderForm() {
            const container = els.providersList;
            const addBtn = container.querySelector('.add-provider-btn');
            if (addBtn) addBtn.remove();
            const div = document.createElement('div');
            div.className = 'channel-item';
            div.style.borderColor = '#6366f1';
            div.innerHTML = `
        <div class="channel-item-header"><span class="channel-item-name" style="color:var(--accent-soft-text);">新供应商</span></div>
        <div class="channel-field"><label>名称</label><input type="text" class="prov-name-new" placeholder="如: DeepSeek"></div>
        <div class="channel-field"><label>ID</label><input type="text" class="prov-id-new" placeholder="如: deepseek-1 (唯一标识)"></div>
        <div class="channel-field"><label>类型</label>
            <select class="prov-type-new">
                <option value="openai">OpenAI (兼容接口)</option>
                <option value="ollama">Ollama</option>
                <option value="llama">Llama.cpp</option>
            </select>
        </div>
        <div class="channel-field"><label>服务器地址</label><input type="text" class="prov-url-new" placeholder="如: https://api.deepseek.com/v1"></div>
        <div class="channel-field"><label>API Key</label><input type="password" class="prov-key-new" placeholder="可选"></div>
        <div class="channel-actions">
          <button class="channel-btn save" onclick="commitNewProvider()"><i class="fa-solid fa-check"></i> 创建</button>
          <button class="channel-btn secondary" onclick="renderProviders()"><i class="fa-solid fa-times"></i> 取消</button>
        </div>
      `;
            container.appendChild(div);
        }

        function commitNewProvider() {
            const name = document.querySelector('.prov-name-new')?.value.trim();
            const id = document.querySelector('.prov-id-new')?.value.trim();
            const type = document.querySelector('.prov-type-new')?.value || 'openai';
            const server_address = document.querySelector('.prov-url-new')?.value.trim();
            const api_key = document.querySelector('.prov-key-new')?.value.trim() || '';
            if (!name || !id) { toast('名称和ID不能为空', 'error'); return; }
            if (state.providers.find(p => p.id === id)) { toast('供应商ID已存在', 'error'); return; }
            (async () => {
                try {
                    const r = await axios.post(API + '/providers/add', { id, name, type, server_address, api_key: '' });
                    if (r.data.status === 'OK') {
                        state.providers.push(r.data.provider);
                        state.settings.providers = state.providers;
                        // 如果有 api_key，通过加密接口单独存储
                        if (api_key) {
                            await axios.post(API + '/provider/key', { id, api_key });
                            r.data.provider.has_key = true;
                        }
                        toast('供应商已添加', 'success');
                        renderProviders();
                        renderProviderDropdown();
                    }
                } catch (e) {
                    toast('添加失败: ' + (e.response?.data?.error || e.message), 'error');
                }
            })();
        }

        function editProviderForm(providerId) {
            const provider = state.providers.find(p => p.id === providerId);
            if (!provider) return;
            const container = els.providersList;
            const item = container.querySelector(`[data-provider-id="${providerId}"]`);
            if (!item) return;
            const hasKey = provider.has_key !== false;
            item.innerHTML = `
        <div class="channel-item-header"><span class="channel-item-name" style="color:var(--accent-soft-text);">编辑供应商</span></div>
        <div class="channel-field"><label>名称</label><input type="text" class="prov-name-edit" value="${esc(provider.name || '')}"></div>
        <div class="channel-field"><label>类型</label>
            <select class="prov-type-edit">
                <option value="openai" ${provider.type === 'openai' ? 'selected' : ''}>OpenAI (兼容接口)</option>
                <option value="ollama" ${provider.type === 'ollama' ? 'selected' : ''}>Ollama</option>
                <option value="llama" ${provider.type === 'llama' ? 'selected' : ''}>Llama.cpp</option>
            </select>
        </div>
        <div class="channel-field"><label>服务器地址</label><input type="text" class="prov-url-edit" value="${esc(provider.server_address || '')}"></div>
        <div class="channel-field"><label>API Key</label><input type="password" class="prov-key-edit" placeholder="${hasKey ? '(已加密存储，留空保持不变)' : '(输入新的 API Key)'}"></div>
        <div style="font-size:11px;color:var(--text-dim);margin-top:-4px;padding-left:4px;">API Key 将加密存储在 workspace/tokens/providers/ 目录下</div>
        <div class="channel-actions">
          <button class="channel-btn save" onclick="commitEditProvider('${esc(providerId)}')"><i class="fa-solid fa-check"></i> 保存</button>
          <button class="channel-btn secondary" onclick="renderProviders()"><i class="fa-solid fa-times"></i> 取消</button>
        </div>
      `;
        }

        function commitEditProvider(providerId) {
            const name = document.querySelector('.prov-name-edit')?.value.trim();
            const type = document.querySelector('.prov-type-edit')?.value;
            const server_address = document.querySelector('.prov-url-edit')?.value.trim();
            const api_key = document.querySelector('.prov-key-edit')?.value.trim();
            if (!name) { toast('名称不能为空', 'error'); return; }
            const payload = { id: providerId, name, type, server_address };
            (async () => {
                try {
                    const r = await axios.post(API + '/providers/update', payload);
                    if (r.data.status === 'OK') {
                        const idx = state.providers.findIndex(p => p.id === providerId);
                        if (idx !== -1) state.providers[idx] = r.data.provider;
                        state.settings.providers = state.providers;
                        // 如果有 api_key，通过加密接口单独存储
                        if (api_key) {
                            await axios.post(API + '/provider/key', { id: providerId, api_key });
                            r.data.provider.has_key = true;
                        } else if (api_key === '') {
                            // 空字符串表示删除 key
                            await axios.post(API + '/provider/key', { id: providerId, api_key: '' });
                            r.data.provider.has_key = false;
                        }
                        toast('供应商已更新', 'success');
                        renderProviders();
                        renderProviderDropdown();
                        initProvider();
                    }
                } catch (e) {
                    toast('更新失败: ' + (e.response?.data?.error || e.message), 'error');
                }
            })();
        }

        function deleteProvider(providerId) {
            if (!confirm('确定删除此供应商？')) return;
            if (state.providers.length <= 1) { toast('不能删除最后一个供应商', 'error'); return; }
            (async () => {
                try {
                    const r = await axios.post(API + '/providers/delete', { id: providerId });
                    if (r.data.status === 'OK') {
                        state.providers = state.providers.filter(p => p.id !== providerId);
                        state.settings.providers = state.providers;
                        if (state.currentProviderId === providerId) {
                            await loadProviders();
                        }
                        toast('供应商已删除', 'success');
                        renderProviders();
                        renderProviderDropdown();
                    }
                } catch (e) {
                    toast('删除失败: ' + (e.response?.data?.error || e.message), 'error');
                }
            })();
        }

        function openProvidersManager() {
            els.providerDropdownMenu.classList.remove('show');
            openOverlay();
            // 切换到供应商标签
            $$('.overlay-tab').forEach(b => b.classList.remove('active'));
            $$('.overlay-panel').forEach(p => p.classList.remove('active'));
            const tab = document.querySelector('.overlay-tab[data-otab="providers"]');
            if (tab) tab.classList.add('active');
            const panel = document.getElementById('opanel-providers');
            if (panel) panel.classList.add('active');
            renderProviders();
        }

        // ============ 模型列表 ============
        async function getModels() {
            try {
                const r = await axios.get(API + '/models');
                state.models = (r.data.data || []).map(m => ({ name: m.aliases?.[0] || m.id, available: m.status?.value === 'loaded' }));
                if (!state.models.length) state.models.push({ name: 'default', available: true });
                if (!state.currentModel) selectModel(state.models[0].name, state.models[0].available !== false);
                renderModels();
            } catch (e) { log('加载模型列表失败', 'error'); }
        }
        function renderModels() {
            els.modelDropdownList.innerHTML = state.models.map(m =>
                `<button class="model-opt" onclick="selectModel('${esc(m.name)}',${!!m.available});$('#model-dropdown-menu').classList.remove('show');">
      <span class="model-dot" style="background:${m.available !== false ? 'var(--ok)' : 'var(--err)'};"></span> ${esc(m.name)}
    </button>`
            ).join('');
        }
        function selectModel(name, ok) {
            state.currentModel = name; els.modelNameDisplay.textContent = name;
            if (els.connectionStatusDot) els.connectionStatusDot.style.background = ok ? 'var(--ok)' : 'var(--err)';
        }
        els.modelDropdownBtn.onclick = (e) => { e.stopPropagation(); els.modelDropdownMenu.classList.toggle('show'); };
        document.addEventListener('click', () => els.modelDropdownMenu.classList.remove('show'));

        // ============ 消息解析与渲染 ============
        function parseMessages(raw) {
            if (!Array.isArray(raw)) return [];
            const uname = state.settings.user_name || 'Yauntyours';
            const aname = state.settings.agent_name || 'assistant';
            let hasMemory = false;
            const parsed = raw.filter(m => m.role !== 'system').map(m => {
                let role = 'ai', content = '', images = [];
                if (m.role === uname || m.role === 'user') role = 'user';
                else if (m.role === aname || m.role === 'assistant' || m.role === 'tool') {
                    role = 'ai';
                } else if (m.role === 'memory') {
                    hasMemory = true;
                    return null;
                }
                if (typeof m.content === 'string') content = m.content;
                else if (Array.isArray(m.content)) { m.content.forEach(p => { if (p.type === 'text') content += p.text + '\n'; else if (p.type === 'image_url') { const u = typeof p.image_url === 'string' ? p.image_url : p.image_url.url; images.push(u); } }); }
                return { role, content: content.trim(), images };
            }).filter(m => m !== null);
            updateMemoryIndicator(hasMemory);
            return parsed;
        }
        function renderMessages() {
            els.messagesContainer.innerHTML = '';
            if (!state.messages.length) {
                if (!state.currentSessionId) {
                    els.messagesContainer.innerHTML = `<div class="empty-state"><i class="fa-solid fa-comments"></i><div style="font-size:16px;font-weight:500;color:var(--text-dim);">未选择会话</div><div style="font-size:13px;margin-top:4px;">请从左侧选择一个会话，或创建新会话。</div></div>`;
                } else {
                    els.messagesContainer.innerHTML = `<div class="empty-state"><i class="fa-solid fa-comments"></i><div style="font-size:16px;font-weight:500;color:var(--text-dim);">上下文为空</div><div style="font-size:13px;margin-top:4px;">开始一段新对话吧。</div></div>`;
                }
                return;
            }
            state.messages.forEach(m => appendMessage(m));
            if (isScrolledToBottom()) {
                els.messagesContainer.scrollTop = els.messagesContainer.scrollHeight;
            }
        }

        function appendMessage(msg) {
            const isUser = msg.role === 'user';
            const row = document.createElement('div'); row.className = `msg-row ${isUser ? 'user' : 'ai'}`;
            row.id = msg._id || '';

            const avatar = document.createElement('div');
            avatar.className = `msg-avatar ${isUser ? 'user-av' : 'ai-av'}`;
            avatar.innerHTML = isUser ? '<i class="fa-solid fa-user"></i>' : '<i class="fa-solid fa-robot"></i>';
            if (!isUser) row.appendChild(avatar);

            const bubble = document.createElement('div');
            bubble.className = `msg-bubble ${isUser ? 'user-bub' : 'ai-bub'}`;

            if (msg._loading) {
                bubble.innerHTML = '<span class="typing-cursor">思考中...</span>';
            } else {
                if (!isUser && msg._thinkings && msg._thinkings.length) {
                    const det = document.createElement('details'); det.className = 'think-block';
                    det.innerHTML = `<summary><i class="fa-solid fa-chevron-right"></i> 内心独白 (${msg._thinkings.length} 步)</summary>`;
                    const content = document.createElement('div'); content.className = 'think-content';
                    msg._thinkings.forEach((s, i) => { content.innerHTML += `<span style="color:var(--accent-color);font-weight:600;">#${i + 1}</span> ${esc(String(s)).replace(/\n/g, '<br>')}<br><br>`; });
                    det.appendChild(content); bubble.appendChild(det);
                }
                if (!isUser && msg._toolCalls && msg._toolCalls.length) {
                    msg._toolCalls.forEach(tc => {
                        const det = document.createElement('details'); det.className = 'tool-call';
                        det.innerHTML = `<summary><span class="tool-icon"><i class="fa-solid fa-terminal"></i></span><span class="tool-name">${esc(tc.name)}</span><span class="tool-args">${esc(tc.args || '')}</span><span class="tool-status ${tc.success ? 'ok' : 'err'}">${tc.success ? '✓' : '✗'}</span></summary>`;
                        const out = document.createElement('div'); out.className = 'tool-output'; out.textContent = tc.output || '';
                        det.appendChild(out); bubble.appendChild(det);
                    });
                }
                const prose = document.createElement('div');
                prose.className = 'prose';
                prose.innerHTML = isUser ? esc(msg.content).replace(/\n/g, '<br>') : marked.parse(fixToolTags(msg.content));
                bubble.appendChild(prose);
                addCopyButtons(prose);
                if (msg.images && msg.images.length) {
                    const imgs = document.createElement('div'); imgs.className = 'msg-images';
                    msg.images.forEach(img => { const el = document.createElement('img'); el.src = img.startsWith('data:') ? img : img; el.onclick = () => window.open(el.src); imgs.appendChild(el); });
                    bubble.appendChild(imgs);
                }
                if (!isUser && msg._usage) {
                    const uf = document.createElement('div'); uf.className = 'usage-footer';
                    uf.innerHTML = `<span>↑${(msg._usage.prompt_cost || 0).toLocaleString()}</span><span>↓${(msg._usage.completion_cost || 0).toLocaleString()}</span><span style="color:var(--accent-color);">Σ${(msg._usage.total_cost || 0).toLocaleString()}</span>`;
                    bubble.appendChild(uf);
                }
            }
            row.appendChild(bubble);
            if (isUser) row.appendChild(avatar);
            els.messagesContainer.appendChild(row);
        }

        function updateAssistantMsg(msgId, data) {
            const row = document.getElementById(msgId);
            if (!row) return;
            const bubble = row.querySelector('.msg-bubble.ai-bub');
            if (!bubble) return;
            bubble.innerHTML = '';

            if (data._thinkings && data._thinkings.length) {
                const det = document.createElement('details'); det.className = 'think-block'; det.open = data._thinkingOpen || false;
                det.innerHTML = `<summary><i class="fa-solid fa-chevron-right"></i> 内心独白 (${data._thinkings.length} 步)</summary>`;
                const content = document.createElement('div'); content.className = 'think-content';
                data._thinkings.forEach((s, i) => { content.innerHTML += `<span style="color:var(--accent-color);font-weight:600;">#${i + 1}</span> ${esc(String(s)).replace(/\n/g, '<br>')}<br><br>`; });
                det.appendChild(content); bubble.appendChild(det);
            }
            if (data._toolCalls && data._toolCalls.length) {
                data._toolCalls.forEach(tc => {
                    const det = document.createElement('details'); det.className = 'tool-call'; det.open = tc._open !== false;
                    det.innerHTML = `<summary><span class="tool-icon"><i class="fa-solid fa-terminal"></i></span><span class="tool-name">${esc(tc.name)}</span><span class="tool-args">${esc(tc.args || '')}</span><span class="tool-status ${tc.success ? 'ok' : 'err'}">${tc.success ? '✓' : '✗'}</span></summary>`;
                    const out = document.createElement('div'); out.className = 'tool-output'; out.textContent = tc.output || '';
                    det.appendChild(out); bubble.appendChild(det);
                });
            }
            const prose = document.createElement('div');
            prose.innerHTML = marked.parse(fixToolTags(data.content || ''));
            addCopyButtons(prose);
            bubble.appendChild(prose);
            if (data._typing) bubble.classList.add('typing-cursor'); else bubble.classList.remove('typing-cursor');
            if (data._usage) {
                const uf = document.createElement('div'); uf.className = 'usage-footer';
                uf.innerHTML = `<span>↑${(data._usage.prompt_cost || 0).toLocaleString()}</span><span>↓${(data._usage.completion_cost || 0).toLocaleString()}</span><span style="color:var(--accent-color);">Σ${(data._usage.total_cost || 0).toLocaleString()}</span>`;
                bubble.appendChild(uf);
            }

            if (data.images && data.images.length) {
                const imgs = document.createElement('div'); imgs.className = 'msg-images';
                data.images.forEach(img => { const el = document.createElement('img'); el.src = img.startsWith('data:') ? img : img; el.onclick = () => window.open(el.src); imgs.appendChild(el); });
                bubble.appendChild(imgs);
            }

            if (isScrolledToBottom()) {
                els.messagesContainer.scrollTop = els.messagesContainer.scrollHeight;
            }
        }

        function addCopyButtons(container) {
            container.querySelectorAll('pre').forEach(pre => {
                if (pre.querySelector('.copy-btn')) return;
                const btn = document.createElement('button'); btn.className = 'copy-btn'; btn.textContent = '复制';
                btn.onclick = () => { const c = pre.querySelector('code')?.textContent || ''; navigator.clipboard.writeText(c).then(() => { btn.textContent = '已复制'; setTimeout(() => btn.textContent = '复制', 2000); }); };
                pre.appendChild(btn);
            });
        }

        // ============ 发送消息（流式/非流式） ============
        async function sendMessageStream() {
            const content = els.userInput.value.trim();
            const imgs = [...state.pendingImages];
            if ((!content && !imgs.length) || state.isTyping) return;
            if (!state.currentSessionId) {
                toast('请先选择一个会话', 'error');
                log('请先在左侧选择一个会话', 'error');
                return;
            }

            // 1. 添加用户消息
            const userMsg = { role: 'user', content, images: imgs };
            state.messages.push(userMsg);
            els.userInput.value = ''; els.userInput.style.height = 'auto'; state.pendingImages = []; renderImagePreviews();
            renderMessages();
            state.isTyping = true; els.sendBtn.disabled = true;

            // 2. 创建占位 AI 消息
            const msgId = 'msg-' + Date.now();
            const place = { role: 'ai', content: '', images: [], _id: msgId, _loading: true, _thinkings: [], _toolCalls: [], _usage: null };
            state.messages.push(place);
            appendMessage(place);
            state.assistantMsgId = msgId;

            // 3. 重置缓冲区
            state.streamBuffers = { content: '', thinking: '', thinkings: [], toolOutputs: [], currentTool: null };

            // 4. 构造请求载荷
            const payload = { messages: content, model: state.currentModel || 'default', think: state.thinkMode, session_id: state.currentSessionId };
            if (imgs.length) payload.images = imgs;

            // 5. 根据配置选择流式或非流式请求
            if (state.settings.stream !== false) {
                await sendStreaming(payload, msgId);
            } else {
                await sendPacked(payload, msgId);
            }

            state.isTyping = false; els.sendBtn.disabled = false;
        }

        // 流式 SSE 请求
        async function sendStreaming(payload, msgId) {
            try {
                const resp = await fetch(API + '/input/stream', {
                    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload)
                });
                if (!resp.ok) throw new Error('HTTP ' + resp.status);

                const reader = resp.body.getReader();
                const decoder = new TextDecoder();
                let buf = '';

                while (true) {
                    const { done, value } = await reader.read();
                    if (done) break;
                    buf += decoder.decode(value, { stream: true });
                    const events = buf.split('\n\n');
                    buf = events.pop() || '';
                    for (const raw of events) {
                        if (!raw.startsWith('data: ')) continue;
                        try {
                            const ev = JSON.parse(raw.slice(6));
                            handleSSE(ev, msgId);
                        } catch (e) { log('SSE事件解析失败: ' + (e.message || e), 'error'); }
                    }
                }
            } catch (e) {
                state.messages = state.messages.filter(m => m._id !== msgId);
                state.messages.push({ role: 'ai', content: `❌ 错误: ${e.message}`, images: [], _thinkings: [], _toolCalls: [] });
                renderMessages();
                log('请求错误: ' + e.message, 'error');
                state.isTyping = false; els.sendBtn.disabled = false;
            }
        }

        // 非流式（Packed）请求
        async function sendPacked(payload, msgId) {
            try {
                const resp = await fetch(API + '/input', {
                    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload)
                });
                if (!resp.ok) throw new Error('HTTP ' + resp.status);

                const result = await resp.json();
                const sb = state.streamBuffers;

                // 填充内容
                sb.content = result.content || '';
                sb.thinkings = result.thinking || [];

                // 填充工具调用
                sb.toolOutputs = (result.tools || []).map(t => ({
                    name: t.name, args: t.args || '', output: t.output || '', success: true, _open: false
                }));

                // 更新 AI 消息 UI
                updateAssistantMsg(msgId, {
                    content: sb.content,
                    _typing: false,
                    _thinkings: sb.thinkings,
                    _toolCalls: sb.toolOutputs,
                    _usage: result.usage
                });

                // 更新上下文使用显示
                if (result.usage) {
                    state.contextUsage.total = result.usage.total_cost || 0;
                    state.contextUsage.max = state.settings.max_context || 128000;
                    updateContextUsage({ total_cost: state.contextUsage.total });
                }

                // 替换占位为最终消息
                state.messages = state.messages.filter(m => m._id !== msgId);
                const finalMsg = {
                    role: 'ai', content: sb.content, images: [],
                    _thinkings: sb.thinkings, _toolCalls: sb.toolOutputs, _usage: result.usage
                };
                state.messages.push(finalMsg);
                renderMessages();
                log('响应完成', 'success');
                // 自动记忆摘要检查
                checkAutoSummary();

            } catch (e) {
                state.messages = state.messages.filter(m => m._id !== msgId);
                state.messages.push({ role: 'ai', content: `❌ 错误: ${e.message}`, images: [], _thinkings: [], _toolCalls: [] });
                renderMessages();
                log('请求错误: ' + e.message, 'error');
                state.isTyping = false; els.sendBtn.disabled = false;
            }
        }

        function handleSSE(ev, msgId) {
            const sb = state.streamBuffers;
            switch (ev.type) {
                case 'token':
                    sb.content += ev.content;
                    updateAssistantMsg(msgId, { content: sb.content, _typing: true, _thinkings: sb.thinkings, _toolCalls: sb.toolOutputs });
                    break;
                case 'thinking':
                    sb.thinking += ev.content;
                    if (!sb.thinkings.length) sb.thinkings.push('');
                    sb.thinkings[sb.thinkings.length - 1] += ev.content;
                    updateAssistantMsg(msgId, { content: sb.content, _typing: true, _thinkings: sb.thinkings, _toolCalls: sb.toolOutputs, _thinkingOpen: true });
                    break;
                case 'tool_start':
                    sb.currentTool = { name: ev.name, args: ev.args || '', output: '', success: true, _open: true };
                    break;
                case 'tool_output':
                    if (sb.currentTool) sb.currentTool.output += (ev.content || '');
                    break;
                case 'tool_end':
                    if (sb.currentTool) {
                        sb.currentTool.success = ev.success !== false;
                        sb.currentTool._open = false;
                        sb.toolOutputs.push(sb.currentTool);
                        sb.currentTool = null;
                    }
                    updateAssistantMsg(msgId, { content: sb.content, _typing: true, _thinkings: sb.thinkings, _toolCalls: sb.toolOutputs });
                    fb.refresh();
                    if (fb.autoExpand && !fb.visible) fb.show();
                    break;
                // 权限系统：ask 策略工具发起授权请求 → 弹出授权窗口
                case 'permission_request': {
                    const pr = { id: ev.id, session_id: ev.session_id || '', tool: ev.tool || '', args: ev.args || '', created_at: ev.created_at || Math.floor(Date.now() / 1000), timeout: ev.timeout || 120 };
                    if (!state.permPending.some(p => p.id === pr.id)) {
                        state.permPending.push(pr);
                        renderPermPending();
                    }
                    if (!state.permQueue.some(q => q.id === pr.id)) {
                        state.permQueue.push(pr);
                        toast(`🔔 工具 ${pr.tool} 请求授权`, 'info');
                    }
                    dequeuePermissionModal();
                    break;
                }
                // 权限系统：授权结果（用户响应或超时）
                case 'permission_result': {
                    const pi = state.permPending.findIndex(p => p.id === ev.id);
                    if (pi !== -1) { state.permPending.splice(pi, 1); renderPermPending(); }
                    state.permQueue = state.permQueue.filter(q => q.id !== ev.id);
                    if (state.permModalId === ev.id) hidePermissionModal();
                    if (ev.timed_out) toast('审批超时，已自动拒绝', 'error');
                    else toast(`工具授权已${ev.granted ? '通过' : '拒绝'}`, ev.granted ? 'success' : 'info');
                    break;
                }
                case 'done':
                    updateAssistantMsg(msgId, { content: sb.content, _typing: false, _thinkings: sb.thinkings, _toolCalls: sb.toolOutputs, _usage: ev.usage });
                    // 更新上下文使用显示
                    if (ev.usage) {
                        state.contextUsage.total = ev.usage.total_cost || 0;
                        state.contextUsage.max = state.settings.max_context || 128000;
                        updateContextUsage({ total_cost: state.contextUsage.total });
                    }
                    // 更新本地消息状态
                    state.messages = state.messages.filter(m => m._id !== msgId);
                    const finalMsg = { role: 'ai', content: sb.content, images: [], _thinkings: sb.thinkings, _toolCalls: sb.toolOutputs, _usage: ev.usage };
                    state.messages.push(finalMsg);
                    // 如果服务器返回了新的消息列表，同步
                    if (ev.messages && Array.isArray(ev.messages)) {
                        const parsed = parseMessages(ev.messages);
                        // 更新session显示中的记忆状态
                        els.currentSessionIdDisplay.textContent = `ID: ${state.currentSessionId}${state.memoryActive ? '[🧠]' : '[📃]'}`;
                        // 将 thinkings 和 usage 附加到最后一条 ai 消息
                        for (let i = parsed.length - 1; i >= 0; i--) {
                            if (parsed[i].role === 'ai') { parsed[i]._thinkings = sb.thinkings; parsed[i]._usage = ev.usage; break; }
                        }
                        state.messages = state.messages.slice(0, state.messages.length - 1).concat(parsed);
                    }
                    renderMessages();
                    log('响应完成', 'success');
                    // 自动记忆摘要检查
                    checkAutoSummary();
                    break;
                case 'error':
                    updateAssistantMsg(msgId, { content: '❌ ' + esc(ev.message), _typing: false, _thinkings: sb.thinkings, _toolCalls: sb.toolOutputs });
                    state.messages = state.messages.filter(m => m._id !== msgId);
                    state.messages.push({ role: 'ai', content: '❌ 错误: ' + ev.message, images: [] });
                    renderMessages();
                    log('错误: ' + ev.message, 'error');
                    state.isTyping = false; els.sendBtn.disabled = false;
                    break;
            }
        }

        // ============ 工具权限系统（审批 + 规则管理） ============
        let permPollTimer = null;
        let permCountdownTimer = null;
        function permPendingCount() { return (state.permPending || []).filter(p => !p._answered).length; }
        function updatePermTabBadge() {
            const n = permPendingCount();
            els.permTabBadge.textContent = n > 0 ? String(n) : '';
            els.permTabBadge.style.display = n > 0 ? 'inline-flex' : 'none';
            const pc = document.getElementById('perm-pending-count');
            if (pc) { pc.textContent = n > 0 ? n + ' 待审批' : ''; pc.style.display = n > 0 ? 'inline-flex' : 'none'; }
        }
        async function loadPermissions() {
            try {
                const r = await axios.get(API + '/permissions');
                const d = r.data || {};
                state.permRules = d.rules || [];
                state.permDefaults = d.defaults || {};
                state.permPending = d.pending || [];
                state.permTimeoutSec = d.timeout_sec || 120;
                renderPermissionPanel();
            } catch (e) { log('加载权限数据失败: ' + (e.message || e), 'error'); }
        }
        function renderPermissionPanel() {
            renderPermPending(); renderPermRules(); renderPermDefaults();
            if (els.permToolList && state.tools?.length)
                els.permToolList.innerHTML = state.tools.map(t => `<option value="${esc(t.name)}">`).join('');
            updatePermTabBadge();
        }
        function renderPermPending() {
            const items = (state.permPending || []).filter(p => !p._answered);
            if (!items.length) {
                els.permPendingList.innerHTML = '<div style="color:var(--text-dim);font-size:12px;text-align:center;padding:10px;">暂无待审批请求</div>';
            } else {
                els.permPendingList.innerHTML = items.map(p => `
    <div class="perm-card">
        <div style="display:flex;align-items:center;gap:8px;flex-wrap:wrap;">
            <span class="perm-tool-chip"><i class="fa-solid fa-screwdriver-wrench" style="font-size:10px;"></i>${esc(p.tool)}</span>
            <span style="font-size:11px;color:var(--text-dim);">会话: ${esc(p.session_id || '-')}</span>
            <span style="font-size:11px;color:var(--text-dim);margin-left:auto;">发起 ${new Date((p.created_at || 0) * 1000).toLocaleTimeString()} · 超时 ${p.timeout || 120}s</span>
        </div>
        <pre class="perm-args">${esc(p.args || '(无参数)')}</pre>
        <div style="display:flex;align-items:center;gap:10px;margin-top:8px;">
            <label style="font-size:11px;color:var(--text-muted);display:flex;align-items:center;gap:5px;cursor:pointer;">
                <input type="checkbox" id="perm-remember-${p.id}"> 记住该选择
            </label>
            <div style="margin-left:auto;display:flex;gap:6px;">
                <button class="perm-btn perm-btn-deny" onclick="respondPermission('${p.id}', false)">拒绝</button>
                <button class="perm-btn perm-btn-allow" onclick="respondPermission('${p.id}', true)">允许</button>
            </div>
        </div>
    </div>`).join('');
            }
            updatePermTabBadge();
        }
        function renderPermRules() {
            const rules = state.permRules || [];
            if (!rules.length) {
                els.permRulesList.innerHTML = '<div style="color:var(--text-dim);font-size:12px;text-align:center;padding:10px;">暂无用户规则，工具按默认策略执行</div>';
                return;
            }
            els.permRulesList.innerHTML = rules.map((r, i) => `
    <div class="perm-rule-row">
        <span class="perm-tool-chip" style="font-size:11px;padding:1px 8px;">${esc(r.tool)}</span>
        <code style="font-size:11px;color:var(--text-dim);">${esc(r.pattern || '*')}</code>
        <span class="perm-badge perm-badge-${esc(r.policy)}">${esc(r.policy)}</span>
        <span style="font-size:10px;color:var(--text-dim);margin-left:auto;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;max-width:140px;">${esc(r.note || '')}</span>
        <button class="perm-btn perm-btn-del" onclick="deletePermissionRule(${i})" title="删除规则"><i class="fa-solid fa-trash-can"></i></button>
    </div>`).join('');
        }
        function renderPermDefaults() {
            const d = state.permDefaults || {};
            const keys = Object.keys(d);
            if (!keys.length) { els.permDefaultsList.innerHTML = '<span style="font-size:12px;color:var(--text-dim);">无默认策略</span>'; return; }
            els.permDefaultsList.innerHTML = keys.map(k => `<span class="perm-badge perm-badge-${esc(d[k])}">${esc(k)} → ${esc(d[k])}</span>`).join('');
        }
        async function addPermissionRule() {
            const tool = $('#perm-rule-tool')?.value.trim();
            if (!tool) { toast('请填写工具名', 'error'); return; }
            const pattern = $('#perm-rule-pattern')?.value.trim() || '';
            const policy = $('#perm-rule-policy')?.value || 'ask';
            try {
                await axios.post(API + '/permissions/set', { tool, pattern, policy });
                toast(`规则已保存: ${tool} → ${policy}`, 'success');
                $('#perm-rule-tool').value = ''; $('#perm-rule-pattern').value = '';
                await loadPermissions();
            } catch (e) { toast('保存规则失败: ' + (e.message || e), 'error'); log('权限规则保存失败: ' + (e.message || e), 'error'); }
        }
        async function deletePermissionRule(idx) {
            const r = (state.permRules || [])[idx];
            if (!r) return;
            if (!confirm(`删除规则 ${r.tool}${r.pattern ? ':' + r.pattern : ''}？`)) return;
            try {
                await axios.post(API + '/permissions/delete', { tool: r.tool, pattern: r.pattern });
                toast('规则已删除', 'success');
                await loadPermissions();
            } catch (e) { toast('删除失败: ' + (e.message || e), 'error'); log('权限规则删除失败: ' + (e.message || e), 'error'); }
        }
        async function respondPermission(id, granted) {
            const remember = !!(document.getElementById('perm-remember-' + id)?.checked || document.getElementById('perm-modal-remember')?.checked);
            try {
                await axios.post(API + '/permission/respond', { id, granted, remember });
                const p = (state.permPending || []).find(x => x.id === id);
                if (p) p._answered = true;
                toast(`已${granted ? '允许' : '拒绝'}工具 ${p ? p.tool : ''}${remember ? '（已记住）' : ''}`, granted ? 'success' : 'info');
                renderPermPending();
                if (state.permModalId === id) hidePermissionModal();
            } catch (e) { toast('响应失败: ' + (e.message || e), 'error'); log('审批响应失败: ' + (e.message || e), 'error'); }
        }
        // ——— 授权弹窗 ———
        function showPermissionModal(req) {
            state.permModalId = req.id;
            els.permApproveBody.innerHTML = `
    <div style="display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:10px;">
        <span class="perm-tool-chip" style="font-size:13px;padding:3px 12px;"><i class="fa-solid fa-screwdriver-wrench" style="font-size:11px;"></i>${esc(req.tool)}</span>
        <span style="font-size:11px;color:var(--text-dim);">会话: ${esc(req.session_id || '-')}</span>
        <span style="font-size:11px;color:var(--text-dim);margin-left:auto;" id="perm-modal-countdown"></span>
    </div>
    <div style="font-size:11px;color:var(--text-muted);margin-bottom:4px;">调用参数</div>
    <pre class="perm-args" style="max-height:220px;">${esc(req.args || '(无参数)')}</pre>
    <div style="display:flex;align-items:center;gap:10px;margin-top:12px;">
        <label style="font-size:12px;color:var(--text-muted);display:flex;align-items:center;gap:6px;cursor:pointer;">
            <input type="checkbox" id="perm-modal-remember"> 记住该选择（写入规则）
        </label>
        <div style="margin-left:auto;display:flex;gap:8px;">
            <button class="perm-btn perm-btn-deny" onclick="respondPermission('${req.id}', false)">拒绝</button>
            <button class="perm-btn perm-btn-allow" onclick="respondPermission('${req.id}', true)">允许</button>
        </div>
    </div>`;
            els.permApproveOverlay.classList.add('open');
            startPermCountdown(req);
        }
        function startPermCountdown(req) {
            if (permCountdownTimer) { clearInterval(permCountdownTimer); permCountdownTimer = null; }
            const tick = () => {
                const remain = (req.created_at || 0) + (req.timeout || 120) - Math.floor(Date.now() / 1000);
                const el = document.getElementById('perm-modal-countdown');
                if (el) el.textContent = remain > 0 ? `⏳ 剩余 ${remain}s` : '等待响应...';
                if (remain <= 0 && permCountdownTimer) { clearInterval(permCountdownTimer); permCountdownTimer = null; }
            };
            tick();
            permCountdownTimer = setInterval(tick, 1000);
        }
        function hidePermissionModal() {
            if (permCountdownTimer) { clearInterval(permCountdownTimer); permCountdownTimer = null; }
            els.permApproveOverlay.classList.remove('open');
            state.permModalId = null;
            dequeuePermissionModal();
        }
        function dequeuePermissionModal() {
            if (state.permModalId) return;
            const next = (state.permQueue || []).find(q => !q._handled);
            if (next) { next._handled = true; showPermissionModal(next); }
        }
        els.permApproveCloseBtn.onclick = () => {
            toast('已关闭授权弹窗，请求将在超时后自动拒绝', 'info');
            if (permCountdownTimer) { clearInterval(permCountdownTimer); permCountdownTimer = null; }
            els.permApproveOverlay.classList.remove('open');
            const closedId = state.permModalId;
            state.permModalId = null;
            if (closedId) { const cur = state.permQueue.find(q => q.id === closedId); if (cur) cur._handled = true; }
            dequeuePermissionModal();
        };
        function startPermPoll() { stopPermPoll(); permPollTimer = setInterval(loadPermissions, 3000); }
        function stopPermPoll() { if (permPollTimer) { clearInterval(permPollTimer); permPollTimer = null; } }

        // ============ 工具/任务/频道 ============
        async function loadTools() {
            try { const r = await axios.get(API + '/tools'); state.tools = r.data || []; renderToolToggles(); } catch (e) { log('加载工具列表失败: ' + (e.message || e), 'error'); }
        }
        function renderToolToggles() {
            const enabledCount = state.tools.filter(t => t.enabled !== false).length;
            document.getElementById('tools-enabled-count').textContent = `${enabledCount}/${state.tools.length}`;
            const filter = (document.getElementById('tools-search-input')?.value || '').toLowerCase();
            const filtered = filter ? state.tools.filter(t => t.name.toLowerCase().includes(filter) || (t.description || '').toLowerCase().includes(filter)) : state.tools;
            els.toolsToggleList.innerHTML = filtered.map(t => `
    <div class="tool-toggle-card">
      <div class="tool-toggle-card-top">
        <span class="tool-toggle-name">${esc(t.name)}</span>
        <label class="tool-toggle-switch">
          <input type="checkbox" ${t.enabled !== false ? 'checked' : ''}
                 onchange="toggleTool('${esc(t.name)}', this.checked)">
          <span class="toggle-track"></span>
        </label>
      </div>
      <div class="tool-toggle-desc">${esc(t.description || '无描述')}</div>
    </div>
  `).join('');
            // 如果没有匹配工具
            if (filtered.length === 0) {
                els.toolsToggleList.innerHTML = '<div style="grid-column:1/-1;text-align:center;padding:24px 8px;color:var(--text-dim);font-size:12px;">没有匹配的工具</div>';
            }
        }
        function filterTools() {
            renderToolToggles();
        }
        async function toggleTool(name, enabled) {
            try {
                await axios.post(API + '/tools/toggle', { name, enabled });
                const t = state.tools.find(t => t.name === name);
                if (t) t.enabled = enabled;
                renderToolToggles();
                toast(`工具 ${name} ${enabled ? '已启用' : '已禁用'}`, 'success');
            } catch (e) {
                toast('切换失败', 'error');
                log('工具切换失败: ' + (e.message || e), 'error');
            }
        }

        async function loadTodos() {
            try { const r = await axios.get(API + '/todos'); state.todos = r.data || []; renderTodos(); } catch (e) { log('加载任务列表失败: ' + (e.message || e), 'error'); }
        }
        async function addTodo() {
            const v = els.newTodoInput.value.trim(); if (!v) return;
            const item = { id: String(Date.now()), done: false, content: v };
            try { const r = await axios.post(API + '/todos/new', item); state.todos.push(r.data); renderTodos(); els.newTodoInput.value = ''; } catch (e) { log('添加任务失败: ' + (e.message || e), 'error'); }
        }
        async function toggleTodo(id) {
            const t = state.todos.find(t => t.id == id); if (!t) return;
            t.done = !t.done;
            try { await axios.post(API + '/todos/' + id, t); renderTodos(); } catch (e) { log('更新任务失败: ' + (e.message || e), 'error'); }
        }
        async function deleteTodo(id) {
            if (!confirm('删除该任务？')) return;
            try { await axios.get(API + '/todos/delete/' + id); state.todos = state.todos.filter(t => t.id != id); renderTodos(); } catch (e) { log('删除任务失败: ' + (e.message || e), 'error'); }
        }
        function renderTodos() {
            if (!state.todos?.length) { els.todosList.innerHTML = '<div style="color:var(--text-dim);font-size:13px;text-align:center;padding:16px;">暂无任务</div>'; return; }
            els.todosList.innerHTML = state.todos.map(t =>
                `<div style="display:flex;align-items:center;justify-content:space-between;padding:8px 10px;background:var(--bg-surface);border-radius:var(--radius-sm);margin-bottom:4px;font-size:13px;border:1px solid var(--border-color);">
      <div style="display:flex;align-items:center;gap:8px;flex:1;cursor:pointer;" onclick="toggleTodo('${t.id}')">
        <div style="width:18px;height:18px;border-radius:4px;display:flex;align-items:center;justify-content:center;border:1px solid ${t.done ? 'var(--ok)' : 'var(--border-hover)'};background:${t.done ? 'var(--ok-soft)' : 'var(--bg-base)'};font-size:10px;color:var(--ok);">${t.done ? '✓' : ''}</div>
        <span style="color:var(--text-main);${t.done ? 'text-decoration:line-through;opacity:0.5' : ''}">${esc(t.content)}</span>
      </div>
      <button onclick="deleteTodo('${t.id}')" style="background:none;border:none;color:var(--text-dim);cursor:pointer;padding:4px;font-size:11px;"><i class="fa-solid fa-trash-can"></i></button>
    </div>`
            ).join('');
        }
        // ============ 事件绑定 ============
        els.newSessionBtn.onclick = newSession;
        els.clearSessionBtn.onclick = clearSession;
        els.summarySessionBtn.onclick = genMemory;
        els.thinkToggle.onchange = (e) => state.thinkMode = e.target.checked;
        els.userInput.onkeydown = (e) => { if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); sendMessageStream(); } };
        els.userInput.addEventListener('input', function () { this.style.height = 'auto'; this.style.height = this.scrollHeight + 'px'; });
        els.sendBtn.onclick = sendMessageStream;
        els.addTodoBtn.onclick = addTodo;
        els.channelNewBtn.onclick = channelNewForm;

        // ============ 文件浏览器 ============
        const fb = {
            cwd: '/',
            usedFiles: new Set(),
            visible: false,
            autoExpand: true,

            async loadDir(path) {
                try {
                    const r = await axios.post(API + '/fs/list', { path: path || '/' });
                    const data = r.data;
                    this.cwd = data.cwd || '/';
                    this.renderBreadcrumb(this.cwd);
                    this.renderFiles(data.entries);
                } catch (e) {
                    log('文件浏览器加载失败: ' + (e.message || e), 'error');
                    els.fbListing.innerHTML = '<div style="padding:24px 12px;color:var(--err);font-size:12px;text-align:center;">加载失败</div>';
                }
            },

            async refreshUsed() {
                try {
                    const r = await axios.get(API + '/fs/used');
                    this.usedFiles = new Set((r.data.files || []).map(f => f.replace(/\\/g, '/')));
                } catch (e) { log('刷新已用文件列表失败: ' + (e.message || e), 'error'); }
            },

            async refresh() {
                await this.refreshUsed();
                if (this.visible) await this.loadDir(this.cwd);
            },

            renderBreadcrumb(path) {
                const parts = path.split('/').filter(Boolean);
                const parentPath = parts.length > 1 ? parts.slice(0, -1).join('/') : '';
                const isRoot = parts.length === 0;
                let html = `<button class="fb-parent-btn" data-path="${esc(parentPath)}" title="返回上一级" ${isRoot ? 'disabled' : ''}><i class="fa-solid fa-arrow-up"></i></button>`;
                html += '<span class="fb-crumb" data-path=""><i class="fa-solid fa-house"></i></span>';
                let acc = '';
                for (const p of parts) {
                    acc += (acc ? '/' : '') + p;
                    html += `<span class="fb-crumb-sep">›</span><span class="fb-crumb" data-path="${esc(acc)}">${esc(p)}</span>`;
                }
                els.fbBreadcrumb.innerHTML = html;
                // 点击返回上一级
                const parentBtn = els.fbBreadcrumb.querySelector('.fb-parent-btn');
                if (parentBtn) parentBtn.onclick = () => fb.loadDir(parentBtn.dataset.path);
                // 点击面包屑导航
                els.fbBreadcrumb.querySelectorAll('.fb-crumb').forEach(el => {
                    el.onclick = () => fb.loadDir(el.dataset.path);
                });
            },

            renderFiles(entries) {
                if (!entries || !entries.length) {
                    els.fbListing.innerHTML = '<div style="padding:24px 12px;color:var(--text-dim);font-size:12px;text-align:center;"><i class="fa-solid fa-folder-open" style="display:block;font-size:24px;margin-bottom:8px;opacity:0.3;"></i>空目录</div>';
                    return;
                }
                const sorted = [...entries].sort((a, b) => {
                    if (a.is_dir !== b.is_dir) return a.is_dir ? -1 : 1;
                    return a.name.localeCompare(b.name);
                });
                els.fbListing.innerHTML = sorted.map(entry => {
                    const relPath = entry.path.replace(/\\/g, '/');
                    const isUsed = this.usedFiles.has(relPath);
                    const iconHtml = entry.is_dir
                        ? '<i class="fa-solid fa-folder" style="color:var(--warn);"></i>'
                        : this.getFileIconHtml(entry.name);
                    return `<div class="fb-item" data-path="${esc(relPath)}" data-dir="${entry.is_dir}">
        <span class="fb-item-icon">${iconHtml}</span>
        <span class="fb-item-status ${isUsed ? 'used' : ''}"></span>
        <span class="fb-item-name">${esc(entry.name)}</span>
        <span class="fb-item-size">${entry.is_dir ? '' : this.formatSize(entry.size)}</span>
      </div>`;
                }).join('');
                // 点击行导航/预览
                els.fbListing.querySelectorAll('.fb-item').forEach(el => {
                    el.onclick = () => {
                        const path = el.dataset.path;
                        if (el.dataset.dir === 'true') {
                            fb.loadDir(path);
                        }
                    };
                });
            },

            getFileIconHtml(filename) {
                let cls = 'fa-solid fa-file';
                const ext = filename.split('.').pop()?.toLowerCase();
                // 扩展名 → 颜色映射
                const colors = {
                    js: '#f7df1e', mjs: '#f7df1e', cjs: '#f7df1e',
                    ts: '#3178c6', tsx: '#3178c6', mts: '#3178c6',
                    jsx: '#61dafb',
                    py: '#3572A5', pyw: '#3572A5',
                    json: '#5B5B5B', jsonc: '#5B5B5B',
                    html: '#e34f26', htm: '#e34f26',
                    css: '#1572b6', scss: '#c6538c', less: '#1d365d',
                    md: '#9B59B6', rst: '#9B59B6',
                    cpp: '#00599C', cxx: '#00599C', cc: '#00599C',
                    h: '#00599C', hpp: '#00599C', hxx: '#00599C',
                    c: '#00599C',
                    go: '#00ADD8', rs: '#DEA584',
                    java: '#b07219', kt: '#7F52FF', scala: '#c22d40',
                    rb: '#CC342D', php: '#4F5D95', pl: '#0298c3',
                    swift: '#F05138',
                    yaml: '#6CB4EE', yml: '#6CB4EE', toml: '#8B8B8B',
                    png: '#FF6B6B', jpg: '#FF6B6B', jpeg: '#FF6B6B',
                    gif: '#FF6B6B', svg: '#FFB13B', webp: '#FF6B6B', ico: '#FFB13B',
                    zip: '#7F8C8D', tar: '#7F8C8D', gz: '#7F8C8D', rar: '#7F8C8D', '7z': '#7F8C8D',
                    pdf: '#E74C3C',
                    doc: '#2B579A', docx: '#2B579A', xls: '#217346', xlsx: '#217346',
                    csv: '#217346', tsv: '#217346',
                    sh: '#4CAF50', bash: '#4CAF50', zsh: '#4CAF50', bat: '#4CAF50', ps1: '#4CAF50',
                    log: '#95A5A6', txt: '#95A5A6',
                    cfg: '#8B8B8B', ini: '#8B8B8B', conf: '#8B8B8B',
                    lock: '#7F8C8D', gitignore: '#DD4C35', dockerignore: '#DD4C35',
                    vue: '#42b883', svelte: '#ff3e00',
                    sql: '#e38c00', db: '#e38c00', sqlite: '#e38c00',
                    makefile: '#E44D26', cmake: '#E44D26',
                    wasm: '#654FF0',
                };
                const color = colors[ext] || '';
                if (cls.startsWith('fa-solid fa-file') && !color) {
                    return '<i class="fa-solid fa-file" style="color:var(--text-dim);"></i>';
                }
                const colorStyle = color ? ` style="color:${color}"` : '';
                return `<i class="${cls}"${colorStyle}></i>`;
            },

            formatSize(bytes) {
                if (!bytes || bytes === '0') return '';
                const n = parseInt(bytes);
                if (n < 1024) return n + ' B';
                if (n < 128000) return (n / 1024).toFixed(1) + ' KB';
                if (n < 1073741824) return (n / 128000).toFixed(1) + ' MB';
                return (n / 1073741824).toFixed(1) + ' GB';
            },

            toggle() {
                this.visible = !this.visible;
                els.fbPanel.classList.toggle('open', this.visible);
                if (this.visible) {
                    this.refreshUsed();
                    this.loadDir(this.cwd);
                }
            },

            collapse() {
                this.visible = false;
                els.fbPanel.classList.remove('open');
            },

            show() {
                if (this.visible) return;
                this.visible = true;
                els.fbPanel.classList.add('open');
                this.refreshUsed();
                this.loadDir(this.cwd);
            },

            updateAutoToggleUI() {
                const btn = document.getElementById('fb-auto-toggle');
                if (!btn) return;
                if (this.autoExpand) {
                    btn.style.color = 'var(--ok)';
                    btn.title = '自动展开已开启';
                } else {
                    btn.style.color = '';
                    btn.title = '自动展开已关闭';
                }
            },
        };

        // 文件浏览器事件
        els.fileBrowserBtn.onclick = () => fb.toggle();
        els.fbRefresh.onclick = () => fb.refresh();
        els.fbCollapse.onclick = () => fb.collapse();

        // 自动展开切换按钮
        document.getElementById('fb-auto-toggle').onclick = (e) => {
            e.stopPropagation();
            fb.autoExpand = !fb.autoExpand;
            fb.updateAutoToggleUI();
            toast(`文件自动展开已${fb.autoExpand ? '开启' : '关闭'}`, 'info');
        };

        // ============ 主题切换 ============
        function applyTheme(theme) {
            state.theme = theme;
            document.documentElement.setAttribute('data-theme', theme);
            localStorage.setItem('theme', theme);
            const icon = els.themeToggleBtn?.querySelector('i');
            if (icon) {
                icon.className = theme === 'dark' ? 'fa-solid fa-moon' : 'fa-solid fa-sun';
            }
        }

        function toggleTheme() {
            const newTheme = state.theme === 'dark' ? 'light' : 'dark';
            applyTheme(newTheme);
            toast(`已切换到${newTheme === 'dark' ? '深色' : '浅色'}主题`, 'info');
        }

        els.themeToggleBtn.onclick = toggleTheme;

        // 初始化主题：localStorage 优先，其次 URL ?theme=light|dark（便于分享/测试）
        (function initTheme() {
            let theme = state.theme;
            try {
                const q = new URLSearchParams(location.search).get('theme');
                if (q === 'light' || q === 'dark') theme = q;
            } catch (e) { /* ignore */ }
            applyTheme(theme);
        })();

        // ============ 初始化 ============
        async function init() {
            initWindowShell();
            applyTheme(state.theme);
            await loadSettings();
            state.user = state.settings.user_name || 'User'; els.currentUserDisplay.textContent = state.user;
            state.agentName = state.settings.agent_name || 'assistant';
            document.title = state.user + ' · Chat WebUI';
            const tbName = $('#tb-name');
            if (tbName) tbName.textContent = state.agentName;
            state.contextUsage.max = state.settings.max_context || 128000;
            await getModels(); await loadTools(); await loadTodos(); await loadChannels(); await loadSessions(); await loadPermissions();
            // 初始化文件浏览器设置
            fb.autoExpand = state.settings.filesystem?.auto_expand !== false;
            fb.updateAutoToggleUI();
        }
        init();
