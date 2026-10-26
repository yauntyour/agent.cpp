'use strict';
/* =====================================================================
   12-attach —— 输入框附件：回形针 / 拖拽 / 粘贴 / 托盘 / 消息里的附件渲染

   文件只在本地存在：选进来时图片立刻读成 data URL（托盘和消息里都要预览），
   其它类型先拿一个 object URL 做预览，真正发送时才读成 base64。
   发送用 cmd('send', {text, attachments:[{name,mime,data_b64}]})。

   附件要不要落盘、怎么进上下文，全是服务端的事；页面只负责选、看、发。
   ===================================================================== */

/* ---------------- 小工具 ---------------- */
const EXT_MIME = {
  png: 'image/png', jpg: 'image/jpeg', jpeg: 'image/jpeg', gif: 'image/gif',
  webp: 'image/webp', bmp: 'image/bmp', svg: 'image/svg+xml', avif: 'image/avif',
  mp4: 'video/mp4', webm: 'video/webm', mov: 'video/quicktime', mkv: 'video/x-matroska',
  m4v: 'video/mp4', avi: 'video/x-msvideo',
  mp3: 'audio/mpeg', wav: 'audio/wav', ogg: 'audio/ogg', opus: 'audio/opus',
  m4a: 'audio/mp4', aac: 'audio/aac', flac: 'audio/flac', amr: 'audio/amr',
  pdf: 'application/pdf', txt: 'text/plain', md: 'text/markdown',
  json: 'application/json', zip: 'application/zip',
};

function extOf(name) {
  const m = /\.([A-Za-z0-9]+)$/.exec(String(name || ''));
  return m ? m[1].toLowerCase() : '';
}

function fmtSize(n) {
  n = Number(n) || 0;
  if (n < 1024) return n + ' B';
  if (n < 1024 * 1024) return (n / 1024).toFixed(n < 10 * 1024 ? 1 : 0) + ' KB';
  return (n / 1024 / 1024).toFixed(n < 10 * 1024 * 1024 ? 1 : 0) + ' MB';
}

function mimeByExt(name) {
  return EXT_MIME[extOf(name)] || 'application/octet-stream';
}

/* 页面自己判 kind（服务端也会再判一次）；sticker 只可能从历史/事件里来 */
function kindOf(name, mime) {
  const m = String(mime || '').toLowerCase();
  if (m.indexOf('image/') === 0) return 'image';
  if (m.indexOf('video/') === 0) return 'video';
  if (m.indexOf('audio/') === 0) return 'audio';
  const ext = extOf(name);
  if (['png', 'jpg', 'jpeg', 'gif', 'webp', 'bmp', 'svg', 'avif'].indexOf(ext) >= 0) return 'image';
  if (['mp4', 'webm', 'mov', 'mkv', 'm4v', 'avi'].indexOf(ext) >= 0) return 'video';
  if (['mp3', 'wav', 'ogg', 'opus', 'm4a', 'aac', 'flac', 'amr', 'silk'].indexOf(ext) >= 0) return 'audio';
  return 'file';
}

function defaultName(kind) {
  return ({ image: 'photo.png', video: 'video.mp4', audio: 'voice.ogg' }[kind] || 'file.bin');
}

function fileToDataUrl(file) {
  return new Promise((resolve, reject) => {
    const rd = new FileReader();
    rd.onload = () => resolve(String(rd.result || ''));
    rd.onerror = () => reject(rd.error || new Error('读取文件失败'));
    rd.readAsDataURL(file);
  });
}

function b64OfDataUrl(url) {
  const s = String(url || '');
  const i = s.indexOf(',');
  return i >= 0 ? s.slice(i + 1) : '';
}

/* ---------------- 托盘：添加 / 移除 / 重画 ---------------- */
async function addFiles(fileList) {
  const files = Array.from(fileList || []).filter(f => f && (f.size || f.name));
  if (!files.length) return;
  if (!S.active) {
    toast('先创建一个角色，才能发图片和文件', 'err');
    if (typeof openPersonaEditor === 'function') openPersonaEditor(true);
    return;
  }
  let rejected = 0;
  for (const f of files) {
    if (ATTACH.items.length >= ATTACH.max) {
      toast('一次最多 ' + ATTACH.max + ' 个附件', 'err');
      break;
    }
    if (f.size > ATTACH.maxBytes) {
      toast('「' + (f.name || '文件') + '」太大：单个上限 ' + fmtSize(ATTACH.maxBytes), 'err');
      rejected++;
      continue;
    }
    if (!f.size) {
      toast('「' + (f.name || '文件') + '」是空文件', 'err');
      rejected++;
      continue;
    }
    const mime = f.type || mimeByExt(f.name);
    const kind = kindOf(f.name, mime);
    const item = {
      id: 'a' + (++ATTACH.seq), name: f.name || defaultName(kind),
      mime, size: f.size, kind, url: '', dataUrl: '', file: f,
    };
    try {
      // 图片：现在就读成 data URL，托盘缩略图 / 消息预览 / 发送都用它
      if (kind === 'image') item.dataUrl = await fileToDataUrl(f);
      item.url = item.dataUrl || URL.createObjectURL(f);
    } catch (e) {
      toast('读取「' + item.name + '」失败：' + (e.message || e), 'err');
      continue;
    }
    ATTACH.items.push(item);
  }
  paintAttachTray();
  paintComposer();
}

