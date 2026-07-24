#pragma once
#ifdef AGENT_ENABLE_TUI

#include "core/module.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <notcurses/notcurses.h>

namespace agent {

struct TUIRect {
    int x, y, w, h;
};

// Notcurses channel pair indices
static constexpr int NC_SOLARIZED_BG = 0x002b36;
static constexpr int NC_SOLARIZED_FG = 0x839496;
static constexpr int NC_SOLARIZED_CYAN = 0x2aa198;
static constexpr int NC_SOLARIZED_BLUE = 0x268bd2;
static constexpr int NC_SOLARIZED_GREEN = 0x859900;
static constexpr int NC_SOLARIZED_YELLOW = 0xb58900;
static constexpr int NC_SOLARIZED_RED = 0xdc322f;
static constexpr int NC_SOLARIZED_MAGENTA = 0xd33682;
static constexpr int NC_SOLARIZED_VIOLET = 0x6c71c4;

class TUI : public Module<TUI> {
public:
    static constexpr std::string_view static_name() { return "tui"; }

    void on_initialize();
    void on_shutdown();

    void run();
    void quit();

    struct RenderContext {
        int screen_w;
        int screen_h;
        std::string input_text;
        int cursor_pos;
        std::vector<std::string> suggestions;
    };

    using RenderCallback = std::function<void(const RenderContext& ctx)>;

    void set_main_content(std::string_view content);
    void append_content(std::string_view content);

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

    void set_history_content(std::string_view history);

    void display_image(std::string_view path_or_base64);
    void display_video_frame(std::string_view file_path, int frame_number);

    void display_mind_map(std::string_view ascii_map);

    void display_todo_list(const nlohmann::json& todos);

    void show_help();
    void show_command_palette();

private:
    void main_loop();
    void render_frame();
    void handle_input(int ch);
    void setup_colors();
    void render_status_bar();
    void render_main_content();
    void render_input_area();
    void render_side_panel();

    std::string m_main_content;
    std::string m_status_line;
    std::string m_context_usage;
    std::string m_model_provider;
    std::string m_model_name;
    std::string m_thinking_mode;
    std::string m_lsp_status;
    std::string m_mcp_status;
    std::string m_input_buffer;
    int m_input_cursor = 0;
    int m_scroll_offset = 0;
    int m_max_scroll = 0;

    InputCallback m_input_callback;
    CommandCallback m_command_callback;

    std::vector<EditDisplay> m_edits;
    std::vector<std::string> m_history_buffer;
    size_t m_history_index = 0;

    std::mutex m_render_mutex;
    std::atomic<bool> m_running{false};
    bool m_quitting = false;

    struct notcurses* m_nc = nullptr;
    struct ncplane* m_status_plane = nullptr;
    struct ncplane* m_content_plane = nullptr;
    struct ncplane* m_input_plane = nullptr;
    struct ncplane* m_side_plane = nullptr;

    unsigned m_term_w = 80;
    unsigned m_term_h = 24;
    unsigned m_side_width = 24;
};

} // namespace agent

#endif // AGENT_ENABLE_TUI
