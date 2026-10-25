#pragma once
#ifndef AGENTCPP_AGENT_TOOLS_HPP
#define AGENTCPP_AGENT_TOOLS_HPP

#include "core/common.hpp"
#include "core/text.hpp"
#include "agent/fs_tools.hpp"
#include "agent/session.hpp"
#include "agent/permissions.hpp"

namespace tool_unit
{
    inline std::pair<size_t, size_t> tools_scan(std::string &context, std::string &data,
                                         const std::string &session_id = "",
                                         const perm_unit::NotifyCallback &notify = nullptr)
    {
        auto arr = extractAllTags(context, "tool");
        size_t count = 0;
        size_t succeed = 0;
        if (arr.size() < 1)
            return {count, succeed};

        for (auto &ctx : arr)
        {
            data += "\n<system_output>\n";
            auto [name, args] = parseArgs(ctx);

            // ═══ 工具权限检查 ═══
            // 命中 ask 策略的工具（如 exec / write / edit / wget）时：
            // - WebUI 流式会话：通过 notify 回调推送 permission_request 事件并等待用户授权
            // - 频道等无交互上下文：按 settings.permissions.channel_ask 策略直接处理（默认拒绝）
            auto perm = perm_unit::manager().authorize(std::string(name), std::string(args),
                                                       session_id, notify);
            if (!perm.allowed)
            {
                data += "[PERMISSION_DENIED] " + perm.reason + "\n";
                data += "\n[TOOL_ERR]\n";
                count += 1;
                data += "\n</system_output>\n";
                continue;
            }
            if (perm.remembered)
                data += "[PERMISSION_SAVED] rule remembered: tool=" + std::string(name) + "\n";

            bool tool_ok = false;
            if (name == "exec")
            {
                try
                {
                    data += exec(std::string(args));
                    data += "\n[TOOL_DONE]\n";
                    tool_ok = true;
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                }
                count += 1;
            }
            else if (name == "read")
            {
                try
                {
                    data += readFile(std::string(args));
                    data += "\n[TOOL_DONE]\n";
                    tool_ok = true;
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                }
                count += 1;
            }
            else if (name == "Image")
            {
                try
                {
                    image_queue.push_back(Image(std::string(args)));
                    data += "[Image has read done]";
                    data += "\n[TOOL_DONE]\n";
                    tool_ok = true;
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                }
                count += 1;
            }
            else if (name == "write")
            {
                try
                {
                    auto [file, content] = parseArgs(args, '|');
                    writeFile(std::string(file), std::string(content));
                    data += "\n[TOOL_DONE]\n";
                    tool_ok = true;
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                }
                count += 1;
            }
            else if (name == "wget")
            {
                try
                {
                    data += wget(std::string(args).c_str());
                    data += "\n[TOOL_DONE]\n";
                    tool_ok = true;
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                }
                count += 1;
            }
            else if (name == "edit")
            {
                try
                {
                    auto [file, rest] = parseArgs(args, '|');
                    editFile(std::string(file), std::string(rest));
                    data += "\n[TOOL_DONE]\n";
                    tool_ok = true;
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                }
                count += 1;
            }
            else
            {
                try
                {

#ifdef _WIN32
                    data += exec("cmd /c chcp 65001>nul && python.exe " +
                                 run_unit::settings["workspace"].get_ref<const std::string &>() +
                                 "/tools/" + std::string(name) + "/run.py" + " 2>&1 " + std::string(args));
#else
                    data += exec("python3 " +
                                 run_unit::settings["workspace"].get_ref<const std::string &>() +
                                 "/tools/" + std::string(name) + "/run.py" + " 2>&1 " + std::string(args));
#endif //_WIN32

                    data += "\n[TOOL_DONE]\n";
                    tool_ok = true;
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                }
                count += 1;
            }
            if (tool_ok)
                succeed += 1;
            else
                data += "Tool execution error, please check that you're using it correctly and that the parameters are correct!";
            data += "\n</system_output>\n";
        }
        return {count, succeed};
    }
} // namespace tool_unit

#endif // AGENTCPP_AGENT_TOOLS_HPP
