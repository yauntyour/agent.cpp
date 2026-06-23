"""
agent.cpp WeChat ClawBot 插件
=============================
从 agent.cpp/settings.json 的 channels[WeChat].config 读取配置。
bot_token 通过 http://127.0.0.1:8080/api/channel/token/WeChat 解密读取，
扫码登录获得的 token 也通过 API 加密存储。
依赖：pip install aiohttp qrcode Pillow
"""

import asyncio
import base64
import json
import logging
import os
import re
import struct
import random
import uuid
import time
import hashlib
import requests
import aiohttp
from urllib.parse import urlparse, quote

# ---------- 日志 ----------
logging.basicConfig(
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s", level=logging.DEBUG
)
logger = logging.getLogger("wx_bot")

# AES 库（用于解密微信 CDN 图片）
_AES = None
try:
    from Cryptodome.Cipher import AES as _AES
except ImportError:
    try:
        from Crypto.Cipher import AES as _AES
    except ImportError:
        logger.warning("未安装 pycryptodome，微信图片解密功能不可用。安装: pip install pycryptodome")

# ---------- 日志 ----------
logging.basicConfig(
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s", level=logging.DEBUG
)
logger = logging.getLogger("wx_bot")


# ---------- 从 settings.json 加载配置 ----------
def _load_settings():
    script_dir = os.path.dirname(__file__)
    settings_path = os.path.join(script_dir, "..", "..", "settings.json")
    with open(settings_path, "r", encoding="utf-8") as f:
        return json.load(f), settings_path


def _load_channel_config(channel_name="WeChat"):
    settings, _ = _load_settings()
    for ch in settings.get("channels", []):
        if ch.get("name") == channel_name:
            cfg = dict(ch.get("config", {}))
            cfg.pop("bot_token", None)
            return cfg
    raise RuntimeError(f"Channel '{channel_name}' not found in settings.json")


def _fetch_bot_token(channel_name="WeChat"):
    """从 C++ 主进程 API 获取解密后的 bot_token"""
    backend = CFG.get("backend_url", "http://127.0.0.1:8080/api/input")
    base_url = backend.rsplit("/api/", 1)[0] if "/api/" in backend else "http://127.0.0.1:8080"
    token_url = f"{base_url}/api/channel/token/{channel_name}"
    try:
        resp = requests.get(token_url, timeout=5)
        if resp.status_code == 200:
            data = resp.json()
            return data.get("token", "")
        elif resp.status_code == 404:
            logger.warning("Token not found for %s (not configured yet)", channel_name)
            return ""
        else:
            logger.error("Failed to fetch token for %s: HTTP %d %s", channel_name, resp.status_code, resp.text)
            return ""
    except Exception as e:
        logger.error("Failed to connect to token API: %s", e)
        return ""


def _save_bot_token_via_api(token: str, channel_name="WeChat"):
    """扫码登录后将 token 加密存储到 C++ 主进程"""
    backend = CFG.get("backend_url", "http://127.0.0.1:8080/api/input")
    base_url = backend.rsplit("/api/", 1)[0] if "/api/" in backend else "http://127.0.0.1:8080"
    api_url = f"{base_url}/api/channel/token"
    try:
        resp = requests.post(api_url, json={"name": channel_name, "token": token}, timeout=5)
        if resp.status_code == 200:
            logger.info("Bot token encrypted and saved via API")
        else:
            logger.error("Failed to save token via API: HTTP %d %s", resp.status_code, resp.text)
    except Exception as e:
        logger.error("Failed to connect to token API for saving: %s", e)


CFG = _load_channel_config("WeChat")

ILINK_BASE = CFG.get("ilink_base", "https://ilinkai.weixin.qq.com")
BACKEND_URL = CFG.get("backend_url", "http://127.0.0.1:8080/api/input")
TIMEOUT = CFG.get("timeout", 600)
THINK = CFG.get("think", False)
MODEL = CFG.get("model", "default")
CHANNEL = "WeChat"
BOT_TOKEN = _fetch_bot_token("WeChat")


# ---------- Markdown 安全分割（微信 max_len=2048）----------
def _find_markdown_spans(text):
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


def _is_safe_split_pos(text, pos, spans):
    if pos <= 0 or pos >= len(text):
        return False
    for s, e in spans:
        if s < pos < e:
            return False
    if pos < len(text) and text[pos].isalnum() and text[pos - 1].isalnum():
        return False
    return True


