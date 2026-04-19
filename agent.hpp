#pragma once
#ifndef __AGENT__H__
#define __AGENT__H__
#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <random>
#include <list>
#include <stdio.h>
#include <unistd.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "base64.hpp"

std::vector<std::string_view> extractAllTags(std::string_view text, std::string tag)
{
    std::vector<std::string_view> results;
    size_t pos = 0;

    while (true)
    {
        // 查找开始标签：<tag>
        size_t start_tag_pos = text.find("<" + tag + ">", pos);
        if (start_tag_pos == std::string_view::npos)
            break;

        // 开始标签后的位置
        size_t content_start = start_tag_pos + tag.size() + 2; // "<tag>" 的长度是 tag.size() + 2

        // 查找结束标签：</tag>
        size_t end_tag_pos = text.find("</" + tag + ">", content_start);
        if (end_tag_pos == std::string_view::npos)
            break;

        // 计算内容长度
        size_t content_length = end_tag_pos - content_start;

        // 提取内容
        results.push_back(text.substr(content_start, content_length));

        // 移动到下一个可能的开始位置（跳过当前标签）
        pos = end_tag_pos + tag.size() + 3; // "</tag>" 长度为 tag.size() + 3
    }

    return results;
}
std::pair<std::string_view, std::string_view> parseArgs(std::string_view input, char c = ':')
{
    size_t colonPos = input.find(c);
    if (colonPos == std::string_view::npos)
    {
        return {input, {}};
    }
    else
    {
        std::string_view name = input.substr(0, colonPos);
        std::string_view args = input.substr(colonPos + 1); // 跳过 ':'
        return {name, args};
    }
}
std::vector<std::string> get_all_files(const std::string &root_dir)
{
    std::vector<std::string> files;
    if (!std::filesystem::exists(root_dir) || !std::filesystem::is_directory(root_dir))
    {
        throw std::runtime_error("Directory does not exist: " + root_dir);
    }
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root_dir))
    {
        if (entry.is_regular_file())
        {
            files.push_back(entry.path().string());
        }
    }

    return files;
}
auto file_parse(const std::string &file_path)
{
    std::filesystem::path p(file_path);
    return std::make_pair(p.stem().string(), p.extension().string());
}

namespace net_unit
{
    void CURL_proxy(CURL *__handle__, const char *proxy, bool ssl = false)
    {
        curl_easy_setopt(__handle__, CURLOPT_PROXY, proxy);
        curl_easy_setopt(__handle__, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
        if (ssl)
        {
            curl_easy_setopt(__handle__, CURLOPT_HTTPPROXYTUNNEL, 1L);
        }
        curl_easy_setopt(__handle__, CURLOPT_PROXYUSERPWD, "user:password");
        curl_easy_setopt(__handle__, CURLOPT_PROXYAUTH, CURLAUTH_BASIC);
    }
    size_t CURL_WriteCallback(void *contents, size_t size, size_t nmemb, std::string &userp)
    {
        userp.append((char *)contents, size * nmemb);
        return size * nmemb;
    }
    bool CURL_get(CURL *__handle__, const char *URL, std::string &buf)
    {
        if (__handle__)
        {
            curl_easy_reset(__handle__);
            curl_easy_setopt(__handle__, CURLOPT_URL, URL);
            curl_easy_setopt(__handle__, CURLOPT_WRITEFUNCTION, CURL_WriteCallback);
            curl_easy_setopt(__handle__, CURLOPT_WRITEDATA, &buf);

            CURLcode res = curl_easy_perform(__handle__);
            if (res != CURLE_OK)
            {
                std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
                return false;
            }
            return true;
        }
        return false;
    }
    bool CURL_post(CURL *__handle__, const char *URL, const std::string &data, std::string &buf, std::string haeder = "")
    {
        if (__handle__)
        {
            curl_easy_reset(__handle__);
            curl_easy_setopt(__handle__, CURLOPT_URL, URL);

            curl_easy_setopt(__handle__, CURLOPT_POST, 1L);

            curl_easy_setopt(__handle__, CURLOPT_POSTFIELDS, data.c_str());
            curl_easy_setopt(__handle__, CURLOPT_POSTFIELDSIZE, data.size());

            struct curl_slist *headers = nullptr;
            if (haeder != "")
            {
                headers = curl_slist_append(headers, haeder.c_str());
                curl_easy_setopt(__handle__, CURLOPT_HTTPHEADER, headers);
            }

            curl_easy_setopt(__handle__, CURLOPT_WRITEFUNCTION, CURL_WriteCallback);
            curl_easy_setopt(__handle__, CURLOPT_WRITEDATA, &buf);

            CURLcode res = curl_easy_perform(__handle__);
            if (headers != nullptr)
            {
                curl_slist_free_all(headers);
            }

            if (res != CURLE_OK)
            {
                std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
                return false;
            }
            return true;
        }
        return false;
    }
    using StreamCallback = std::function<void(const char *, size_t)>;
    size_t WriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
        StreamCallback *callback = (StreamCallback *)userdata;
        try
        {
            (*callback)(ptr, size *nmemb);
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
        }
        return size * nmemb;
    }

