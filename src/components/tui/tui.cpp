#ifdef AGENT_ENABLE_TUI
#include "components/tui/tui.hpp"
#include <notcurses/notcurses.h>
#include <algorithm>
#include <sstream>
#include <cstring>

namespace agent {

void TUI::on_initialize() {
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
    m_max_scroll = static_cast<int>(std::count(m_main_content.begin(), m_main_content.end(), '\n'));
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
}

void TUI::display_image(std::string_view path_or_base64) {
}

void TUI::display_video_frame(std::string_view file_path, int frame_number) {
#ifdef AGENT_HAS_FFMPEG
#endif
}

void TUI::display_mind_map(std::string_view ascii_map) {
    std::lock_guard lock(m_render_mutex);
    m_main_content += ascii_map;
    m_max_scroll = static_cast<int>(std::count(m_main_content.begin(), m_main_content.end(), '\n'));
}

void TUI::display_todo_list(const nlohmann::json& todos) {
    std::lock_guard lock(m_render_mutex);
    std::string output = "\n=== Todo List ===\n";
    for (size_t i = 0; i < todos.size(); ++i) {
        auto status = todos[i].value("status", "pending");
        auto content = todos[i].value("content", "");
        std::string marker = (status == "completed") ? "[x]" : (status == "in_progress") ? "[>]" : "[ ]";
        output += marker + " " + content + "\n";
    }
    m_main_content += output;
    m_max_scroll = static_cast<int>(std::count(m_main_content.begin(), m_main_content.end(), '\n'));
}

void TUI::show_help() {}
void TUI::show_command_palette() {}

void TUI::setup_colors() {
    if (!m_nc) return;

    ncpalette* pal = ncpalette_new(m_nc);
    ncpalette_set_rgb8(pal, 0, 0x00, 0x2b, 0x36);
    ncpalette_set_rgb8(pal, 1, 0x83, 0x94, 0x96);
    ncpalette_set_rgb8(pal, 2, 0x2a, 0xa1, 0x98);
    ncpalette_set_rgb8(pal, 3, 0x26, 0x8b, 0xd2);
    ncpalette_set_rgb8(pal, 4, 0x85, 0x99, 0x00);
    ncpalette_set_rgb8(pal, 5, 0xb5, 0x89, 0x00);
    ncpalette_set_rgb8(pal, 6, 0xdc, 0x32, 0x2f);
    ncpalette_set_rgb8(pal, 7, 0xd3, 0x36, 0x82);
    ncpalette_set_rgb8(pal, 8, 0x6c, 0x71, 0xc4);
    ncpalette_free(pal);
}

void TUI::main_loop() {
    notcurses_options opts{};
    opts.flags = NCOPTION_INHIBIT_SETLOCALE;
    m_nc = notcurses_init(&opts, nullptr);
    if (!m_nc) return;

    struct ncplane* stdplane = notcurses_stdplane(m_nc);
    ncplane_dim_yx(stdplane, &m_term_h, &m_term_w);

    setup_colors();

    unsigned side_w = std::max<unsigned>(20, m_term_w / 5);
    m_side_width = side_w;

    ncplane_options status_opts{};
    status_opts.y = 0;
    status_opts.x = 0;
    status_opts.rows = 1;
    status_opts.cols = m_term_w;
    m_status_plane = ncplane_create(stdplane, &status_opts);

    ncplane_options content_opts{};
    content_opts.y = 1;
    content_opts.x = 0;
    content_opts.rows = m_term_h - 4;
    content_opts.cols = m_term_w - side_w;
    m_content_plane = ncplane_create(stdplane, &content_opts);

    ncplane_options side_opts{};
    side_opts.y = 1;
    side_opts.x = m_term_w - side_w;
    side_opts.rows = m_term_h - 4;
    side_opts.cols = side_w;
    m_side_plane = ncplane_create(stdplane, &side_opts);

    ncplane_options input_opts{};
    input_opts.y = m_term_h - 3;
    input_opts.x = 0;
    input_opts.rows = 3;
    input_opts.cols = m_term_w;
    m_input_plane = ncplane_create(stdplane, &input_opts);

    while (m_running.load()) {
        render_frame();
        notcurses_render(m_nc);

        ncinput ni;
        int ch = notcurses_get_blocking(m_nc, &ni);
        if (ch == (int)'q' && ni.ctrl) {
            m_running.store(false);
            break;
        }
        if (ch == NCKEY_RESIZE) {
            ncplane_dim_yx(stdplane, &m_term_h, &m_term_w);
            side_w = std::max<unsigned>(20, m_term_w / 5);
            m_side_width = side_w;
            ncplane_resize(m_status_plane, 0, 0, 1, m_term_w, 0, 0, 0, 0);
            ncplane_resize(m_content_plane, 0, 0, m_term_h - 4, m_term_w - side_w, 0, 0, 0, 0);
            ncplane_resize(m_side_plane, 0, 0, m_term_h - 4, side_w, 0, 0, 0, 0);
            ncplane_move_yx(m_side_plane, 1, m_term_w - side_w);
            ncplane_resize(m_input_plane, 0, 0, 3, m_term_w, 0, 0, 0, 0);
            ncplane_move_yx(m_input_plane, m_term_h - 3, 0);
        }
        handle_input(ch);
    }

    ncplane_destroy(m_side_plane);
    ncplane_destroy(m_input_plane);
    ncplane_destroy(m_content_plane);
    ncplane_destroy(m_status_plane);
    notcurses_stop(m_nc);
    m_nc = nullptr;
}

void TUI::render_frame() {
    if (!m_nc) return;
    render_status_bar();
    render_main_content();
    render_input_area();
    render_side_panel();
}

void TUI::render_status_bar() {
    if (!m_status_plane) return;

    std::lock_guard lock(m_render_mutex);
    ncplane_erase(m_status_plane);

    ncplane_set_fg_rgb8(m_status_plane, 0x00, 0x2b, 0x36);
    ncplane_set_bg_rgb8(m_status_plane, 0xee, 0xe8, 0xd5);
    ncplane_set_fg_rgb8(m_status_plane, 0x00, 0x2b, 0x36);

    std::string left;
    if (!m_model_provider.empty() && !m_model_name.empty()) {
        left += " ";
        left += m_model_provider;
        left += ":";
        left += m_model_name;
        if (!m_thinking_mode.empty() && m_thinking_mode != "auto") {
            left += "  think:" + m_thinking_mode;
        }
    }
    ncplane_putstr(m_status_plane, left.c_str());

    std::string center = "agent.cpp v" + std::string(AGENT_VERSION);
    ncplane_printf_aligned(m_status_plane, m_term_w / 2, NCALIGN_CENTER, "%s", center.c_str());

    std::string right;
    if (!m_context_usage.empty()) {
        right += " ctx:" + m_context_usage;
    }
    if (!m_lsp_status.empty()) {
        right += " LSP:" + m_lsp_status;
    }
    if (!m_mcp_status.empty()) {
        right += " MCP:" + m_mcp_status;
    }
    if (!m_status_line.empty()) {
        right += " " + m_status_line;
    }
    if (!right.empty()) {
        ncplane_printf_aligned(m_status_plane, m_term_w - 1, NCALIGN_RIGHT, "%s", right.c_str());
    }
}

void TUI::render_main_content() {
    if (!m_content_plane) return;

    std::lock_guard lock(m_render_mutex);
    ncplane_erase(m_content_plane);

    ncplane_set_fg_rgb8(m_content_plane, 0x83, 0x94, 0x96);
    ncplane_set_bg_rgb8(m_content_plane, 0x00, 0x2b, 0x36);

    unsigned rows, cols;
    ncplane_dim_yx(m_content_plane, &rows, &cols);
    if (rows == 0 || cols == 0) return;

    std::vector<std::string> lines;
    std::istringstream stream(m_main_content);
    std::string line;
    while (std::getline(stream, line)) {
        while (!line.empty() && line.size() > cols) {
            std::string wrap = line.substr(0, cols);
            lines.push_back(wrap);
            line = line.substr(cols);
        }
        if (!line.empty() || !m_main_content.empty()) {
            lines.push_back(line);
        }
    }

    int total_lines = static_cast<int>(lines.size());
    int viewport = static_cast<int>(rows);
    if (m_max_scroll > viewport) {
        m_scroll_offset = m_max_scroll - viewport;
    } else {
        m_scroll_offset = 0;
    }

    int start = std::max(0, total_lines - viewport - m_scroll_offset);
    if (total_lines <= viewport) start = 0;

    for (int i = 0; i < viewport && (start + i) < total_lines; ++i) {
        const auto& l = lines[start + i];
        int y = i;
        ncplane_cursor_move_yx(m_content_plane, y, 0);
        ncplane_putstr(m_content_plane, l.c_str());
    }
}

void TUI::render_input_area() {
    if (!m_input_plane) return;

    std::lock_guard lock(m_render_mutex);
    ncplane_erase(m_input_plane);

    ncplane_set_fg_rgb8(m_input_plane, 0x83, 0x94, 0x96);
    ncplane_set_bg_rgb8(m_input_plane, 0x07, 0x36, 0x42);

    unsigned rows, cols;
    ncplane_dim_yx(m_input_plane, &rows, &cols);
    if (rows == 0 || cols == 0) return;

    ncplane_set_bg_rgb8(m_input_plane, 0x07, 0x36, 0x42);
    for (unsigned r = 0; r < rows; ++r) {
        ncplane_cursor_move_yx(m_input_plane, r, 0);
        for (unsigned c = 0; c < cols; ++c) {
            ncplane_putchar(m_input_plane, ' ');
        }
    }

    ncplane_cursor_move_yx(m_input_plane, 0, 0);
    ncplane_set_fg_rgb8(m_input_plane, 0x2a, 0xa1, 0x98);
    ncplane_putstr(m_input_plane, "  > ");

    ncplane_set_fg_rgb8(m_input_plane, 0xfd, 0xf6, 0xe3);

    int prompt_offset = 5;
    int display_col = prompt_offset;
    for (int i = 0; i < static_cast<int>(m_input_buffer.size()) && static_cast<unsigned>(display_col) < cols - 1; ++i) {
        ncplane_cursor_move_yx(m_input_plane, 0, display_col);
        ncplane_putchar(m_input_plane, m_input_buffer[i]);
        display_col++;
    }

    int cursor_vis_x = prompt_offset + m_input_cursor;
    if (static_cast<unsigned>(cursor_vis_x) < cols - 1) {
        ncplane_cursor_move_yx(m_input_plane, 0, cursor_vis_x);
        ncplane_set_fg_rgb8(m_input_plane, 0xfd, 0xf6, 0xe3);
    }
}

void TUI::render_side_panel() {
    if (!m_side_plane) return;

    std::lock_guard lock(m_render_mutex);
    ncplane_erase(m_side_plane);

    unsigned rows, cols;
    ncplane_dim_yx(m_side_plane, &rows, &cols);
    if (rows == 0 || cols == 0) return;

    ncplane_set_fg_rgb8(m_side_plane, 0x83, 0x94, 0x96);
    ncplane_set_bg_rgb8(m_side_plane, 0x00, 0x2b, 0x36);

    ncplane_set_fg_rgb8(m_side_plane, 0x93, 0xa1, 0xa1);
    ncplane_cursor_move_yx(m_side_plane, 0, 0);
    ncplane_putstr(m_side_plane, "  Edit History");

    for (unsigned c = 0; c < cols; ++c) {
        ncplane_cursor_move_yx(m_side_plane, 1, c);
        ncplane_putchar(m_side_plane, ' ');

        ncplane_set_bg_rgb8(m_side_plane, 0x07, 0x36, 0x42);
        ncplane_set_fg_rgb8(m_side_plane, 0x58, 0x6e, 0x75);
    }

    int max_edits = static_cast<int>(rows) - 2;
    if (max_edits <= 0) return;
    int start = static_cast<int>(m_edits.size()) > max_edits
                    ? static_cast<int>(m_edits.size()) - max_edits
                    : 0;

    for (int i = 0; i < max_edits && (start + i) < static_cast<int>(m_edits.size()); ++i) {
        const auto& edit = m_edits[start + i];
        ncplane_cursor_move_yx(m_side_plane, i + 2, 0);
        ncplane_set_fg_rgb8(m_side_plane, 0x2a, 0xa1, 0x98);

        std::string display = edit.file_path;
        if (display.size() > cols - 2) {
            display = "..." + display.substr(display.size() - cols + 5);
        }
        ncplane_putstr(m_side_plane, (" " + display).c_str());
    }
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
    } else if (ch == 27) {
        m_input_buffer.clear();
        m_input_cursor = 0;
    } else if (ch == 8 || ch == 127 || ch == NCKEY_BACKSPACE) {
        if (m_input_cursor > 0) {
            m_input_buffer.erase(m_input_cursor - 1, 1);
            m_input_cursor--;
        }
    } else if (ch >= 32 && ch <= 126) {
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
    } else if (ch == NCKEY_LEFT) {
        if (m_input_cursor > 0) m_input_cursor--;
    } else if (ch == NCKEY_RIGHT) {
        if (m_input_cursor < static_cast<int>(m_input_buffer.size())) m_input_cursor++;
    } else if (ch == NCKEY_PGUP) {
        m_scroll_offset = std::min(m_max_scroll, m_scroll_offset + 5);
    } else if (ch == NCKEY_PGDOWN) {
        m_scroll_offset = std::max(0, m_scroll_offset - 5);
    }
}

} // namespace agent
#endif