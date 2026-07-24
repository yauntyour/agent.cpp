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
#include <memory>
#include <nlohmann/json.hpp>

namespace ftxui {
class ComponentBase;
using Component = std::shared_ptr<ComponentBase>;
} // namespace ftxui

namespace agent {

struct Theme {
    std::string name = "default";
    
    struct {
        std::string primary = "#268bd2";
        std::string secondary = "#2aa198";
        std::string success = "#859900";
        std::string warning = "#b58900";
        std::string error = "#dc322f";
        std::string info = "#6c71c4";
        std::string muted = "#586e75";
    } colors;
    
    struct {
        std::string background = "#002b36";
        std::string foreground = "#839496";
        std::string status_bar = "#073642";
        std::string input = "#073642";
        std::string border = "#586e75";
    } background;
    
    static Theme from_config(const std::string& name, const std::string& primary, 
                            const std::string& secondary, const std::string& success,
                            const std::string& warning, const std::string& error,
                            const std::string& bg, const std::string& fg,
                            const std::string& status, const std::string& input,
                            const std::string& border) {
        Theme t;
        t.name = name;
        t.colors.primary = primary;
        t.colors.secondary = secondary;
        t.colors.success = success;
        t.colors.warning = warning;
        t.colors.error = error;
        t.background.background = bg;
        t.background.foreground = fg;
        t.background.status_bar = status;
        t.background.input = input;
        t.background.border = border;
        return t;
    }
};

class TUI : public Module<TUI> {
public:
    static constexpr std::string_view static_name() { return "tui"; }
    
    TUI();
    ~TUI();

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
    
    void set_theme(const Theme& theme);
    Theme get_theme() const;

private:
    void main_loop();
    void create_components();
    ftxui::Component create_status_bar();
    ftxui::Component create_content_area();
    ftxui::Component create_input_area();
    ftxui::Component create_edit_panel();
    
    void handle_input(std::string_view input);
    void handle_tab_completion(std::string& buffer);
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
    
    Theme m_theme;
    
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace agent

#endif