    bool CURL_stream_post(
        CURL *curl,
        const char *url,
        const std::string &post_data,
        const std::string &header,
        StreamCallback on_token)
    {
        if (!curl || !url)
            return false;

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &on_token);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L); // 5min timeout
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        // 设置 headers
        struct curl_slist *header_list = nullptr;
        header_list = curl_slist_append(header_list, header.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

        CURLcode res = curl_easy_perform(curl);

        curl_slist_free_all(header_list);
        return (res == CURLE_OK);
    }
} // namespace net_unit
namespace tool_unit
{
    void appendFile(const std::string &path, const std::string &content)
    {
        std::cout << "INFO - appendFile('" << path << "')" << std::endl;
        std::ofstream file(path, std::ios::binary | std::ios::app);
        if (!file.is_open())
        {
            throw std::runtime_error("Fail to open/create file:'" + path + "'" + " Please check the true path");
        }
        file.write(content.data(), content.size());
        if (file.fail())
        {
            throw std::runtime_error("Fail to open/create file:'" + path + "'" + " Please check the true path");
        }
        file.close();
    }
    std::string exec(const std::string &cmd)
    {
        FILE *pipe = popen(cmd.c_str(), "r");
        if (!pipe)
        {
            return "popen failed";
        }

        std::string result;
        int ch;
        while ((ch = fgetc(pipe)) != EOF)
        {
            result += static_cast<char>(ch);
        }
        int code = pclose(pipe);
        printf("INFO - exec(\"%s\") exit-code: %d\n", cmd.c_str(), code);
        return result;
    }
    std::string readFile(const std::string &path)
    {
        std::cout << "INFO - readFile('" << path << "')" << std::endl;
        std::ifstream file(path, std::ios::binary | std::ios::in);
        if (!file)
            throw std::runtime_error("Error - No such file or directory in path:" + path + "Please check the true path");
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        file.close();
        return content;
    }
    void writeFile(const std::string &path, const std::string &content)
    {
        std::cout << "INFO - writeFile('" << path << "')" << std::endl;
        std::ofstream file(path, std::ios::binary | std::ios::out);
        if (!file.is_open())
        {
            throw std::runtime_error("Error - Could not create the file:" + path + " Please check the true path");
        }
        file.write(content.data(), content.size());
        if (file.fail())
        {
            throw std::runtime_error("Fail write to file:'" + path + "'" + " Please check the true path");
        }
        file.close();
    }
    std::string wget(const char *URL)
    {
        CURL *curl = curl_easy_init();
        std::string buf;
        net_unit::CURL_get(curl, URL, buf);
        return buf;
    }
    std::string Image(const std::string &path)
    {
        std::cout << "INFO - Image('" << path << "')" << std::endl;
        std::ifstream file(path, std::ios::binary | std::ios::in);
        if (!file)
            throw std::runtime_error("Error - No such file or directory in path:" + path + "Please check the true path");
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        file.close();
        auto base = base64::to_base64(content);
        return "data:image/jpeg;base64," + base;
    }
    std::list<std::string> image_queue;
}
namespace run_unit
{
    nlohmann::json settings;
    nlohmann::json tools_list;
    std::mutex ctx_lock;
    nlohmann::json Agent_session_context = nlohmann::json::array();
    size_t over_ctx = 0;

