#ifdef AGENT_ENABLE_TUI
#include "components/tui/tui.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

namespace agent {

namespace {

#ifdef _WIN32
void enable_vt_mode() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hOut, &mode);
    mode |= 0x0004;
    SetConsoleMode(hOut, mode);

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hIn, &mode);
    mode |= 0x0200;
    mode &= ~0x0040;
    SetConsoleMode(hIn, mode);
}

void get_term_size(int& w, int& h) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
}
#else
void enable_vt_mode() {}

void get_term_size(int& w, int& h) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        w = ws.ws_col;
        h = ws.ws_row;
    }
}

struct RawMode {
    termios orig;
    RawMode() {
        tcgetattr(STDIN_FILENO, &orig);
        termios raw = orig;
        raw.c_lflag &= ~(ECHO | ICANON);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    }
    ~RawMode() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig); }
};
#endif

constexpr const char* ESC = "\033";
constexpr const char* CSI = "\033[";
constexpr const char* RESET = "\033[0m";
constexpr const char* BOLD = "\033[1m";
constexpr const char* DIM = "\033[2m";

constexpr const char* FG_CYAN = "\033[36m";
constexpr const char* FG_GREEN = "\033[32m";
constexpr const char* FG_YELLOW = "\033[33m";
constexpr const char* FG_RED = "\033[31m";
constexpr const char* FG_WHITE = "\033[37m";
constexpr const char* FG_GRAY = "\033[90m";

constexpr const char* BG_DARK = "\033[48;2;0;43;54m";
constexpr const char* BG_INPUT = "\033[48;2;7;54;66m";
constexpr const char* BG_STATUS = "\033[48;2;238;232;213m";
constexpr const char* FG_STATUS = "\033[38;2;0;43;54m";

inline std::string operator+(const char* a, const std::string& b) {
    return std::string(a) + b;
}

void move_to(int row, int col) {
    std::cout << CSI << row << ";" << col << "H" << std::flush;
}

void clear_screen() {
    std::cout << CSI << "2J" << CSI << "H" << std::flush;
}

void clear_line() {
    std::cout << CSI << "2K\r" << std::flush;
}

void hide_cursor() { std::cout << CSI << "25l" << std::flush; }
void show_cursor() { std::cout << CSI << "25h" << std::flush; }

std::string truncate(const std::string& s, int max_w) {
    if ((int)s.size() <= max_w) return s;
    if (max_w <= 3) return s.substr(0, max_w);
    return s.substr(0, max_w - 3) + "...";
}

} // anonymous namespace

void TUI::on_initialize() {
#ifdef _WIN32
    enable_vt_mode();
#endif
}

void TUI::on_shutdown() {
    if (m_running.load()) {
        quit();
        show_cursor();
        std::cout << RESET << std::endl;
    }
}

void TUI::run() {
    m_running.store(true);
    main_loop();
}

void TUI::quit() {
    m_running.store(false);
}

void TUI::set_main_content(std::string_view content) {
    std::lock_guard lock(m_mutex);
    m_main_content = content;
}

void TUI::append_content(std::string_view content) {
    std::lock_guard lock(m_mutex);
    m_main_content += content;
}

void TUI::clear_content() {
    std::lock_guard lock(m_mutex);
    m_main_content.clear();
}

void TUI::set_status(std::string_view status_line) {
    std::lock_guard lock(m_mutex);
    m_status_line = status_line;
}

void TUI::set_context_usage(double percent) {
    std::lock_guard lock(m_mutex);
    m_context_pct = percent;
}

void TUI::set_model_info(std::string_view provider, std::string_view model, std::string_view thinking) {
    std::lock_guard lock(m_mutex);
    m_model_provider = provider;
    m_model_name = model;
    m_thinking_mode = thinking;
}

void TUI::set_lsp_status(std::string_view status) {
    std::lock_guard lock(m_mutex);
    m_lsp_status = status;
}

void TUI::set_mcp_status(std::string_view status) {
    std::lock_guard lock(m_mutex);
    m_mcp_status = status;
}

void TUI::on_input_submit(InputCallback callback) {
    m_input_callback = std::move(callback);
}

void TUI::on_command(CommandCallback callback) {
    m_command_callback = std::move(callback);
}

void TUI::show_edit(const EditDisplay& edit) {
    std::lock_guard lock(m_mutex);
    m_edits.push_back(edit);
    if (m_edits.size() > 50) m_edits.erase(m_edits.begin());
}

void TUI::show_edits(const std::vector<EditDisplay>& edits) {
    std::lock_guard lock(m_mutex);
    m_edits = edits;
}