function removeAttach(id) {
  const i = ATTACH.items.findIndex(x => x.id === id);
  if (i < 0) return;
  const it = ATTACH.items.splice(i, 1)[0];
  // 还没发出去：这张图不可能再被消息引用，把 object URL 还回去
  if (it && it.url && it.url.indexOf('blob:') === 0) URL.revokeObjectURL(it.url);
  paintAttachTray();
  paintComposer();
}

/* 发送成功后清空托盘。这里故意不 revoke object URL：刚渲染的那条消息还在用它预览。 */
function clearAttach() {
  ATTACH.items = [];
  paintAttachTray();
  paintComposer();
}

function paintAttachTray() {
  const tray = $('#attach-tray');
  if (!tray) return;
  tray.innerHTML = '';
  tray.classList.toggle('hidden', !ATTACH.items.length);
  for (const it of ATTACH.items) {
    const row = el('div', 'attach-item');
    const thumb = el('div', 'thumb');
    if (it.kind === 'image' && it.url) {
      const img = document.createElement('img');
      img.src = it.url; img.alt = it.name; img.loading = 'lazy';
      img.onclick = () => openLightbox(it.url, it.name);
      thumb.appendChild(img);
    } else {
      thumb.innerHTML = icon(it.kind === 'video' ? 'video' : (it.kind === 'audio' ? 'audio' : 'file'));
    }
    const info = el('div', 'info');
    info.appendChild(el('div', 'nm', it.name));
    info.appendChild(el('div', 'sz', fmtSize(it.size)));
    const rm = el('button', 'rm');
    rm.type = 'button';
    rm.title = '移除附件';
    rm.innerHTML = icon('x');
    rm.onclick = () => removeAttach(it.id);
    row.append(thumb, info, rm);
    tray.appendChild(row);
  }
}

/* 消息流里要用的本地预览（data URL / object URL），服务器回来之前先这么显示 */
function attachPreviews() {
  return ATTACH.items.map(it => ({
    kind: it.kind, name: it.name, size: it.size, url: it.url,
  }));
}

/* 发送用的 base64 载荷 */
async function attachPayload() {
  const out = [];
  for (const it of ATTACH.items) {
    let dataUrl = it.dataUrl;
    if (!dataUrl && it.file) dataUrl = await fileToDataUrl(it.file);
    if (!dataUrl) continue;
    out.push({
      name: it.name,
      mime: it.mime || mimeByExt(it.name),
      data_b64: b64OfDataUrl(dataUrl),
    });
  }
  return out;
}

/* ---------------- 消息里的附件 ----------------
   一条消息的 attachments 可能是历史里的 {kind,name,size,url,path}，也可能是
   本地刚发出去的预览 {kind,name,size,url}；两种都按 kind 渲染。 */
function attachFileChip(a) {
  const hasUrl = !!a.url;
  const n = el(hasUrl ? 'a' : 'div', 'att-file');
  if (hasUrl) {
    n.href = a.url;
    n.download = a.name || '';
    n.rel = 'noreferrer';
  }
  const ico = el('span', 'i');
  ico.innerHTML = icon('file');
  n.append(ico, el('span', 'n', a.name || '文件'), el('span', 's', fmtSize(a.size)));
  return n;
}

