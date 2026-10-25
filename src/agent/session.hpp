#pragma once
#include "core/common.hpp"
#include "core/text.hpp"
#include "agent/fs_tools.hpp"

namespace run_unit
{
    inline std::string cs_prompt = "";
    inline nlohmann::json settings;
    inline nlohmann::json tools_list;
    inline std::string setting_file_path;

    // 文件使用跟踪（多线程事件循环下需要加锁）
    inline std::unordered_set<std::string> used_files;
    inline std::mutex used_files_mutex;

    inline void mark_file_used(const std::string &path)
    {
        std::lock_guard<std::mutex> lk(used_files_mutex);
        used_files.insert(path);
        if (used_files.size() > 100)
            used_files.erase(used_files.begin());
    }

    inline std::vector<std::string> get_used_files()
    {
        std::lock_guard<std::mutex> lk(used_files_mutex);
        return {used_files.begin(), used_files.end()};
    }

    class DataManager
    {
    private:
        std::string data_file;

    public:
        nlohmann::json data = {};
        DataManager() = default;
        DataManager(const std::string &workspace) : data_file(workspace + "/sys/data.json")
        {
            try
            {
                data = nlohmann::json::parse(tool_unit::readFile(data_file));
                if (!data.contains("usages"))
                {
                    data["usages"] = nlohmann::json();
                    data["usages"]["memory"] = {
                        {"prompt_cost", 0},
                        {"completion_cost", 0},
                        {"total_cost", 0}};
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << e.what() << '\n';
            }
        }
        ~DataManager()
        {
            // 未初始化（data_file 为空）时不要写盘：全局对象在退出阶段析构，
            // 空路径会抛异常并触发 std::terminate
            if (data_file.empty() || data.is_null())
                return;
            try
            {
                tool_unit::writeFile(data_file, data.dump(4));
            }
            catch (...)
            {
            }
        }
    };

    inline DataManager agent_data_manager;

    // 从消息中提取 base64 图片到 asset_array，替换为 "#n"，返回替换后的消息（使用 std::swap 零拷贝）
    inline nlohmann::json extract_images_from_message(const nlohmann::json &msg, nlohmann::json &asset_array)
    {
        if (!msg.contains("content") || !msg["content"].is_array())
            return msg;

        nlohmann::json new_msg = msg;

        for (auto &part : new_msg["content"])
        {
            if (part.is_object() && part.value("type", "") == "image_url")
            {
                std::string *url_ptr = nullptr;
                if (part["image_url"].is_string())
                    url_ptr = &part["image_url"].get_ref<std::string &>();
                else if (part["image_url"].is_object() && part["image_url"].contains("url"))
                    url_ptr = &part["image_url"]["url"].get_ref<std::string &>();

                if (url_ptr && url_ptr->find("data:") == 0)
                {
                    asset_array.push_back("");
                    std::swap(asset_array.back().get_ref<std::string &>(), *url_ptr);
                    part = "#" + std::to_string(asset_array.size() - 1);
                }
            }
        }

        return new_msg;
    }

    // 从 asset_array 恢复 base64 图片到消息中替换 "#n"（使用 std::swap 零拷贝）
    inline void restore_images_in_message(nlohmann::json &msg, nlohmann::json &asset_array)
    {
        if (!msg.contains("content") || !msg["content"].is_array() || asset_array.empty())
            return;

        for (auto &part : msg["content"])
        {
            if (part.is_string())
            {
                auto &tag = part.get_ref<std::string &>();
                if (tag.size() > 1 && tag[0] == '#')
                {
                    try
                    {
                        int idx = std::stoi(tag.substr(1));
                        if (idx >= 0 && idx < (int)asset_array.size())
                        {
                            nlohmann::json img;
                            img["type"] = "image_url";
                            img["image_url"] = nlohmann::json::object();
                            img["image_url"]["url"] = "";
                            std::swap(img["image_url"]["url"].get_ref<std::string &>(), asset_array[idx].get_ref<std::string &>());
                            part = std::move(img);
                        }
                    }
                    catch (...)
                    {
                    }
                }
            }
        }
    }

    struct SessionContext
    {
        nlohmann::json messages = nlohmann::json::array(); // 每个元素为 {role, content}
        nlohmann::json memory = {{"keywords", ""}, {"abstracts", ""}, {"created_at", "-1"}};
        size_t last_saved_index = 0;
        bool loaded = false;
        bool thinking = false;
        std::string session_id;