void TUI::display_mind_map(std::string_view ascii_map) {
    std::lock_guard lock(m_mutex);
    m_main_content += "\n";
    m_main_content += ascii_map;
    m_main_content += "\n";
}

void TUI::display_todo_list(const nlohmann::json& todos) {
    std::lock_guard lock(m_mutex);
    std::string output = std::string("\n") + FG_CYAN + "=== Todo List ===" + RESET + "\n";
    for (size_t i = 0; i < todos.size(); ++i) {
        auto status = todos[i].value("status", "pending");
        auto content = todos[i].value("content", "");
        if (status == "completed")
            output += std::string(FG_GREEN) + "[x]" + RESET + " " + content + "\n";
        else if (status == "in_progress")
            output += std::string(FG_YELLOW) + "[>]" + RESET + " " + content + "\n";
        else
            output += std::string(FG_GRAY) + "[ ]" + RESET + " " + content + "\n";
    }
    m_main_content += output + "\n";
}

void TUI::show_help() {
    std::lock_guard lock(m_mutex);
    m_main_content += std::string(FG_CYAN) + "\n=== Help ===" + RESET + "\n";
    m_main_content += "  /help     - Show this help\n";
    m_main_content += "  /new      - Create new session\n";
    m_main_content += "  /model    - Switch model\n";
    m_main_content += "  /sessions - List sessions\n";
    m_main_content += "  /memory   - View memories\n";
    m_main_content += "  /history  - Edit history\n";
    m_main_content += "  /quit     - Exit\n";
    m_main_content += "  Ctrl+C    - Interrupt/Exit\n\n";
}

void TUI::render_status_bar() {
    move_to(1, 1);
    std::cout << BG_STATUS << FG_STATUS << BOLD;

    std::string left = " ";
    if (!m_model_provider.empty()) {
        left += m_model_provider + ":" + m_model_name;
    }

    std::string center = "agent.cpp v" AGENT_VERSION;

    std::string right;
    if (m_context_pct > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "ctx:%.0f%% ", m_context_pct);
        right += buf;
    }
    if (!m_lsp_status.empty()) right += "LSP:" + m_lsp_status + " ";
    if (!m_mcp_status.empty()) right += "MCP:" + m_mcp_status + " ";

    int left_w = (int)left.size();
    int center_w = (int)center.size();
    int right_w = (int)right.size();
    int pad_left = (m_term_w - center_w) / 2 - left_w;
    int pad_right = m_term_w - left_w - center_w - std::max(0, pad_left) - right_w;

    std::cout << left;
    for (int i = 0; i < std::max(0, pad_left); ++i) std::cout << ' ';
    std::cout << center;
    for (int i = 0; i < std::max(0, pad_right); ++i) std::cout << ' ';
    std::cout << right;

    std::cout << RESET;
}

void TUI::render_content() {
    std::lock_guard lock(m_mutex);

    int content_start = 2;
    int content_end = m_term_h - 2;
    int visible_rows = content_end - content_start;

    std::vector<std::string> lines;
    std::istringstream stream(m_main_content);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }

    int total = (int)lines.size();
    int start = std::max(0, total - visible_rows);

    for (int i = 0; i < visible_rows; ++i) {
        move_to(content_start + i, 1);
        clear_line();
        int idx = start + i;
        if (idx < total) {
            std::cout << truncate(lines[idx], m_term_w);
        }
    }
}

void TUI::render_input_prompt() {
    move_to(m_term_h - 1, 1);
    std::cout << BG_INPUT << FG_CYAN << " > " << RESET << BG_INPUT << " " << std::flush;
}

void TUI::render() {
    get_term_size(m_term_w, m_term_h);
    hide_cursor();
    render_status_bar();
    render_content();
    render_input_prompt();
    show_cursor();
    move_to(m_term_h - 1, 5);
    std::cout << std::flush;
}

