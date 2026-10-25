#pragma once
#include "core/common.hpp"
#include "core/text.hpp"
#include "core/sysinfo.hpp"
#include "agent/session.hpp"

namespace cs_unit
{
    struct Command
    {
        std::string name;
        std::function<std::string(std::string_view)> callback;
    };

    static std::vector<Command> command_map = {
        {"system_status", [](std::string_view args) -> std::string
         {
             try
             {
                 return get_system_status();
             }
             catch (const std::exception &e)
             {
                 return e.what();
             }
         }},
        {"tools", [](std::string_view args) -> std::string
         {
             try
             {
                 return run_unit::tools_list.dump();
             }
             catch (const std::exception &e)
             {
                 return e.what();
             }
         }},
        {"restart", [](std::string_view args) -> std::string
         { return "Restart OK"; }},
        {"time", [](std::string_view args) -> std::string
         {
             try
             {
                 std::time_t t = std::time(nullptr);
                 return std::asctime(std::localtime(&t));
             }
             catch (const std::exception &e)
             {
                 return e.what();
             }
         }},
        {"random", [](std::string_view args) -> std::string
         {
             try
             {
                 int seed;
                 sscanf(args.data(), "%d", &seed);
                 std::mt19937 gen(seed);
                 std::uniform_int_distribution<int> dist(-1e9, 1e9);
                 return std::to_string(dist(gen));
             }
             catch (const std::exception &e)
             {
                 return e.what();
             }
         }},
        {"mem-keys", [](std::string_view args) -> std::string
         {
             try
             {
                 std::string keys;
                 std::string mem_dir = run_unit::settings["workspace"].get_ref<const std::string &>() + "/memorys";
                 auto &mgr = run_unit::agent_session_manager;
                 auto load_sessions = get_all_files(mem_dir);
                 for (auto &session : load_sessions)
                 {
                     auto [name, ext] = file_parse(session);
                     if (ext == ".json")
                     {
                         nlohmann::json memory = mgr.get_cached_memory(session);
                         if (memory.contains("abstracts") && memory.contains("keywords"))
                             if (!memory["keywords"].get_ref<const std::string &>().empty() && !memory["abstracts"].get_ref<const std::string &>().empty())
                                 keys += name + ": " + memory["keywords"].get_ref<const std::string &>() + "\n";
                     }
                 }
                 return keys;
             }
             catch (const std::exception &e)
             {
                 return e.what();
             }
         }},
        {"mem-get", [](std::string_view args) -> std::string
         {
             try
             {
                 std::string query;
                 std::string mem_dir = run_unit::settings["workspace"].get_ref<const std::string &>() + "/memorys";
                 auto &mgr = run_unit::agent_session_manager;
                 auto load_sessions = get_all_files(mem_dir);
                 for (auto &session : load_sessions)
                 {
                     auto [name, ext] = file_parse(session);
                     if (ext == ".json" && args.find(name) != std::string::npos)
                     {
                         nlohmann::json memory = mgr.get_cached_memory(session);
                         if (memory.contains("abstracts") && memory.contains("keywords"))
                             query += name + ": " + memory["abstracts"].get_ref<const std::string &>() + "\n";
                     }
                 }
                 return query;
             }
             catch (const std::exception &e)
             {
                 return e.what();
             }
         }}};

    // 同样使用 <system_output> 包裹cs输出
    inline size_t cs_scan(std::string &context, std::string &data)
    {
        auto arr = extractAllTags(context, "cs");
        size_t count = 0;
        if (arr.size() < 1)
            return count;

        for (auto &tag : arr)
        {
            data += "<system_output>\n";
            auto [name, args] = parseArgs(tag);
            for (auto &cmd : command_map)
            {
                if (cmd.name == name)
                {
                    data += cmd.callback(args);
                    data += "\n[CS_DONE]\n";
                    count += 1;
                }
            }
            data += "\n</system_output>\n";
        }
        return count;
    }
} // namespace cs_unit
