#include "components/tools/tools.hpp"
#include "components/permission/permission.hpp"
#include "components/service/service.hpp"
#include "components/memory/memory.hpp"
#include "components/agent/agent.hpp"
#include "components/notice/notice.hpp"
#include "components/mcp/mcp.hpp"
#include "core/exception.hpp"
#include "utils/fs.hpp"
#include "utils/crypto.hpp"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace agent {

using json = nlohmann::json;

void Tools::on_initialize() {
    register_builtin_tools();
}

void Tools::on_shutdown() {}

void Tools::register_tool(const ToolInfo& info, ToolExecutor executor) {
    m_tool_info[info.name] = info;
    m_executors[info.name] = std::move(executor);
}

void Tools::unregister_tool(std::string_view name) {
    m_tool_info.erase(std::string(name));
    m_executors.erase(std::string(name));
}

bool Tools::has_tool(std::string_view name) const {
    return m_executors.contains(std::string(name));
}

std::vector<ToolInfo> Tools::list_tools() const {
    std::vector<ToolInfo> result;
    for (auto& [name, info] : m_tool_info) {
        result.push_back(info);
    }
    return result;
}

std::vector<ToolDefinition> Tools::get_definitions() const {
    std::vector<ToolDefinition> defs;
    for (auto& [name, info] : m_tool_info) {
        ToolDefinition td;
        td.name = info.name;
        td.description = info.description;
        defs.push_back(td);
    }
    return defs;
}

ToolResult Tools::execute(const ToolCall& call) {
    auto it = m_executors.find(call.name);
    if (it == m_executors.end()) {
        return {call.id, "", true, "Unknown tool: " + call.name};
    }
    return it->second(call);
}

std::vector<ToolResult> Tools::execute_batch(const std::vector<ToolCall>& calls) {
    std::vector<ToolResult> results;
    results.reserve(calls.size());
    for (auto& call : calls) {
        results.push_back(execute(call));
    }
    return results;
}

ToolResult Tools::execute_async(const ToolCall& call) {
    return execute(call);
}

void Tools::add_mcp_tools(std::string_view server_id, const std::vector<ToolInfo>& tools) {
    std::vector<std::string> names;
    for (auto& t : tools) {
        register_tool(t, [server_id = std::string(server_id), tool_name = t.name](const ToolCall& call) -> ToolResult {
            auto* mcp = ModuleRegistry::instance().get<MCP>();
            if (!mcp) return {call.id, "", true, "MCP component not available"};

            auto result = mcp->call_tool(server_id, tool_name, call.arguments);
            if (result.contains("error")) {
                std::string err = result["error"].is_string()
                    ? result["error"].get<std::string>()
                    : result["error"].dump();
                return {call.id, "", true, err};
            }

            std::string content;
            if (result.contains("content")) {
                for (auto& c : result["content"]) {
                    if (c.contains("text")) content += c["text"].get<std::string>();
                }
            } else {
                content = result.dump(2);
            }
            return {call.id, content, false, ""};
        });
        names.push_back(t.name);
    }
    m_mcp_tools[std::string(server_id)] = names;
}

void Tools::remove_mcp_tools(std::string_view server_id) {
    auto it = m_mcp_tools.find(std::string(server_id));
    if (it != m_mcp_tools.end()) {
        for (auto& name : it->second) {
            unregister_tool(name);
        }
        m_mcp_tools.erase(it);
    }
}

