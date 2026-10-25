// ============================================================================
//  channels/qq.hpp —— QQ 频道（OneBot v11 协议）
//
//  适配 NapCat / Lagrange / go-cqhttp 等所有实现了 OneBot v11 的端。
//
//  数据流（反向 HTTP 上报，无需 WebSocket）：
//
//      QQ 客户端 ──► OneBot 实现 ──POST──► http://<host>:<port>/api/channel/qq/event
//                                              │
//                                       事件队列（按频道名分桶）
//                                              │
//                                         qq_worker ──► agent::channel_chat
//                                              │
//                        ◄──POST {api_base}/send_msg──  回复用户/群
//
//  settings.json → channels[] 里 type 为 "qq" 的条目配置：
//      api_base       OneBot 的 HTTP API 地址，默认 http://127.0.0.1:3000
//      access_token   OneBot 配置里的 access token（也可用 /api/channel/token 加密存）
//      webhook_path   反向上报路径，默认 /api/channel/qq/event
//      self_id        Bot 自己的 QQ 号；群聊里用于判断是否被 @
//      group_at_only  群聊仅在 @Bot 时回复，默认 true
// ============================================================================
#pragma once
#ifndef AGENTCPP_CHANNELS_QQ_HPP
#define AGENTCPP_CHANNELS_QQ_HPP

#include "channels/common.hpp"
#include "agent/runtime.hpp"

namespace bot
{
    namespace qq
    {
        // ---------------- 事件队列 ----------------
        inline std::mutex &queue_mutex()
        {
            static std::mutex m;
            return m;
        }
        inline std::condition_variable &queue_cv()
        {
            static std::condition_variable cv;
            return cv;
        }
        inline std::map<std::string, std::deque<json>> &queues()
        {
            static std::map<std::string, std::deque<json>> q;
            return q;
        }

        // 把一条 OneBot 事件塞进对应频道名的队列（由 HTTP 路由调用）
        inline void push_event(const std::string &name, const json &ev)
        {
            {
                std::lock_guard<std::mutex> lk(queue_mutex());
                auto &q = queues()[name];
                q.push_back(ev);
                // 防止上游刷屏把内存打满
                while (q.size() > 256)
                    q.pop_front();
            }
            queue_cv().notify_all();
        }

        // ---------------- HTTP API 调用 ----------------
        inline json api_call(const json &cfg, const std::string &action, const json &body,
                             const std::string &token, const std::string &proxy)
        {
            std::string base = cfg.value("api_base", "http://127.0.0.1:3000");
            while (!base.empty() && base.back() == '/')
                base.pop_back();
            std::map<std::string, std::string> hdrs{{"Content-Type", "application/json"}};
            if (!token.empty())
                hdrs["Authorization"] = "Bearer " + token;

            HttpResp resp = http_post(base + "/" + action, body.dump(), 30, hdrs, proxy);
            if (resp.status != 200)
                throw std::runtime_error("qq " + action + " HTTP " + std::to_string(resp.status));
            json data = json::parse(resp.body);
            if (data.value("retcode", 0) != 0)
                throw std::runtime_error("qq " + action + " failed: " + data.dump());
            return data;
        }

        // ---------------- 发送 ----------------
        inline void send_segments(const json &cfg, const std::string &token, const std::string &proxy,
                                  const std::string &message_type, const std::string &target,
                                  const json &segments)
        {
            json body = {{"message_type", message_type}, {"message", segments}};
            if (message_type == "group")
                body["group_id"] = target;
            else
                body["user_id"] = target;
            api_call(cfg, "send_msg", body, token, proxy);
        }

        inline void send_text(const json &cfg, const std::string &token, const std::string &proxy,
                              const std::string &message_type, const std::string &target,
                              const std::string &text)
        {
            if (text.empty())
                return;
            json segments = json::array({{{"type", "text"}, {"data", {{"text", text}}}}});
            send_segments(cfg, token, proxy, message_type, target, segments);
        }

        // ---------------- 事件解析 ----------------
        struct Incoming
        {
            bool valid = false;
            std::string message_type; // private / group
            std::string target;       // user_id 或 group_id
            std::string sender_name;
            std::string text;
            std::vector<std::string> images; // 原始 URL
        };

        inline std::string segment_text(const json &seg)
        {
            std::string type = seg.value("type", "");
            const json &data = seg.value("data", json::object());
            if (type == "text")
                return data.value("text", "");
            if (type == "image")
                return "";
            if (type == "face")
                return "[face]";
            return "";
        }

