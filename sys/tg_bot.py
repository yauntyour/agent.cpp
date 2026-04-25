import logging
import requests
import json
import base64
import asyncio
import re
from typing import List, Tuple, Optional
from telegram import Update
from telegram.constants import ChatAction
from telegram.ext import (
    ApplicationBuilder,
    ContextTypes,
    CommandHandler,
    MessageHandler,
    filters,
)

# ---------- 日志 ----------
logging.basicConfig(
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s", level=logging.INFO
)
logger = logging.getLogger(__name__)

# ---------- 配置（可按需替换为环境变量） ----------
PROXY = "http://127.0.0.1:10809"
BOT_API_TOKEN = "8784040222:AAEcJBGK6tj_Qar4wqA4UBjdK64nDhC1wEs"
BACKEND_URL = "http://127.0.0.1:8080/api/input"
TIMEOUT = 600


# ---------- Markdown 安全分割（保留原逻辑） ----------
def _find_markdown_spans(text: str) -> List[Tuple[int, int]]:
    spans = []
    for match in re.finditer(r"```.*?```", text, re.DOTALL):
        spans.append((match.start(), match.end()))
    for match in re.finditer(r"`[^`\n]+`", text):
        spans.append((match.start(), match.end()))
    for match in re.finditer(r"!?\[.*?\]\(.*?\)", text):
        spans.append((match.start(), match.end()))
    for match in re.finditer(r"\*\*.*?\*\*", text):
        spans.append((match.start(), match.end()))
    for match in re.finditer(r"__.*?__", text):
        spans.append((match.start(), match.end()))
    for match in re.finditer(r"(?<!\*)\*[^*\n]+\*(?!\*)", text):
        spans.append((match.start(), match.end()))
    for match in re.finditer(r"(?<!_)_[^_\n]+_(?!_)", text):
        spans.append((match.start(), match.end()))
    spans.sort()
    merged = []
    for s, e in spans:
        if merged and s <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], e))
        else:
            merged.append((s, e))
    return merged


def _is_safe_split_pos(text: str, pos: int, spans: List[Tuple[int, int]]) -> bool:
    if pos <= 0 or pos >= len(text):
        return False
    for s, e in spans:
        if s < pos < e:
            return False
    if pos < len(text) and text[pos].isalnum() and text[pos - 1].isalnum():
        return False
    return True


def _split_long_paragraph_safe(para: str, max_len: int) -> List[str]:
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
        for candidate in range(end, start, -1):
            if _is_safe_split_pos(para, candidate, spans):
                if candidate > 0 and para[candidate - 1] in "。！？!?.":
                    safe_pos = candidate
                    break
        if safe_pos == end and not _is_safe_split_pos(para, safe_pos, spans):
            for candidate in range(end, start, -1):
                if _is_safe_split_pos(para, candidate, spans):
                    safe_pos = candidate
                    break
            else:
                safe_pos = start + max_len
        part = para[start:safe_pos].rstrip()
        if part:
            parts.append(part)
        start = safe_pos
        while start < len(para) and para[start] == " ":
            start += 1
    return parts


def split_markdown_text(text: str, max_len: int = 4096) -> List[str]:
    if len(text) <= max_len:
        return [text]
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
    final_parts = []
    for p in parts:
        if len(p) > max_len:
            for i in range(0, len(p), max_len):
                final_parts.append(p[i : i + max_len])
        else:
            final_parts.append(p)
    return final_parts


async def safe_send(message, text: str, **kwargs):
    await message.reply_text(text, **kwargs)


# ---------- 辅助解析函数 ----------
def extract_assistant_reply(messages: List[dict]) -> Optional[str]:
    """
    从后端返回的消息列表中提取最后一个助手（非 user / tool）的文本回复。
    内容可能是字符串或数组，数组时拼接所有 text 片段。
    """
    for msg in reversed(messages):
        role = msg.get("role", "")
        if role in ("user", "tool"):
            continue
        content = msg.get("content", "")
        if isinstance(content, str):
            return content.strip()
        if isinstance(content, list):
            text_parts = []
            for part in content:
                if part.get("type") == "text":
                    text_parts.append(part.get("text", ""))
            combined = "".join(text_parts).strip()
            if combined:
                return combined
    return None


def extract_images(messages: List[dict]) -> List[str]:
    """
    扫描所有消息，提取 content 数组中的 image_url 对象，返回 base64 字符串列表。
    """
    images = []
    for msg in messages:
        content = msg.get("content")
        if not isinstance(content, list):
            continue
        for part in content:
            if part.get("type") == "image_url":
                url = part.get("image_url", {}).get("url", "")
                if url:
                    images.append(url)
    return images


# ---------- Bot 命令 ----------
async def start(update: Update, context: ContextTypes.DEFAULT_TYPE):
    await context.bot.send_message(
        chat_id=update.effective_chat.id,
        text="Welcome to my bot!",
    )


async def echo(update: Update, context: ContextTypes.DEFAULT_TYPE):
    await context.bot.send_chat_action(
        chat_id=update.effective_chat.id, action=ChatAction.TYPING
    )

    # 构造请求体
    req_data = {
        "model": "default",
        "think": False,  # 如需开启思考模式可改为 True
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

    # 请求后端
    try:
        resp = requests.post(BACKEND_URL, json=req_data, timeout=TIMEOUT)
        resp.raise_for_status()
        data = resp.json()
    except Exception as e:
        logger.error(f"Backend request failed: {e}")
        await update.message.reply_text("连接丢失了嘤嘤嘤~")
        return

    # 解析回复
    messages = data.get("messages", [])
    reply_text = extract_assistant_reply(messages)
    reply_images = extract_images(messages)

    # 思考过程（可选发送）
    thinkings = data.get("thinkings", [])
    if thinkings and isinstance(thinkings, list):
        thinking_blocks = [f"💭 {t}" for t in thinkings if t]
        if thinking_blocks:
            thinking_text = "\n\n".join(thinking_blocks)
            try:
                await safe_send(update.message, thinking_text)
            except Exception as e:
                logger.warning(f"Failed to send thinking: {e}")

    # 发送文本回复
    if reply_text:
        parts = split_markdown_text(reply_text)
        for idx, part in enumerate(parts):
            await safe_send(update.message, part)
            if idx < len(parts) - 1:
                await asyncio.sleep(0.1)

    # 发送图片回复
    for img_b64 in reply_images:
        try:
            # 去除 data URI 前缀
            if "," in img_b64:
                img_b64 = img_b64.split(",")[1]
            img_bytes = base64.b64decode(img_b64)
            await update.message.reply_photo(photo=img_bytes)
        except Exception as e:
            logger.error(f"Failed to send image: {e}")
            await update.message.reply_text("⚠️ 无法解析或发送后端返回的图像。")


async def error_handler(update: Update, context: ContextTypes.DEFAULT_TYPE):
    logger.warning(f"Update {update} caused error {context.error}")
    if update and update.effective_message:
        await update.effective_message.reply_text(f"⚠️ Bot 内部错误: {context.error}")


# ---------- 主入口 ----------
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
