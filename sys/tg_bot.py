import logging
import requests
import json
import base64
import asyncio
import re
from typing import List, Tuple
from telegram import Update
from telegram.constants import ChatAction
from telegram.ext import (
    ApplicationBuilder,
    ContextTypes,
    CommandHandler,
    MessageHandler,
    filters,
)

# 启用日志，方便看到报错细节
logging.basicConfig(
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s", level=logging.INFO
)
logger = logging.getLogger(__name__)

PROXY = ""
BOT_API_TOKEN = ""


# ---------- Markdown 感知分割函数 ----------
def _find_markdown_spans(text: str) -> List[Tuple[int, int]]:
    """返回所有不应被分割的Markdown结构区间 [(start, end), ...]"""
    spans = []
    # 代码块 ``` ... ```
    for match in re.finditer(r"```.*?```", text, re.DOTALL):
        spans.append((match.start(), match.end()))
    # 行内代码 `...`
    for match in re.finditer(r"`[^`\n]+`", text):
        spans.append((match.start(), match.end()))
    # 链接 [text](url) 或 ![alt](url)
    for match in re.finditer(r"!?\[.*?\]\(.*?\)", text):
        spans.append((match.start(), match.end()))
    # 粗体 **...** 或 __...__
    for match in re.finditer(r"\*\*.*?\*\*", text):
        spans.append((match.start(), match.end()))
    for match in re.finditer(r"__.*?__", text):
        spans.append((match.start(), match.end()))
    # 斜体 *...* 或 _..._（避免匹配到 ** 内部）
    for match in re.finditer(r"(?<!\*)\*[^*\n]+\*(?!\*)", text):
        spans.append((match.start(), match.end()))
    for match in re.finditer(r"(?<!_)_[^_\n]+_(?!_)", text):
        spans.append((match.start(), match.end()))
    # 合并重叠区间
    spans.sort()
    merged = []
    for s, e in spans:
        if merged and s <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], e))
        else:
            merged.append((s, e))
    return merged


def _is_safe_split_pos(text: str, pos: int, spans: List[Tuple[int, int]]) -> bool:
    """判断 pos 是否不在任何不安全区间内，且不在单词中间"""
    if pos <= 0 or pos >= len(text):
        return False
    for s, e in spans:
        if s < pos < e:
            return False
    # 避免在英文单词中间分割（中文无影响）
    if pos < len(text) and text[pos].isalnum() and text[pos - 1].isalnum():
        return False
    return True


def _split_long_paragraph_safe(para: str, max_len: int) -> List[str]:
    """安全分割一个超长段落（无换行符）"""
    if len(para) <= max_len:
        return [para]
    spans = _find_markdown_spans(para)
    parts = []
    start = 0
    while start < len(para):
        end = start + max_len
        if end >= len(para):
            parts.append(para[start:])
            break
        safe_pos = end
        # 优先找句子边界（。！？!?.）
        for candidate in range(end, start, -1):
            if _is_safe_split_pos(para, candidate, spans):
                if candidate > 0 and para[candidate - 1] in "。！？!?.":
                    safe_pos = candidate
                    break
        # 若没有句子边界，找空格
        if safe_pos == end and not _is_safe_split_pos(para, safe_pos, spans):
            for candidate in range(end, start, -1):
                if _is_safe_split_pos(para, candidate, spans):
                    safe_pos = candidate
                    break
            else:
                safe_pos = start + max_len  # 强制截断
        part = para[start:safe_pos].rstrip()
        if part:
            parts.append(part)
        start = safe_pos
        # 跳过分割后的空格
        while start < len(para) and para[start] == " ":
            start += 1
    return parts