// ── Built-in tool registration ────────────────────────────────────
void Tools::register_builtin_tools() {
    register_tool({"read", "Read a file's contents from the local filesystem.",
                   {"file", "io"}}, std::bind(&Tools::tool_read, this, std::placeholders::_1));

    register_tool({"write", "Write or overwrite a file at the given path with the given content.",
                   {"file", "io"}}, std::bind(&Tools::tool_write, this, std::placeholders::_1));

    register_tool({"edit", "Edit a file by specifying start/end line numbers and new content, or by string replacement.",
                   {"file", "io"}}, std::bind(&Tools::tool_edit, this, std::placeholders::_1));

    register_tool({"search", "Search file contents or file names using regex, glob patterns, or plain text. Auto-respects .gitignore.",
                   {"file", "search"}}, std::bind(&Tools::tool_search, this, std::placeholders::_1));

    register_tool({"exec", "Run a shell command in the terminal. Subject to permission checks for dangerous commands.",
                   {"system", "io"}}, std::bind(&Tools::tool_exec, this, std::placeholders::_1));

    register_tool({"task", "Start a background or foreground service process. Background processes are cleaned up when agent exits.",
                   {"system", "service"}}, std::bind(&Tools::tool_task, this, std::placeholders::_1));

    register_tool({"question", "Ask the user a question when more information is needed.",
                   {"interaction"}}, std::bind(&Tools::tool_question, this, std::placeholders::_1));

    register_tool({"websearch", "Search the web using configurable search engines (Bing, Google). Supports proxy, chaining, and auto-detection of URL vs keyword search.",
                   {"web", "search"}}, std::bind(&Tools::tool_websearch, this, std::placeholders::_1));

    register_tool({"webfetch", "Download content from a URL. Supports proxy, custom headers, and resumable downloads.",
                   {"web", "io"}}, std::bind(&Tools::tool_webfetch, this, std::placeholders::_1));

    register_tool({"mind-map", "Draw an ASCII-style mind map for project planning and visualization.",
                   {"planning"}}, std::bind(&Tools::tool_mind_map, this, std::placeholders::_1));

    register_tool({"todolist", "Manage a structured task list. Supports create, update, delete, and list operations.",
                   {"planning"}}, std::bind(&Tools::tool_todolist, this, std::placeholders::_1));

    register_tool({"memory", "Save, search, and retrieve persistent memory entries across sessions.",
                   {"memory"}}, std::bind(&Tools::tool_memory, this, std::placeholders::_1));

    register_tool({"image", "Read and analyze image files. Requires model with vision support or a vision-to-text bridge model.",
                   {"file", "media"}}, std::bind(&Tools::tool_image, this, std::placeholders::_1));

    register_tool({"fs", "File system operations: list, create, delete, move, copy files and directories. Sandboxed to workspace.",
                   {"file", "io"}}, std::bind(&Tools::tool_fs, this, std::placeholders::_1));

    register_tool({"subagent", "Launch a sub-agent to handle complex tasks autonomously (explorer, sub-agent, background task).",
                   {"agent"}}, std::bind(&Tools::tool_subagent, this, std::placeholders::_1));

    register_tool({"git-saved", "Create a save point by committing current changes to the git repository.",
                   {"git", "version-control"}}, std::bind(&Tools::tool_git_saved, this, std::placeholders::_1));

    register_tool({"git-restore", "Restore the working directory to a previous git save point.",
                   {"git", "version-control"}}, std::bind(&Tools::tool_git_restore, this, std::placeholders::_1));
}

// ── Tool: read ────────────────────────────────────────────────────
ToolResult Tools::tool_read(const ToolCall& call) {
    TOOL_CATCH_BEGIN(call)
        auto file_path = call.arguments.value("filePath", "");
        auto offset = call.arguments.value("offset", 0);
        auto limit = call.arguments.value("limit", 2000);

        if (file_path.empty()) return {call.id, "", true, "filePath is required"};

        auto content = fsutil::read_file(file_path);
        std::stringstream ss(content);
        std::string line;
        std::string result;
        int current_line = 0;
        int start_line = offset > 0 ? offset : 1;
        int lines_read = 0;

        while (std::getline(ss, line)) {
            current_line++;
            if (current_line >= start_line) {
                result += std::to_string(current_line) + ": " + line + "\n";
                lines_read++;
                if (limit > 0 && lines_read >= limit) break;
            }
        }

        return {call.id, result.empty() ? content : result, false, ""};
    TOOL_CATCH_END(call)
}