def _split_long_paragraph_safe(para, max_len):
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


def split_markdown_text(text, max_len=2048):
    """将 markdown 文本按段落安全分割，避免消息超长"""
    if len(text) <= max_len:
        return [text]
    # 先按段落分割
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
    # 最终兜底：强制按长度切割
    final_parts = []
    for p in parts:
        if len(p) > max_len:
            for i in range(0, len(p), max_len):
                final_parts.append(p[i : i + max_len])
        else:
            final_parts.append(p)
    return final_parts


# ---------- 解析后端回复（与 tg_bot.py 保持一致）----------
def extract_assistant_reply(messages):
    """从后端 messages 列表中提取 assistant 的最后一条文本回复"""
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


def extract_images(messages):
    """复用 tg_bot.py 一致的图片提取逻辑"""
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


# ---------- WeChat 消息解析 ----------
# iLink item 类型: 1=TEXT, 2=IMAGE, 3=VOICE, 4=FILE, 5=VIDEO
# 参考: https://github.com/ghostrunner-art/n8n-nodes-wechatbot-peng

def _format_file_size(size_bytes):
    """格式化文件大小为可读字符串"""
    if not size_bytes:
        return ""
    try:
        size_bytes = int(size_bytes)
    except (ValueError, TypeError):
        return f" ({size_bytes})" if size_bytes else ""
    if size_bytes >= 1048576:
        return f" ({size_bytes / 1048576:.1f} MB)"
    elif size_bytes >= 1024:
        return f" ({size_bytes / 1024:.1f} KB)"
    else:
        return f" ({size_bytes} B)"


def _parse_text_item(item):
    """解析 type=1 文本消息"""
    return item.get("text_item", {}).get("text", "")


# ---------- iLink 加密 CDN 图片解密 ----------

# 微信 CDN domain
_CDN_HOSTS = [
    "novac2c.cdn.weixin.qq.com",
]


def _aes_ecb_decrypt(ciphertext: bytes, aes_key_b64: str) -> bytes:
    """AES-128-ECB 解密微信 CDN 加密内容。

    密钥解码链（与微信客户端一致）:
      base64_decode(aes_key) → 32 个 ASCII 十六进制字符 → bytes.fromhex() → 16 字节 AES key
    """
    if _AES is None:
        raise RuntimeError("pycryptodome not installed; cannot decrypt CDN images")
    # base64 → 32 hex chars → 16 raw bytes
    hex_str = base64.b64decode(aes_key_b64).decode("ascii")
    key_bytes = bytes.fromhex(hex_str)
    cipher = _AES.new(key_bytes, _AES.MODE_ECB)
    plaintext = cipher.decrypt(ciphertext)
    # PKCS7 unpad
    pad_len = plaintext[-1]
    if 1 <= pad_len <= 16:
        plaintext = plaintext[:-pad_len]
    return plaintext


async def _cdn_download(
    session: aiohttp.ClientSession,
    encrypt_query_param: str,
    aes_key_b64: str,
) -> bytes | None:
    """从微信 CDN 下载加密图片并解密为原始字节。

    encrypt_query_param: image_item.media.encrypt_query_param
    aes_key_b64: image_item.media.aes_key
    """

    for host in _CDN_HOSTS:
        try:
            url = f"https://{host}/c2c/download?encrypted_query_param={quote(encrypt_query_param, safe='')}"
            logger.debug("[CDN] download attempt: %s...", url[:120])
            async with session.get(
                url,
                headers={"User-Agent": "MicroMessenger/8.0"},
                timeout=aiohttp.ClientTimeout(total=30),
            ) as resp:
                if resp.status == 200:
                    ciphertext = await resp.read()
                    if len(ciphertext) < 32:
                        logger.warning("[CDN] downloaded data too small: %d bytes", len(ciphertext))
                        continue
                    logger.info("[CDN] downloaded %d bytes from %s", len(ciphertext), host)
                    try:
                        plain = _aes_ecb_decrypt(ciphertext, aes_key_b64)
                        logger.info("[CDN] decrypted OK: %d bytes → %d bytes", len(ciphertext), len(plain))
                        return plain
                    except Exception as e:
                        logger.warning("[CDN] decrypt failed: %s", e)
                        # 有些 CDN 不经加密，直接返回明文
                        # 检查文件头（JPEG: FF D8, PNG: 89 50）
                        if ciphertext[:2] == b'\xff\xd8' or ciphertext[:4] == b'\x89PNG':
                            logger.info("[CDN] Image appears unencrypted, using raw bytes")
                            return ciphertext
                        continue
                else:
                    logger.debug("[CDN] %s → HTTP %s", host, resp.status)
        except Exception as e:
            logger.debug("[CDN] %s error: %s", host, e)
    return None


