'use strict';
/* =====================================================================
   10-avatar-crop —— 头像截取：选图之后先裁再上传
   =====================================================================
   原来的流程是选完图直接上传整张：横竖比不一的全尺寸图被 object-fit: cover
   硬切成圆头像，想露脸还是露腰都由不得用户。这里加一步截取：

   - 正方形视口（头像在界面上全是圆的，裁正方形就够）；圆形Mask圈出最终范围
   - 拖动平移、滚轮 / 滑块缩放；边界钳制保证图片始终覆盖视口（裁不出黑边）
   - 确认后按当前变换把视口区域画进 512×512 画布，出 PNG 再走 persona.avatar
   ===================================================================== */

const CROP = {
  url: '',            // object URL（用完要 revoke）
  img: null,          // 解码后的原图
  nw: 0, nh: 0,       // 原图自然尺寸
  scale: 1,           // 当前缩放（图片像素 → 视口像素）
  minScale: 1,        // 下限：cover，图片至少铺满视口
  maxScale: 4,        // 上限：cover 的若干倍
  x: 0, y: 0,         // 图片中心相对视口中心的偏移（视口 px）
  stage: 272,           // 视口边长（CSS px，随窗口变；量不到时保留旧值）
  out: 512,           // 输出分辨率
  drag: null,         // 拖动状态
};

function cropStageEl() { return $('#crop-stage'); }
function cropCanvas() { return $('#crop-canvas'); }

/* 视口当前边长：CSS 里是 min(272px, 66vw)，以实测为准。
   量不到（display:none 之类）就保留上一次的值，别把边长打成 0。 */
function cropMeasure() {
  const r = cropStageEl().getBoundingClientRect();
  if (r.width > 0) CROP.stage = Math.round(r.width);
  const cv = cropCanvas();
  const dpr = Math.min(3, window.devicePixelRatio || 1);
  const bw = Math.round(CROP.stage * dpr), bh = Math.round(CROP.stage * dpr);
  if (cv.width !== bw || cv.height !== bh) { cv.width = bw; cv.height = bh; }
}

/* 图片必须始终盖住视口：偏移超过这条边就会出现空边 */
function cropClamp() {
  const mx = Math.max(0, (CROP.nw * CROP.scale - CROP.stage) / 2);
  const my = Math.max(0, (CROP.nh * CROP.scale - CROP.stage) / 2);
  CROP.x = Math.min(mx, Math.max(-mx, CROP.x));
  CROP.y = Math.min(my, Math.max(-my, CROP.y));
}

function cropRender() {
  const cv = cropCanvas();
  const ctx = cv.getContext('2d');
  const dpr = cv.width / CROP.stage;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, CROP.stage, CROP.stage);
  const w = CROP.nw * CROP.scale, h = CROP.nh * CROP.scale;
  const dx = (CROP.stage - w) / 2 + CROP.x;
  const dy = (CROP.stage - h) / 2 + CROP.y;
  ctx.imageSmoothingQuality = 'high';
  ctx.drawImage(CROP.img, dx, dy, w, h);
}

/* 滑块 0..100 → minScale..maxScale，对数映射（低倍率段更好捻） */
function cropSliderScale(v) {
  const t = Math.min(100, Math.max(0, v)) / 100;
  return CROP.minScale * Math.pow(CROP.maxScale / CROP.minScale, t);
}
function cropSliderValue() {
  const t = Math.log(CROP.scale / CROP.minScale) / Math.log(CROP.maxScale / CROP.minScale);
  return Math.round(Math.min(1, Math.max(0, t)) * 100);
}
function cropSyncSlider() {
  const z = $('#crop-zoom');
  z.value = String(cropSliderValue());
  z.style.setProperty('--fill', z.value + '%');
}

/* 以某个视口坐标为锚缩放：图片上该点在缩放前后停在原地 */
function cropZoomAt(next, ax, ay) {
  const w = CROP.nw * CROP.scale, h = CROP.nh * CROP.scale;
  const dx = (CROP.stage - w) / 2 + CROP.x;
  const dy = (CROP.stage - h) / 2 + CROP.y;
  // 锚点在图片自然坐标系里的位置
  const u = (ax - dx) / CROP.scale, v = (ay - dy) / CROP.scale;
  CROP.scale = Math.min(CROP.maxScale, Math.max(CROP.minScale, next));
  const w2 = CROP.nw * CROP.scale, h2 = CROP.nh * CROP.scale;
  CROP.x = ax - u * CROP.scale - (CROP.stage - w2) / 2;
  CROP.y = ay - v * CROP.scale - (CROP.stage - h2) / 2;
  cropClamp();
}

/* 加载代号：连选两张图时，后一张会把代号+1，先加载的那张 onload 直接作废，
   不然慢半拍的那张会把新图的截取状态覆盖掉 */
let cropGen = 0;