function attachGallery(atts) {
  const list = (atts || []).filter(a => a && a.kind);
  if (!list.length) return null;
  const wrap = el('div', 'atts');
  for (const a of list) {
    const kind = a.kind || 'file';
    if (kind === 'image' && a.url) {
      const btn = el('button', 'att-img');
      btn.type = 'button';
      btn.title = a.name ? a.name + '（点开看大图）' : '查看大图';
      const img = document.createElement('img');
      img.src = a.url; img.alt = a.name || '图片'; img.loading = 'lazy';
      btn.appendChild(img);
      btn.onclick = () => openLightbox(a.url, a.name);
      wrap.appendChild(btn);
    } else if (kind === 'sticker' && a.url) {
      const img = document.createElement('img');
      img.className = 'att-sticker';
      img.src = a.url; img.alt = a.name || '表情包'; img.loading = 'lazy';
      img.onclick = () => openLightbox(a.url, a.name);
      wrap.appendChild(img);
    } else if (kind === 'video' && a.url) {
      const v = document.createElement('video');
      v.className = 'att-video';
      v.controls = true; v.preload = 'metadata'; v.src = a.url;
      wrap.appendChild(v);
    } else if (kind === 'audio' && a.url) {
      const au = document.createElement('audio');
      au.className = 'att-audio';
      au.controls = true; au.preload = 'metadata'; au.src = a.url;
      wrap.appendChild(au);
    } else {
      wrap.appendChild(attachFileChip(a));
    }
  }
  return wrap;
}

/* ---------------- 图片全屏预览 ---------------- */
function openLightbox(url, name) {
  if (!url) return;
  const box = $('#lightbox');
  const img = $('#lightbox-img');
  img.src = url;
  img.alt = name || '图片';
  box.classList.remove('hidden');
}
function closeLightbox() {
  const box = $('#lightbox');
  if (!box || box.classList.contains('hidden')) return;
  box.classList.add('hidden');
  $('#lightbox-img').removeAttribute('src');
}
$('#lightbox-close').onclick = closeLightbox;
$('#lightbox').addEventListener('pointerdown', (e) => {
  if (e.target === $('#lightbox')) closeLightbox();
});
// 用捕获阶段：lightbox 开着时 Esc 只关它，别同时把抽屉关了 / 把回合停了
document.addEventListener('keydown', (e) => {
  if (e.key !== 'Escape') return;
  const box = $('#lightbox');
  if (!box || box.classList.contains('hidden')) return;
  e.preventDefault();
  e.stopPropagation();
  closeLightbox();
}, true);

/* ---------------- 入口：回形针 / 粘贴 / 拖拽 ---------------- */
$('#btn-attach').onclick = () => {
  if (!S.active) {
    toast('先创建一个角色，才能发图片和文件', 'err');
    openPersonaEditor(true);
    return;
  }
  $('#attach-file').click();
};

$('#attach-file').addEventListener('change', (e) => {
  const files = Array.from(e.target.files || []);
  e.target.value = ''; // 复制出来再清空：清空后 FileList 会跟着空
  addFiles(files);
});

// 粘贴：只拦带文件的剪贴板，纯文字照常贴进输入框
document.addEventListener('paste', (e) => {
  const cd = e.clipboardData;
  if (!cd) return;
  const files = Array.from(cd.files || []);
  if (!files.length && cd.items) {
    for (const it of Array.from(cd.items)) {
      if (it.kind === 'file') {
        const f = it.getAsFile();
        if (f) files.push(f);
      }
    }
  }
  if (!files.length) return;
  e.preventDefault();
  addFiles(files);
});

// 拖拽：整个对话区都是落点（含输入框）
const CHAT = $('#chat');
let dragDepth = 0;
function hasFileDrag(e) {
  const dt = e.dataTransfer;
  if (!dt) return false;
  for (const t of Array.from(dt.types || [])) if (t === 'Files') return true;
  return false;
}
CHAT.addEventListener('dragenter', (e) => {
  if (!hasFileDrag(e)) return;
  e.preventDefault();
  dragDepth++;
  CHAT.classList.add('drag-over');
});
CHAT.addEventListener('dragover', (e) => {
  if (!hasFileDrag(e)) return;
  e.preventDefault();
  try { e.dataTransfer.dropEffect = 'copy'; } catch { }
});
CHAT.addEventListener('dragleave', () => {
  dragDepth = Math.max(0, dragDepth - 1);
  if (!dragDepth) CHAT.classList.remove('drag-over');
});
CHAT.addEventListener('drop', (e) => {
  if (!hasFileDrag(e)) return;
  e.preventDefault();
  dragDepth = 0;
  CHAT.classList.remove('drag-over');
  addFiles(Array.from((e.dataTransfer && e.dataTransfer.files) || []));
});
// 落在对话区外面：别让浏览器直接把文件打开（会顶掉整个页面）
document.addEventListener('dragover', (e) => { if (hasFileDrag(e)) e.preventDefault(); });
document.addEventListener('drop', (e) => {
  if (hasFileDrag(e) && !CHAT.contains(e.target)) e.preventDefault();
});