def _parse_image_item(item):
    """解析 type=2 图片消息 → ("encrypted", (encrypt_query_param, aes_key)) 或 None

    iLink image_item 结构:
      {
        "image_item": {
          "media": {
            "encrypt_query_param": "...",
            "aes_key": "..."       // base64(hex_string)
          },
          "thumb_media": { ... }
        }
      }
    """
    img_item = item.get("image_item") or item.get("img_item") or {}

    # 获取 full-resolution media（优先）或 thumb
    media = img_item.get("media") or img_item.get("full") or {}
    encrypt_query = media.get("encrypt_query_param", "")
    aes_key = media.get("aes_key", "")

    # 如果没有 media，检查 thumb_media
    if not encrypt_query or not aes_key:
        thumb = img_item.get("thumb_media") or img_item.get("thumb") or {}
        encrypt_query = encrypt_query or thumb.get("encrypt_query_param", "")
        aes_key = aes_key or thumb.get("aes_key", "")

    # 旧版字段兼容（url / data）
    if not encrypt_query or not aes_key:
        img_url = img_item.get("url") or img_item.get("cdn_url") or img_item.get("image_url") or ""
        img_data = img_item.get("data") or img_item.get("content") or ""
        if img_url:
            logger.debug("[parse_image] legacy url: %s", img_url[:80])
            return ("url", img_url)
        elif img_data:
            logger.debug("[parse_image] legacy data: %d bytes", len(img_data))
            return ("data", img_data)

        # 尝试 item 自身
        img_url = item.get("url") or item.get("cdn_url") or ""
        img_data = item.get("data") or item.get("content") or ""
        if img_url:
            return ("url", img_url)
        elif img_data:
            return ("data", img_data)
        return None

    logger.info(
        "[parse_image] encrypted CDN media: encrypt_query=%s..., aes_key=%s...",
        encrypt_query[:60],
        aes_key[:20],
    )
    return ("encrypted", (encrypt_query, aes_key))


def _parse_voice_item(item):
    """解析 type=3 语音消息 → 描述文本"""
    v = item.get("voice_item") or {}
    duration = v.get("duration") or v.get("play_length") or v.get("len", "")
    duration_str = f" {duration}s" if duration else ""
    return f"[📞 Voice Message{duration_str}]"


def _parse_file_item(item):
    """解析 type=4 文件消息 → 描述文本"""
    f = item.get("file_item") or {}
    filename = f.get("file_name") or f.get("filename") or f.get("title") or "未知文件"
    size_str = _format_file_size(f.get("file_size") or f.get("size") or f.get("file_len", ""))
    return f"[📎 File: {filename}{size_str}]"


def _parse_video_item(item):
    """解析 type=5 视频消息 → 描述文本（视频无法直接传给 agent）"""
    v = item.get("video_item") or {}
    duration = v.get("duration") or v.get("play_length", "")
    duration_str = f" {duration}s" if duration else ""
    thumbnail = v.get("thumb_url") or v.get("cover_url", "")
    return f"[🎬 Video Message{duration_str}]"


def parse_wx_items(item_list):
    """解析 WeChat 消息的 item_list，返回 (text, image_items)

    统一处理所有 iLink 消息类型:
      type=1 TEXT   → 拼入 text
      type=2 IMAGE  → 收集到 image_items
      type=3 VOICE  → 转为描述文本
      type=4 FILE   → 转为描述文本
      type=5 VIDEO  → 转为描述文本
    其他未知类型     → 转为占位描述

    image_items 每项为 (kind, payload):
      - ("encrypted", (encrypt_query_param, aes_key)): 加密 CDN 图片，需下载+解密
      - ("url", str):   旧版：图片直接 URL
      - ("data", str):  旧版：图片原始 base64 数据
    """
    text_parts = []
    images = []

    _ITEM_PARSERS = {
        1: ("text", _parse_text_item),
        2: ("image", _parse_image_item),
        3: ("voice", _parse_voice_item),
        4: ("file", _parse_file_item),
        5: ("video", _parse_video_item),
    }

    for item in item_list:
        item_type = item.get("type", 0)
        if item_type in _ITEM_PARSERS:
            kind, parser = _ITEM_PARSERS[item_type]
            result = parser(item)
            if result is None:
                continue
            if kind == "text":
                if result:
                    text_parts.append(result)
            elif kind == "image":
                images.append(result)
            else:
                # voice / file / video → 描述文本
                text_parts.append(result)
        else:
            # 未知类型 → 占位描述
            text_parts.append(f"[📩 Unknown message type={item_type}]")

    text = "".join(text_parts).strip()
    return text, images


