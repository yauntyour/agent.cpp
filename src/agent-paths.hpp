// agent-paths.hpp — 角色数据的落盘位置（唯一一份）
// =============================================================================
// 每个角色独占 <数据根>/characters/<角色>/ 一棵树，自己的东西全在里面：
//
//   characters/<role>/
//     Agent.md            角色卡：YAML-ish front matter + 人设正文
//     avatar.<ext>        头像（文件名记在 Agent.md 的 avatar 里）
//     prompt.json         提示词分层覆盖 + 主人与关系记录
//     state.json          内部状态：{"last_sessions":{"<cwd-key>":"<会话 id>"}}
//     sessions/           这个角色的会话（按工作区分组，cell.cpp 写这里）
//       sessions.json     工作区索引 {cwd-key: 工作区绝对路径}
//       <cwd-key>/<会话 id>/...
//     memorys/memory.jsonl  长期记忆，一行一条 {"t":unix,"text":"..."}
//     uploads/            聊天附件（桌面 + IM）
//     audio/              TTS 产出
//     stickers/           表情包
//
// 角色的默认工作区（cwd）在 <程序目录>/workspaces/<role>/，是数据根**旁边**
// 的一棵普通目录：数据根（.agent/）整棵树对文件工具都是敏感区，只有
// characters/<role>/ 被宿主放进「可读不可写」名单；工作区要能随便读写，所以
// 不能落在数据根里面。会话按工作区分组，同一个角色换工作区就是另一组会话。
//
// 旧版布局（.agent/personas/<id>/persona.json + 全局 sessions/uploads/audio/
// stickers）由 app_core::migrate_legacy_layout() 一次性搬进新家，这里不再提供
// 任何旧路径入口。
#pragma once

#include <filesystem>
#include <string>

namespace app::paths
{
    // <数据根>/characters
    inline std::filesystem::path characters_dir() { return cell::root / "characters"; }

    // <数据根>/characters/<role>
    inline std::filesystem::path character_dir(const std::string &role)
    {
        return characters_dir() / role;
    }

    // 角色卡。目录里有它就是「这个角色存在」。
    inline std::filesystem::path agent_md(const std::string &role)
    {
        return character_dir(role) / "Agent.md";
    }

    // 提示词分层覆盖 + 关系记录（AI 通过 settings 工具改）
    inline std::filesystem::path prompt_json(const std::string &role)
    {
        return character_dir(role) / "prompt.json";
    }

    // 角色内部状态（上次打开的会话等）；不是配置，界面不展示
    inline std::filesystem::path state_json(const std::string &role)
    {
        return character_dir(role) / "state.json";
    }

    // 这个角色的会话根（cell::exec_context_guard 的 sessions_root）
    inline std::filesystem::path sessions_dir(const std::string &role)
    {
        return character_dir(role) / "sessions";
    }

    // 工作区索引 {cwd-key: 绝对路径}，由 cell.cpp 维护
    inline std::filesystem::path sessions_index(const std::string &role)
    {
        return sessions_dir(role) / "sessions.json";
    }

    inline std::filesystem::path memorys_dir(const std::string &role)
    {
        return character_dir(role) / "memorys";
    }

    // 一行一条 {"t":unix,"text":"..."}
    inline std::filesystem::path memory_file(const std::string &role)
    {
        return memorys_dir(role) / "memory.jsonl";
    }

    inline std::filesystem::path uploads_dir(const std::string &role)
    {
        return character_dir(role) / "uploads";
    }

    inline std::filesystem::path audio_dir(const std::string &role)
    {
        return character_dir(role) / "audio";
    }

    inline std::filesystem::path stickers_dir(const std::string &role)
    {
        return character_dir(role) / "stickers";
    }

    // <程序目录>/workspaces —— 角色的默认工作区在这下面。刻意放在数据根
    // 外面：数据根对文件工具是只读/敏感区，工作区必须能自由读写。
    inline std::filesystem::path workspaces_root() { return cell::root.parent_path() / "workspaces"; }

    inline std::filesystem::path default_workspace(const std::string &role)
    {
        return workspaces_root() / role;
    }

    // 头像：角色目录里的 avatar.*（没有就返回空路径）。
    // 扩展名不固定（png / jpg / webp / gif），所以按文件名找而不是拼一个。
    inline std::filesystem::path avatar_file(const std::string &role)
    {
        std::error_code ec;
        std::filesystem::path dir = character_dir(role);
        if (!std::filesystem::is_directory(dir, ec))
            return {};
        for (auto &e : std::filesystem::directory_iterator(dir, ec))
        {
            if (ec || !e.is_regular_file(ec))
                continue;
            if (e.path().stem() == "avatar")
                return e.path();
        }
        return {};
    }
} // namespace app::paths