// ── Tool: write ───────────────────────────────────────────────────
ToolResult Tools::tool_write(const ToolCall& call) {
    TOOL_CATCH_BEGIN(call)
        auto file_path = call.arguments.value("filePath", "");
        auto content = call.arguments.value("content", "");

        if (file_path.empty()) return {call.id, "", true, "filePath is required"};

        fsutil::write_file(file_path, content);
        LOG_INFO("Tools", "File written: " + file_path);
        return {call.id, "File written successfully: " + file_path, false, ""};
    TOOL_CATCH_END(call)
}

// ── Tool: edit ────────────────────────────────────────────────────
ToolResult Tools::tool_edit(const ToolCall& call) {
    TOOL_CATCH_BEGIN(call)
        auto file_path = call.arguments.value("filePath", "");
        if (file_path.empty()) return {call.id, "", true, "filePath is required"};

        if (call.arguments.contains("oldString") && call.arguments.contains("newString")) {
            auto old_str = call.arguments["oldString"].get<std::string>();
            auto new_str = call.arguments["newString"].get<std::string>();
            fsutil::edit_file_replace(file_path, old_str, new_str);

            size_t count = 0;
            auto content = fsutil::read_file(file_path);
            size_t pos = 0;
            while ((pos = content.find(old_str, pos)) != std::string::npos) { count++; pos++; }
            LOG_INFO("Tools", "Replaced " + std::to_string(count) + " occurrences in " + file_path);
            return {call.id, "Replaced " + std::to_string(count) + " occurrences in " + file_path, false, ""};
        } else if (call.arguments.contains("startLine") && call.arguments.contains("endLine")) {
            auto start_line = call.arguments["startLine"].get<size_t>();
            auto end_line = call.arguments["endLine"].get<size_t>();
            auto new_content = call.arguments.value("content", "");
            fsutil::edit_file(file_path, start_line, end_line, new_content);
            LOG_INFO("Tools", "Lines " + std::to_string(start_line) + "-" + std::to_string(end_line) + " replaced in " + file_path);
            return {call.id, "Lines " + std::to_string(start_line) + "-" + std::to_string(end_line) + " replaced in " + file_path, false, ""};
        } else {
            return {call.id, "", true, "Use oldString/newString for replacement or startLine/endLine with content for line-range edit"};
        }
    TOOL_CATCH_END(call)
}

// ── Tool: search ──────────────────────────────────────────────────
ToolResult Tools::tool_search(const ToolCall& call) {
    TOOL_CATCH_BEGIN(call)
        auto pattern = call.arguments.value("pattern", "");
        auto regex_mode = call.arguments.value("regex", false);
        auto case_sensitive = call.arguments.value("caseSensitive", true);
        auto recursive = call.arguments.value("recursive", true);
        auto search_path = call.arguments.value("path", ".");

        if (pattern.empty()) return {call.id, "", true, "pattern is required"};

        fsutil::SearchOptions opts;
        opts.pattern = pattern;
        opts.regex_mode = regex_mode;
        opts.case_sensitive = case_sensitive;
        opts.recursive = recursive;
        opts.search_path = search_path;
        opts.respect_gitignore = true;

        if (call.arguments.contains("include")) {
            auto inc = call.arguments["include"];
            if (inc.is_array()) for (auto& i : inc) opts.include_patterns.push_back(i.get<std::string>());
            else opts.include_patterns.push_back(inc.get<std::string>());
        }

        if (call.arguments.contains("exclude")) {
            auto exc = call.arguments["exclude"];
            if (exc.is_array()) for (auto& e : exc) opts.exclude_patterns.push_back(e.get<std::string>());
            else opts.exclude_patterns.push_back(exc.get<std::string>());
        }

        // Check if it's a file name search (glob mode)
        bool file_search = call.arguments.value("fileSearch", false);
        if (file_search) {
            auto files = fsutil::glob_search(pattern, search_path);
            std::string result;
            for (auto& f : files) {
                result += f.string() + "\n";
            }
            LOG_DEBUG("Tools", "File search found " + std::to_string(files.size()) + " results");
            return {call.id, result.empty() ? "No files found" : result, false, ""};
        }

        auto results = fsutil::search_content(opts);
        std::string output;
        for (auto& r : results) {
            output += r.file_path.string() + ":" + std::to_string(r.line_number) + ": " + r.line_content + "\n";
        }
        LOG_DEBUG("Tools", "Content search found " + std::to_string(results.size()) + " matches");
        return {call.id, output.empty() ? "No matches found" : output, false, ""};
    TOOL_CATCH_END(call)
}