async def download_and_decrypt_image(
    session: aiohttp.ClientSession,
    img_item: tuple,
) -> bytes | None:
    """统一图片下载入口：支持 encrypted / url / data 三种格式

    img_item: ("encrypted", (encrypt_query_param, aes_key))
               ("url", url_string)
               ("data", base64_string)
    返回：解码后的 JPEG/PNG 原始字节，或 None
    """
    img_type, img_content = img_item
    if img_type == "encrypted":
        encrypt_query, aes_key = img_content
        return await _cdn_download(session, encrypt_query, aes_key)
    elif img_type == "url":
        img_url = img_content
        # 用旧的直连下载方式（需带 iLink headers）
        # 但这个方式已经不太可能工作了——保留兼容
        logger.debug("[download] legacy URL download: %s", img_url[:80])
        async with session.get(
            img_url,
            timeout=aiohttp.ClientTimeout(total=30),
        ) as resp:
            if resp.status == 200:
                return await resp.read()
        return None
    elif img_type == "data":
        # 直接解码 base64
        img_data = img_content
        # 去掉 data URI 前缀
        for prefix in ["data:image/jpeg;base64,", "data:image/png;base64,", "data:image/webp;base64,"]:
            if img_data.startswith(prefix):
                img_data = img_data[len(prefix):]
                break
        try:
            return base64.b64decode(img_data)
        except Exception:
            return None
    return None


async def _resp_json(resp: aiohttp.ClientResponse) -> dict:
    """iLink API 统一返回 application/octet-stream，强行按 JSON 解析"""
    text = await resp.text()
    logger.debug("[iLink] HTTP %s, body(%d): %s", resp.status, len(text), text[:500])
    if not text:
        logger.warning("[iLink] Empty response body, HTTP %s", resp.status)
        return {"ret": -1, "msg": "empty response"}
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        logger.warning("[iLink] Non-JSON response: %s", text[:500])
        return {"ret": -2, "msg": "non-json response", "raw": text[:1000]}


# ---------- 扫码登录 ----------
async def login_flow(session: aiohttp.ClientSession):
    """获取二维码 → 弹窗显示 → 阻塞等待扫码/过期/超时 → 返回 token 或 None"""
    logger.info("获取登录二维码...")

    # 1. 从 iLink 获取二维码
    async with session.get(f"{ILINK_BASE}/ilink/bot/get_bot_qrcode?bot_type=3") as resp:
        data = await _resp_json(resp)

    if data.get("ret") != 0:
        logger.error("获取二维码失败: %s", data)
        return None

    qrcode_token = data["qrcode"]
    qr_url = data["qrcode_img_content"]
    logger.info("QR token: %s", qrcode_token)

    # 2. 生成二维码图片并弹出
    try:
        import qrcode as _qr

        qr = _qr.QRCode()
        qr.add_data(qr_url)
        qr.make(fit=True)
        img = qr.make_image(fill_color="black", back_color="white")
        img.thumbnail((400, 400))
        img.show()
        logger.info("二维码已弹出，请用微信扫描。")
    except Exception as e:
        logger.warning("无法弹出图片: %s，请手动打开: %s", e, qr_url)

    # 3. 阻塞等待扫码
    token = None
    last_state = ""
    logger.info("等待扫码中...")

    for i in range(60):  # 最多 120 秒
        await asyncio.sleep(2)
        try:
            async with session.get(
                f"{ILINK_BASE}/ilink/bot/get_qrcode_status?qrcode={qrcode_token}"
            ) as resp:
                status = await _resp_json(resp)
        except Exception as e:
            logger.warning("QR poll [%d] error: %s", i, e)
            continue

        state = status.get("status", "")
        if state != last_state:
            logger.info("QR status → %s", state)
            last_state = state

        if state == "scanned":
            logger.info("已扫码，等待确认...")
        elif state in ("confirmed", "success"):
            token = status.get("bot_token") or status.get("token", "")
            if token:
                logger.info("扫码成功！")
                break
        elif state == "expired":
            logger.error("二维码已过期")
            break
        elif state == "pending":
            pass

    return token