std::string TUI::read_line() {
    std::string buffer;
    int cursor = 0;

    auto refresh_input = [&]() {
        move_to(m_term_h - 1, 1);
        clear_line();
        std::cout << BG_INPUT << FG_CYAN << " > " << RESET << BG_INPUT << FG_WHITE;
        int max_w = m_term_w - 5;
        int start = std::max(0, cursor - max_w + 1);
        std::string visible = buffer.substr(start, max_w);
        std::cout << visible;
        for (int i = (int)visible.size(); i < max_w; ++i) std::cout << ' ';
        move_to(m_term_h - 1, 5 + cursor - start);
        std::cout << std::flush;
    };

    while (m_running.load()) {
#ifdef _WIN32
        if (!_kbhit()) {
            Sleep(10);
            continue;
        }
        int ch = _getch();
        if (ch == 0 || ch == 0xE0) {
            int ext = _getch();
            switch (ext) {
            case 72:
                if (!m_history.empty() && m_history_idx > 0) {
                    m_history_idx--;
                    buffer = m_history[m_history_idx];
                    cursor = (int)buffer.size();
                }
                break;
            case 80:
                if (m_history_idx < m_history.size() - 1) {
                    m_history_idx++;
                    buffer = m_history[m_history_idx];
                    cursor = (int)buffer.size();
                } else if (m_history_idx == m_history.size() - 1) {
                    m_history_idx = m_history.size();
                    buffer.clear();
                    cursor = 0;
                }
                break;
            case 75: if (cursor > 0) cursor--; break;
            case 77: if (cursor < (int)buffer.size()) cursor++; break;
            case 83:
                if (cursor < (int)buffer.size()) buffer.erase(cursor, 1);
                break;
            }
            refresh_input();
            continue;
        }
#else
        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) continue;
        int ch = (unsigned char)c;

        if (ch == 27) {
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;
            if (seq[0] == '[') {
                switch (seq[1]) {
                case 'A':
                    if (!m_history.empty() && m_history_idx > 0) {
                        m_history_idx--;
                        buffer = m_history[m_history_idx];
                        cursor = (int)buffer.size();
                    }
                    break;
                case 'B':
                    if (m_history_idx < m_history.size() - 1) {
                        m_history_idx++;
                        buffer = m_history[m_history_idx];
                        cursor = (int)buffer.size();
                    } else {
                        m_history_idx = m_history.size();
                        buffer.clear();
                        cursor = 0;
                    }
                    break;
                case 'C': if (cursor < (int)buffer.size()) cursor++; break;
                case 'D': if (cursor > 0) cursor--; break;
                case '3':
                    { char extra; read(STDIN_FILENO, &extra, 1);
                      if (cursor < (int)buffer.size()) buffer.erase(cursor, 1); }
                    break;
                }
                refresh_input();
                continue;
            }
            continue;
        }
#endif

        if (ch == '\r' || ch == '\n') {
            std::cout << RESET << std::endl;
            return buffer;
        } else if (ch == 3) {
            return "\x03";
        } else if (ch == 8 || ch == 127) {
            if (cursor > 0) {
                buffer.erase(cursor - 1, 1);
                cursor--;
            }
        } else if (ch >= 32 && ch <= 126) {
            buffer.insert(cursor, 1, (char)ch);
            cursor++;
        }
        refresh_input();
    }
    return "";
}

void TUI::main_loop() {
#ifndef _WIN32
    RawMode raw;
#endif

    clear_screen();

    {
        std::lock_guard lock(m_mutex);
        m_main_content = std::string(FG_CYAN) + R"(
   __ _  ___ _ __   ___ _ __   ___ _ __    ___ _ __  _ __
  / _` |/ _ \ '_ \ / _ \ '_ \ / _ \ '_ \  / __| '_ \| '_ \
 | (_| |  __/ | | |  __/ | | |  __/ |_) | \__ \ |_) | |_) |
  \__,_|\___|_| |_|\___|_| |_|\___| .__/  |___/ .__/| .__/
                                  |_|         |_|   |_|
)" + RESET + "\n";
        m_main_content += std::string("  ") + FG_WHITE + "agent.cpp v" + AGENT_VERSION + RESET + " " + DIM + "— Modular AI Coding Agent" + RESET + "\n";
        m_main_content += std::string("  ") + FG_GRAY + "Type /help for commands, Ctrl+C to exit" + RESET + "\n\n";
    }

    render();

    while (m_running.load()) {
        std::string input = read_line();

        if (input == "\x03") {
            m_running.store(false);
            break;
        }

        if (input.empty()) {
            render();
            continue;
        }

        m_history.push_back(input);
        m_history_idx = m_history.size();

        {
            std::lock_guard lock(m_mutex);
            m_main_content += std::string(FG_GREEN) + "> " + RESET + FG_WHITE + input + RESET + "\n";
        }

        if (input[0] == '/') {
            if (input == "/quit" || input == "/exit" || input == "/q") {
                m_running.store(false);
                break;
            }
            if (input == "/help" || input == "/h") {
                show_help();
            } else if (m_command_callback) {
                m_command_callback(input);
            }
        } else {
            if (m_input_callback) {
                m_input_callback(input);
            }
        }

        render();
    }

    clear_screen();
    move_to(1, 1);
    show_cursor();
    std::cout << RESET;
}

} // namespace agent
#endif