// ── Tool: exec ────────────────────────────────────────────────────
ToolResult Tools::tool_exec(const ToolCall& call) {
    TOOL_CATCH_BEGIN(call)
        auto command = call.arguments.value("command", "");
        auto workdir = call.arguments.value("workdir", ".");

        if (command.empty()) return {call.id, "", true, "command is required"};

        // Permission check for dangerous commands
        auto& perm = ModuleRegistry::instance().require<Permission>();
        auto check = perm.check_command(command);
        if (!check.is_safe) {
            LOG_WARN("Tools", "Command blocked: " + command);
            return {call.id, "", true, "Command blocked: " + check.warning +
                    ". Dangerous patterns: " + [&]() {
                        std::string s;
                        for (auto& p : check.dangerous_patterns_found) s += p + " ";
                        return s;
                    }()};
        }

        LOG_DEBUG("Tools", "Executing command: " + command);

        std::string full_cmd;
#ifdef _WIN32
        full_cmd = "cd /d \"" + workdir + "\" && " + command + " 2>&1";
#else
        full_cmd = "cd \"" + workdir + "\" && " + command + " 2>&1";
#endif

        FILE* pipe = _popen(full_cmd.c_str(), "r");
        if (!pipe) return {call.id, "", true, "Failed to execute command"};

        std::string output;
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            output += buffer;
        }
        int rc = _pclose(pipe);

        if (rc != 0) {
            LOG_WARN("Tools", "Command exited with code " + std::to_string(rc) + ": " + command);
        }

        return {call.id, output.empty() ? "(no output)" : output, false,
                rc != 0 ? "Exit code: " + std::to_string(rc) : ""};
    TOOL_CATCH_END(call)
}

// ── Tool: task ────────────────────────────────────────────────────
ToolResult Tools::tool_task(const ToolCall& call) {
    TOOL_CATCH_BEGIN(call)
        auto name = call.arguments.value("name", "background-task");
        auto command = call.arguments.value("command", "");
        auto background = call.arguments.value("background", false);

        if (command.empty()) return {call.id, "", true, "command is required"};

        auto& svc = ModuleRegistry::instance().require<Service>();
        auto task_id = svc.start_task(name, command, background);
        LOG_INFO("Tools", "Task started: " + name + " (ID: " + task_id + ") in " +
                (background ? "background" : "foreground") + " mode");
        return {call.id, "Task started: " + name + " (ID: " + task_id + ") in " +
                (background ? "background" : "foreground") + " mode", false, ""};
    TOOL_CATCH_END(call)
}

// ── Tool: question ────────────────────────────────────────────────
ToolResult Tools::tool_question(const ToolCall& call) {
    auto question_text = call.arguments.value("question", "");
    if (question_text.empty()) return {call.id, "", true, "question is required"};

    auto& notice = ModuleRegistry::instance().require<Notice>();
    notice.info("Agent Question", question_text);

    return {call.id, "Question asked: " + question_text, false, ""};
}

