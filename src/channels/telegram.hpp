#pragma once
#include "channels/common.hpp"

namespace bot
{
    // ════════════════════════════════════════════════════════════════
    // Telegram Bot
    // ════════════════════════════════════════════════════════════════
    namespace tg
    {
        inline std::string api_url(const std::string &token, const std::string &method)
        {
            return "https://api.telegram.org/bot" + token + "/" + method;
        }

        inline json api_call(const std::string &token, const std::string &method, const json &body,
                             long timeout, const std::string &proxy)
        {
            HttpResp resp = http_post(api_url(token, method), body.dump(), timeout,
                                      {{"Content-Type", "application/json"}}, proxy);
            if (resp.status != 200)
                throw std::runtime_error("tg " + method + " HTTP " + std::to_string(resp.status));
            json data = json::parse(resp.body);
            if (!data.value("ok", false))
                throw std::runtime_error("tg " + method + " failed: " + data.value("description", ""));
            return data;
        }

        inline void send_message(const std::string &token, long chat_id, const std::string &text,
                                 const std::string &proxy)
        {
            json body = {{"chat_id", chat_id}, {"text", text}};
            api_call(token, "sendMessage", body, 30, proxy);
        }

        inline void send_chat_action(const std::string &token, long chat_id, const std::string &action,
                                     const std::string &proxy)
        {
            json body = {{"chat_id", chat_id}, {"action", action}};
            try
            {
                api_call(token, "sendChatAction", body, 15, proxy);
            }
            catch (...)
            {
            }
        }

        inline std::string download_file(const std::string &token, const std::string &file_id,
                                         const std::string &proxy)
        {
            json data = api_call(token, "getFile", {{"file_id", file_id}}, 30, proxy);
            std::string path = data.value("result", json()).value("file_path", "");
            if (path.empty())
                return "";
            std::string url = "https://api.telegram.org/file/bot" + token + "/" + path;
            return http_get(url, 60, {}, proxy).body;
        }

        inline void send_photo(const std::string &token, long chat_id, const std::string &img_bytes,
                               const std::string &proxy)
        {
            CURL *curl = curl_easy_init();
            if (!curl)
                return;
            struct curl_slist *list = nullptr;
            list = curl_slist_append(list, "Content-Type: multipart/form-data");
            curl_mime *mime = curl_mime_init(curl);
            {
                curl_mimepart *part = curl_mime_addpart(mime);
                curl_mime_name(part, "chat_id");
                curl_mime_data(part, std::to_string(chat_id).c_str(), CURL_ZERO_TERMINATED);
            }
            {
                curl_mimepart *part = curl_mime_addpart(mime);
                curl_mime_name(part, "photo");
                curl_mime_data(part, img_bytes.data(), img_bytes.size());
                curl_mime_filename(part, "image.jpg");
                curl_mime_type(part, "image/jpeg");
            }
            std::string body;
            curl_easy_setopt(curl, CURLOPT_URL, api_url(token, "sendPhoto").c_str());
            curl_easy_setopt(curl, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1");
            curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_impl::write_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
            if (!proxy.empty())
            {
                curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
                curl_easy_setopt(curl, CURLOPT_HTTPPROXYTUNNEL, 1L);
            }
            curl_easy_perform(curl);
            curl_mime_free(mime);
            curl_slist_free_all(list);
            curl_easy_cleanup(curl);
        }
    } // namespace tg