    struct SessionContext
    {
        nlohmann::json messages = nlohmann::json::array();
        nlohmann::json memory = {{"keywords", ""}, {"abstracts", ""}};
        bool loaded = false;
        bool thinking = false;
        std::string session_id;
        SessionContext()
        {
            session_id = std::to_string(std::time(nullptr));
        }
        SessionContext(const std::string &session_id) : session_id(session_id)
        {
        }
        bool is_memory_empty()
        {
            return memory["keywords"].get_ref<const std::string &>().empty() && memory["abstracts"].get_ref<const std::string &>().empty();
        }
        std::pair<std::string, std::string> summary_query()
        {
            std::time_t t = std::time(nullptr);
            std::string time = std::asctime(std::localtime(&t));
            std::string abstracts_query = "Summarize the following content, including the current Time, and output only the summary as a memory output:\nTime:" + time;
            std::string keywords_query = "Extract keywords from the following content, and output only the keywords:\n";
            for (auto &item : messages)
            {
                abstracts_query += item.get<std::string>() + "\n\n";
                keywords_query += item.get<std::string>() + "\n\n";
            }
            return {abstracts_query, keywords_query};
        }
    };

    class SessionManager
    {
    public:
        std::unordered_map<std::string, std::shared_ptr<SessionContext>> sessions;
        std::string current_session_id;
        std::string workspace = "";
        std::shared_ptr<SessionContext> get(const std::string &id)
        {
            auto it = sessions.find(id);
            if (it != sessions.end())
            {
                if (it->second->loaded == false)
                {
                    try
                    {
                        it->second->messages = nlohmann::json::parse(tool_unit::readFile(workspace + "/sessions/" + it->second->session_id + ".json"));
                        if (!it->second->messages.is_array())
                        {
                            it->second->messages.clear();
                            it->second->messages = nlohmann::json::array();
                            std::cout << "WARN - Session " << it->second->session_id << "file read but is not a array, reset to an empty array." << std::endl;
                        }
                    }
                    catch (const std::exception &e)
                    {
                        std::cerr << e.what() << '\n';
                    }
                    auto memory_path = workspace + "/memorys/" + it->second->session_id + ".json";
                    if (std::filesystem::exists(memory_path))
                    {
                        try
                        {
                            it->second->memory = nlohmann::json::parse(tool_unit::readFile(memory_path));
                            if (!it->second->memory.contains("abstracts") || !it->second->memory.contains("keywords"))
                            {
                                it->second->memory = {{"keywords", ""}, {"abstracts", ""}};
                                std::cout << "WARN - Session " << it->second->session_id << "file read but is not a valid memory file, reset to an empty memory." << std::endl;
                            }
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
            return session;
        }
        std::shared_ptr<SessionContext> get_current()
        {
            if (current_session_id == "")
            {
                return create();
            }
            return get(current_session_id);
        }

        std::vector<std::string> list_sessions() const
        {
            std::vector<std::string> result;
            for (const auto &pair : sessions)
            {
                result.push_back(pair.first);
            }
            return result;
        }
        void clear_current()
        {
            auto ses = get_current();
            ses->messages.clear();
            ses->memory.clear();
            ses->memory = {{"keywords", ""}, {"abstracts", ""}};
        }

        void remove_session(const std::string &id)
        {
            sessions.erase(id);
        }
        void change_session(const std::string &id)
        {
            if (current_session_id == id)
            {
                return;
            }
            if (!current_session_id.empty())
            {
                if (sessions[current_session_id]->loaded == true)
                {
                    tool_unit::writeFile(workspace + "/sessions/" + current_session_id + ".json", sessions[current_session_id]->messages.dump(4));
                    tool_unit::writeFile(workspace + "/memorys/" + current_session_id + ".json", sessions[current_session_id]->memory.dump(4));
                    sessions[current_session_id]->messages.clear();
                    sessions[current_session_id]->memory.clear();
                    sessions[current_session_id]->loaded = false;
                }
            }
            current_session_id = id;
        }
        SessionManager() = default;
        SessionManager(const std::string &workspace) : workspace(workspace)
        {
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
                        tool_unit::writeFile(workspace + "/sessions/" + ses.first + ".json", ses.second->messages.dump(4));
                        tool_unit::writeFile(workspace + "/memorys/" + ses.first + ".json", ses.second->memory.dump(4));
                    }
                    ses.second->memory.clear();
                    ses.second->messages.clear();
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << e.what() << '\n';
            }
        }
    };

    SessionManager agent_session_manager;
    int init_check(const std::string &settings_path)
    {
        settings = nlohmann::json::parse(tool_unit::readFile(settings_path));

        // 1. 检查必需的顶级字段
        if (!settings.contains("workspace"))
        {
            throw std::runtime_error("Error - missing 'workspace' in settings");
        }
        if (!settings.contains("channels"))
        {
            throw std::runtime_error("Error - missing 'channels' in settings");
        }
        if (!settings.contains("model"))
        {
            throw std::runtime_error("Error - missing 'model' in settings");
        }
        if (!settings.contains("prompt_path"))
        {
            throw std::runtime_error("Error - missing 'prompt_path' in settings");
        }

        std::filesystem::path workspace = settings["workspace"].get<std::string>();
        if (workspace.empty())
        {
            throw std::runtime_error("Error - workspace path is empty");
        }

        std::filesystem::create_directories(workspace / "sessions");
        std::filesystem::create_directories(workspace / "memorys");
        std::filesystem::create_directories(workspace / "assets");
        std::filesystem::create_directories(workspace / "tools");

        std::filesystem::path sysPath = workspace / "sys";
        if (!std::filesystem::exists(sysPath) || !std::filesystem::is_directory(sysPath))
        {
            throw std::runtime_error("Error - sys directory not found or is not a directory. Please check your workspace.");
        }

        for (auto &channel : settings["channels"])
        {
            if (!channel.contains("name") || channel["name"].get<std::string>().empty())
            {
                throw std::runtime_error("Error - channel missing 'name' or name is empty");
            }
            std::string channelName = channel["name"].get<std::string>();
            std::filesystem::path sessionFile = workspace / "sessions" / (channelName + ".json");
            if (!std::filesystem::exists(sessionFile))
            {
                tool_unit::writeFile(sessionFile.string(), "[]");
            }
        }

        std::string model = settings["model"].get<std::string>();
        if (model.empty())
        {
            throw std::runtime_error("Error - model is empty. Please check your settings.");
        }

        std::filesystem::path promptPath = settings["prompt_path"].get<std::string>();
        if (promptPath.empty())
        {
            throw std::runtime_error("Error - prompt_path is empty");
        }
        if (!std::filesystem::exists(promptPath) || !std::filesystem::is_regular_file(promptPath))
        {
            throw std::runtime_error("Error - prompt_path not found or is not a regular file. Please check your settings.");
        }
        if (!settings.contains("server_address") || settings["server_address"].get<std::string>().empty())
        {
            throw std::runtime_error("Error - missing or empty 'server_address' in settings");
        }
        std::filesystem::path webui = workspace / "webui.html";
        if (!std::filesystem::exists(webui))
        {
            throw std::runtime_error("Error - webui.html not found. Please check your workspace.");
        }
        else
        {
            settings["webui"] = webui.string();
        }
        std::filesystem::path tools_list_path = workspace / "tools/tools.json";
        if (!std::filesystem::exists(tools_list_path))
        {
            std::cout << "Warning - No tools.json found, creating a new one..." << std::endl;
            tool_unit::writeFile(tools_list_path.string(), "[]");
            tools_list = nlohmann::json::array();
        }
        else
        {
            tools_list = nlohmann::json::parse(tool_unit::readFile(tools_list_path.string()));
        }
        agent_session_manager = SessionManager(workspace.string());
        return 0;
    }
} // run_unit
namespace tool_unit
{
    bool tools_scan(std::string &context, std::string &data)
    {
        auto arr = extractAllTags(context, "tool");
        if (arr.size() < 1)
        {
            return false;
        }
        data += "\n````\n";
        bool flag = false;
        for (auto &ctx : arr)
        {
            auto [name, args] = parseArgs(ctx);
            if (name == "exec")
            {
                try
                {
                    data += exec(std::string(args));
                    data += "\n[TOOL_DONE]\n";
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                    flag = true;
                }
            }
            else if (name == "read")
            {
                try
                {
                    data += readFile(std::string(args));
                    data += "\n[TOOL_DONE]\n";
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                    flag = true;
                }
            }
            else if (name == "Image")
            {
                try
                {
                    image_queue.push_back(Image(std::string(args)));
                    data += "[Image has read done]";
                    data += "\n[TOOL_DONE]\n";
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                    flag = true;
                }
            }
            else if (name == "write")
            {
                try
                {
                    auto [file, content] = parseArgs(args, '|');
                    writeFile(std::string(file), std::string(content));
                    data += "\n[TOOL_DONE]\n";
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                    flag = true;
                }
            }
            else if (name == "wget")
            {
                try
                {
                    data += wget(std::string(args).c_str());
                    data += "\n[TOOL_DONE]\n";
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                    flag = true;
                }
            }
            else
            {
                try
                {
                    data += exec("cmd /c chcp 65001>nul && python.exe " + run_unit::settings["workspace"].get_ref<const std::string &>() + "/tools/" + std::string(name) + "/run.py" + " 2>&1 " + std::string(args));
                    data += "\n[TOOL_DONE]\n";
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                    flag = true;
                }
            }
        }
        if (flag)
        {
            data += "Did an error occur when calling the tool? Please check that you're using it correctly and that the parameters are correct!";
        }
        data += "\n````\n";
        return true;
    }
} // namespace tool_unit

namespace LLMProviders
{
    /**
     * @brief 专为本地 llama.cpp 服务器设计的客户端
     * @note  llama.cpp 默认端口通常是 8080，API 接口兼容 OpenAI 格式
     */
    class LlamaClient
    {
    private:
        std::string base_url_; // 例如: "http://localhost:8080"
        CURL *curl_;