def make_headers(token: str) -> dict:
    uin = base64.b64encode(struct.pack("<I", random.randint(0, 0xFFFFFFFF))).decode()
    return {
        "Content-Type": "application/json",
        "AuthorizationType": "ilink_bot_token",
        "X-WECHAT-UIN": uin,
        "Authorization": f"Bearer {token}",
    }


def _make_client_id():
    """每一条发送消息需要一个唯一 client_id，缺失会被静默丢弃"""
    return f"claw_{int(time.time() * 1000)}_{uuid.uuid4().hex[:12]}"


# ---------- 消息发送 ----------
async def send_text(
    session: aiohttp.ClientSession,
    headers: dict,
    to_user: str,
    ctx_token: str,
    text: str,
) -> dict:
    """发送文本消息"""
    payload = {
        "msg": {
            "to_user_id": to_user,
            "message_type": 2,
            "message_state": 2,
            "context_token": ctx_token,
            "client_id": _make_client_id(),
            "item_list": [{"type": 1, "text_item": {"text": text}}],
        }
    }
    logger.debug("[send_text] payload: %s", json.dumps(payload, ensure_ascii=False)[:500])
    async with session.post(
        f"{ILINK_BASE}/ilink/bot/sendmessage",
        headers=headers,
        json=payload,
        timeout=aiohttp.ClientTimeout(total=30),
    ) as resp:
        result = await _resp_json(resp)
        logger.info("[send_text] HTTP %s, resp: %s", resp.status, json.dumps(result, ensure_ascii=False)[:300])
        return result