// ── Tool: websearch ───────────────────────────────────────────────
ToolResult Tools::tool_websearch(const ToolCall& call) {
    TOOL_CATCH_BEGIN(call)
        auto query = call.arguments.value("query", "");
        auto engine = call.arguments.value("engine", "bing");
        auto count = call.arguments.value("count", 10);

        if (query.empty()) return {call.id, "", true, "query is required"};

        auto& cfg = Config::instance();
        auto& client = http::HttpClient::instance();

        std::string url;
        std::map<std::string, std::string> headers;

        if (engine == "google" && !cfg.google_api_key.empty() && !cfg.google_cse_id.empty()) {
            http::URLBuilder ub("https://www.googleapis.com/customsearch/v1");
            ub.add_query("key", cfg.google_api_key);
            ub.add_query("cx", cfg.google_cse_id);
            ub.add_query("q", query);
            ub.add_query("num", std::to_string(count));
            url = ub.build();
        } else {
            http::URLBuilder ub("https://api.bing.microsoft.com/v7.0/search");
            ub.add_query("q", query);
            ub.add_query("count", std::to_string(count));
            url = ub.build();
            headers["Ocp-Apim-Subscription-Key"] = cfg.bing_api_key;
        }

        if (!cfg.websearch_proxy.empty()) {
            client.set_default_proxy(cfg.websearch_proxy);
        }

        LOG_DEBUG("Tools", "Searching: " + query + " (engine: " + engine + ")");

        auto resp = client.get(url, headers);
        if (!resp.ok()) {
            LOG_ERROR("Tools", "Search failed: " + resp.error + " (HTTP " + std::to_string(resp.status_code) + ")");
            return {call.id, "", true, "Search failed: " + resp.error};
        }

        auto j = json::parse(resp.body);

        // Extract and format results
        std::string output = "Search results for: " + query + "\n\n";
        int idx = 1;

        auto extractResults = [&](const json& items) {
            for (auto& item : items) {
                if (idx > count) break;
                output += std::to_string(idx++) + ". ";
                if (item.contains("name")) output += item["name"].get<std::string>() + "\n";
                else if (item.contains("title")) output += item["title"].get<std::string>() + "\n";

                if (item.contains("url")) output += "   URL: " + item["url"].get<std::string>() + "\n";
                else if (item.contains("link")) output += "   URL: " + item["link"].get<std::string>() + "\n";

                if (item.contains("snippet")) output += "   " + item["snippet"].get<std::string>() + "\n";
                else if (item.contains("description")) output += "   " + item["description"].get<std::string>() + "\n";
                output += "\n";
            }
        };

        if (engine == "google" && j.contains("items")) {
            extractResults(j["items"]);
        } else if (j.contains("webPages") && j["webPages"].contains("value")) {
            extractResults(j["webPages"]["value"]);
        } else if (j.contains("value")) {
            extractResults(j["value"]);
        }

        LOG_INFO("Tools", "Search completed: " + std::to_string(idx - 1) + " results found");
        return {call.id, output, false, ""};
    TOOL_CATCH_END(call)
}

// ── Tool: webfetch ────────────────────────────────────────────────
ToolResult Tools::tool_webfetch(const ToolCall& call) {
    TOOL_CATCH_BEGIN(call)
        auto url = call.arguments.value("url", "");
        auto output_file = call.arguments.value("output", "");
        auto resume = call.arguments.value("resume", false);

        if (url.empty()) return {call.id, "", true, "url is required"};

        auto& cfg = Config::instance();
        auto& client = http::HttpClient::instance();

        if (!cfg.webfetch_proxy.empty()) client.set_default_proxy(cfg.webfetch_proxy);

        LOG_DEBUG("Tools", "Fetching URL: " + url);

        if (!output_file.empty()) {
            // Check for existing partial download
            int64_t resume_from = -1;
            if (resume && fs::exists(output_file)) {
                resume_from = fs::file_size(output_file);
            }
            client.download(url, output_file, resume_from);
            auto size = fs::file_size(output_file);
            LOG_INFO("Tools", "Downloaded " + std::to_string(size) + " bytes to " + output_file);
            return {call.id, "Downloaded " + std::to_string(size) + " bytes to " + output_file, false, ""};
        }

        auto resp = client.get(url);
        if (!resp.ok()) {
            LOG_ERROR("Tools", "Failed to fetch URL: " + resp.error + " (HTTP " + std::to_string(resp.status_code) + ")");
            return {call.id, "", true, "Failed to fetch: " + resp.error};
        }

        LOG_INFO("Tools", "Fetched " + std::to_string(resp.body.size()) + " bytes from " + url);
        return {call.id, resp.body, false, ""};
    TOOL_CATCH_END(call)
}

