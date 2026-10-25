#pragma once
#include "core/common.hpp"
#include "core/text.hpp"
#include "agent/fs_tools.hpp"
#include "agent/session.hpp"

namespace perm_unit
{
    using json = nlohmann::json;

    // 权限策略
    enum class Policy : int
    {
        Allow = 0,
        Deny = 1,
        Ask = 2
    };

    inline std::string policy_to_string(Policy p)
    {
        switch (p)
        {
        case Policy::Allow:
            return "allow";
        case Policy::Deny:
            return "deny";
        case Policy::Ask:
            return "ask";
        }
        return "ask";
    }

    inline Policy policy_from_string(const std::string &s)
    {
        if (s == "allow")
            return Policy::Allow;
        if (s == "deny")
            return Policy::Deny;
        return Policy::Ask;
    }

    // 待审批请求
    struct PendingRequest
    {
        std::string id;         // 唯一请求 ID
        std::string session_id; // 会话 ID / 频道名
        std::string tool;       // 工具名
        std::string args;       // 参数（已脱敏）
        long long created_at = 0;
        int timeout_sec = 120;
        bool answered = false;  // 用户是否已响应
        bool granted = false;   // 是否允许
        bool remember = false;  // 是否记住该选择
        bool timed_out = false; // 是否等待超时
    };

    // 授权决策
    struct Decision
    {
        bool allowed = false;     // 是否允许执行
        bool remembered = false;  // 是否生成了持久规则
        std::string reason;       // 决策原因
        std::string request_id;   // 发生询问时的请求 ID
    };

    // 通知回调：finished=false 表示请求创建（通知 WebUI 弹出授权窗口）；
    // finished=true 表示已获得结果（超时/用户响应），req 携带最终状态
    using NotifyCallback = std::function<void(const PendingRequest &req, bool finished)>;

    // 权限规则
    struct Rule
    {
        std::string tool = "*";    // 工具名，* 表示全部
        std::string pattern = "";  // 参数匹配模式，空或 * 表示全部参数
        Policy policy = Policy::Ask;
        std::string note;          // 备注
        long long created_at = 0;
    };

    class PermissionManager
    {
    private:
        std::string perm_file; // workspace/permissions.json
        std::vector<Rule> rules;
        std::vector<std::shared_ptr<PendingRequest>> pending; // 待审批队列
        mutable std::mutex mtx;
        std::map<std::string, Policy> defaults; // 默认策略

        // 内置兜底默认策略：高风险内置工具 ask，其余 allow
        static Policy builtin_default(const std::string &tool)
        {
            if (tool == "exec" || tool == "write" || tool == "edit" || tool == "wget")
                return Policy::Ask;
            return Policy::Allow; // read / Image / 自定义工具
        }

