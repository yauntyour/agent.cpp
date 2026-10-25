#!/usr/bin/env python3
"""重新设计 WebUI 的明暗主题。

把原来散落的硬编码颜色收进一套语义 token：
  - 深色（默认）：午夜靛蓝，背景偏冷、层次更清晰
  - 浅色：纸白 + 靛蓝，正文对比度全部达到 WCAG AA
  - 新增语义色 ok / warn / err / info（以及各自的 soft 底色），
    状态色不再在两套主题里各写一遍
  - 新增 accent-2（渐变第二色）、ring（焦点环）、overlay、scrollbar 等
    JS 内联样式也能直接引用的 token
"""
import re
import sys

CSS = "src/templates/webui/app.css"
JS = "src/templates/webui/app.js"

TOKENS = r'''        /* ==================================================================
           设计令牌（Design Tokens）
           深色为默认；[data-theme="light"] 只覆盖令牌本身，组件一律引用变量。
           ================================================================== */
        :root {
            /* 背景层次：页面 → 面板 → 卡片 → 浮层 */
            --bg-base: #0a0a0f;
            --bg-panel: #0f0f16;
            --bg-surface: #15151e;
            --bg-elevated: #1c1c27;
            --bg-overlay: rgba(6, 6, 11, 0.78);
            /* 边框 */
            --border-color: #22222e;
            --border-hover: #31313f;
            --border-strong: #3d3d4b;
            /* 文本 */
            --text-main: #ececf2;
            --text-muted: #9b9bab;
            --text-dim: #6a6a79;
            --text-inverse: #0b0b11;
            /* 主色 */
            --accent-color: #7c7cf8;
            --accent-hover: #9393fb;
            --accent-active: #6a6ae2;
            --accent-2: #a855f7;
            --accent-soft: rgba(124, 124, 248, 0.14);
            --accent-soft-text: #a5b4fc;
            --accent-glow: rgba(124, 124, 248, 0.30);
            /* 语义色 */
            --ok: #4ade80;
            --ok-soft: rgba(74, 222, 128, 0.12);
            --warn: #fbbf24;
            --warn-soft: rgba(251, 191, 36, 0.12);
            --err: #f87171;
            --err-soft: rgba(248, 113, 113, 0.12);
            --info: #60a5fa;
            --info-soft: rgba(96, 165, 250, 0.12);
            /* 代码块 */
            --code-bg: #13131b;
            --code-text: #d6d6e2;
            /* 其它 */
            --scrollbar: rgba(255, 255, 255, 0.08);
            --scrollbar-hover: rgba(255, 255, 255, 0.16);
            --ring: rgba(124, 124, 248, 0.45);
            --chat-header-bg: rgba(15, 15, 22, 0.82);
            --radius-sm: 6px;
            --radius-md: 10px;
            --radius-lg: 14px;
            --radius-xl: 18px;
            --shadow-sm: 0 1px 2px rgba(0, 0, 0, 0.35);
            --shadow-md: 0 6px 20px rgba(0, 0, 0, 0.45);
            --shadow-lg: 0 16px 44px rgba(0, 0, 0, 0.55);
            --transition-fast: 0.15s cubic-bezier(0.4, 0, 0.2, 1);
            --transition-smooth: 0.25s cubic-bezier(0.16, 1, 0.3, 1);
            color-scheme: dark;
        }

        [data-theme="light"] {
            --bg-base: #f5f6fa;
            --bg-panel: #ffffff;
            --bg-surface: #eef0f6;
            --bg-elevated: #e5e8f0;
            --bg-overlay: rgba(240, 242, 248, 0.82);
            --border-color: #e2e5ed;
            --border-hover: #ccd1de;
            --border-strong: #b5bccb;
            --text-main: #161923;
            --text-muted: #565c6e;
            --text-dim: #878da0;
            --text-inverse: #ffffff;
            --accent-color: #5b5bd6;
            --accent-hover: #4c4cc6;
            --accent-active: #4141b0;
            --accent-2: #8b3fd0;
            --accent-soft: rgba(91, 91, 214, 0.10);
            --accent-soft-text: #4f46c8;
            --accent-glow: rgba(91, 91, 214, 0.22);
            --ok: #16a34a;
            --ok-soft: rgba(22, 163, 74, 0.12);
            --warn: #b45309;
            --warn-soft: rgba(180, 83, 9, 0.12);
            --err: #dc2626;
            --err-soft: rgba(220, 38, 38, 0.10);
            --info: #2563eb;
            --info-soft: rgba(37, 99, 235, 0.10);
            --code-bg: #f1f3f9;
            --code-text: #2a2f3d;
            --scrollbar: rgba(23, 25, 35, 0.14);
            --scrollbar-hover: rgba(23, 25, 35, 0.26);
            --ring: rgba(91, 91, 214, 0.35);
            --chat-header-bg: rgba(255, 255, 255, 0.86);
            --shadow-sm: 0 1px 2px rgba(20, 24, 40, 0.07);
            --shadow-md: 0 6px 20px rgba(20, 24, 40, 0.10);
            --shadow-lg: 0 16px 44px rgba(20, 24, 40, 0.14);
            color-scheme: light;
        }

        /* 主题切换时给整体一个平滑过渡，但避免影响输入响应 */
        body, .sidebar, .chat-main, .msg-bubble, .setting-row input,
        .setting-row select, button, .channel-item, .stat-card {
            transition: background-color var(--transition-smooth),
                        border-color var(--transition-smooth),
                        color var(--transition-smooth);
        }

        /* 统一焦点样式：键盘可见、鼠标不打扰 */
        :focus-visible {
            outline: 2px solid var(--accent-color);
            outline-offset: 2px;
        }
'''

