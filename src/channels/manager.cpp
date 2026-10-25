#include "channels/channel.hpp"

#include "channels/common.hpp"
#include "channels/telegram.hpp"
#include "channels/wechat.hpp"
#include "channels/qq.hpp"
#include "core/config.hpp"
#include "core/log.hpp"

namespace channels
{
    namespace
    {
        std::mutex &reg_mutex()
        {
            static std::mutex m;
            return m;
        }
        std::vector<Driver> &registry()
        {
            static std::vector<Driver> r;
            return r;
        }
        std::mutex &run_mutex()
        {
            static std::mutex m;
            return m;
        }
        std::set<std::string> &running_set()
        {
            static std::set<std::string> s;
            return s;
        }

        std::string lower(std::string s)
        {
            for (auto &c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        // 老配置没有 type，只能从 path 里猜（tg_bot.py / wx_bot.py / qq_bot.py）
        std::string infer_type(const json &ch)
        {
            std::string type = ch.value("type", "");
            if (!type.empty())
                return lower(type);
            std::string p = lower(ch.value("path", ""));
            if (p.find("wx_bot") != std::string::npos || p.find("wechat") != std::string::npos)
                return "wechat";
            if (p.find("qq") != std::string::npos)
                return "qq";
            if (p.find("tg_bot") != std::string::npos || p.find("telegram") != std::string::npos)
                return "telegram";
            return "telegram";
        }
    } // namespace

    void register_driver(Driver d)
    {
        std::lock_guard<std::mutex> lk(reg_mutex());
        for (auto &existing : registry())
        {
            if (existing.type == d.type)
            {
                existing = std::move(d);
                return;
            }
        }
        registry().push_back(std::move(d));
    }

    const Driver *find_driver(const std::string &type_or_hint)
    {
        const std::string key = lower(type_or_hint);
        std::lock_guard<std::mutex> lk(reg_mutex());
        for (auto &d : registry())
        {
            if (lower(d.type) == key)
                return &d;
            for (auto &a : d.aliases)
                if (lower(a) == key)
                    return &d;
        }
        // 再按 path 片段猜一次
        for (auto &d : registry())
            if (key.find(lower(d.type)) != std::string::npos)
                return &d;
        return nullptr;
    }

    std::vector<std::string> driver_types()
    {
        std::lock_guard<std::mutex> lk(reg_mutex());
        std::vector<std::string> out;
        for (auto &d : registry())
            out.push_back(d.type);
        return out;
    }

    void register_builtin()
    {
        static std::once_flag once;
        std::call_once(once, []()
                       {
                           register_driver({"telegram", {"tg", "tg_bot", "telegram_bot"}, "Telegram",
                                            [](const json &cfg, const std::string &name)
                                            { bot::tg_worker(cfg, name); },
                                            "需要 BotFather 申请的 bot token"});
                           register_driver({"wechat", {"wx", "wx_bot", "wechat_bot"}, "WeChat",
                                            [](const json &cfg, const std::string &name)
                                            { bot::wx_worker(cfg, name); },
                                            "扫码登录 iLink ClawBot，token 加密存于 tokens/"});
                           register_driver({"qq", {"qq_bot", "onebot"}, "QQ (OneBot v11)",
                                            [](const json &cfg, const std::string &name)
                                            { bot::qq::worker(cfg, name); },
                                            "配置 OneBot 的 api_base，并把事件上报到 webhook_path"});
                       });
    }

    void set_status(const std::string &name, bool running, const std::string &state,
                    const std::string &detail, const std::string &error)
    {
        bot::update_status(name, running, state, detail, error);
    }

    json status_json() { return bot::channel_status_json(); }
    json qr_json(const std::string &name) { return bot::channel_qr_json(name); }
    std::string load_token(const std::string &name) { return bot::load_channel_token(name); }
    bool save_token(const std::string &name, const std::string &token)
    {
        return bot::save_channel_token(name, token);
    }

    bool start(const std::string &name)
    {
        register_builtin();

        {
            std::lock_guard<std::mutex> lk(run_mutex());
            if (running_set().count(name))
            {
                if (bot::stop_requested(name))
                {
                    logx::warn("channel", name + " is still stopping, retry later");
                    return false;
                }
                return true;
            }
        }

        if (!run_unit::settings.contains("channels"))
            return false;

        for (auto &ch : run_unit::settings["channels"])
        {
            if (ch.value("name", "") != name)
                continue;
            if (ch.value("status", "") != "active")
            {
                logx::warn("channel", name + " is not active, skipped");
                return false;
            }

            const std::string type = infer_type(ch);
            const Driver *driver = find_driver(type);
            if (!driver)
            {
                logx::error("channel", "no driver for type '" + type + "' (channel " + name + ")");
                set_status(name, false, "error", "未知频道类型: " + type);
                return false;
            }

            json cfg = ch.contains("config") && ch["config"].is_object() ? ch["config"] : json::object();
            cfg["channel"] = name;

            bot::clear_stop(name);
            {
                std::lock_guard<std::mutex> lk(run_mutex());
                running_set().insert(name);
            }

            Worker worker = driver->run;
            std::thread([worker, cfg, name]()
                        {
                            try
                            {
                                worker(cfg, name);
                            }
                            catch (const std::exception &e)
                            {
                                logx::error("channel", name + ": " + e.what());
                                set_status(name, false, "error", e.what(), e.what());
                            }
                            catch (...)
                            {
                                set_status(name, false, "error", "unknown error");
                            }
                            {
                                std::lock_guard<std::mutex> lk(run_mutex());
                                running_set().erase(name);
                            }
                            if (bot::stop_requested(name))
                            {
                                set_status(name, false, "stopped", "已停止");
                                bot::clear_stop(name);
                            }
                        })
                .detach();

            logx::info("channel", "spawned " + driver->display + " worker: " + name);
            return true;
        }
        return false;
    }

    bool stop(const std::string &name)
    {
        {
            std::lock_guard<std::mutex> lk(run_mutex());
            if (!running_set().count(name))
                return false;
        }
        bot::request_stop(name);
        logx::info("channel", "stop requested: " + name);
        return true;
    }

    void start_all()
    {
        static std::once_flag curl_once;
        std::call_once(curl_once, []()
                       { curl_global_init(CURL_GLOBAL_DEFAULT); });

        register_builtin();

        if (!run_unit::settings.contains("channels"))
            return;
        for (auto &ch : run_unit::settings["channels"])
        {
            const std::string name = ch.value("name", "");
            if (name.empty() || ch.value("status", "") != "active")
                continue;
            start(name);
        }
    }
} // namespace channels