    inline void tg_worker(json cfg, const std::string &name)
    {
        std::string token = load_channel_token(name);
        if (token.empty())
        {
            update_status(name, false, "no_token", "Bot token 未配置（请在频道管理器填写 Token）");
            return;
        }
        std::string proxy = cfg.value("proxy", "");
        bool think = cfg.value("think", false);
        std::string model = cfg.value("model", "default");
        std::string channel_name = cfg.value("channel", "Telegram");

        update_status(name, true, "running", "长轮询中");
        std::cout << "[" << name << "] Telegram bot started (proxy=" << (proxy.empty() ? "none" : proxy) << ")" << std::endl;

        long offset = 0;
        while (!stop_requested(name))
        {
            try
            {
                json body = {{"offset", offset}, {"timeout", 30}};
                HttpResp resp = http_post(tg::api_url(token, "getUpdates"), body.dump(), 60,
                                          {{"Content-Type", "application/json"}}, proxy);
                if (resp.status != 200)
                {
                    update_status(name, true, "running", "getUpdates HTTP " + std::to_string(resp.status));
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                    continue;
                }
                json data = json::parse(resp.body);
                if (!data.value("ok", false))
                {
                    // 可能 token 失效
                    update_status(name, false, "error", "Telegram API: " + data.value("description", "unknown"));
                    std::this_thread::sleep_for(std::chrono::seconds(10));
                    continue;
                }
                for (auto &upd : data.value("result", json::array()))
                {
                    long uid = upd.value("update_id", 0L);
                    if (uid)
                        offset = uid + 1;
                    if (!upd.contains("message"))
                        continue;
                    const json &msg = upd["message"];
                    long chat_id = msg.value("chat", json()).value("id", 0L);
                    if (!chat_id)
                        continue;
                    std::string text = msg.value("text", "");
                    std::string caption = msg.value("caption", "");

                    if (text == "/start")
                    {
                        try
                        {
                            tg::send_message(token, chat_id, "Welcome to my bot!", proxy);
                        }
                        catch (const std::exception &e)
                        {
                            std::cerr << "[" << name << "] /start failed: " << e.what() << std::endl;
                        }
                        continue;
                    }
                    if (!text.empty() && text[0] == '/')
                        continue; // 其他命令忽略

                    tg::send_chat_action(token, chat_id, "typing", proxy);

                    json req = {{"model", model}, {"think", think}, {"channel", channel_name}};
                    if (!text.empty())
                    {
                        req["messages"] = text;
                    }
                    else if (msg.contains("photo") && msg["photo"].is_array() && !msg["photo"].empty())
                    {
                        std::string file_id = msg["photo"].back().value("file_id", "");
                        std::string img_bin = tg::download_file(token, file_id, proxy);
                        if (img_bin.empty())
                        {
                            std::cerr << "[" << name << "] download photo failed" << std::endl;
                            continue;
                        }
                        req["messages"] = caption.empty() ? "[IMAGE]" : caption;
                        req["images"] = json::array({std::string("data:image/jpeg;base64,") + base64::to_base64(img_bin)});
                    }
                    else
                    {
                        try
                        {
                            tg::send_message(token, chat_id, "Message type is empty or not supported.", proxy);
                        }
                        catch (...)
                        {
                        }
                        continue;
                    }

                    std::cout << "[" << name << "] ← " << std::to_string(chat_id) << ": " << text.substr(0, 80) << std::endl;

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
                            tg::send_message(token, chat_id, "连接丢失了嘤嘤嘤~", proxy);
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
                            for (auto &part : split_markdown_text(t, 4096))
                                try
                                {
                                    tg::send_message(token, chat_id, part, proxy);
                                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                }
                                catch (const std::exception &e)
                                {
                                    std::cerr << "[" << name << "] send thinking failed: " << e.what() << std::endl;
                                }
                    }

                    if (!reply_text.empty())
                    {
                        auto parts = split_markdown_text(reply_text, 4096);
                        for (size_t i = 0; i < parts.size(); ++i)
                        {
                            try
                            {
                                tg::send_message(token, chat_id, parts[i], proxy);
                            }
                            catch (const std::exception &e)
                            {
                                std::cerr << "[" << name << "] send text failed: " << e.what() << std::endl;
                            }
                            if (i < parts.size() - 1)
                                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }
                    }

                    for (auto &img_b64 : extract_images(messages))
                    {
                        try
                        {
                            std::string data_uri = img_b64;
                            size_t comma = data_uri.find(',');
                            if (comma != std::string::npos)
                                data_uri = data_uri.substr(comma + 1);
                            std::string img_bytes = base64::from_base64(data_uri);
                            tg::send_photo(token, chat_id, img_bytes, proxy);
                        }
                        catch (const std::exception &e)
                        {
                            std::cerr << "[" << name << "] send image failed: " << e.what() << std::endl;
                            try
                            {
                                tg::send_message(token, chat_id, "⚠️ 无法解析或发送后端返回的图像。", proxy);
                            }
                            catch (...)
                            {
                            }
                        }
                    }
                }
            }
            catch (const std::exception &e)
            {
                update_status(name, true, "running", std::string("轮询错误: ") + e.what());
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
        }
    }
} // namespace bot