// ── Tool: mind-map ────────────────────────────────────────────────
ToolResult Tools::tool_mind_map(const ToolCall& call) {
    TOOL_CATCH_BEGIN(call)
        auto title = call.arguments.value("title", "Mind Map");

        json nodes;
        if (call.arguments.contains("nodes") && call.arguments["nodes"].is_string()) {
            nodes = json::parse(call.arguments["nodes"].get<std::string>());
        } else if (call.arguments.contains("nodes")) {
            nodes = call.arguments["nodes"];
        } else {
            nodes = json::array();
        }

        std::string result;
        result += title + "\n";
        result += std::string(title.size(), '=') + "\n\n";

        std::function<void(const json&, int, std::string)> render = [&](const json& n, int depth, std::string prefix) {
            if (n.is_object()) {
                std::string name = n.value("name", n.value("title", ""));
                auto children = n.value("children", json::array());
                result += prefix + (depth > 0 ? "├── " : "") + name + "\n";
                for (size_t i = 0; i < children.size(); ++i) {
                    std::string child_prefix = prefix + (depth > 0 ? "│   " : "    ");
                    if (i == children.size() - 1) {
                        child_prefix = prefix + (depth > 0 ? "└── " : "");
                    }
                    render(children[i], depth + 1, child_prefix);
                }
            }
        };

        render(nodes, 0, "");
        return {call.id, result, false, ""};
    TOOL_CATCH_END(call)
}

// ── Tool: todolist ────────────────────────────────────────────────
ToolResult Tools::tool_todolist(const ToolCall& call) {
    TOOL_CATCH_BEGIN(call)
        auto action = call.arguments.value("action", "list");

        if (action == "list") {
            std::string output = "Todo List:\n";
            for (size_t i = 0; i < m_todos.size(); ++i) {
                auto status = m_todos[i].value("status", "pending");
                auto content = m_todos[i].value("content", "");
                std::string marker = (status == "completed") ? "[x]" : (status == "in_progress") ? "[>]" : "[ ]";
                output += std::to_string(i + 1) + ". " + marker + " " + content + "\n";
            }
            return {call.id, output.empty() ? "No todos" : output, false, ""};
        } else if (action == "add") {
            json item;
            item["content"] = call.arguments.value("content", "");
            item["status"] = "pending";
            item["priority"] = call.arguments.value("priority", "medium");
            m_todos.push_back(item);
            LOG_INFO("Tools", "Added todo #" + std::to_string(m_todos.size()));
            return {call.id, "Added todo #" + std::to_string(m_todos.size()), false, ""};
        } else if (action == "update") {
            auto idx = call.arguments.value("index", 1) - 1;
            if (idx < 0 || idx >= (int)m_todos.size()) return {call.id, "", true, "Invalid index"};
            if (call.arguments.contains("status")) m_todos[idx]["status"] = call.arguments["status"];
            if (call.arguments.contains("content")) m_todos[idx]["content"] = call.arguments["content"];
            LOG_INFO("Tools", "Updated todo #" + std::to_string(idx + 1));
            return {call.id, "Updated todo #" + std::to_string(idx + 1), false, ""};
        } else if (action == "delete") {
            auto idx = call.arguments.value("index", 1) - 1;
            if (idx < 0 || idx >= (int)m_todos.size()) return {call.id, "", true, "Invalid index"};
            m_todos.erase(m_todos.begin() + idx);
            LOG_INFO("Tools", "Deleted todo #" + std::to_string(idx + 1));
            return {call.id, "Deleted todo #" + std::to_string(idx + 1), false, ""};
        } else if (action == "clear") {
            m_todos.clear();
            LOG_INFO("Tools", "Cleared all todos");
            return {call.id, "Cleared all todos", false, ""};
        }

        return {call.id, "", true, "Unknown action: " + action};
    TOOL_CATCH_END(call)
}

