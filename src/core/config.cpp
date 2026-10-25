#include "core/config.hpp"

#include "core/base64.hpp"
#include "core/log.hpp"
#include "core/seed_assets.hpp"
#include "agent/tools.hpp"

namespace config
{
    namespace
    {
        fs::path g_file;
        json g_defaults;
        bool g_loaded = false;

        void write_if_missing(const fs::path &p, const std::string &content)
        {
            std::error_code ec;
            if (fs::exists(p, ec))
                return;
            fs::create_directories(p.parent_path(), ec);
            std::ofstream f(p, std::ios::binary);
            if (!f)
                throw std::runtime_error("cannot create file: " + paths::to_utf8(p));
            f << content;
        }

        void mkdirs(const fs::path &p)
        {
            std::error_code ec;
            fs::create_directories(p, ec);
        }
    } // namespace

    // ------------------------------------------------------------------ 默认值
    const json &defaults()
    {
        if (g_defaults.is_null())
        {
            g_defaults = json::parse(R"({
    "agent_name": "assistant",
    "user_name": "user",
    "root": "",
    "workspace": "workspace",
    "prompt": "agent.txt",
    "webui": {
        "path": "webui",
        "index": "index.html",
        "source": "embedded",
        "password": ""
    },
    "model": "default",
    "stream": true,
    "max_mpc_rounds": 5,
    "max_context": 1048576,
    "auto_summary_threshold": 0,
    "filesystem": { "auto_expand": false },
    "window": { "close_to_tray": true },
    "server": { "host": "127.0.0.1", "port": 8080 },
    "permissions": {
        "timeout_sec": 120,
        "channel_ask": "deny",
        "defaults": {
            "exec": "ask",
            "write": "ask",
            "edit": "ask",
            "wget": "ask",
            "read": "allow",
            "Image": "allow",
            "*": "allow"
        }
    },
    "providers": [
        {
            "id": "openai-default",
            "name": "OpenAI",
            "type": "openai",
            "server_address": "http://localhost:11434",
            "has_key": false
        }
    ],
    "current_provider": "openai-default",
    "channels": [
        {
            "name": "Telegram",
            "type": "telegram",
            "status": "inactive",
            "user_count": 1,
            "config": { "model": "default", "think": false, "proxy": "" }
        },
        {
            "name": "WeChat",
            "type": "wechat",
            "status": "inactive",
            "user_count": 1,
            "config": { "model": "default", "think": false, "ilink_base": "https://ilinkai.weixin.qq.com" }
        },
        {
            "name": "QQ",
            "type": "qq",
            "status": "inactive",
            "user_count": 1,
            "config": {
                "model": "default",
                "think": false,
                "api_base": "http://127.0.0.1:3000",
                "access_token": "",
                "webhook_path": "/api/channel/qq/event",
                "self_id": "",
                "group_at_only": true
            }
        }
    ]
})");
        }
        return g_defaults;
    }

    // ------------------------------------------------------------------ 读取
    bool load(const std::string &path)
    {
        const json &d = defaults();
        fs::path target = path.empty() ? paths::settings_file() : paths::from_utf8(path);
        if (target.is_relative())
            target = paths::resolve(target);
        g_file = target;

        bool created = false;
        std::error_code ec;
        if (!fs::exists(target, ec))
        {
            fs::create_directories(target.parent_path(), ec);
            std::ofstream f(target, std::ios::binary);
            if (!f)
                throw std::runtime_error("cannot create settings file: " + paths::to_utf8(target));
            f << d.dump(4) << std::endl;
            created = true;
        }

        json loaded;
        try
        {
            std::ifstream f(target, std::ios::binary);
            if (!f)
                throw std::runtime_error("cannot open " + paths::to_utf8(target));
            f >> loaded;
        }
        catch (const std::exception &e)
        {
            throw std::runtime_error(std::string("invalid settings.json: ") + e.what());
        }
        if (!loaded.is_object())
            throw std::runtime_error("settings.json must contain a JSON object");

        // 补齐缺失字段（保留用户已有值，只追加新键）
        for (auto it = d.begin(); it != d.end(); ++it)
        {
            if (!loaded.contains(it.key()))
                loaded[it.key()] = it.value();
        }
        // webui 子对象逐键补齐
        if (!loaded["webui"].is_object())
            loaded["webui"] = d["webui"];
        for (auto it = d["webui"].begin(); it != d["webui"].end(); ++it)
            if (!loaded["webui"].contains(it.key()))
                loaded["webui"][it.key()] = it.value();
        // 老版本把密码放在顶层 webui_password
        if (loaded.contains("webui_password") && loaded["webui"]["password"].get<std::string>().empty())
            loaded["webui"]["password"] = loaded["webui_password"];

        run_unit::settings = std::move(loaded);
        run_unit::setting_file_path = paths::to_utf8(target);
        g_loaded = true;

        // root 覆盖（配置文件里写死的数据根）
        if (run_unit::settings["root"].is_string() && !run_unit::settings["root"].get<std::string>().empty())
            paths::set_agent_root(run_unit::settings["root"].get<std::string>());

        return created;
    }

    void save()
    {
        if (!g_loaded)
            return;
        std::ofstream f(g_file, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            logx::error("config", "cannot write " + paths::to_utf8(g_file));
            return;
        }
        f << run_unit::settings.dump(4) << std::endl;
    }

    const fs::path &file() { return g_file; }

    // ------------------------------------------------------------------ 便捷读取
    std::string str(const char *key, const std::string &def)
    {
        if (!run_unit::settings.is_object() || !run_unit::settings.contains(key))
            return def;
        const auto &v = run_unit::settings[key];
        return v.is_string() ? v.get<std::string>() : def;
    }

    std::string str_path(const std::string &path, const std::string &def)
    {
        if (!run_unit::settings.is_object() || path.empty())
            return def;
        const json *cur = &run_unit::settings;
        size_t pos = 0;
        while (cur && cur->is_object())
        {
            size_t dot = path.find('.', pos);
            const std::string key = (dot == std::string::npos) ? path.substr(pos)
                                                              : path.substr(pos, dot - pos);
            if (!cur->contains(key))
                return def;
            cur = &(*cur)[key];
            if (dot == std::string::npos)
                break;
            pos = dot + 1;
        }
        return (cur && cur->is_string()) ? cur->get<std::string>() : def;
    }

    size_t num(const char *key, size_t def)
    {
        if (!run_unit::settings.is_object() || !run_unit::settings.contains(key))
            return def;
        const auto &v = run_unit::settings[key];
        return v.is_number_integer() ? v.get<size_t>() : def;
    }

    bool flag(const char *key, bool def)
    {
        if (!run_unit::settings.is_object() || !run_unit::settings.contains(key))
            return def;
        const auto &v = run_unit::settings[key];
        return v.is_boolean() ? v.get<bool>() : def;
    }

    json obj(const char *key)
    {
        if (!run_unit::settings.is_object() || !run_unit::settings.contains(key))
            return json::object();
        return run_unit::settings[key];
    }

    std::string user_name() { return str("user_name", "user"); }
    std::string agent_name() { return str("agent_name", "assistant"); }

    // ------------------------------------------------------------------ 目录
    fs::path workspace_dir()
    {
        std::string ws = str("workspace", "workspace");
        if (ws.empty())
            ws = "workspace";
        return paths::resolve(ws);
    }

    fs::path webui_path()
    {
        json w = obj("webui");
        std::string p = w.value("path", std::string("webui"));
        if (p.empty())
            p = "webui";
        return paths::resolve(p);
    }

    std::string webui_index()
    {
        json w = obj("webui");
        std::string idx = w.value("index", std::string("index.html"));
        return idx.empty() ? "index.html" : idx;
    }

    fs::path logs_dir() { return paths::agent_root() / "logs"; }
    fs::path sessions_dir() { return workspace_dir() / "sessions"; }
    fs::path memory_dir() { return workspace_dir() / "memorys"; }
    fs::path assets_dir() { return workspace_dir() / "assets" / "messages"; }
    fs::path tokens_dir() { return workspace_dir() / "tokens"; }
    fs::path sys_dir() { return workspace_dir() / "sys"; }
    fs::path tools_dir() { return workspace_dir() / "tools"; }

    fs::path in_workspace(const std::string &rel)
    {
        fs::path p = paths::from_utf8(rel);
        return p.is_absolute() ? p : (workspace_dir() / p);
    }

    // ------------------------------------------------------------------ 布局
    void ensure_layout()
    {
        const fs::path root = paths::agent_root();
        const fs::path ws = workspace_dir();

        mkdirs(root);
        mkdirs(root / "logs");
        mkdirs(ws);
        mkdirs(ws / "sessions");
        mkdirs(ws / "memorys");
        mkdirs(ws / "assets" / "messages");
        mkdirs(ws / "tokens" / "providers");
        mkdirs(ws / "sys");
        mkdirs(ws / "tools");

        // 提示词 / 工具脚本 / 初始 JSON 全部编译进 exe（src/templates → seed_assets.hpp），
        // 首次启动按需写盘；已存在的文件一律不动。
        // 表格里的路径相对数据根；workspace/ 前缀要映射到配置里的实际工作区。
        for (std::size_t i = 0; i < seed::kAssetCount; ++i)
        {
            const seed::Asset &a = seed::kAssets[i];
            const std::string rel = a.path;
            const fs::path target = rel.rfind("workspace/", 0) == 0
                                        ? ws / paths::from_utf8(rel.substr(10))
                                        : root / paths::from_utf8(rel);
            std::error_code ec;
            if (fs::exists(target, ec))
                continue;
            fs::create_directories(target.parent_path(), ec);
            std::string b64;
            b64.reserve(a.size * 4 / 3 + 8);
            for (const char *const *p = a.chunks; p && *p; ++p)
                b64 += *p;
            write_if_missing(target, base64::from_base64(b64));
        }
    }

    // ------------------------------------------------------------------ 校验
    bool validate_json(const json &j)
    {
        auto fail = [](const char *msg)
        {
            logx::error("config", msg);
            return false;
        };
        if (!j.is_object())
            return fail("settings must be a JSON object");
        if (!j.contains("user_name") || !j["user_name"].is_string())
            return fail("missing 'user_name'");
        if (!j.contains("agent_name") || !j["agent_name"].is_string())
            return fail("missing 'agent_name'");
        if (!j.contains("model") || !j["model"].is_string() || j["model"].get<std::string>().empty())
            return fail("missing or empty 'model'");
        if (!j.contains("prompt") || !j["prompt"].is_string())
            return fail("missing 'prompt'");
        if (!j.contains("stream") || !j["stream"].is_boolean())
            return fail("missing 'stream'");
        if (!j.contains("max_mpc_rounds") || !j["max_mpc_rounds"].is_number_integer())
            return fail("missing 'max_mpc_rounds'");
        if (!j.contains("max_context") || !j["max_context"].is_number_integer())
            return fail("missing 'max_context'");
        if (!j.contains("providers") || !j["providers"].is_array() || j["providers"].empty())
            return fail("'providers' must be a non-empty array");
        for (auto &p : j["providers"])
        {
            if (!p.is_object() || p.value("id", "").empty())
                return fail("every provider needs a non-empty 'id'");
            if (!p.contains("server_address") || !p["server_address"].is_string())
                return fail("every provider needs 'server_address'");
        }
        if (!j.contains("channels") || !j["channels"].is_array())
            return fail("'channels' must be an array");
        for (auto &c : j["channels"])
        {
            if (!c.is_object())
                return fail("channel entry must be an object");
            if (c.value("name", "").empty())
                return fail("every channel needs a non-empty 'name'");
            if (!c.contains("status") || !c["status"].is_string())
                return fail("every channel needs 'status'");
        }
        if (j.contains("webui"))
        {
            const json &w = j["webui"];
            if (!w.is_object())
                return fail("'webui' must be an object");
            if (w.contains("path") && !w["path"].is_string())
                return fail("'webui.path' must be a string");
            if (w.contains("index") && !w["index"].is_string())
                return fail("'webui.index' must be a string");
            if (w.contains("source") && !w["source"].is_string())
                return fail("'webui.source' must be a string (auto | disk | embedded)");
        }
        return true;
    }

    void deep_merge(json &dst, const json &src)
    {
        if (!dst.is_object() || !src.is_object())
        {
            dst = src;
            return;
        }
        for (auto it = src.begin(); it != src.end(); ++it)
        {
            if (dst.contains(it.key()) && dst[it.key()].is_object() && it.value().is_object())
                deep_merge(dst[it.key()], it.value());
            else
                dst[it.key()] = it.value();
        }
    }

    bool apply(const json &incoming, std::string &error)
    {
        if (!incoming.is_object())
        {
            error = "settings must be a JSON object";
            return false;
        }
        json merged = run_unit::settings.is_object() ? run_unit::settings : json::object();
        deep_merge(merged, incoming);

        // 补齐默认值，避免前端漏字段导致后续读取拿到空值
        for (auto it = defaults().begin(); it != defaults().end(); ++it)
            if (!merged.contains(it.key()))
                merged[it.key()] = it.value();
        if (!merged["webui"].is_object())
            merged["webui"] = defaults()["webui"];
        for (auto it = defaults()["webui"].begin(); it != defaults()["webui"].end(); ++it)
            if (!merged["webui"].contains(it.key()))
                merged["webui"][it.key()] = it.value();
        if (!merged["server"].is_object())
            merged["server"] = defaults()["server"];
        for (auto it = defaults()["server"].begin(); it != defaults()["server"].end(); ++it)
            if (!merged["server"].contains(it.key()))
                merged["server"][it.key()] = it.value();

        if (!validate_json(merged))
        {
            error = "invalid settings";
            return false;
        }
        run_unit::settings = std::move(merged);
        save();
        return true;
    }

    void validate()
    {
        if (!run_unit::settings.contains("model") || str("model").empty())
            throw std::runtime_error("settings.model is empty");
        if (str("prompt").empty())
            throw std::runtime_error("settings.prompt is empty");

        const fs::path prompt = in_workspace(str("prompt"));
        std::error_code ec;
        if (!fs::exists(prompt, ec))
            throw std::runtime_error("prompt file not found: " + paths::to_utf8(prompt));

        if (str_path("webui.source", "embedded") != "embedded")
        {
            const fs::path webui = webui_path();
            if (!fs::exists(webui, ec))
                throw std::runtime_error("webui path not found: " + paths::to_utf8(webui) +
                                         " (set webui.source to \"embedded\" to use the built-in UI)");
        }

        if (!fs::exists(sys_dir() / "cs.txt", ec))
            throw std::runtime_error("sys/cs.txt not found in workspace: " + paths::to_utf8(sys_dir()));

        if (!run_unit::settings.contains("providers") || !run_unit::settings["providers"].is_array() ||
            run_unit::settings["providers"].empty())
            throw std::runtime_error("settings.providers must be a non-empty array");

        if (!run_unit::settings.contains("channels") || !run_unit::settings["channels"].is_array())
            throw std::runtime_error("settings.channels must be an array");

        for (auto &ch : run_unit::settings["channels"])
        {
            if (!ch.is_object() || ch.value("name", "").empty())
                throw std::runtime_error("every channel needs a non-empty name");
            // 老配置没有 type，靠 path 里的脚本名推断
            if (!ch.contains("type") || ch["type"].get<std::string>().empty())
            {
                std::string p = ch.value("path", "");
                std::string type = "telegram";
                if (p.find("wx_bot") != std::string::npos)
                    type = "wechat";
                else if (p.find("qq_bot") != std::string::npos)
                    type = "qq";
                ch["type"] = type;
            }
            if (!ch.contains("config") || !ch["config"].is_object())
                ch["config"] = json::object();
        }
    }

    // ------------------------------------------------------------------ 迁移
    bool migrate_legacy()
    {
        std::error_code ec;
        const fs::path old_settings = paths::exe_dir() / "settings.json";
        const fs::path old_ws = paths::exe_dir() / "workspace";
        const fs::path new_settings = paths::settings_file();

        if (fs::exists(new_settings, ec))
            return false;
        if (!fs::exists(old_settings, ec) && !fs::exists(old_ws, ec))
            return false;

        logx::info("config", "migrating legacy layout -> .agent/");
        mkdirs(paths::agent_root());

        if (fs::exists(old_settings, ec))
        {
            fs::copy_file(old_settings, new_settings, fs::copy_options::overwrite_existing, ec);
            if (ec)
                logx::warn("config", "cannot copy settings.json: " + ec.message());
        }
        if (fs::exists(old_ws, ec) && fs::is_directory(old_ws, ec))
        {
            const fs::path new_ws = paths::agent_root() / "workspace";
            if (!fs::exists(new_ws, ec))
            {
                fs::copy(old_ws, new_ws, fs::copy_options::recursive |
                                              fs::copy_options::overwrite_existing, ec);
                if (ec)
                    logx::warn("config", "cannot copy workspace: " + ec.message());
            }
            // 老的 webui.html 一并搬到 .agent/webui/index.html
            const fs::path old_webui = old_ws / "webui.html";
            if (fs::exists(old_webui, ec))
            {
                const fs::path new_webui = paths::agent_root() / "webui";
                mkdirs(new_webui);
                fs::copy_file(old_webui, new_webui / "index.html",
                              fs::copy_options::overwrite_existing, ec);
            }
        }

        // 改写 workspace 字段为相对路径，避免继续指向 exe 同级的旧目录
        try
        {
            std::ifstream f(new_settings, std::ios::binary);
            if (f)
            {
                json j;
                f >> j;
                if (j.is_object())
                {
                    j["workspace"] = "workspace";
                    if (j.contains("webui") && j["webui"].is_object())
                        j["webui"]["path"] = "webui";
                    std::ofstream out(new_settings, std::ios::binary | std::ios::trunc);
                    out << j.dump(4) << std::endl;
                }
            }
        }
        catch (...)
        {
            logx::warn("config", "legacy settings rewrite skipped");
        }
        return true;
    }
} // namespace config