        SessionContext()
        {
            session_id = std::to_string(std::time(nullptr));
        }

        SessionContext(const std::string &session_id) : session_id(session_id) {}

        bool is_memory_empty()
        {
            return memory["keywords"].get_ref<const std::string &>().empty() &&
                   memory["abstracts"].get_ref<const std::string &>().empty();
        }

        std::string summary_query()
        {
            std::time_t t = std::time(nullptr);
            std::string time = std::asctime(std::localtime(&t));
            std::string query;
            if (is_memory_empty())
            {
                query += "Analyze the conversation below to produce both a summary and keywords. Include the current time.\nTime:" + time + "\n\nConversation:\n";
            }
            else
            {
                query += "Update the existing memory with new conversation data. Include the current time.\nTime:" + time;
                query += "\n\nOld Abstracts:\n" + memory["abstracts"].get<std::string>();
                query += "\n\nOld Keywords:\n" + memory["keywords"].get<std::string>();
                query += "\n\nNew conversation messages to incorporate (only new messages since last save):\n";
            }

            for (size_t i = last_saved_index; i < messages.size(); i++)
            {
                auto &msg = messages[i];
                std::string content;
                if (msg.is_object() && msg.contains("content"))
                {
                    if (msg["content"].is_string())
                        content = msg["content"].get<std::string>();
                    else if (msg["content"].is_array())
                    {
                        for (auto &part : msg["content"])
                        {
                            if (part["type"] == "text")
                                content += part["text"].get<std::string>();
                        }
                    }
                }
                else if (msg.is_string())
                {
                    content = msg.get<std::string>();
                }
                query += content + "\n\n";
            }

            query += "Respond with valid JSON only (no other text):\n{\"abstracts\": \"...\", \"keywords\": \"...\"}";
            return query;
        }
    };

    class SessionManager
    {
    public:
        std::unordered_map<std::string, std::shared_ptr<SessionContext>> sessions;
        std::string current_session_id;
        std::string workspace = "";

        struct MemoryCacheEntry
        {
            nlohmann::json data;
            std::filesystem::file_time_type mtime;
        };
        std::unordered_map<std::string, MemoryCacheEntry> memory_cache;

        nlohmann::json get_cached_memory(const std::string &path)
        {
            auto it = memory_cache.find(path);
            auto mtime = std::filesystem::last_write_time(path);
            if (it != memory_cache.end() && it->second.mtime == mtime)
                return it->second.data;
            auto data = nlohmann::json::parse(tool_unit::readFile(path));
            memory_cache[path] = {data, mtime};
            return data;
        }

        void invalidate_cache(const std::string &path)
        {
            memory_cache.erase(path);
        }

        std::shared_ptr<SessionContext> get(const std::string &id)
        {
            auto it = sessions.find(id);
            if (it != sessions.end())
            {
                if (it->second->loaded == false)
                {
                    // 从磁盘加载会话消息
                    try
                    {
                        std::string path = workspace + "/sessions/" + it->second->session_id + ".json";
                        if (std::filesystem::exists(path))
                        {
                            it->second->messages = nlohmann::json::parse(tool_unit::readFile(path));
                            if (!it->second->messages.is_array())
                            {
                                it->second->messages = nlohmann::json::array();
                                std::cout << "WARN - Session " << it->second->session_id << " file is not an array, reset." << std::endl;
                            }
                            // 恢复图片数据
                            nlohmann::json asset_array;
                            std::string asset_path = workspace + "/assets/messages/" + it->second->session_id + ".json";
                            if (std::filesystem::exists(asset_path))
                            {
                                try
                                {
                                    asset_array = nlohmann::json::parse(tool_unit::readFile(asset_path));
                                }
                                catch (...)
                                {
                                    asset_array = nlohmann::json::array();
                                }
                            }
                            for (auto &msg : it->second->messages)
                                restore_images_in_message(msg, asset_array);
                        }
                    }
                    catch (const std::exception &e)
                    {
                        std::cerr << e.what() << '\n';
                    }

                    // 加载记忆
                    auto memory_path = workspace + "/memorys/" + it->second->session_id + ".json";
                    if (std::filesystem::exists(memory_path))
                    {
                        try
                        {
                            it->second->memory = nlohmann::json::parse(tool_unit::readFile(memory_path));
                            if (!it->second->memory.contains("abstracts") || !it->second->memory.contains("keywords"))
                                it->second->memory = {{"keywords", ""}, {"abstracts", ""}, {"created_at", "-1"}};
                            if (it->second->memory.contains("last_saved_index"))
                                it->second->last_saved_index = it->second->memory["last_saved_index"].get<size_t>();
                        }
                        catch (const std::exception &e)
                        {
                            std::cerr << e.what() << '\n';
                        }
                    }
                    it->second->loaded = true;
                }
                return it->second;
            }
            return nullptr;
        }