def split_markdown_text(text: str, max_len: int = 4096) -> List[str]:
    """分割 Markdown 文本，保证每个片段语法完整"""
    if len(text) <= max_len:
        return [text]
    # 按段落分割（连续两个换行）
    paragraphs = text.split("\n\n")
    parts = []
    current = ""
    for para in paragraphs:
        candidate = (current + "\n\n" + para) if current else para
        if len(candidate) <= max_len:
            current = candidate
        else:
            if current:
                parts.append(current)
                current = ""
            if len(para) > max_len:
                sub_parts = _split_long_paragraph_safe(para, max_len)
                parts.extend(sub_parts)
            else:
                current = para
    if current:
        parts.append(current)
    # 最终检查（防止仍有超长片段）
    final_parts = []
    for p in parts:
        if len(p) > max_len:
            # 降级为纯文本分割（简单按长度切）
            for i in range(0, len(p), max_len):
                final_parts.append(p[i : i + max_len])
        else:
            final_parts.append(p)
    return final_parts


# ---------- 安全发送 Markdown ----------
async def safe_send(message, text: str, **kwargs):
    await message.reply_text(text, **kwargs)


# ---------- Bot 命令处理 ----------
async def start(update: Update, context: ContextTypes.DEFAULT_TYPE):
    await context.bot.send_message(
        chat_id=update.effective_chat.id,
        text="Welcome to my bot!",
    )


async def echo(update: Update, context: ContextTypes.DEFAULT_TYPE):
    await context.bot.send_chat_action(
        chat_id=update.effective_chat.id, action=ChatAction.TYPING
    )
    # 构造发给后端的请求数据
    req_data = {
        "model": "default",
        "think": False,
        "channel": "Telegram",
    }

    if update.message.text:
        req_data["messages"] = update.message.text
    elif update.message.photo:
        img = await update.message.photo[-1].get_file()
        img_bin = await img.download_as_bytearray()
        img_base64 = "data:image/jpeg;base64," + base64.b64encode(img_bin).decode()
        caption = update.message.caption if update.message.caption else "[IMAGE]"
        req_data["messages"] = caption
        req_data["images"] = [img_base64]
    else:
        await update.message.reply_text("Message type is empty or not supported.")
        return

    try:
        response_data = requests.post(
            "http://127.0.0.1:8080/api/input", json=req_data, timeout=6000
        ).json()
    except Exception as e:
        logger.error(f"Failed to connect to backend: {e}")
        await update.message.reply_text("连接丢失了嘤嘤嘤~")
        return

    reply_text = ""
    reply_images = []

    # 解析后端的全新响应格式
    if "messages" in response_data and isinstance(response_data["messages"], list):
        for item in response_data["messages"]:
            if item.get("type") == "response":
                reply_text = item.get("content", "")
            elif item.get("type") == "images":
                reply_images = item.get("content", [])
    else:
        # 兼容旧版本或异常格式
        reply_text = str(response_data)

    # 1. 发送文本回复（Markdown 分段 + 安全发送）
    if reply_text:
        text_parts = split_markdown_text(reply_text)
        for idx, part in enumerate(text_parts):
            await safe_send(update.message, part)
            if idx < len(text_parts) - 1:
                await asyncio.sleep(0.1)  # 避免触发频率限制

    # 2. 发送图像回复
    for img_b64 in reply_images:
        try:
            # 去除可能存在的 data URI 前缀 (如 data:image/jpeg;base64,)
            if "," in img_b64:
                img_b64 = img_b64.split(",")[1]
            img_bytes = base64.b64decode(img_b64)
            await update.message.reply_photo(photo=img_bytes)
        except Exception as e:
            logger.error(f"Failed to decode and send image: {e}")
            await update.message.reply_text("⚠️ 无法解析或发送后端返回的图像。")


async def error_handler(update: Update, context: ContextTypes.DEFAULT_TYPE):
    logger.warning(f"Update {update} caused error {context.error}")
    if update and update.effective_message:
        await update.effective_message.reply_text(f"⚠️ Bot 内部错误: {context.error}")


# ---------- 主程序 ----------
if __name__ == "__main__":
    try:
        application = (
            ApplicationBuilder()
            .token(BOT_API_TOKEN)
            .proxy(PROXY)
            .get_updates_proxy(PROXY)
            .build()
        )

        application.add_handler(CommandHandler("start", start))
        application.add_handler(MessageHandler(filters.ALL & ~filters.COMMAND, echo))
        application.add_error_handler(error_handler)

        print("Bot is polling...")
        application.run_polling()
    except KeyboardInterrupt:
        print("Exiting...")
