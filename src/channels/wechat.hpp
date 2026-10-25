#pragma once
#include "channels/common.hpp"
#include "channels/codec.hpp"
#include "channels/qr.hpp"

namespace bot
{
    // ════════════════════════════════════════════════════════════════
    // WeChat ClawBot（iLink）
    // ════════════════════════════════════════════════════════════════
    namespace wx
    {
        inline const std::string CDN_HOST = "novac2c.cdn.weixin.qq.com";

        inline std::map<std::string, std::string> make_headers(const std::string &token)
        {
            // X-WECHAT-UIN: base64(pack("<I", rand32))
            uint32_t r = (uint32_t)std::random_device{}() ^ (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count();
            std::string raw;
            raw.push_back((char)(r & 0xFF));
            raw.push_back((char)((r >> 8) & 0xFF));
            raw.push_back((char)((r >> 16) & 0xFF));
            raw.push_back((char)((r >> 24) & 0xFF));
            return {{"Content-Type", "application/json"},
                    {"AuthorizationType", "ilink_bot_token"},
                    {"X-WECHAT-UIN", base64::to_base64(raw)},
                    {"Authorization", "Bearer " + token}};
        }

        inline std::string make_client_id()
        {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
            return "claw_" + std::to_string(now) + "_" + rand_hex(6);
        }

        inline json ilink_call(const std::string &ilink_base, const std::string &path, const json &body,
                               const std::map<std::string, std::string> &hdrs, long timeout)
        {
            HttpResp resp = http_post(ilink_base + path, body.dump(), timeout, hdrs);
            if (resp.body.empty())
                return {{"ret", -1}, {"msg", "empty response"}};
            try
            {
                return json::parse(resp.body);
            }
            catch (...)
            {
                return {{"ret", -2}, {"msg", "non-json response"}, {"raw", resp.body.substr(0, 1000)}};
            }
        }

        inline json ilink_get(const std::string &ilink_base, const std::string &path,
                              const std::map<std::string, std::string> &hdrs = {}, long timeout = 15)
        {
            HttpResp resp = http_get(ilink_base + path, timeout, hdrs);
            if (resp.body.empty())
                return {{"ret", -1}, {"msg", "empty response"}};
            try
            {
                return json::parse(resp.body);
            }
            catch (...)
            {
                return {{"ret", -2}, {"msg", "non-json response"}};
            }
        }

        // 解析微信消息 item_list → (text, images)
        // images: [("encrypted", encrypt_query, aes_key_b64), ("url", url), ("data", b64)]
        inline std::pair<std::string, std::vector<std::array<std::string, 3>>> parse_items(const json &item_list)
        {
            std::string text;
            std::vector<std::array<std::string, 3>> images;
            for (auto &item : item_list)
            {
                int type = item.value("type", 0);
                switch (type)
                {
                case 1: // TEXT
                {
                    std::string t = item.value("text_item", json()).value("text", "");
                    if (!t.empty())
                        text += t;
                    break;
                }
                case 2: // IMAGE
                {
                    json img_item = item.value("image_item", json());
                    if (img_item.is_null())
                        img_item = item.value("img_item", json());
                    json media = img_item.value("media", json());
                    if (media.is_null())
                        media = img_item.value("full", json());
                    std::string eq = media.value("encrypt_query_param", "");
                    std::string ak = media.value("aes_key", "");
                    if (eq.empty() || ak.empty())
                    {
                        json thumb = img_item.value("thumb_media", json());
                        if (thumb.is_null())
                            thumb = img_item.value("thumb", json());
                        if (eq.empty())
                            eq = thumb.value("encrypt_query_param", "");
                        if (ak.empty())
                            ak = thumb.value("aes_key", "");
                    }
                    if (!eq.empty() && !ak.empty())
                    {
                        images.push_back({"encrypted", eq, ak});
                        break;
                    }
                    std::string url = img_item.value("url", img_item.value("cdn_url", img_item.value("image_url", "")));
                    std::string data = img_item.value("data", img_item.value("content", ""));
                    if (!url.empty())
                        images.push_back({"url", url, ""});
                    else if (!data.empty())
                        images.push_back({"data", data, ""});
                    break;
                }
                case 3: // VOICE
                {
                    json v = item.value("voice_item", json());
                    std::string dur = v.value("duration", v.value("play_length", v.value("len", "")));
                    text += "[📞 Voice Message" + (dur.empty() ? "" : " " + dur + "s") + "]";
                    break;
                }
                case 4: // FILE
                {
                    json f = item.value("file_item", json());
                    std::string fn = f.value("file_name", f.value("filename", f.value("title", "未知文件")));
                    std::string sz = f.value("file_size", f.value("size", f.value("file_len", "")));
                    std::string size_str;
                    try
                    {
                        long long b = std::stoll(sz);
                        if (b >= 1048576)
                            size_str = " (" + std::to_string(b / 1048576.0).substr(0, 4) + " MB)";
                        else if (b >= 1024)
                            size_str = " (" + std::to_string(b / 1024.0).substr(0, 4) + " KB)";
                        else
                            size_str = " (" + std::to_string(b) + " B)";
                    }
                    catch (...)
                    {
                    }
                    text += "[📎 File: " + fn + size_str + "]";
                    break;
                }
                case 5: // VIDEO
                {
                    json v = item.value("video_item", json());
                    std::string dur = v.value("duration", v.value("play_length", ""));
                    text += "[🎬 Video Message" + (dur.empty() ? "" : " " + dur + "s") + "]";
                    break;
                }
                default:
                    text += "[📩 Unknown message type=" + std::to_string(type) + "]";
                    break;
                }
            }
            // strip
            size_t b = text.find_first_not_of(" \t\r\n");
            size_t e = text.find_last_not_of(" \t\r\n");
            if (b == std::string::npos)
                text.clear();
            else
                text = text.substr(b, e - b + 1);
            return {text, images};
        }

        // CDN 下载 + AES-ECB 解密
        inline std::string cdn_download(const std::string &encrypt_query, const std::string &aes_key_b64)
        {
            std::string url = "https://" + CDN_HOST + "/c2c/download?encrypted_query_param=" + url_encode(encrypt_query);
            HttpResp resp = http_get(url, 30, {{"User-Agent", "MicroMessenger/8.0"}});
            if (resp.status != 200 || resp.body.size() < 32)
                return "";
            try
            {
                std::string key = wx_cdn_key_bytes(aes_key_b64);
                std::string plain = aes_ecb_decrypt_pkcs7(key, resp.body);
                return plain;
            }
            catch (...)
            {
                // 未加密的图片直接返回
                if ((resp.body.size() >= 2 && (unsigned char)resp.body[0] == 0xFF && (unsigned char)resp.body[1] == 0xD8) ||
                    (resp.body.size() >= 4 && resp.body.substr(0, 4) == "\x89PNG"))
                    return resp.body;
                return "";
            }
        }

        // CDN 上传：POST 优先，失败回退 PUT，捕获 x-encrypted-param
        inline std::string cdn_upload(const std::string &upload_url, const std::string &data)
        {
            for (auto method : {"POST", "PUT"})
            {
                HttpResp resp = http_request(method, upload_url, data, 70,
                                             {{"Content-Type", "application/octet-stream"}});
                if (resp.status == 200 || resp.status == 201)
                {
                    auto it = resp.headers.find("x-encrypted-param");
                    if (it != resp.headers.end() && !it->second.empty())
                        return it->second;
                    try
                    {
                        json body = json::parse(resp.body);
                        std::string ep = body.value("encrypt_query_param", body.value("encrypted_param", ""));
                        if (!ep.empty())
                            return ep;
                    }
                    catch (...)
                    {
                    }
                    // 最后兜底：URL query
                    size_t q = upload_url.find('?');
                    if (q != std::string::npos && q + 1 < upload_url.size())
                        return upload_url.substr(q + 1);
                    return "";
                }
            }
            return "";
        }

        inline json send_text(const std::string &ilink_base, const std::map<std::string, std::string> &hdrs,
                              const std::string &to_user, const std::string &ctx_token, const std::string &text,
                              long timeout = 30)
        {
            // 注意：json::array({{"type",1},...}) 会把元素推断成数组 ["type",1]，
            // 必须先用显式对象构造再放入数组，否则服务器返回 "invalid request"
            json item = {{"type", 1}, {"text_item", {{"text", text}}}};
            json payload = {
                {"msg", {{"to_user_id", to_user}, {"message_type", 2}, {"message_state", 2}, {"context_token", ctx_token}, {"client_id", make_client_id()}, {"item_list", json::array({item})}}}};
            json resp = ilink_call(ilink_base, "/ilink/bot/sendmessage", payload, hdrs, timeout);
            // context_token 可能已过期/一次性使用：带 token 失败时去掉后重试一次，
            // 否则回复会静默丢失（ret != 0 且无错误打印）
            if (resp.value("ret", 0) != 0 && !ctx_token.empty())
            {
                json retry = payload;
                retry["msg"].erase("context_token");
                resp = ilink_call(ilink_base, "/ilink/bot/sendmessage", retry, hdrs, timeout);
            }
            if (resp.value("ret", 0) != 0)
                std::cerr << "[wx] sendmessage failed: " << resp.dump().substr(0, 300) << std::endl;
            return resp;
        }

        inline json send_image(const std::string &ilink_base, const std::map<std::string, std::string> &hdrs,
                               const std::string &to_user, const std::string &ctx_token,
                               const std::string &img_bytes, const std::string &caption = "")
        {
            try
            {
                std::mt19937 rng(std::random_device{}());
                auto rand_key16 = [&rng]()
                {
                    std::string k;
                    k.reserve(16);
                    for (int i = 0; i < 16; ++i)
                        k += (char)(rng() % 256);
                    return k;
                };
                auto key_hex_b64 = [](const std::string &k)
                {
                    std::string hex;
                    hex.reserve(32);
                    static const char *hx = "0123456789abcdef";
                    for (unsigned char c : k)
                    {
                        hex += hx[c >> 4];
                        hex += hx[c & 0x0F];
                    }
                    return base64::to_base64(hex);
                };

                std::string key16 = rand_key16();
                std::string aes_key_b64 = key_hex_b64(key16);

                std::string ciphertext = aes_ecb_encrypt_pkcs7(key16, img_bytes);

                // 缩略图：无图像库，取前 2KB（与 Python 无 PIL 回退一致）
                std::string thumb = img_bytes.size() > 2048 ? img_bytes.substr(0, 2048) : img_bytes;
                std::string thumb_key16 = rand_key16();
                std::string thumb_aes_key_b64 = key_hex_b64(thumb_key16);
                std::string thumb_ciphertext = aes_ecb_encrypt_pkcs7(thumb_key16, thumb);

                // getuploadurl
                std::string filekey = "img_" + rand_hex(4);
                json up_req = {{"to_user_id", to_user},
                               {"filekey", filekey},
                               {"media_type", 1},
                               {"rawsize", (long)img_bytes.size()},
                               {"rawfilemd5", md5_hex(img_bytes)},
                               {"filesize", (long)ciphertext.size()},
                               {"thumb_rawsize", (long)thumb.size()},
                               {"thumb_rawfilemd5", md5_hex(thumb)},
                               {"thumb_filesize", (long)thumb_ciphertext.size()}};
                json up_data;
                try
                {
                    up_data = ilink_call(ilink_base, "/ilink/bot/getuploadurl", up_req, hdrs, 15);
                }
                catch (...)
                {
                    return {{"ret", -1}};
                }
                std::string upload_full_url = up_data.value("upload_full_url", up_data.value("upload_url", ""));
                std::string thumb_upload_url = up_data.value("thumb_upload_full_url", up_data.value("thumb_upload_url", ""));
                if (upload_full_url.empty())
                    return {{"ret", -1}};

                std::string encrypt_query_param = cdn_upload(upload_full_url, ciphertext);
                if (encrypt_query_param.empty())
                    return {{"ret", -1}};
                std::string thumb_encrypt_param = encrypt_query_param;
                if (!thumb_upload_url.empty() && thumb_upload_url != upload_full_url)
                {
                    std::string p = cdn_upload(thumb_upload_url, thumb_ciphertext);
                    if (!p.empty())
                        thumb_encrypt_param = p;
                }

                json image_item = {
                    {"type", 2},
                    {"image_item",
                     {{"media", {{"encrypt_query_param", encrypt_query_param}, {"aes_key", aes_key_b64}}},
                      {"thumb_media", {{"encrypt_query_param", thumb_encrypt_param}, {"aes_key", thumb_aes_key_b64}}}}}};
                json item_list = json::array({image_item});
                if (!caption.empty())
                    item_list.push_back({{"type", 1}, {"text_item", {{"text", caption}}}});

                json payload = {
                    {"msg", {{"to_user_id", to_user}, {"message_type", 2}, {"message_state", 2}, {"context_token", ctx_token}, {"client_id", make_client_id()}, {"item_list", item_list}}}};
                json resp = ilink_call(ilink_base, "/ilink/bot/sendmessage", payload, hdrs, 30);
                if (resp.value("ret", 0) != 0 && !ctx_token.empty())
                {
                    json retry = payload;
                    retry["msg"].erase("context_token");
                    resp = ilink_call(ilink_base, "/ilink/bot/sendmessage", retry, hdrs, 30);
                }
                if (resp.value("ret", 0) != 0)
                    std::cerr << "[wx] sendimage failed: " << resp.dump().substr(0, 300) << std::endl;
                return resp;
            }
            catch (...)
            {
                return {{"ret", -1}};
            }
        }

        inline void send_paragraphs(const std::string &ilink_base, const std::map<std::string, std::string> &hdrs,
                                    const std::string &to_user, const std::string &ctx_token,
                                    const std::string &text, size_t max_len = 2048, int delay_ms = 200)
        {
            auto parts = split_markdown_text(text, max_len);
            for (size_t i = 0; i < parts.size(); ++i)
            {
                try
                {
                    send_text(ilink_base, hdrs, to_user, ctx_token, parts[i]);
                }
                catch (const std::exception &e)
                {
                    std::cerr << "[wx] send_text failed: " << e.what() << std::endl;
                }
                if (i < parts.size() - 1)
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }
        }

        // 扫码登录：生成二维码 SVG → 存状态 → 轮询确认 → 返回 token
        inline std::string login_flow(const std::string &ilink_base, const std::string &channel_name)
        {
            json data = ilink_get(ilink_base, "/ilink/bot/get_bot_qrcode?bot_type=3");
            if (data.value("ret", -1) != 0)
            {
                update_status(channel_name, false, "error", "获取二维码失败: " + data.dump());
                return "";
            }
            std::string qrcode_token = data.value("qrcode", "");
            std::string qr_url = data.value("qrcode_img_content", "");
            if (qrcode_token.empty() || qr_url.empty())
            {
                update_status(channel_name, false, "error", "二维码响应缺少 qrcode 字段");
                return "";
            }
            std::cout << "[" << channel_name << "] 请扫码登录: " << qr_url << std::endl;

            // 生成 SVG 并 base64，存入状态供 webui 展示
            {
                std::lock_guard<std::mutex> lk(status_mutex());
                auto &st = status_map()[channel_name];
                st.name = channel_name;
                st.running = true;
                st.state = "waiting_qr";
                st.detail = "等待扫码...";
                st.qr_url = qr_url;
                try
                {
                    st.qr_svg_b64 = base64::to_base64(qr_svg(qr_url));
                }
                catch (...)
                {
                    st.qr_svg_b64.clear();
                }
            }

            std::string last_state;
            for (int i = 0; i < 60; ++i)
            {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                json status;
                try
                {
                    status = ilink_get(ilink_base, "/ilink/bot/get_qrcode_status?qrcode=" + url_encode(qrcode_token));
                }
                catch (...)
                {
                    continue;
                }
                std::string state = status.value("status", "");
                if (!state.empty() && state != last_state)
                {
                    std::cout << "[" << channel_name << "] QR status → " << state << std::endl;
                    last_state = state;
                }
                if (state == "scanned")
                {
                    update_status(channel_name, true, "waiting_qr", "已扫码，等待确认...");
                }
                else if (state == "confirmed" || state == "success")
                {
                    std::string token = status.value("bot_token", status.value("token", ""));
                    if (!token.empty())
                    {
                        update_status(channel_name, true, "running", "扫码成功！");
                        return token;
                    }
                }
                else if (state == "expired")
                {
                    update_status(channel_name, false, "error", "二维码已过期");
                    return "";
                }
            }
            update_status(channel_name, false, "error", "等待扫码超时（120 秒）");
            return "";
        }
    } // namespace wx