        inline Incoming parse_event(const json &ev, const std::string &self_id, bool group_at_only)
        {
            Incoming in;
            if (ev.value("post_type", "") != "message")
                return in;
            in.message_type = ev.value("message_type", "");
            if (in.message_type != "private" && in.message_type != "group")
                return in;

            // 自己发的消息不处理，避免回环
            if (!self_id.empty() && std::to_string(ev.value("user_id", 0LL)) == self_id &&
                ev.contains("target_id"))
                return in;

            if (in.message_type == "group")
            {
                if (!ev.contains("group_id"))
                    return in;
                in.target = std::to_string(ev.value("group_id", 0LL));
            }
            else
            {
                if (!ev.contains("user_id"))
                    return in;
                in.target = std::to_string(ev.value("user_id", 0LL));
            }
            if (ev.contains("sender") && ev["sender"].is_object())
                in.sender_name = ev["sender"].value("card", ev["sender"].value("nickname", ""));

            bool at_me = false;
            std::string text;
            if (ev.contains("message") && ev["message"].is_array())
            {
                for (auto &seg : ev["message"])
                {
                    std::string type = seg.value("type", "");
                    if (type == "at")
                    {
                        std::string qq = seg.value("data", json::object()).value("qq", "");
                        if (!self_id.empty() && qq == self_id)
                        {
                            at_me = true;
                            continue;
                        }
                        text += "@" + qq + " ";
                        continue;
                    }
                    if (type == "image")
                    {
                        std::string url = seg.value("data", json::object()).value("url", "");
                        if (url.empty())
                            url = seg.value("data", json::object()).value("file", "");
                        if (!url.empty())
                            in.images.push_back(url);
                        continue;
                    }
                    text += segment_text(seg);
                }
            }
            else
            {
                text = ev.value("raw_message", "");
            }

            // 群聊：默认只在被 @ 时回应
            if (in.message_type == "group" && group_at_only && !at_me)
                return in;

            in.text = text;
            in.valid = !(in.text.empty() && in.images.empty());
            return in;
        }

        // ---------------- worker ----------------
        inline void worker(json cfg, const std::string &name)
        {
            std::string token = cfg.value("access_token", "");
            if (token.empty())
                token = load_channel_token(name);
            const std::string proxy = cfg.value("proxy", "");
            const std::string model = cfg.value("model", "default");
            const bool think = cfg.value("think", false);
            const bool at_only = cfg.value("group_at_only", true);
            const std::string self_id = cfg.value("self_id", "");
            const std::string channel_name = cfg.value("channel", name);
            const std::string api_base = cfg.value("api_base", "http://127.0.0.1:3000");

            update_status(name, true, "running",
                          "OneBot: " + api_base + "（等待事件上报）");
            std::cout << "[" << name << "] QQ worker started, api=" << api_base << std::endl;

            while (!stop_requested(name))
            {
                json ev;
                {
                    std::unique_lock<std::mutex> lk(queue_mutex());
                    queue_cv().wait_for(lk, std::chrono::seconds(3),
                                        [&]()
                                        { return !queues()[name].empty(); });
                    auto &q = queues()[name];
                    if (q.empty())
                        continue;
                    ev = std::move(q.front());
                    q.pop_front();
                }

                try
                {
                    // 心跳事件：只刷新状态
                    if (ev.value("post_type", "") == "meta_event")
                    {
                        if (ev.value("meta_event_type", "") == "lifecycle")
                            update_status(name, true, "running", "OneBot 已连接: " + ev.value("self_id", ""));
                        continue;
                    }

                    Incoming in = parse_event(ev, self_id, at_only);
                    if (!in.valid)
                        continue;

                    std::cout << "[" << name << "] ← " << in.message_type << ":" << in.target
                              << " " << in.text.substr(0, 80) << std::endl;

                    // 图片转 data URI
                    json req_images = json::array();
                    for (auto &url : in.images)
                    {
                        std::string bytes;
                        if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0)
                            bytes = http_get(url, 30, {}, proxy).body;
                        else if (url.rfind("base64://", 0) == 0)
                            bytes = base64::from_base64(url.substr(9));
                        if (!bytes.empty())
                            req_images.push_back(std::string("data:image/jpeg;base64,") +
                                                 base64::to_base64(bytes));
                    }

                    json req = {
                        {"model", model},
                        {"think", think},
                        {"channel", channel_name},
                        {"messages", in.text.empty() ? "[IMAGE]" : in.text}};
                    if (!req_images.empty())
                        req["images"] = req_images;

                    json resp = agent::channel_chat(req);
                    std::string reply = resp.value("content", "");
                    if (reply.empty())
                        reply = extract_assistant_reply(resp.value("messages", json::array()));

                    // 思考过程单独发一段，方便用户理解长回复
                    json thinkings = resp.value("thinking", json::array());
                    if (thinkings.is_array() && !thinkings.empty())
                    {
                        std::string t;
                        for (auto &tk : thinkings)
                            if (tk.is_string() && !tk.get<std::string>().empty())
                                t += (t.empty() ? "" : "\n\n") + std::string("💭 ") + tk.get<std::string>();
                        if (!t.empty())
                            send_text(cfg, token, proxy, in.message_type, in.target, t);
                    }

                    if (!reply.empty())
                        send_text(cfg, token, proxy, in.message_type, in.target, reply);

                    for (auto &img_b64 : extract_images(resp.value("messages", json::array())))
                    {
                        std::string data_uri = img_b64;
                        size_t comma = data_uri.find(',');
                        if (comma != std::string::npos)
                            data_uri = data_uri.substr(comma + 1);
                        json segs = json::array(
                            {{{"type", "image"},
                              {"data", {{"file", "base64://" + data_uri}}}}});
                        send_segments(cfg, token, proxy, in.message_type, in.target, segs);
                    }

                    update_status(name, true, "running", "最近一条消息已处理");
                }
                catch (const std::exception &e)
                {
                    std::cerr << "[" << name << "] qq error: " << e.what() << std::endl;
                    update_status(name, true, "running", std::string("处理失败: ") + e.what(), e.what());
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }
        }
    } // namespace qq
} // namespace bot

#endif // AGENTCPP_CHANNELS_QQ_HPP