// ── Tool: memory ──────────────────────────────────────────────────
ToolResult Tools::tool_memory(const ToolCall& call) {
    TOOL_CATCH_BEGIN(call)
        auto action = call.arguments.value("action", "search");
        auto& mem = ModuleRegistry::instance().require<Memory>();

        if (action == "save") {
            MemoryEntry entry;
            entry.title = call.arguments.value("title", "");
            entry.content = call.arguments.value("content", "");
            entry.keywords = call.arguments.value("keywords", std::vector<std::string>{});
            entry.category = call.arguments.value("category", "general");
            entry.importance = call.arguments.value("importance", 0.5);
            entry.created_at = entry.updated_at = std::chrono::system_clock::now().time_since_epoch().count();
            auto saved = mem.save_entry(entry);
            LOG_INFO("Tools", "Memory saved: " + saved.title);
            return {call.id, "Memory saved: " + saved.title, false, ""};
        } else if (action == "search") {
            auto text = call.arguments.value("query", call.arguments.value("text", ""));
            auto category = call.arguments.value("category", "");
            auto max_results = call.arguments.value("maxResults", 5);

            MemoryQuery mq;
            mq.text = text;
            mq.category = category;
            mq.max_results = max_results;

            auto results = mem.search(mq);
            std::string output = "Memory search results:\n";
            for (auto& r : results) {
                output += "- [" + r.category + "] " + r.title + ": " + r.content + "\n";
            }
            LOG_DEBUG("Tools", "Memory search returned " + std::to_string(results.size()) + " results");
            return {call.id, output.empty() ? "No memories found" : output, false, ""};
        } else if (action == "list") {
            auto all = mem.list_all();
            std::string output = "All memories:\n";
            for (auto& r : all) {
                output += "- [" + r.category + "] " + r.title + "\n";
            }
            return {call.id, output, false, ""};
        }

        return {call.id, "", true, "Unknown action: " + action};
    TOOL_CATCH_END(call)
}

// ── Tool: image ───────────────────────────────────────────────────
ToolResult Tools::tool_image(const ToolCall& call) {
    TOOL_CATCH_BEGIN(call)
        auto file_path = call.arguments.value("filePath", "");
        if (file_path.empty()) return {call.id, "", true, "filePath is required"};

        auto& provider = ModuleRegistry::instance().require<Provider>();
        if (!provider.supports_vision()) {
            LOG_WARN("Tools", "Vision not supported by current model");
            return {call.id, "", true, "Current model does not support vision. Configure a vision-capable model or a vision-to-text bridge."};
        }

        // Read image and encode as base64
        std::ifstream f(file_path, std::ios::binary);
        if (!f) return {call.id, "", true, "Cannot open image: " + file_path};
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)),
                                         std::istreambuf_iterator<char>());
        auto b64 = crypto::to_base64(data);

        // Determine MIME type from extension
        std::string ext = fs::path(file_path).extension().string();
        std::string mime = "image/png";
        if (ext == ".jpg" || ext == ".jpeg") mime = "image/jpeg";
        else if (ext == ".gif") mime = "image/gif";
        else if (ext == ".webp") mime = "image/webp";
        else if (ext == ".bmp") mime = "image/bmp";

        LOG_DEBUG("Tools", "Loaded image: " + file_path + " (" + std::to_string(data.size()) + " bytes)");
        return {call.id, "data:" + mime + ";base64," + b64, false, ""};
    TOOL_CATCH_END(call)
}

