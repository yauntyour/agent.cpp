'use strict';
/* =====================================================================
   01-markdown —— 够用就好的渲染器
   角色对话里 *星号* 表示动作而不是斜体，所以这里刻意不实现 Markdown 的
   单星号斜体：*…* → .act（低饱和斜体 + 不朗读），_…_ 才是斜体。
   ===================================================================== */
function mdInline(s) {
  return s
    .replace(/`([^`]+)`/g, (_, c) => `<code>${c}</code>`)
    .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
    .replace(/\*([^*\n]+)\*/g, '<span class="act">$1</span>')
    .replace(/_([^_\n]+)_/g, '<em>$1</em>')
    .replace(/\[([^\]]+)\]\((https?:[^)\s]+)\)/g, '<a href="$2" target="_blank" rel="noreferrer">$1</a>');
}

function mdToHtml(src) {
  if (!src) return '';
  const safe = esc(src).replace(/\r\n/g, '\n');
  const parts = safe.split(/```/);
  let out = '';
  for (let i = 0; i < parts.length; i++) {
    if (i % 2 === 1) {
      // 代码块：第一行可能是语言名
      const body = parts[i].replace(/^[a-zA-Z0-9_+-]*\n/, '');
      out += `<pre><code>${body.replace(/\n$/, '')}</code></pre>`;
      continue;
    }
    const lines = parts[i].split('\n');
    let para = [];
    let list = null; // 'ul' | 'ol'
    const flushPara = () => {
      if (!para.length) return;
      out += `<p>${mdInline(para.join('\n')).replace(/\n/g, '<br>')}</p>`;
      para = [];
    };
    const flushList = () => { if (list) { out += `</${list}>`; list = null; } };
    for (const raw of lines) {
      const line = raw.replace(/\s+$/, '');
      if (!line.trim()) { flushPara(); flushList(); continue; }
      const h = /^(#{1,3})\s+(.*)$/.exec(line);
      if (h) { flushPara(); flushList(); out += `<h${h[1].length}>${mdInline(h[2])}</h${h[1].length}>`; continue; }
      const bq = /^>\s?(.*)$/.exec(line);
      if (bq) { flushPara(); flushList(); out += `<blockquote>${mdInline(bq[1])}</blockquote>`; continue; }
      const ul = /^[-*+]\s+(.*)$/.exec(line);
      if (ul) {
        flushPara();
        if (list !== 'ul') { flushList(); out += '<ul>'; list = 'ul'; }
        out += `<li>${mdInline(ul[1])}</li>`;
        continue;
      }
      const ol = /^\d+[.)]\s+(.*)$/.exec(line);
      if (ol) {
        flushPara();
        if (list !== 'ol') { flushList(); out += '<ol>'; list = 'ol'; }
        out += `<li>${mdInline(ol[1])}</li>`;
        continue;
      }
      if (list) flushList();
      para.push(line);
    }
    flushPara();
    flushList();
  }
  return out;
}
