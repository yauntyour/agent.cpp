#pragma once
#ifdef AGENT_ENABLE_TUI

#include "core/module.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <deque>
#include <nlohmann/json.hpp>

namespace agent {

class TUI : public Module<TUI> {
public:
    static constexpr std::string_view static_name() { return "tui"; }

    void on_initialize();
    void on_shutdown();

    void run();
    void quit();

    void set_main_content(std::string_view content);
    void append_content(std::string_view content);
    void clear_content();

    void set_status(std::string_view status_line);
    void set_context_usage(double percent);
    void set_model_info(std::string_view provider, std::string_view model, std::string_view thinking);
    void set_lsp_status(std::string_view status);
    void set_mcp_status(std::string_view status);

    using InputCallback = std::function<void(std::string_view text)>;
    void on_input_submit(InputCallback callback);

    using CommandCallback = std::function<void(std::string_view command)>;
    void on_command(CommandCallback callback);

    struct EditDisplay {
        std::string file_path;
        std::string diff;
        std::string summary;
    };

    void show_edit(const EditDisplay& edit);
    void show_edits(const std::vector<EditDisplay>& edits);

    void display_mind_map(std::string_view ascii_map);
    void display_todo_list(const nlohmann::json& todos);
    void show_help();

    void set_command_completions(const std::vector<std::string>& commands);

private:
    void main_loop();
    void render();
    void render_status_bar();
    void render_content();
    void render_input_prompt();
    std::string read_line();
    void handle_tab_completion(std::string& buffer, int& cursor);
    std::string find_common_prefix(const std::vector<std::string>& matches) const;

    std::string m_main_content;
    std::string m_status_line;
    double m_context_pct = 0.0;
    std::string m_model_provider;
    std::string m_model_name;
    std::string m_thinking_mode;
    std::string m_lsp_status;
    std::string m_mcp_status;

    InputCallback m_input_callback;
    CommandCallback m_command_callback;

    std::vector<EditDisplay> m_edits;
    std::deque<std::string> m_history;
    size_t m_history_idx = 0;

    std::vector<std::string> m_command_completions;
    std::vector<std::string> m_completion_matches;
    size_t m_completion_idx = 0;
    std::string m_last_completion_prefix;

    std::mutex m_mutex;
    std::atomic<bool> m_running{false};

    int m_term_w = 80;
    int m_term_h = 24;
};

} // namespace agent

#endif
