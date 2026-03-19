#pragma once
#ifndef __AGENT__H__
#define __AGENT__H__
#include <iostream>
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

// 提取所有 <tag>...<\tag> 内容的线性迭代器
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
            throw "Fail to open/create file:'" + path + "'";
        }
        file.write(content.data(), content.size());
        if (file.fail())
        {
            throw "Fail append to file:'" + path + "'";
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
            throw "Error: No such file or directory in path:" + path;
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
            throw "Error: Could not create the file:" + path;
        }
        file.write(content.data(), content.size());
        if (file.fail())
        {
            throw "Fail write to file:'" + path + "'";
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
            throw "Error: No such file or directory in path:" + path;
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        file.close();
        auto base = base64::to_base64(content);
        return base;
    }
    std::list<std::string> image_queue;
    bool tools_scan(std::string &context, std::string &data)
    {
        auto arr = extractAllTags(context, "tool");
        if (arr.size() < 1)
        {
            return false;
        }
        data += "\n```\n";
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
                }
            }
            else
            {
                try
                {
                    data += exec("cmd /c chcp 65001>nul && python.exe ./sys/sys_tools.py 2>&1" + std::string(ctx));
                    data += "\n[TOOL_DONE]\n";
                }
                catch (const std::exception &e)
                {
                    data += e.what();
                    data += "\n[TOOL_ERR]\n";
                }
            }
        }
        data += "\n```\n";
        return true;
    }
} // namespace tool_unit

namespace LLMProviders
{
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
                 return tool_unit::exec("cmd /c chcp 65001>nul && python.exe ./sys/sys_state.py 2>&1");
             }
             catch (const std::exception &e)
             {
                 return e.what();
             }
         }},
        {"tools_status", [](std::string_view args) -> std::string
         {
             try
             {
                 return tool_unit::exec("cmd /c chcp 65001>nul && python.exe ./sys/sys_tools.py 2>&1");
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
    };
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
        data += "```\n";
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
        data += "\n```\n";
        return true;
    }
} // namespace cs_unit

#endif //!__AGENT__H__