async def send_image(
    session: aiohttp.ClientSession,
    headers: dict,
    to_user: str,
    ctx_token: str,
    img_bytes: bytes,
    caption: str = "",
) -> dict:
    """上传并发送图片消息（完整 iLink 加密 CDN 流程）

    流程:
      1. 生成随机 16 字节 AES-128 密钥 + PKCS7 加密图片
      2. getuploadurl（计算 md5 / size）
      3. POST 上传密文到 CDN，抓取 x-encrypted-param 响应头
      4. 构造 image_item（media.encrypt_query_param, media.aes_key）
      5. 通过 sendmessage 发送
    """
    if _AES is None:
        logger.error("pycryptodome 未安装，无法发送图片")
        return {"ret": -1}

    raw_size = len(img_bytes)
    raw_md5 = hashlib.md5(img_bytes).hexdigest()

    # 1. 生成 AES-128 密钥 + PKCS7 填充 + ECB 加密
    aes_key = os.urandom(16)
    # PKCS7 填充到 16 字节边界
    pad_len = 16 - (raw_size % 16)
    padded = img_bytes + bytes([pad_len]) * pad_len

    cipher = _AES.new(aes_key, _AES.MODE_ECB)
    ciphertext = cipher.encrypt(padded)
    ct_size = len(ciphertext)

    # aes_key 格式: base64(hex_string) —— 与微信客户端解析链一致
    aes_key_b64 = base64.b64encode(aes_key.hex().encode("ascii")).decode("ascii")

    # 生成缩略图（250×缩放好的 JPEG）
    thumb_bytes, thumb_aes_key, thumb_aes_key_b64 = _make_thumbnail(img_bytes)
    thumb_raw_size = len(thumb_bytes)
    thumb_raw_md5 = hashlib.md5(thumb_bytes).hexdigest()
    # PKCS7 填充 + 加密缩略图
    thumb_pad_len = 16 - (thumb_raw_size % 16)
    thumb_padded = thumb_bytes + bytes([thumb_pad_len]) * thumb_pad_len
    thumb_cipher = _AES.new(thumb_aes_key, _AES.MODE_ECB)
    thumb_ciphertext = thumb_cipher.encrypt(thumb_padded)
    thumb_ct_size = len(thumb_ciphertext)

    # 2. 获取上传 URL
    filekey = f"img_{uuid.uuid4().hex[:8]}"
    up_req = {
        "to_user_id": to_user,
        "filekey": filekey,
        "media_type": 1,  # IMAGE
        "rawsize": raw_size,
        "rawfilemd5": raw_md5,
        "filesize": ct_size,
        "thumb_rawsize": thumb_raw_size,
        "thumb_rawfilemd5": thumb_raw_md5,
        "thumb_filesize": thumb_ct_size,
    }
    logger.debug("[send_image] getuploadurl req: %s", json.dumps(up_req, ensure_ascii=False)[:300])

    try:
        async with session.post(
            f"{ILINK_BASE}/ilink/bot/getuploadurl",
            headers=headers,
            json=up_req,
            timeout=aiohttp.ClientTimeout(total=15),
        ) as up_resp:
            up_data = await _resp_json(up_resp)
    except Exception as e:
        logger.warning("getuploadurl 失败: %s", e)
        return {"ret": -1}

    logger.debug("[send_image] getuploadurl resp: %s", json.dumps(up_data, ensure_ascii=False)[:500])

    upload_full_url = up_data.get("upload_full_url") or up_data.get("upload_url", "")
    thumb_upload_url = up_data.get("thumb_upload_full_url") or up_data.get("thumb_upload_url", "")

    if not upload_full_url:
        logger.warning("未获取到 upload_full_url: %s", up_data)
        return {"ret": -1}

    # 3. 上传密文到 CDN — POST 优先，失败回退 PUT
    encrypt_query_param = await _cdn_upload(session, upload_full_url, ciphertext)
    if not encrypt_query_param:
        return {"ret": -1}

    # 上传缩略图
    thumb_encrypt_param = encrypt_query_param  # 默认复用，如果没有单独 URL
    if thumb_upload_url and thumb_upload_url != upload_full_url:
        thumb_encrypt_param = await _cdn_upload(session, thumb_upload_url, thumb_ciphertext) or thumb_encrypt_param

    # 4. 构造 image_item
    image_item = {
        "type": 2,
        "image_item": {
            "media": {
                "encrypt_query_param": encrypt_query_param,
                "aes_key": aes_key_b64,
            },
            "thumb_media": {
                "encrypt_query_param": thumb_encrypt_param,
                "aes_key": thumb_aes_key_b64,
            },
        },
    }
    item_list = [image_item]
    if caption:
        item_list.append({"type": 1, "text_item": {"text": caption}})

    # 5. 发送消息
    try:
        async with session.post(
            f"{ILINK_BASE}/ilink/bot/sendmessage",
            headers=headers,
            json={
                "msg": {
                    "to_user_id": to_user,
                    "message_type": 2,
                    "message_state": 2,
                    "context_token": ctx_token,
                    "client_id": _make_client_id(),
                    "item_list": item_list,
                }
            },
            timeout=aiohttp.ClientTimeout(total=30),
        ) as resp:
            result = await _resp_json(resp)
            logger.info("[send_image] sendmessage resp: %s", json.dumps(result, ensure_ascii=False)[:300])
            return result
    except Exception as e:
        logger.warning("send_image 发送消息失败: %s", e)
        return {"ret": -1}


async def _cdn_upload(
    session: aiohttp.ClientSession,
    upload_url: str,
    data: bytes,
) -> str | None:
    """上传数据到微信 CDN，返回 x-encrypted-param（即 encrypt_query_param）

    先用 POST 尝试，404 时回退 PUT
    """
    upload_headers = {"Content-Type": "application/octet-stream"}
    for method in ("POST", "PUT"):
        try:
            async with session.request(
                method,
                upload_url,
                data=data,
                headers=upload_headers,
                timeout=aiohttp.ClientTimeout(total=60),
            ) as resp:
                logger.info(
                    "[CDN upload] %s %s → HTTP %s, headers: %s",
                    method,
                    upload_url[:80],
                    resp.status,
                    {k.lower(): v for k, v in resp.headers.items() if "encrypt" in k.lower() or "x-" in k.lower()},
                )
                if resp.status in (200, 201):
                    # 关键：encrypt_query_param 来自响应头 x-encrypted-param，不是 URL 中的 token
                    encrypt_param = resp.headers.get("x-encrypted-param") or resp.headers.get("X-Encrypted-Param", "")
                    if encrypt_param:
                        return encrypt_param
                    # fallback: 从响应体 JSON 中取
                    try:
                        body = await resp.json()
                        encrypt_param = body.get("encrypt_query_param") or body.get("encrypted_param", "")
                        if encrypt_param:
                            return encrypt_param
                    except Exception:
                        pass
                    # 最后的 fallback: 尝试从 URL 中提取 query 部分
                    # （旧版 API 的 upload_url 中的 query param 就是 encrypt_query_param）
                    parsed = urlparse(upload_url)
                    if parsed.query:
                        logger.warning("[CDN upload] using URL query as encrypt_query_param (fallback)")
                        return parsed.query
                    logger.warning("[CDN upload] 上传成功但无法提取 encrypt_query_param")
        except Exception as e:
            logger.debug("[CDN upload] %s error: %s", method, e)
    return None