    inline void wx_worker(json cfg, const std::string &name)
    {
        std::string ilink_base = cfg.value("ilink_base", "https://ilinkai.weixin.qq.com");
        bool think = cfg.value("think", false);
        std::string model = cfg.value("model", "default");
        std::string channel_name = cfg.value("channel", "WeChat");

        std::string token = load_channel_token(name);
        if (token.empty())
        {
            std::cout << "[" << name << "] BOT_TOKEN 为空，触发扫码登录..." << std::endl;
            update_status(name, true, "waiting_qr", "获取登录二维码...");
            token = wx::login_flow(ilink_base, name);
            if (token.empty())
                return;
            if (!save_channel_token(name, token))
                std::cerr << "[" << name << "] 保存 token 失败" << std::endl;
        }

        update_status(name, true, "running", "轮询中");
        std::cout << "[" << name << "] WeChat ClawBot started, token: " << token.substr(0, 20) << "..." << std::endl;

        auto hdrs = wx::make_headers(token);
        std::string cursor;
        while (!stop_requested(name))
        {
            try
            {
                json updates;
                try
                {
                    updates = wx::ilink_call(ilink_base, "/ilink/bot/getupdates",
                                             {{"get_updates_buf", cursor}}, hdrs, 60);
                }
                catch (const std::exception &e)
                {
                    update_status(name, true, "running", std::string("getupdates 失败: ") + e.what());
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    continue;
                }
                // ilink_call 失败不抛异常（返回 ret!=0）：避免断网时空转打请求
                if (updates.value("ret", 0) != 0)
                {
                    std::string msg = updates.value("msg", updates.value("errmsg", ""));
                    update_status(name, true, "running", "getupdates 失败: " + msg);
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    continue;
                }

                std::string new_cursor = updates.value("get_updates_buf", "");
                if (!new_cursor.empty() && new_cursor != cursor)
                    cursor = new_cursor;

                for (auto &raw : updates.value("msgs", json::array()))
                {
                    json msg = raw.contains("msg") ? raw["msg"] : raw;
                    std::string to_user = msg.value("from_user_id", raw.value("from_user_id", ""));
                    std::string ctx_token = msg.value("context_token", raw.value("context_token", ""));
                    json item_list = msg.value("item_list", raw.value("item_list", json::array()));

                    auto [user_text, wx_images] = wx::parse_items(item_list);
                    if (to_user.empty() || (user_text.empty() && wx_images.empty()))
                        continue;

                    std::cout << "[" << name << "] → " << to_user.substr(0, 20) << ": "
                              << user_text.substr(0, 80)
                              << (wx_images.empty() ? "" : " +" + std::to_string(wx_images.size()) + " image(s)")
                              << std::endl;

                    // 发送"正在输入"
                    try
                    {
                        wx::ilink_call(ilink_base, "/ilink/bot/sendtyping",
                                       {{"to_user_id", to_user}, {"context_token", ctx_token}}, hdrs, 10);
                    }
                    catch (...)
                    {
                    }

                    json req = {{"model", model}, {"think", think}, {"channel", channel_name}};

                    // 处理图片 → data:image/jpeg;base64
                    json req_images = json::array();
                    for (auto &img_item : wx_images)
                    {
                        std::string kind = img_item[0];
                        std::string img_bytes;
                        if (kind == "encrypted")
                            img_bytes = wx::cdn_download(img_item[1], img_item[2]);
                        else if (kind == "url")
                            img_bytes = http_get(img_item[1], 30).body;
                        else if (kind == "data")
                        {
                            std::string d = img_item[1];
                            for (auto &prefix : {"data:image/jpeg;base64,", "data:image/png;base64,", "data:image/webp;base64,"})
                                if (d.rfind(prefix, 0) == 0)
                                {
                                    d = d.substr(strlen(prefix));
                                    break;
                                }
                            img_bytes = base64::from_base64(d);
                        }
                        if (!img_bytes.empty())
                            req_images.push_back(std::string("data:image/jpeg;base64,") + base64::to_base64(img_bytes));
                    }
                    if (!req_images.empty())
                    {
                        req["messages"] = user_text.empty() ? "[IMAGE]" : user_text;
                        req["images"] = req_images;
                    }
                    else
                    {
                        req["messages"] = user_text;
                    }

                    json data_resp;
                    try
                    {
                        data_resp = agent::channel_chat(req);
                    }
                    catch (const std::exception &e)
                    {
                        std::cerr << "[" << name << "] channel_chat error: " << e.what() << std::endl;
                        try
                        {
                            wx::send_text(ilink_base, hdrs, to_user, ctx_token, "连接丢失了嘤嘤嘤~");
                        }
                        catch (...)
                        {
                        }
                        continue;
                    }

                    json messages = data_resp.value("messages", json::array());
                    std::string reply_text = extract_assistant_reply(messages);
                    if (reply_text.empty())
                        reply_text = data_resp.value("content", "");
                    json thinkings = data_resp.value("thinking", json::array());
                    if (!thinkings.is_array())
                        thinkings = data_resp.value("thinkings", json::array());

                    if (thinkings.is_array())
                    {
                        std::string t;
                        for (auto &tk : thinkings)
                            if (tk.is_string() && !tk.get<std::string>().empty())
                            {
                                if (!t.empty())
                                    t += "\n\n";
                                t += "💭 " + tk.get<std::string>();
                            }
                        if (!t.empty())
                            wx::send_paragraphs(ilink_base, hdrs, to_user, ctx_token, t);
                    }

                    if (!reply_text.empty())
                        wx::send_paragraphs(ilink_base, hdrs, to_user, ctx_token, reply_text);

                    for (auto &img_b64 : extract_images(messages))
                    {
                        try
                        {
                            std::string data_uri = img_b64;
                            size_t comma = data_uri.find(',');
                            if (comma != std::string::npos)
                                data_uri = data_uri.substr(comma + 1);
                            std::string img_bytes = base64::from_base64(data_uri);
                            wx::send_image(ilink_base, hdrs, to_user, ctx_token, img_bytes);
                        }
                        catch (const std::exception &e)
                        {
                            std::cerr << "[" << name << "] send_image failed: " << e.what() << std::endl;
                        }
                    }
                }
            }
            catch (const std::exception &e)
            {
                update_status(name, true, "running", std::string("循环错误: ") + e.what());
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
    }
} // namespace bot