        void load_defaults()
        {
            defaults.clear();
            try
            {
                if (run_unit::settings.contains("permissions") &&
                    run_unit::settings["permissions"].contains("defaults") &&
                    run_unit::settings["permissions"]["defaults"].is_object())
                {
                    for (auto &[k, v] : run_unit::settings["permissions"]["defaults"].items())
                    {
                        if (v.is_string())
                            defaults[k] = policy_from_string(v.get<std::string>());
                    }
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "Warning - failed to parse permission defaults: " << e.what() << std::endl;
            }
            // 确保关键内置工具存在默认策略
            for (auto &t : {"exec", "write", "edit", "wget", "read", "Image"})
            {
                if (!defaults.count(t))
                    defaults[t] = builtin_default(t);
            }
            if (!defaults.count("*"))
                defaults["*"] = Policy::Allow;
        }

        static bool match_pattern(const std::string &args, const std::string &pattern)
        {
            if (pattern.empty() || pattern == "*")
                return true;
            return args.find(pattern) != std::string::npos;
        }

        // 参数脱敏：截断过长参数
        static std::string mask_args(const std::string &args)
        {
            if (args.size() <= 200)
                return args;
            return args.substr(0, 200) + "...(truncated)";
        }

        void save_locked()
        {
            json arr = json::array();
            for (auto &r : rules)
                arr.push_back({{"tool", r.tool},
                               {"pattern", r.pattern},
                               {"policy", policy_to_string(r.policy)},
                               {"note", r.note},
                               {"created_at", r.created_at}});
            try
            {
                tool_unit::writeFile(perm_file, arr.dump(4));
            }
            catch (const std::exception &e)
            {
                std::cerr << "Warning - failed to save permissions.json: " << e.what() << std::endl;
            }
        }

    public:
        // 初始化：加载规则文件与默认策略（main 启动阶段调用）
        void init(const std::string &workspace)
        {
            std::lock_guard<std::mutex> lk(mtx);
            perm_file = workspace + "/permissions.json";
            load_defaults();
            rules.clear();
            if (std::filesystem::exists(perm_file))
            {
                try
                {
                    auto j = json::parse(tool_unit::readFile(perm_file));
                    if (j.is_array())
                    {
                        for (auto &r : j)
                        {
                            Rule rule;
                            rule.tool = r.value("tool", std::string("*"));
                            rule.pattern = r.value("pattern", std::string(""));
                            rule.policy = policy_from_string(r.value("policy", std::string("ask")));
                            rule.note = r.value("note", std::string(""));
                            rule.created_at = r.value("created_at", (long long)0);
                            rules.push_back(rule);
                        }
                    }
                }
                catch (const std::exception &e)
                {
                    std::cerr << "Warning - failed to load permissions.json: " << e.what() << std::endl;
                }
            }
            else
            {
                std::cout << "Info - No permissions.json found, creating a new one..." << std::endl;
                save_locked();
            }
        }

        // 全部规则（供 HTTP API / WebUI 权限面板）
        json rules_json() const
        {
            std::lock_guard<std::mutex> lk(mtx);
            json arr = json::array();
            for (auto &r : rules)
                arr.push_back({{"tool", r.tool},
                               {"pattern", r.pattern},
                               {"policy", policy_to_string(r.policy)},
                               {"note", r.note},
                               {"created_at", r.created_at}});
            return arr;
        }

        json defaults_json() const
        {
            std::lock_guard<std::mutex> lk(mtx);
            json obj = json::object();
            for (auto &[k, v] : defaults)
                obj[k] = policy_to_string(v);
            return obj;
        }

        // 待审批队列（供 HTTP API / WebUI 轮询）
        json pending_json() const
        {
            std::lock_guard<std::mutex> lk(mtx);
            json arr = json::array();
            for (auto &p : pending)
            {
                if (p->answered)
                    continue;
                arr.push_back({{"id", p->id},
                               {"session_id", p->session_id},
                               {"tool", p->tool},
                               {"args", p->args},
                               {"created_at", p->created_at},
                               {"timeout", p->timeout_sec}});
            }
            return arr;
        }

        int timeout_sec() const
        {
            try
            {
                if (run_unit::settings.contains("permissions") &&
                    run_unit::settings["permissions"].contains("timeout_sec"))
                    return run_unit::settings["permissions"]["timeout_sec"].get<int>();
            }
            catch (...)
            {
            }
            return 120;
        }

        // 新增/更新规则（HTTP API：POST /api/permissions/set）
        bool set_rule(const std::string &tool, const std::string &pattern,
                      const std::string &policy_str, std::string &err)
        {
            if (tool.empty())
            {
                err = "tool is required";
                return false;
            }
            if (policy_str != "allow" && policy_str != "deny" && policy_str != "ask")
            {
                err = "policy must be one of: allow / deny / ask";
                return false;
            }
            std::lock_guard<std::mutex> lk(mtx);
            for (auto &r : rules)
            {
                if (r.tool == tool && r.pattern == pattern)
                {
                    r.policy = policy_from_string(policy_str);
                    r.note = "user";
                    save_locked();
                    return true;
                }
            }
            Rule r;
            r.tool = tool;
            r.pattern = pattern;
            r.policy = policy_from_string(policy_str);
            r.note = "user";
            r.created_at = (long long)std::time(nullptr);
            rules.push_back(r);
            save_locked();
            return true;
        }

        // 删除规则（HTTP API：POST /api/permissions/delete）
        bool delete_rule(const std::string &tool, const std::string &pattern, std::string &err)
        {
            std::lock_guard<std::mutex> lk(mtx);
            auto it = std::remove_if(rules.begin(), rules.end(),
                                     [&](const Rule &r)
                                     { return r.tool == tool && r.pattern == pattern; });
            if (it == rules.end())
            {
                err = "rule not found";
                return false;
            }
            rules.erase(it, rules.end());
            save_locked();
            return true;
        }

        // 响应审批（HTTP API：POST /api/permission/respond）
        bool respond(const std::string &id, bool granted, bool remember, std::string &err)
        {
            std::lock_guard<std::mutex> lk(mtx);
            for (auto &p : pending)
            {
                if (p->id == id)
                {
                    p->answered = true;
                    p->granted = granted;
                    p->remember = remember;
                    if (remember)
                    {
                        // 记住选择：为该工具建立全局规则（pattern 为空 = 匹配全部参数）
                        bool exists = false;
                        for (auto &r : rules)
                        {
                            if (r.tool == p->tool && r.pattern.empty())
                            {
                                r.policy = granted ? Policy::Allow : Policy::Deny;
                                r.note = "user (remembered)";
                                exists = true;
                                break;
                            }
                        }
                        if (!exists)
                        {
                            Rule r;
                            r.tool = p->tool;
                            r.pattern = "";
                            r.policy = granted ? Policy::Allow : Policy::Deny;
                            r.note = "user (remembered)";
                            r.created_at = (long long)std::time(nullptr);
                            rules.push_back(r);
                        }
                        save_locked();
                    }
                    return true;
                }
            }
            err = "pending request not found: " + id;
            return false;
        }

        // ═══ 核心授权流程 ═══
        // 说明：授权等待期间不阻塞事件循环 —— 服务器采用多线程 io_context.run()，
        // /api/permission/respond 请求由其他工作线程并发处理，此处仅需自旋等待。
        Decision authorize(const std::string &tool, const std::string &args,
                           const std::string &session_id,
                           const NotifyCallback &notify = nullptr)
        {
            Decision dec;
            dec.reason = "policy";

            // 1. 用户规则优先（规则文件 → permissions.json）
            {
                std::lock_guard<std::mutex> lk(mtx);
                for (auto &r : rules)
                {
                    if ((r.tool == tool || r.tool == "*") && match_pattern(args, r.pattern))
                    {
                        dec.allowed = (r.policy == Policy::Allow);
                        dec.reason = "rule: " + r.tool + (r.pattern.empty() ? "" : ":" + r.pattern) +
                                     " -> " + policy_to_string(r.policy);
                        return dec;
                    }
                }
            }

            // 2. 默认策略（settings.json → permissions.defaults）
            Policy pol;
            {
                std::lock_guard<std::mutex> lk(mtx);
                auto it = defaults.find(tool);
                pol = (it != defaults.end()) ? it->second : defaults["*"];
            }
            if (pol == Policy::Allow)
            {
                dec.allowed = true;
                dec.reason = "default allow";
                return dec;
            }
            if (pol == Policy::Deny)
            {
                dec.allowed = false;
                dec.reason = "default deny";
                return dec;
            }

            // 3. ask 且无交互上下文（频道 bot / 无通知回调）：
            //    按 settings.permissions.channel_ask 处理（默认 deny，安全优先）
            if (!notify)
            {
                bool channel_allow = false;
                try
                {
                    if (run_unit::settings.contains("permissions") &&
                        run_unit::settings["permissions"].contains("channel_ask"))
                        channel_allow = run_unit::settings["permissions"]["channel_ask"].get<std::string>() == "allow";
                }
                catch (...)
                {
                }
                dec.allowed = channel_allow;
                dec.reason = channel_allow ? "channel auto-approve (channel_ask=allow)"
                                           : "channel context: denied (no interactive approval, channel_ask=deny)";
                return dec;
            }

            // 4. 创建待审批请求并通知调用方（WebUI 弹窗）
            auto req = std::make_shared<PendingRequest>();
            req->id = make_id();
            req->session_id = session_id;
            req->tool = tool;
            req->args = mask_args(args);
            req->created_at = (long long)std::time(nullptr);
            req->timeout_sec = timeout_sec();
            {
                std::lock_guard<std::mutex> lk(mtx);
                pending.push_back(req);
            }
            try
            {
                notify(*req, false);
            }
            catch (...)
            {
            }

            // 5. 等待用户响应：响应请求由事件循环的其他工作线程并发处理
            auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::seconds(std::max(1, req->timeout_sec));
            while (std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    if (req->answered)
                        break;
                }
            }

            // 6. 收尾：超时标记 + 从队列移除 + 通知结果
            {
                std::lock_guard<std::mutex> lk(mtx);
                if (!req->answered)
                {
                    req->timed_out = true;
                    req->granted = false;
                }
                pending.erase(std::remove(pending.begin(), pending.end(), req), pending.end());
            }
            try
            {
                notify(*req, true);
            }
            catch (...)
            {
            }

            dec.request_id = req->id;
            if (req->timed_out)
            {
                dec.allowed = false;
                dec.reason = "approval timeout (" + std::to_string(req->timeout_sec) + "s)";
            }
            else
            {
                dec.allowed = req->granted;
                dec.remembered = req->remember;
                dec.reason = req->granted ? "user approved" : "user denied";
            }
            return dec;
        }

        static std::string make_id()
        {
            static std::mt19937_64 rng(std::random_device{}());
            std::ostringstream oss;
            oss << std::hex << std::setw(16) << std::setfill('0') << rng()
                << std::setw(16) << std::setfill('0') << rng();
            return oss.str();
        }
    };

    inline PermissionManager &manager()
    {
        static PermissionManager instance;
        return instance;
    }
} // namespace perm_unit
