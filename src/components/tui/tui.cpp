#ifdef AGENT_ENABLE_TUI
#include "components/tui/tui.hpp"
#include <notcurses/notcurses.h>

namespace agent {

void TUI::on_initialize() {
    // Initialize Notcurses
}

void TUI::on_shutdown() {
    quit();
}

void TUI::run() {
    m_running.store(true);
    main_loop();
}

void TUI::quit() {
    m_quitting = true;
    m_running.store(false);
}

void TUI::set_main_content(std::string_view content) {
    std::lock_guard lock(m_render_mutex);
    m_main_content = content;
}

void TUI::append_content(std::string_view content) {
    std::lock_guard lock(m_render_mutex);
    m_main_content += content;
    m_max_scroll = static_cast<int>(std::count(m_main_content.begin(), m_main_content.end(), '\n'));
}

void TUI::set_status(std::string_view status_line) {
    std::lock_guard lock(m_render_mutex);
    m_status_line = status_line;
}

void TUI::set_context_usage(double percent) {
    std::lock_guard lock(m_render_mutex);
    m_context_usage = std::to_string(static_cast<int>(percent)) + "%";
}

void TUI::set_model_info(std::string_view provider, std::string_view model, std::string_view thinking) {
    std::lock_guard lock(m_render_mutex);
    m_model_provider = provider;
    m_model_name = model;
    m_thinking_mode = thinking;
}

void TUI::set_lsp_status(std::string_view status) {
    std::lock_guard lock(m_render_mutex);
    m_lsp_status = status;
}

void TUI::set_mcp_status(std::string_view status) {
    std::lock_guard lock(m_render_mutex);
    m_mcp_status = status;
}

void TUI::on_input_submit(InputCallback callback) {
    m_input_callback = std::move(callback);
}

void TUI::on_command(CommandCallback callback) {
    m_command_callback = std::move(callback);
}

void TUI::show_edit(const EditDisplay& edit) {
    std::lock_guard lock(m_render_mutex);
    m_edits.push_back(edit);
}

void TUI::show_edits(const std::vector<EditDisplay>& edits) {
    std::lock_guard lock(m_render_mutex);
    m_edits = edits;
}

void TUI::set_history_content(std::string_view history) {
    std::lock_guard lock(m_render_mutex);
    // Set side panel content
}

void TUI::display_image(std::string_view path_or_base64) {
    // Use Notcurses visual or FFmpeg to render image
}

void TUI::display_video_frame(std::string_view file_path, int frame_number) {
#ifdef AGENT_HAS_FFMPEG
    // Use FFmpeg to decode frame and display via Notcurses
#endif
}

void TUI::display_mind_map(std::string_view ascii_map) {
    std::lock_guard lock(m_render_mutex);
    // Display as pre-formatted text
    m_main_content += ascii_map;
}

void TUI::display_todo_list(const nlohmann::json& todos) {
    std::lock_guard lock(m_render_mutex);
    std::string output = "=== Todo List ===\n";
    for (size_t i = 0; i < todos.size(); ++i) {
        auto status = todos[i].value("status", "pending");
        auto content = todos[i].value("content", "");
        std::string marker = (status == "completed") ? "[x]" : (status == "in_progress") ? "[>]" : "[ ]";
        output += marker + " " + content + "\n";
    }
    m_main_content += output;
}

void TUI::show_help() {}
void TUI::show_command_palette() {}

void TUI::main_loop() {
    // Initialize Notcurses
    struct notcurses* nc = notcurses_init(nullptr, nullptr);
    if (!nc) return;

    struct ncplane* stdplane = notcurses_stdplane(nc);

    while (m_running.load()) {
        notcurses_render(nc);

        ncinput ni;
        int ch = notcurses_getc_blocking(nc, &ni);
        if (ch == (int)'q' && !m_input_buffer.empty()) {
            // Ctrl+Q to quit
        }
        handle_input(ch);
    }

    notcurses_stop(nc);
}

void TUI::render_frame() {
    // Render all components using Notcurses planes
}

void TUI::handle_input(int ch) {
    std::lock_guard lock(m_render_mutex);

    if (ch == '\n' || ch == '\r') {
        if (!m_input_buffer.empty()) {
            if (m_input_buffer[0] == '/' && m_command_callback) {
                m_command_callback(m_input_buffer);
            } else if (m_input_callback) {
                m_input_callback(m_input_buffer);
            }
            m_history_buffer.push_back(m_input_buffer);
            m_history_index = m_history_buffer.size();
            m_input_buffer.clear();
            m_input_cursor = 0;
        }
    } else if (ch == 27) { // ESC
        m_input_buffer.clear();
    } else if (ch == 8 || ch == 127) { // Backspace
        if (m_input_cursor > 0) {
            m_input_buffer.erase(m_input_cursor - 1, 1);
            m_input_cursor--;
        }
    } else if (ch >= 32 && ch <= 126) { // Printable
        m_input_buffer.insert(m_input_cursor, 1, static_cast<char>(ch));
        m_input_cursor++;
    } else if (ch == NCKEY_UP) {
        if (m_history_index > 0) {
            m_history_index--;
            if (!m_history_buffer.empty()) {
                m_input_buffer = m_history_buffer[m_history_index];
                m_input_cursor = static_cast<int>(m_input_buffer.size());
            }
        }
    } else if (ch == NCKEY_DOWN) {
        if (m_history_index < m_history_buffer.size()) {
            m_history_index++;
            m_input_buffer = m_history_index < m_history_buffer.size()
                                 ? m_history_buffer[m_history_index] : "";
            m_input_cursor = static_cast<int>(m_input_buffer.size());
        }
    }
}

void TUI::setup_colors() {}
void TUI::render_status_bar(int y, int w) {}
void TUI::render_main_content(int x, int y, int w, int h) {}
void TUI::render_input_area(int y, int w) {}
void TUI::render_side_panel(int x, int y, int w, int h) {}

} // namespace agent
#endif
