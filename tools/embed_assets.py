#!/usr/bin/env python3
"""把目录树打包成 C++ 头文件（base64 分片表）。

两处用途：

  python tools/embed_assets.py --src src/templates/webui \
      --out src/server/webui_assets.hpp \
      --ns webui::embedded --guard AGENTCPP_SERVER_WEBUI_ASSETS_HPP --index index.html

      → WebUI 静态资源，运行时由 webui 模块解码后作为 HTTP 响应体返回。

  python tools/embed_assets.py --src src/templates/workspace \
      --out src/core/seed_assets.hpp --prefix workspace/ \
      --ns config::seed --guard AGENTCPP_CORE_SEED_ASSETS_HPP

      → 首次启动时写进 <数据根>/workspace/ 的模板文件（提示词 / 工具脚本 / 初始 JSON）。

这样仓库里不需要任何「运行时资源目录」：单个 exe 自带完整界面与默认工作区。

设计取舍：
  * 用 base64 而不是 `\\xNN` 字节数组：源码体积约为原始的 1.33 倍，
    而十六进制数组是 4~6 倍，能明显压低编译期内存与编译时间。
  * 每个文件切成 8KB 分片后拼接：C++ 标准只保证 65535 字节的字符串
    字面量，分片可以规避各编译器不同的上限。
  * 全部用 `inline constexpr` 变量，多 TU 包含也只有一份实例。
"""
import argparse
import base64
import os
import sys

CHUNK = 8 * 1024

MIME = {
    ".html": "text/html",
    ".htm": "text/html",
    ".css": "text/css",
    ".js": "application/javascript",
    ".mjs": "application/javascript",
    ".json": "application/json",
    ".map": "application/json",
    ".txt": "text/plain",
    ".md": "text/markdown",
    ".py": "text/x-python",
    ".sh": "text/x-shellscript",
    ".svg": "image/svg+xml",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".jpeg": "image/jpeg",
    ".gif": "image/gif",
    ".webp": "image/webp",
    ".ico": "image/x-icon",
    ".woff": "font/woff",
    ".woff2": "font/woff2",
    ".ttf": "font/ttf",
    ".otf": "font/otf",
    ".mp3": "audio/mpeg",
    ".wav": "audio/wav",
    ".mp4": "video/mp4",
    ".webm": "video/webm",
    ".wasm": "application/wasm",
    ".pdf": "application/pdf",
}

BANNER = """// ============================================================================
//  @FILE@ —— 由 tools/embed_assets.py 自动生成，请勿手工编辑
//
//  源目录：@SRC@
//  文件数：@COUNT@
//  总字节：@TOTAL@
//
//  重新生成：python tools/embed_assets.py --src @SRC_REL@ --out @OUT_REL@
// ============================================================================
#pragma once
#ifndef @GUARD@
#define @GUARD@

#include <cstddef>

namespace @NS@
{
    struct Asset
    {
        const char *path;                 // 相对路径，统一用 '/'
        const char *mime;                 // Content-Type（写盘的表格用不到）
        const char *const *chunks;        // base64 分片（以 nullptr 结尾）
        std::size_t size;                 // 解码后的字节数
    };

@EXTRA@
"""


def collect(src_dir, prefix):
    items = []
    for root, _dirs, files in os.walk(src_dir):
        for name in sorted(files):
            full = os.path.join(root, name)
            rel = os.path.relpath(full, src_dir).replace(os.sep, "/")
            if rel.startswith(".") or "__pycache__" in rel:
                continue
            with open(full, "rb") as f:
                data = f.read()
            mime = MIME.get(os.path.splitext(name)[1].lower(), "application/octet-stream")
            items.append((prefix + rel, mime, data))
    items.sort(key=lambda x: x[0])
    return items


def ident(rel):
    return "b64_" + "".join(ch if ch.isalnum() else "_" for ch in rel)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--ns", required=True, help="生成代码的命名空间，如 webui::embedded")
    ap.add_argument("--guard", required=True, help="头文件 include guard")
    ap.add_argument("--prefix", default="", help="路径前缀，如 workspace/")
    ap.add_argument("--index", default="", help="目录模式首页（仅 WebUI 需要）")
    args = ap.parse_args()

    if not os.path.isdir(args.src):
        print("source dir not found: " + args.src, file=sys.stderr)
        return 1

    items = collect(args.src, args.prefix)
    if not items:
        print("no assets found in " + args.src, file=sys.stderr)
        return 1

    total = sum(len(d) for _, _, d in items)
    extra = ('        // 目录模式的首页\n'
             '        inline constexpr const char *kIndex = "%s";\n' % args.index) if args.index else ""

    src_rel = os.path.relpath(args.src, root).replace(os.sep, "/")
    out_rel = os.path.relpath(args.out, root).replace(os.sep, "/")
    banner = (BANNER
              .replace("@FILE@", os.path.basename(args.out))
              .replace("@SRC@", src_rel)
              .replace("@SRC_REL@", src_rel)
              .replace("@OUT_REL@", out_rel)
              .replace("@COUNT@", str(len(items)))
              .replace("@TOTAL@", str(total))
              .replace("@GUARD@", args.guard)
              .replace("@NS@", args.ns)
              .replace("@EXTRA@", extra))
    out = [banner]

    # ---- 数据分片 ----
    for rel, _mime, data in items:
        # 先整体编码再切分：逐块编码会在每块末尾补 '='，拼接后就废了
        b64 = base64.b64encode(data).decode("ascii")
        chunks = [b64[i:i + CHUNK] for i in range(0, len(b64), CHUNK)]
        out.append("        inline constexpr const char *%s[] = {\n" % ident(rel))
        for c in chunks:
            out.append('            "%s",\n' % c)
        out.append("            nullptr};\n\n")

    # ---- 索引表 ----
    out.append("        inline constexpr Asset kAssets[] = {\n")
    for rel, mime, data in items:
        out.append('            {"%s", "%s", %s, %d},\n' % (rel, mime, ident(rel), len(data)))
    out.append("        };\n\n")
    out.append("        inline constexpr std::size_t kAssetCount = "
               "sizeof(kAssets) / sizeof(kAssets[0]);\n")
    out.append("} // namespace %s\n\n" % args.ns)
    out.append("#endif // %s\n" % args.guard)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    text = "".join(out)
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)

    print("embedded %d files (%.1f KB) -> %s (%.1f KB source)"
          % (len(items), total / 1024.0, out_rel, len(text) / 1024.0))
    for rel, mime, data in items:
        print("  %-34s %-24s %7d B" % (rel, mime, len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