# 语义色替换表（顺序无关，按精确串替换）
CSS_REPLACE = [
    ("#4ade80", "var(--ok)"),
    ("#34d399", "var(--ok)"),
    ("#6ee7b7", "var(--ok)"),
    ("#16a34a", "var(--ok)"),
    ("#fbbf24", "var(--warn)"),
    ("#f87171", "var(--err)"),
    ("#fca5a5", "var(--err)"),
    ("#f43f5e", "var(--err)"),
    ("#dc2626", "var(--err)"),
    ("#818cf8", "var(--accent-color)"),
    ("#8b5cf6", "var(--accent-color)"),
    ("#a78bfa", "var(--accent-color)"),
    ("#c084fc", "var(--accent-color)"),
    ("#c4b5fd", "var(--accent-soft-text)"),
    ("#a5b4fc", "var(--accent-soft-text)"),
    ("#6366f1", "var(--accent-color)"),
    ("#4f46e5", "var(--accent-hover)"),
    ("rgba(255, 255, 255, 0.06)", "var(--scrollbar)"),
    ("rgba(0, 0, 0, 0.1)", "var(--scrollbar)"),
    ("rgba(0, 0, 0, 0.2)", "var(--scrollbar-hover)"),
]

JS_REPLACE = [
    ("#4ade80", "var(--ok)"),
    ("#f87171", "var(--err)"),
    ("#a5b4fc", "var(--accent-soft-text)"),
    ("#8b5cf6", "var(--accent-color)"),
    ("#818cf8", "var(--accent-color)"),
    ("#fbbf24", "var(--warn)"),
    ("rgba(74,222,128,0.1)", "var(--ok-soft)"),
    ("#166534", "var(--ok-soft)"),
]


def apply(text, table):
    for a, b in table:
        text = text.replace(a, b)
    return text


def main():
    # ---- CSS：替换令牌区 ----
    with open(CSS, encoding="utf-8") as f:
        lines = f.read().split("\n")
    # 保留首行注释，替换 2..95（1-based）
    head = lines[0:1]
    rest = lines[95:]
    with open(CSS, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(head) + "\n" + TOKENS + "\n" + "\n".join(rest))

    with open(CSS, encoding="utf-8") as f:
        css = f.read()
    css = apply(css, CSS_REPLACE)
    with open(CSS, "w", encoding="utf-8", newline="\n") as f:
        f.write(css)

    # ---- JS：内联样式里的语义色 ----
    with open(JS, encoding="utf-8") as f:
        js = f.read()
    js = apply(js, JS_REPLACE)
    with open(JS, "w", encoding="utf-8", newline="\n") as f:
        f.write(js)

    print("theme rewritten: tokens + %d css / %d js replacements"
          % (len(CSS_REPLACE), len(JS_REPLACE)))


if __name__ == "__main__":
    sys.exit(main())