def _make_thumbnail(img_bytes: bytes, max_size: int = 250) -> tuple:
    """生成缩略图用于 send_image 的 thumb_media

    返回: (thumb_bytes, aes_key, aes_key_b64)
    """
    thumb_aes_key = os.urandom(16)
    thumb_aes_key_b64 = base64.b64encode(thumb_aes_key.hex().encode("ascii")).decode("ascii")
    try:
        from PIL import Image as _PIL_Image
        import io

        img = _PIL_Image.open(io.BytesIO(img_bytes))
        img.thumbnail((max_size, max_size), _PIL_Image.LANCZOS)
        buf = io.BytesIO()
        # 转 RGB（以防 RGBA/灰度/P 模式）
        if img.mode in ("RGBA", "P", "LA"):
            img = img.convert("RGB")
        img.save(buf, format="JPEG", quality=60)
        return buf.getvalue(), thumb_aes_key, thumb_aes_key_b64
    except ImportError:
        pass
    # 无 PIL 时：取前 2KB 作为"缩略图"（勉强可显示）
    fallback = img_bytes[:2048] if len(img_bytes) > 2048 else img_bytes
    return fallback, thumb_aes_key, thumb_aes_key_b64


async def send_paragraphs(
    session: aiohttp.ClientSession,
    headers: dict,
    to_user: str,
    ctx_token: str,
    text: str,
    max_len: int = 2048,
    delay: float = 0.2,
):
    """按段落安全分割并逐条发送文本消息"""
    parts = split_markdown_text(text, max_len)
    for idx, part in enumerate(parts):
        result = await send_text(session, headers, to_user, ctx_token, part)
        logger.info(
            "[←] sent text (%d/%d) ret=%s",
            idx + 1,
            len(parts),
            result.get("ret"),
        )
        if idx < len(parts) - 1:
            await asyncio.sleep(delay)
    return len(parts)