// ── Tool: fs ──────────────────────────────────────────────────────
ToolResult Tools::tool_fs(const ToolCall& call) {
    TOOL_CATCH_BEGIN(call)
        auto action = call.arguments.value("action", "list");
        auto path = call.arguments.value("path", ".");

        if (action == "list") {
            auto entries = fsutil::list_directory(path);
            std::string output = "Contents of " + path + ":\n";
            for (auto& e : entries) {
                std::string type = e.is_directory ? "[DIR] " : (e.is_symlink ? "[LNK] " : "[FILE]");
                output += type + " " + e.path.filename().string() +
                          " (" + std::to_string(e.size) + " bytes)\n";
            }
            return {call.id, output.empty() ? "Empty directory" : output, false, ""};
        } else if (action == "mkdir") {
            fsutil::create_directories(path);
            LOG_INFO("Tools", "Created directory: " + path);
            return {call.id, "Created directory: " + path, false, ""};
        } else if (action == "remove") {
            auto recursive = call.arguments.value("recursive", false);
            if (fsutil::is_directory(path) && recursive) {
                fsutil::remove_directory(path);
            } else {
                fsutil::remove_file(path);
            }
            LOG_INFO("Tools", "Removed: " + path);
            return {call.id, "Removed: " + path, false, ""};
        } else if (action == "copy") {
            auto dest = call.arguments.value("dest", "");
            if (dest.empty()) return {call.id, "", true, "dest is required"};
            fs::copy(path, dest, fs::copy_options::recursive);
            LOG_INFO("Tools", "Copied: " + path + " -> " + dest);
            return {call.id, "Copied: " + path + " -> " + dest, false, ""};
        } else if (action == "move") {
            auto dest = call.arguments.value("dest", "");
            if (dest.empty()) return {call.id, "", true, "dest is required"};
            fs::rename(path, dest);
            LOG_INFO("Tools", "Moved: " + path + " -> " + dest);
            return {call.id, "Moved: " + path + " -> " + dest, false, ""};
        } else if (action == "exists") {
            return {call.id, std::string(fs::exists(path) ? "true" : "false"), false, ""};
        } else if (action == "size") {
            auto sz = fs::file_size(path);
            return {call.id, std::to_string(sz) + " bytes", false, ""};
        }

        return {call.id, "", true, "Unknown action: " + action};
    TOOL_CATCH_END(call)
}

// ── Tool: subagent ────────────────────────────────────────────────
ToolResult Tools::tool_subagent(const ToolCall& call) {
    TOOL_CATCH_BEGIN(call)
        auto agent_type = call.arguments.value("type", "explorer");
        auto task = call.arguments.value("task", "");
        auto mode = call.arguments.value("mode", "foreground");

        if (task.empty()) return {call.id, "", true, "task is required"};

        auto& agent = ModuleRegistry::instance().require<Agent>();

        LOG_INFO("Tools", "Launching sub-agent: type=" + agent_type + ", mode=" + mode);

        if (agent_type == "explorer") {
            auto result = agent.execute_explorer(task);
            return {call.id, result.output, result.success, result.error};
        } else if (mode == "background") {
            AgentConfig cfg;
            cfg.type = AgentType::SubAgent;
            cfg.name = "sub-agent-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
            cfg.system_prompt = call.arguments.value("systemPrompt", "");
            cfg.mode = SubAgentMode::Background;
            auto agent_id = agent.launch_background_sub_agent(cfg, task);
            LOG_INFO("Tools", "Background sub-agent started: " + agent_id);
            return {call.id, "Background sub-agent started: " + agent_id, false, ""};
        } else {
            AgentConfig cfg;
            cfg.type = AgentType::SubAgent;
            cfg.name = "sub-agent";
            cfg.mode = SubAgentMode::Foreground;
            cfg.system_prompt = call.arguments.value("systemPrompt", "");
            auto result = agent.execute_sub_agent(cfg, task);
            return {call.id, result.output, result.success, result.error};
        }
    TOOL_CATCH_END(call)
}

// ── Tool: git-saved ───────────────────────────────────────────────
ToolResult Tools::tool_git_saved(const ToolCall& call) {
    TOOL_CATCH_BEGIN(call)
        auto message = call.arguments.value("message", "agent save point");
        LOG_INFO("Tools", "Creating git save point: " + message);
        auto result = tool_exec({"git-save", "exec", {{"command", "git add -A && git commit -m \"" + message + "\""}, {"workdir", "."}}});
        return {call.id, result.content, result.is_error, result.error_message};
    TOOL_CATCH_END(call)
}

// ── Tool: git-restore ─────────────────────────────────────────────
ToolResult Tools::tool_git_restore(const ToolCall& call) {
    TOOL_CATCH_BEGIN(call)
        auto target = call.arguments.value("target", "HEAD~1");
        LOG_INFO("Tools", "Restoring git to: " + target);
        auto result = tool_exec({"git-restore", "exec", {{"command", "git reset --hard " + target}, {"workdir", "."}}});
        return {call.id, result.content, result.is_error, result.error_message};
    TOOL_CATCH_END(call)
}

} // namespace agent

