#pragma once
#ifdef AGENT_ENABLE_TUI

#include "core/module.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <mutex>
#include <ncurses.h>

namespace agent {

struct TUIRect {
    int x, y, w, h;
};

enum class TUIColor : int {
    Default = 1,
    Primary,
    Secondary,
    Success,
    Warning,
    Error,
    Info,
    Dim,
    Accent,
    Highlight
};

class TUI : public Module<TUI> {
public:
    static constexpr std::string_view static_name() { return "tui"; }

    void on_initialize() override;
    void on_shutdown() override;

    // ── Lifecycle ──────────────────────────────────────────────
    void run();
    void quit();

    // ── Content areas ──────────────────────────────────────────
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

    // ── Status bar ─────────────────────────────────────────────
    void set_status(std::string_view status_line);
    void set_context_usage(double percent);
    void set_model_info(std::string_view provider, std::string_view model, std::string_view thinking);
    void set_lsp_status(std::string_view status);
    void set_mcp_status(std::string_view status);

    // ── Input handling ─────────────────────────────────────────
    using InputCallback = std::function<void(std::string_view text)>;
    void on_input_submit(InputCallback callback);

    using CommandCallback = std::function<void(std::string_view command)>;
    void on_command(CommandCallback callback);

    // ── Edits display ──────────────────────────────────────────
    struct EditDisplay {
        std::string file_path;
        std::string diff;
        std::string summary;
    };

    void show_edit(const EditDisplay& edit);
    void show_edits(const std::vector<EditDisplay>& edits);

    // ── History browsing ───────────────────────────────────────
    void set_history_content(std::string_view history);

    // ── Media display ──────────────────────────────────────────
    void display_image(std::string_view path_or_base64);
    void display_video_frame(std::string_view file_path, int frame_number);

    // ── Mind map display ───────────────────────────────────────
    void display_mind_map(std::string_view ascii_map);

    // ── Todo list display ──────────────────────────────────────
    void display_todo_list(const nlohmann::json& todos);

    // ── System ─────────────────────────────────────────────────
    void show_help();
    void show_command_palette();

private:
    void main_loop();
    void render_frame();
    void handle_input(int ch);
    void setup_colors();
    void render_status_bar(int y, int w);
    void render_main_content(int x, int y, int w, int h);
    void render_input_area(int y, int w);
    void render_side_panel(int x, int y, int w, int h);

    struct Area {
        TUIRect rect;
        std::string title;
        std::string content;
        int scroll_offset = 0;
    };

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

    std::unique_ptr<struct TUIData> m_data;
};

} // namespace agent

#endif // AGENT_ENABLE_TUI