        std::shared_ptr<SessionContext> create()
        {
            auto session = std::make_shared<SessionContext>();
            sessions[session->session_id] = session;
            current_session_id = session->session_id;
            session->loaded = true;
            return session;
        }

        std::shared_ptr<SessionContext> get_current()
        {
            if (current_session_id.empty())
            {
                auto session = std::make_shared<SessionContext>();
                sessions[session->session_id] = session;
                current_session_id = session->session_id;
                session->loaded = true;
                return session;
            }
            return get(current_session_id);
        }

        std::vector<std::string> list_sessions() const
        {
            std::vector<std::string> result;
            for (const auto &pair : sessions)
                result.push_back(pair.first);
            return result;
        }

        void clear_current()
        {
            auto ses = get_current();
            ses->messages.clear();
            ses->memory.clear();
            ses->memory = {{"keywords", ""}, {"abstracts", ""}, {"created_at", "-1"}};
            ses->last_saved_index = 0;
        }

        void remove_session(const std::string &id)
        {
            sessions.erase(id);
        }

        void change_session(const std::string &id)
        {
            if (current_session_id == id)
                return;
            if (!current_session_id.empty())
            {
                auto it = sessions.find(current_session_id);
                if (it != sessions.end() && it->second->loaded)
                {
                    nlohmann::json save_msgs = nlohmann::json::array();
                    nlohmann::json asset_array = nlohmann::json::array();
                    for (auto &msg : it->second->messages)
                        save_msgs.push_back(extract_images_from_message(msg, asset_array));
                    if (!asset_array.empty())
                        tool_unit::writeFile(workspace + "/assets/messages/" + current_session_id + ".json",
                                             asset_array.dump(4));
                    tool_unit::writeFile(workspace + "/sessions/" + current_session_id + ".json",
                                         save_msgs.dump(4));
                    it->second->memory["last_saved_index"] = it->second->last_saved_index;
                    tool_unit::writeFile(workspace + "/memorys/" + current_session_id + ".json",
                                         it->second->memory.dump(4));
                    it->second->loaded = false;
                    it->second->messages.clear();
                    it->second->memory.clear();
                }
            }
            current_session_id = id;
        }

        SessionManager() = default;

        void init(const std::string &ws)
        {
            workspace = ws;
            try
            {
                auto load_sessions = get_all_files(workspace + "/sessions");
                for (auto &session : load_sessions)
                {
                    auto [name, ext] = file_parse(session);
                    if (ext == ".json")
                    {
                        auto ses_obj = std::make_shared<SessionContext>(name);
                        sessions[name] = ses_obj;
                        std::cout << "Found session: " << name << std::endl;
                    }
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << e.what() << '\n';
            }
        }

        ~SessionManager()
        {
            try
            {
                for (auto &ses : sessions)
                {
                    if (ses.second->loaded)
                    {
                        nlohmann::json save_msgs = nlohmann::json::array();
                        nlohmann::json asset_array = nlohmann::json::array();
                        for (auto &msg : ses.second->messages)
                            save_msgs.push_back(extract_images_from_message(msg, asset_array));
                        if (!asset_array.empty())
                            tool_unit::writeFile(workspace + "/assets/messages/" + ses.first + ".json",
                                                 asset_array.dump(4));
                        tool_unit::writeFile(workspace + "/sessions/" + ses.first + ".json",
                                             save_msgs.dump(4));
                        ses.second->memory["last_saved_index"] = ses.second->last_saved_index;
                        tool_unit::writeFile(workspace + "/memorys/" + ses.first + ".json",
                                             ses.second->memory.dump(4));
                    }
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << e.what() << '\n';
            }
        }
    };

    inline SessionManager agent_session_manager;
} // namespace run_unit
