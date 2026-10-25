#pragma once
#ifndef __ROUTER__H__
#define __ROUTER__H__

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <memory>
#include <map>

namespace rt
{
    enum service_state
    {
        FLAG_DONE = 0,
        FLAG_ERROR,
        FLAG_WARN
    };

    // 修改回调函数类型，增加 params 参数用于接收动态路由参数
    using HandlerFunc = std::function<int(std::string &, std::string &, const std::map<std::string, std::string> &)>;

    // 流式传输支持
    using WriteCallback = std::function<void(const std::string &)>;
    using StreamHandlerFunc = std::function<void(std::string &, WriteCallback, const std::map<std::string, std::string> &)>;

    int default_func(std::string &url, std::string &data, const std::map<std::string, std::string> &params)
    {
        data = "<null>";
        return FLAG_DONE;
    }

    struct node
    {
        std::string name = "";
        bool is_dynamic = false; // 标记是否为动态节点 (例如 :id)
        HandlerFunc func = default_func;
        std::unordered_map<std::string, std::shared_ptr<node>> list;

        node() = default;
        node(std::string &name, bool dynamic, HandlerFunc func = default_func)
            : name(name), is_dynamic(dynamic), func(func) {}

        ~node()
        {
            list.clear();
        }
    };

    class router
    {
    private:
        std::shared_ptr<node> base = std::shared_ptr<node>(new node());
        std::unordered_map<std::string, std::shared_ptr<node>> list;
        std::unordered_map<std::string, StreamHandlerFunc> stream_handlers_;

        void fix_url(std::string &url)
        {
            if (!url.empty())
            {
                std::string result = "";
                char end = 0;
                for (size_t i = 0; i < url.length(); i++)
                {
                    if (url[i] == '/' && end == '/')
                    {
                        continue;
                    }
                    result += url[i];
                    end = url[i];
                }
                url = result;
                if (url != "/")
                {
                    if (url[0] == '/')
                        url = url.substr(1, url.length() - 1);
                    if (url[url.length() - 1] == '/')
                        url = url.substr(0, url.length() - 1);
                }
                else
                {
                    url = "";
                }
            }
        }

        // 递归查找匹配节点，同时收集参数
        std::shared_ptr<node> find_match(std::unordered_map<std::string, std::shared_ptr<node>> &current_list,
                                         const std::vector<std::string> &parts,
                                         size_t index,
                                         std::map<std::string, std::string> &params)
        {
            if (index >= parts.size())
            {
                // 路径遍历结束，返回当前节点（如果是根节点需特殊处理，这里简化逻辑）
                return nullptr;
            }

            std::string current_part = parts[index];
            bool is_last = (index == parts.size() - 1);

            // 1. 尝试精确匹配静态节点
            auto it = current_list.find(current_part);
            if (it != current_list.end())
            {
                if (is_last)
                {
                    return it->second; // 找到最终节点
                }
                else
                {
                    return find_match(it->second->list, parts, index + 1, params);
                }
            }

            // 2. 如果没有静态匹配，尝试匹配动态节点 (is_dynamic == true)
            for (auto &pair : current_list)
            {
                if (pair.second->is_dynamic)
                {
                    // 将路径部分存入参数表，key 为节点名 (去掉可能的 ':' 前缀)
                    std::string param_key = pair.second->name;
                    if (param_key.front() == ':')
                        param_key = param_key.substr(1);

                    params[param_key] = current_part;

                    if (is_last)
                    {
                        return pair.second; // 找到最终动态节点
                    }
                    else
                    {
                        auto res = find_match(pair.second->list, parts, index + 1, params);
                        if (res)
                            return res; // 如果后续路径也匹配成功
                        // 如果后续不匹配，回溯（此处简单实现直接返回，复杂场景需移除参数）
                        params.erase(param_key);
                    }
                }
            }

            return nullptr;
        }

    public:
        router() = default;
        ~router() { list.clear(); }

        // 注册路由，支持 ":param" 格式
        void on(std::string url, HandlerFunc func = default_func)
        {
            fix_url(url);
            std::vector<std::string> parts;
            split_on(url, '/', [&](std::string part, size_t i)
                     { parts.push_back(part); });

            auto *temp = &list;

            for (size_t i = 0; i < parts.size(); ++i)
            {
                std::string part = parts[i];
                bool is_dynamic = (part.length() > 0 && part[0] == ':');

                auto it = temp->find(part);
                if (it != temp->end())
                {
                    // 节点已存在
                    if (i == parts.size() - 1)
                    {
                        it->second->func = func;
                        it->second->is_dynamic = is_dynamic; // 更新动态标记
                    }
                    temp = &(it->second->list);
                }
                else
                {
                    // 创建新节点
                    std::shared_ptr<node> new_node = std::make_shared<node>(part, is_dynamic, (i == parts.size() - 1) ? func : default_func);
                    (*temp)[part] = new_node;
                    temp = &(new_node->list);
                }
            }
            if (parts.size() == 1 && parts[0] == "")
            {
                base->func = func;
            }
        }

        // 注册流式路由
        void on_stream(std::string url, StreamHandlerFunc func)
        {
            fix_url(url);
            stream_handlers_[url] = std::move(func);
        }

        // 检查是否存在流式路由处理器
        bool has_stream_handler(const std::string &url)
        {
            std::string fixed = url;
            fix_url(fixed);
            return stream_handlers_.find(fixed) != stream_handlers_.end();
        }

        // 获取流式路由处理器
        StreamHandlerFunc get_stream_handler(const std::string &url)
        {
            std::string fixed = url;
            fix_url(fixed);
            auto it = stream_handlers_.find(fixed);
            if (it != stream_handlers_.end())
                return it->second;
            return nullptr;
        }

        // 获取匹配结果及参数
        std::pair<std::weak_ptr<node>, std::map<std::string, std::string>> get(std::string url)
        {
            fix_url(url);
            std::map<std::string, std::string> params;

            if (url == "")
            {
                return {std::weak_ptr<node>(base), params};
            }

            std::vector<std::string> parts;
            split_on(url, '/', [&](std::string part, size_t i)
                     { parts.push_back(part); });

            auto matched_node = find_match(list, parts, 0, params);

            if (matched_node)
            {
                return {std::weak_ptr<node>(matched_node), params};
            }

            return {std::weak_ptr<node>(), params};
        }

        void split_on(std::string &str, char d, std::function<void(std::string, size_t)> cb)
        {
            size_t start = 0;
            for (size_t i = 0; i <= str.size(); ++i)
            {
                if (i == str.size() || str[i] == d)
                {
                    cb(str.substr(start, i - start), i);
                    start = i + 1;
                }
            }
        }
    };
}

#endif