# ---------- 主逻辑 ----------
async def main():
    global BOT_TOKEN

    logger.info("━━━ agent.cpp WeChat ClawBot 启动 ━━━")
    logger.info("后端: %s", BACKEND_URL)

    # 判断 bot_token 是否有效
    token = BOT_TOKEN if BOT_TOKEN and BOT_TOKEN not in ("null", "None", "") else ""

    async with aiohttp.ClientSession() as session:

        if not token:
            logger.info("BOT_TOKEN 为空，触发扫码登录...")
            token = await login_flow(session)
            if not token:
                logger.error("登录失败")
                return
            _save_bot_token_via_api(token)
            BOT_TOKEN = token

        logger.info("Bot Token: %s...", token[:20])
        headers = make_headers(token)

        # ── 主循环 ──
        cursor = ""
        poll_count = 0
        while True:
            try:
                async with session.post(
                    f"{ILINK_BASE}/ilink/bot/getupdates",
                    headers=headers,
                    json={"get_updates_buf": cursor},
                    timeout=aiohttp.ClientTimeout(total=45),
                ) as resp:
                    updates = await _resp_json(resp)

                new_cursor = updates.get("get_updates_buf", "")
                if new_cursor and new_cursor != cursor:
                    cursor = new_cursor
                    poll_count = 0

                poll_count += 1
                if poll_count == 1 or poll_count % 10 == 0:
                    logger.info("轮询中... (#%d)", poll_count)

                msg_list = updates.get("msgs", [])

                for raw in msg_list:
                    msg = raw.get("msg") or raw
                    to_user = msg.get("from_user_id") or raw.get("from_user_id", "")
                    ctx_token = msg.get("context_token") or raw.get("context_token", "")
                    items = msg.get("item_list") or raw.get("item_list", [])

                    # 解析 WeChat 消息（文本 + 图片）
                    user_text, wx_images = parse_wx_items(items)
                    if not to_user or (not user_text and not wx_images):
                        continue

                    logger.info(
                        "[→] %s: %s%s",
                        to_user[:20],
                        user_text[:80] if user_text else "",
                        f" +{len(wx_images)} image(s)" if wx_images else "",
                    )

                    # 发送"正在输入"
                    try:
                        async with session.post(
                            f"{ILINK_BASE}/ilink/bot/sendtyping",
                            headers=headers,
                            json={"to_user_id": to_user, "context_token": ctx_token},
                            timeout=aiohttp.ClientTimeout(total=10),
                        ):
                            pass
                    except Exception:
                        pass

                    # ── 构造请求（与 tg_bot.py 保持一致）──
                    req_data = {
                        "model": MODEL,
                        "think": THINK,
                        "channel": CHANNEL,
                    }

                    # 处理微信图片 → data:image/jpeg;base64,... 格式
                    req_images = []
                    for img_item in wx_images:
                        img_bytes = await download_and_decrypt_image(session, img_item)
                        if img_bytes:
                            img_b64 = "data:image/jpeg;base64," + base64.b64encode(img_bytes).decode()
                            req_images.append(img_b64)
                            logger.info("[IMG] downloaded+decrypted: %d bytes → base64 %d chars", len(img_bytes), len(img_b64))
                        else:
                            logger.warning("[IMG] download_and_decrypt_image returned None for %s", img_item[0])

                    if req_images:
                        # 与 tg_bot.py 一致：有图片时 caption 作为 messages
                        req_data["messages"] = user_text if user_text else "[IMAGE]"
                        req_data["images"] = req_images
                    else:
                        req_data["messages"] = user_text

                    # ── 调 agent.cpp 后端 ──
                    try:
                        resp = requests.post(
                            BACKEND_URL, json=req_data, timeout=TIMEOUT
                        )
                        resp.raise_for_status()
                        data = resp.json()
                        logger.info(
                            "Backend response: content=%s, rounds=%s, thinking=%s, tools=%s",
                            str(data.get("content", ""))[:100],
                            data.get("rounds"),
                            len(data.get("thinking") or data.get("thinkings", [])),
                            len(data.get("tools", [])),
                        )
                    except Exception as e:
                        logger.error("Backend error: %s", e)
                        await send_text(
                            session, headers, to_user, ctx_token, "连接丢失了嘤嘤嘤~"
                        )
                        continue

                    # ── 解析回复（与 tg_bot.py 保持一致）──
                    messages = data.get("messages", [])
                    reply_text = extract_assistant_reply(messages)
                    if not reply_text:
                        reply_text = data.get("content", "").strip()
                    reply_images = extract_images(messages)

                    # ── 发送思考过程（与 tg_bot.py 一致：💭 前缀）──
                    thinkings = data.get("thinkings") or data.get("thinking", [])
                    if thinkings and isinstance(thinkings, list):
                        blocks = [f"💭 {t}" for t in thinkings if t]
                        if blocks:
                            thinking_text = "\n\n".join(blocks)
                            await send_paragraphs(
                                session, headers, to_user, ctx_token, thinking_text
                            )

                    # ── 发送文本回复（按段落分割）──
                    if reply_text:
                        sent_count = await send_paragraphs(
                            session, headers, to_user, ctx_token, reply_text
                        )
                        logger.info("[←] text reply: %d paragraph(s)", sent_count)
                    else:
                        logger.info("[←] no text reply")

                    # ── 发送图片回复（仿照 tg_bot.py reply_photo 模式）──
                    for img_idx, img_b64 in enumerate(reply_images):
                        try:
                            # 解析 data URI（格式: data:image/xxx;base64,...）
                            if "," in img_b64:
                                img_b64_data = img_b64.split(",")[1]
                            else:
                                img_b64_data = img_b64
                            img_bytes = base64.b64decode(img_b64_data)

                            result = await send_image(
                                session, headers, to_user, ctx_token, img_bytes
                            )
                            logger.info(
                                "[←] sent image (%d/%d) ret=%s",
                                img_idx + 1,
                                len(reply_images),
                                result.get("ret"),
                            )
                        except Exception as e:
                            logger.warning("发送图片 %d 失败: %s", img_idx + 1, e)

            except asyncio.CancelledError:
                break
            except Exception as e:
                logger.error("Loop error: %s", e)
                await asyncio.sleep(5)

    logger.info("WeChat ClawBot 已停止")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("用户中断")