    public:
        explicit LlamaClient(const std::string &base_url = "http://localhost:8080")
            : base_url_(base_url), curl_(curl_easy_init())
        {
        }

        ~LlamaClient()
        {
            if (curl_)
            {
                curl_easy_cleanup(curl_);
            }
        }
        bool unload_model(std::string &model)
        {
            std::string buf;
            std::string url = base_url_ + "/models/unload";

            nlohmann::json request = {{"model", model}};

            if (!net_unit::CURL_post(curl_, url.c_str(), request.dump(), buf, "Content-Type: application/json"))
            {
                return false;
            }
            try
            {
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        bool generate(const nlohmann::json &request, nlohmann::json &response)
        {
            std::string buf;
            std::string url = base_url_ + "/chat/completions";

            if (!net_unit::CURL_post(curl_, url.c_str(), request.dump(), buf, "Content-Type: application/json"))
            {
                return false;
            }
            try
            {
                response = nlohmann::json::parse(buf);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }
    };

    class OllamaClient
    {
    private:
        std::string base_url_; // 通常为 "http://localhost:11434"
        CURL *curl_;

    public:
        explicit OllamaClient() = default;
        explicit OllamaClient(const std::string &base_url = "http://localhost:11434")
            : base_url_(base_url), curl_(curl_easy_init()) {}

        ~OllamaClient()
        {
            if (curl_)
            {
                curl_easy_cleanup(curl_);
            }
        }

        /**
         * @brief 非流式生成：传入完整请求 JSON（必须包含 "model" 和 "prompt" 或其他合法字段）
         * @param request 完整的 Ollama /api/generate 请求体（JSON）
         * @param response 输出：完整的响应 JSON（包含 "response", "done", "context" 等）
         * @return 是否成功
         */
        bool generate(const nlohmann::json &request, nlohmann::json &response)
        {
            std::string buf;
            std::string url = base_url_ + "/api/generate";

            if (!net_unit::CURL_post(curl_, url.c_str(), request.dump(), buf, "Content-Type: application/json"))
            {
                return false;
            }
            try
            {
                response = nlohmann::json::parse(buf);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }
        bool stream_generate(const nlohmann::json &request, std::string &response, std::string &thinking)
        {
            std::string url = base_url_ + "/api/generate";
            std::vector<const char *> header_vec;
            header_vec.push_back("Content-Type: application/json");
            header_vec.push_back(nullptr);
            return net_unit::CURL_stream_post(curl_, url.c_str(), request.dump(), "Content-Type: application/json", [&](const char *ptr, size_t size)
                                              {
                                                  try
                                                  {
                                                      nlohmann::json j = nlohmann::json::parse(ptr, ptr + size);
                                                      if (j["done"] == true)
                                                      {
                                                          return;
                                                      }
                                                      if (j["response"] != nullptr)
                                                      {
                                                          response += j["response"];
                                                      }
                                                      if (j["thinking"] != nullptr)
                                                      {
                                                          thinking += j["thinking"];
                                                      }
                                                      return;
                                                  }
                                                  catch (const std::exception& e)
                                                  {
                                                      std::cerr << e.what() << '\n';
                                                      return;
                                                  } });
        }
        bool stream_chat(const nlohmann::json &request, std::string &response, std::string &thinking)
        {
            std::string url = base_url_ + "/api/chat";
            std::vector<const char *> header_vec;
            header_vec.push_back("Content-Type: application/json");
            header_vec.push_back(nullptr);
            return net_unit::CURL_stream_post(curl_, url.c_str(), request.dump(), "Content-Type: application/json", [&](const char *ptr, size_t size)
                                              {
                                                 try
                                                 {
                                                    nlohmann::json j = nlohmann::json::parse(ptr, ptr + size);
                                                 if (j["done"] == true)
                                                 {
                                                     return;
                                                 }
                                                 auto &msg = j["message"];
                                                 if (msg["content"] != nullptr)
                                                 {
                                                     response += msg["content"];
                                                 }
                                                 if (msg["thinking"] != nullptr)
                                                 {
                                                     thinking += msg["thinking"];
                                                 }
                                                 return; 
                                                 }
                                                 catch(const std::exception& e)
                                                 {
                                                    std::cerr << e.what() << '\n';
                                                    return;
                                                 } });
        }
    };
} // namespace LLMProviders

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
                 return tool_unit::exec("cmd /c chcp 65001>nul && python.exe " + run_unit::settings["workspace"].get_ref<const std::string &>() + "/sys/sys_state.py 2>&1");
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
                 return tool_unit::exec("cmd /c chcp 65001>nul && python.exe " + run_unit::settings["workspace"].get_ref<const std::string &>() + "/sys/sys_tools.py 2>&1");
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
                 std::string time = std::asctime(std::localtime(&t));
                 return time;
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
                 sscanf_s(args.data(), "%d", &seed);
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
                 auto load_sessions = get_all_files(run_unit::settings["workspace"].get_ref<const std::string &>() + "/memorys");
                 for (auto &session : load_sessions)
                 {
                     auto [name, ext] = file_parse(session);
                     if (ext == ".json")
                     {
                         nlohmann::json memory = nlohmann::json::parse(tool_unit::readFile(session));
                         if (memory.contains("abstracts") && memory.contains("keywords"))
                         {
                             keys += std::format("Session ID:{} {}", name, memory["keywords"].get_ref<const std::string &>()) + "\n\n";
                         }
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
                 auto load_sessions = get_all_files(run_unit::settings["workspace"].get_ref<const std::string &>() + "/memorys");

                 for (auto &session : load_sessions)
                 {
                     auto [name, ext] = file_parse(session);
                     if (ext == ".json")
                     {
                         if (args.find(name) != std::string::npos)
                         {
                             nlohmann::json memory = nlohmann::json::parse(tool_unit::readFile(session));
                             if (memory.contains("abstracts") && memory.contains("keywords"))
                             {
                                 query += std::format("SessionID:{} {}", name, memory["abstracts"].get_ref<const std::string &>()) + "\n\n";
                             }
                         }
                     }
                 }
                 return query;
             }
             catch (const std::exception &e)
             {
                 return e.what();
             }
         }}};
    /*
    The available commands are as follows:
        - Returns the current system status: system_status
        - Returns the status of all tools: tools_status
        - Ask your master to restart the system: restart
        - Returns the current date and time: time
        - Returns a random number by <seed> in [-1e9,1e9]: random:<seed>
    */
    bool cs_scan(std::string &context, std::string &data)
    {
        auto arr = extractAllTags(context, "cs");
        if (arr.size() < 1)
        {
            return false;
        }
        data += "````\n";
        for (auto &tag : arr)
        {
            auto [name, args] = parseArgs(tag);
            for (auto &cmd : command_map)
            {
                if (cmd.name == name)
                {
                    data += cmd.callback(args);
                    data += "\n[CS_DONE]\n";
                }
            }
        }
        data += "\n````\n";
        return true;
    }
} // namespace cs_unit

#endif //!__AGENT__H__