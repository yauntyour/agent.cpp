#pragma once
#ifndef AGENTCPP_AGENT_FS_TOOLS_HPP
#define AGENTCPP_AGENT_FS_TOOLS_HPP

#include "core/common.hpp"
#include "core/text.hpp"
#include "core/base64.hpp"
#include "net/http_client.hpp"

// 文件使用追踪由会话层提供；此处只做前向声明，避免循环依赖
namespace run_unit
{
    void mark_file_used(const std::string &path);
}

namespace tool_unit
{
    inline std::string exec(const std::string &cmd)
    {
        FILE *pipe = popen(cmd.c_str(), "r");
        if (!pipe)
            return "popen failed";

        std::string result;
        int ch;
        while ((ch = fgetc(pipe)) != EOF)
            result += static_cast<char>(ch);
        int code = pclose(pipe);
        printf("INFO - exec(\"%s\") exit-code: %d\n", cmd.c_str(), code);
        return result;
    }

    inline std::string readFile(const std::string &path)
    {
        std::cout << "INFO - readFile('" << path << "')" << std::endl;
        run_unit::mark_file_used(path);
        std::ifstream file(path, std::ios::binary | std::ios::in);
        if (!file)
            throw std::runtime_error("Error - No such file or directory in path:" + path + " Please check the true path");
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        file.close();
        return content;
    }

    inline void writeFile(const std::string &path, const std::string &content)
    {
        std::cout << "INFO - writeFile('" << path << "')" << std::endl;
        run_unit::mark_file_used(path);
        std::ofstream file(path, std::ios::binary | std::ios::out);
        if (!file.is_open())
            throw std::runtime_error("Error - Could not create the file:" + path + " Please check the true path");
        file.write(content.data(), content.size());
        if (file.fail())
            throw std::runtime_error("Fail write to file:'" + path + "'" + " Please check the true path");
        file.close();
    }

    inline std::string wget(const char *URL)
    {
        CURL *curl = curl_easy_init();
        std::string buf;
        net_unit::CURL_get(curl, URL, buf);
        return buf;
    }

    inline std::string Image(const std::string &path)
    {
        std::cout << "INFO - Image('" << path << "')" << std::endl;
        run_unit::mark_file_used(path);
        std::ifstream file(path, std::ios::binary | std::ios::in);
        if (!file)
            throw std::runtime_error("Error - No such file or directory in path:" + path + " Please check the true path");
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        file.close();
        auto base = base64::to_base64(content);
        return "data:image/jpeg;base64," + base;
    }

    inline std::list<std::string> image_queue;

    inline void editFile(const std::string &path, const std::string &args)
    {
        std::string content = readFile(path);
        std::vector<std::string> lines;
        std::istringstream iss(content);
        std::string line;
        while (std::getline(iss, line))
            lines.push_back(line);

        auto tokens = splitEscaped(args, '|');
        if (tokens.empty())
            throw std::runtime_error("edit: missing operation");

        const std::string &op = tokens[0];

        if (op == "replace_line")
        {
            if (tokens.size() < 3)
                throw std::runtime_error("edit replace_line: need line_number and new_content");
            size_t lineNum = std::stoul(tokens[1]);
            if (lineNum == 0 || lineNum > lines.size())
                throw std::runtime_error("edit replace_line: line number out of range");
            lines[lineNum - 1] = tokens[2];
        }
        else if (op == "insert_after")
        {
            if (tokens.size() < 3)
                throw std::runtime_error("edit insert_after: need line_number and content");
            size_t lineNum = std::stoul(tokens[1]);
            if (lineNum > lines.size())
                throw std::runtime_error("edit insert_after: line number out of range");
            lines.insert(lines.begin() + lineNum, tokens[2]);
        }
        else if (op == "delete_line")
        {
            if (tokens.size() < 2)
                throw std::runtime_error("edit delete_line: need line_number");
            size_t lineNum = std::stoul(tokens[1]);
            if (lineNum == 0 || lineNum > lines.size())
                throw std::runtime_error("edit delete_line: line number out of range");
            lines.erase(lines.begin() + lineNum - 1);
        }
        else if (op == "append")
        {
            if (tokens.size() < 2)
                throw std::runtime_error("edit append: need content");
            lines.push_back(tokens[1]);
        }
        else if (op == "prepend")
        {
            if (tokens.size() < 2)
                throw std::runtime_error("edit prepend: need content");
            lines.insert(lines.begin(), tokens[1]);
        }
        else if (op == "replace")
        {
            if (tokens.size() < 3)
                throw std::runtime_error("edit replace: need old_text and new_text");
            std::string oldStr = tokens[1];
            std::string newStr = tokens[2];
            for (auto &l : lines)
            {
                size_t pos = 0;
                while ((pos = l.find(oldStr, pos)) != std::string::npos)
                {
                    l.replace(pos, oldStr.length(), newStr);
                    pos += newStr.length();
                }
            }
        }
        else
        {
            throw std::runtime_error("edit: unknown operation '" + op + "'");
        }

        std::ostringstream oss;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            oss << lines[i];
            if (i != lines.size() - 1)
                oss << '\n';
        }
        writeFile(path, oss.str());
    }
} // namespace tool_unit

#endif // AGENTCPP_AGENT_FS_TOOLS_HPP