function openAvatarCrop(file) {
  if (!file) return;
  if (!/^image\//.test(file.type)) { toast('只能选图片文件', 'err'); return; }
  if (file.size > 8 * 1024 * 1024) { toast('图片太大（上限 8MB）', 'err'); return; }
  if (!(S.persona || {}).id) { toast('还没有打开角色', 'err'); return; }

  closeAvatarCrop(); // 上一张没裁完的先收掉
  const gen = ++cropGen;
  CROP.url = URL.createObjectURL(file);
  const img = new Image();
  img.onload = () => {
    if (gen !== cropGen) return;
    CROP.img = img;
    CROP.nw = img.naturalWidth; CROP.nh = img.naturalHeight;
    // 必须先显示再测量：display:none 的元素量出来是 0，视口边长会掉到下限
    $('#crop-modal').classList.remove('hidden');
    cropMeasure();
    // cover 起步：小图不被无谓放大，大图先看到全景再捻
    CROP.minScale = Math.max(CROP.stage / CROP.nw, CROP.stage / CROP.nh);
    CROP.maxScale = CROP.minScale * 6;
    CROP.scale = CROP.minScale;
    CROP.x = 0; CROP.y = 0;
    cropClamp();
    cropRender();
    cropSyncSlider();
  };
  img.onerror = () => {
    if (gen !== cropGen) return;
    toast('图片解码失败', 'err');
    closeAvatarCrop();
  };
  img.src = CROP.url;
}

function closeAvatarCrop() {
  $('#crop-modal').classList.add('hidden');
  CROP.img = null; CROP.drag = null;
  if (CROP.url) { URL.revokeObjectURL(CROP.url); CROP.url = ''; }
}

/* ---------------- 交互 ---------------- */
$('#crop-stage').addEventListener('pointerdown', (e) => {
  if (!CROP.img) return;
  e.preventDefault();
  CROP.drag = { px: e.clientX, py: e.clientY, x: CROP.x, y: CROP.y };
  cropStageEl().setPointerCapture(e.pointerId);
});
$('#crop-stage').addEventListener('pointermove', (e) => {
  if (!CROP.drag || !CROP.img) return;
  CROP.x = CROP.drag.x + (e.clientX - CROP.drag.px);
  CROP.y = CROP.drag.y + (e.clientY - CROP.drag.py);
  cropClamp();
  cropRender();
});
const cropEndDrag = (e) => {
  if (!CROP.drag) return;
  CROP.drag = null;
  try { cropStageEl().releasePointerCapture(e.pointerId); } catch { }
};
$('#crop-stage').addEventListener('pointerup', cropEndDrag);
$('#crop-stage').addEventListener('pointercancel', cropEndDrag);

$('#crop-stage').addEventListener('wheel', (e) => {
  if (!CROP.img) return;
  e.preventDefault();
  const r = cropStageEl().getBoundingClientRect();
  const ax = e.clientX - r.left, ay = e.clientY - r.top;
  cropZoomAt(CROP.scale * (e.deltaY < 0 ? 1.12 : 1 / 1.12), ax, ay);
  cropRender();
  cropSyncSlider();
}, { passive: false });

$('#crop-zoom').addEventListener('input', (e) => {
  if (!CROP.img) return;
  cropZoomAt(cropSliderScale(Number(e.target.value)), CROP.stage / 2, CROP.stage / 2);
  cropRender();
  cropSyncSlider();
});

$('#crop-cancel').onclick = () => closeAvatarCrop();
// 点遮罩（卡片外面）也算取消
$('#crop-modal').addEventListener('pointerdown', (e) => {
  if (e.target === $('#crop-modal')) closeAvatarCrop();
});

window.addEventListener('resize', () => {
  if (!CROP.img || $('#crop-modal').classList.contains('hidden')) return;
  // 视口边长变了：按比例带着缩放和偏移走，画面不跳
  const old = CROP.stage;
  cropMeasure();
  const k = CROP.stage / old;
  CROP.minScale *= k; CROP.maxScale *= k; CROP.scale *= k;
  CROP.x *= k; CROP.y *= k;
  cropClamp();
  cropRender();
  cropSyncSlider();
});

/* ---------------- 确认：按当前变换裁出正方形 ---------------- */
$('#crop-ok').onclick = async () => {
  if (!CROP.img) return;
  const btn = $('#crop-ok');
  btn.disabled = true;
  try {
    // 视口 (0,0)-(stage,stage) 对应图片上的源矩形：
    // 图片自然点 (u,v) 落在视口 (u*scale + dx, v*scale + dy)
    const w = CROP.nw * CROP.scale, h = CROP.nh * CROP.scale;
    const dx = (CROP.stage - w) / 2 + CROP.x;
    const dy = (CROP.stage - h) / 2 + CROP.y;
    const sx = -dx / CROP.scale, sy = -dy / CROP.scale;
    const sw = CROP.stage / CROP.scale, sh = CROP.stage / CROP.scale;
    const out = document.createElement('canvas');
    out.width = CROP.out; out.height = CROP.out;
    const octx = out.getContext('2d');
    octx.imageSmoothingQuality = 'high';
    octx.drawImage(CROP.img, sx, sy, sw, sh, 0, 0, CROP.out, CROP.out);

    const blob = await new Promise(res => out.toBlob(res, 'image/png'));
    if (!blob) throw new Error('图片编码失败');
    const buf = await blob.arrayBuffer();
    let bin = '';
    const bytes = new Uint8Array(buf);
    for (let i = 0; i < bytes.length; i += 0x8000)
      bin += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));

    closeAvatarCrop();
    await cmd('persona.avatar', { id: (S.persona || {}).id, data_b64: btoa(bin), ext: 'png' });
    await refreshState();
    toast('头像已更新', 'ok');
  } catch (err) {
    toast(err.message, 'err');
  } finally {
    btn.disabled = false;
  }
};
