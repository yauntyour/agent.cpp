#ifdef AGENT_ENABLE_TUI
#include "components/tui/tui.hpp"
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/event.hpp>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace agent {

using namespace ftxui;

namespace {
Color hex_to_color(const std::string& hex) {
    if (hex.empty() || hex[0] != '#') return Color::Default;
    
    auto hex_to_int = [](const std::string& s) -> uint8_t {
        return static_cast<uint8_t>(std::stoul(s, nullptr, 16));
    };
    
    if (hex.size() == 7) {
        uint8_t r = hex_to_int(hex.substr(1, 2));
        uint8_t g = hex_to_int(hex.substr(3, 2));
        uint8_t b = hex_to_int(hex.substr(5, 2));
        return Color::RGB(r, g, b);
    }
    return Color::Default;
}
} // anonymous namespace

struct TUI::Impl {
    ScreenInteractive screen = ScreenInteractive::Fullscreen();
    Component main_container;
    Component status_bar;
    Component content_area;
    Component input_area;
    Component edit_panel;
    
    std::string input_buffer;
    int scroll_offset = 0;
    bool show_edits_panel = false;
};

TUI::TUI() : m_impl(std::make_unique<Impl>()) {}

TUI::~TUI() = default;

void TUI::on_initialize() {
    create_components();
}

void TUI::on_shutdown() {
    if (m_running.load()) {
        quit();
    }
}

void TUI::run() {
    m_running.store(true);
    main_loop();
}

void TUI::quit() {
    m_running.store(false);
    m_impl->screen.Exit();
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
    m_impl->show_edits_panel = true;
}

void TUI::show_edits(const std::vector<EditDisplay>& edits) {
    std::lock_guard lock(m_mutex);
    m_edits = edits;
    m_impl->show_edits_panel = !edits.empty();
}

void TUI::display_mind_map(std::string_view ascii_map) {
    std::lock_guard lock(m_mutex);
    m_main_content += "\n";
    m_main_content += ascii_map;
    m_main_content += "\n";
}

void TUI::display_todo_list(const nlohmann::json& todos) {
    std::lock_guard lock(m_mutex);
    std::string output = "\n\x1b[36m=== Todo List ===\x1b[0m\n";
    for (size_t i = 0; i < todos.size(); ++i) {
        auto status = todos[i].value("status", "pending");
        auto content = todos[i].value("content", "");
        if (status == "completed")
            output += "\x1b[32m[x]\x1b[0m " + content + "\n";
        else if (status == "in_progress")
            output += "\x1b[33m[>]\x1b[0m " + content + "\n";
        else
            output += "\x1b[90m[ ]\x1b[0m " + content + "\n";
    }
    m_main_content += output + "\n";
}

void TUI::set_command_completions(const std::vector<std::string>& commands) {
    m_command_completions = commands;
    std::sort(m_command_completions.begin(), m_command_completions.end());
}

void TUI::set_theme(const Theme& theme) {
    m_theme = theme;
}

Theme TUI::get_theme() const {
    return m_theme;
}

std::string TUI::find_common_prefix(const std::vector<std::string>& matches) const {
    if (matches.empty()) return "";
    std::string prefix = matches[0];
    for (size_t i = 1; i < matches.size(); ++i) {
        const auto& s = matches[i];
        size_t j = 0;
        while (j < prefix.size() && j < s.size() && prefix[j] == s[j]) ++j;
        prefix.resize(j);
        if (prefix.empty()) break;
    }
    return prefix;
}

void TUI::handle_tab_completion(std::string& buffer) {
    if (buffer.empty() || m_command_completions.empty()) return;

    bool is_command = !buffer.empty() && buffer[0] == '/';
    if (!is_command) return;

    std::string prefix = buffer;

    if (m_completion_matches.empty() || prefix != m_last_completion_prefix) {
        m_completion_matches.clear();
        for (const auto& cmd : m_command_completions) {
            if (cmd.size() >= prefix.size() &&
                cmd.compare(0, prefix.size(), prefix) == 0) {
                m_completion_matches.push_back(cmd);
            }
        }
        m_last_completion_prefix = prefix;
        m_completion_idx = 0;
    }

    if (m_completion_matches.empty()) return;

    if (m_completion_matches.size() == 1) {
        buffer = m_completion_matches[0];
        m_completion_matches.clear();
    } else {
        std::string common = find_common_prefix(m_completion_matches);
        if (common.size() > prefix.size()) {
            buffer = common;
        } else {
            if (m_completion_idx < m_completion_matches.size()) {
                buffer = m_completion_matches[m_completion_idx];
                m_completion_idx = (m_completion_idx + 1) % m_completion_matches.size();
            }
        }
    }
}

void TUI::show_help() {
    std::lock_guard lock(m_mutex);
    m_main_content += "\x1b[36m\n=== Help ===\x1b[0m\n";
    m_main_content += "  /help     - Show this help\n";
    m_main_content += "  /new      - Create new session\n";
    m_main_content += "  /model    - Switch model\n";
    m_main_content += "  /sessions - List sessions\n";
    m_main_content += "  /memory   - View memories\n";
    m_main_content += "  /history  - Edit history\n";
    m_main_content += "  /quit     - Exit\n";
    m_main_content += "  Ctrl+C    - Interrupt/Exit\n\n";
}

void TUI::create_components() {
    m_impl->status_bar = create_status_bar();
    m_impl->content_area = create_content_area();
    m_impl->input_area = create_input_area();
    m_impl->edit_panel = create_edit_panel();
    
    auto main_layout = Container::Vertical({
        m_impl->status_bar,
        m_impl->content_area,
        m_impl->input_area,
    });
    
    if (m_impl->show_edits_panel) {
        m_impl->main_container = ResizableSplitLeft(
            m_impl->edit_panel,
            main_layout,
            &m_term_w
        );
    } else {
        m_impl->main_container = main_layout;
    }
}

Component TUI::create_status_bar() {
    return Renderer([this] {
        std::lock_guard lock(m_mutex);
        
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
        
        auto left_elem = text(left) | color(hex_to_color(m_theme.colors.primary));
        auto center_elem = text(center) | bold | color(hex_to_color(m_theme.background.foreground));
        auto right_elem = text(right) | color(hex_to_color(m_theme.colors.muted));
        
        auto status_bar = hbox({
            left_elem,
            filler(),
            center_elem,
            filler(),
            right_elem,
        }) | bgcolor(hex_to_color(m_theme.background.status_bar));
        
        return status_bar;
    });
}

Component TUI::create_content_area() {
    return Renderer([this] {
        std::lock_guard lock(m_mutex);
        
        std::vector<Element> lines;
        std::istringstream stream(m_main_content);
        std::string line;
        
        while (std::getline(stream, line)) {
            lines.push_back(text(line));
        }
        
        if (lines.empty()) {
            lines.push_back(text("Welcome to agent.cpp!") | color(hex_to_color(m_theme.colors.primary)));
        }
        
        auto content = vbox(std::move(lines));
        
        return content | vscroll_indicator | yframe | flex;
    });
}

Component TUI::create_input_area() {
    auto input_option = InputOption();
    input_option.on_enter = [this] {
        if (m_impl->input_buffer.empty()) return;
        
        std::string input = m_impl->input_buffer;
        m_impl->input_buffer.clear();
        
        m_history.push_back(input);
        m_history_idx = m_history.size();
        
        {
            std::lock_guard lock(m_mutex);
            m_main_content += "\x1b[32m> \x1b[0m\x1b[37m" + input + "\x1b[0m\n";
        }
        
        handle_input(input);
    };
    
    auto input = Input(&m_impl->input_buffer, "Type a command...", input_option);
    
    return Renderer(input, [this, input] {
        auto prompt = text(" > ") | color(hex_to_color(m_theme.colors.primary));
        auto input_elem = input->Render() | color(hex_to_color(m_theme.background.foreground));
        
        return hbox({
            prompt,
            input_elem,
        }) | bgcolor(hex_to_color(m_theme.background.input));
    });
}

Component TUI::create_edit_panel() {
    return Renderer([this] {
        std::lock_guard lock(m_mutex);
        
        if (m_edits.empty()) {
            return text("No edits") | color(hex_to_color(m_theme.colors.muted));
        }
        
        std::vector<Element> edit_elements;
        for (const auto& edit : m_edits) {
            auto file_path = text(edit.file_path) | bold | color(hex_to_color(m_theme.colors.info));
            auto summary = text(edit.summary) | color(hex_to_color(m_theme.colors.secondary));
            
            edit_elements.push_back(vbox({
                file_path,
                summary,
                separator(),
            }));
        }
        
        return vbox(std::move(edit_elements)) | vscroll_indicator | yframe;
    });
}

void TUI::handle_input(std::string_view input) {
    if (input[0] == '/') {
        if (input == "/quit" || input == "/exit" || input == "/q") {
            quit();
            return;
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
}

void TUI::main_loop() {
    std::string logo = R"(
   __ _  ___ _ __   ___ _ __   ___ _ __    ___ _ __  _ __
  / _` |/ _ \ '_ \ / _ \ '_ \ / _ \ '_ \  / __| '_ \| '_ \
 | (_| |  __/ | | |  __/ | | |  __/ |_) | \__ \ |_) | |_) |
  \__,_|\___|_| |_|\___|_| |_|\___| .__/  |___/ .__/| .__/
                                  |_|         |_|   |_|
)";
    
    {
        std::lock_guard lock(m_mutex);
        m_main_content = "\x1b[36m" + logo + "\x1b[0m\n";
        m_main_content += "  \x1b[37magent.cpp v" AGENT_VERSION "\x1b[0m \x1b[2m— Modular AI Coding Agent\x1b[0m\n";
        m_main_content += "  \x1b[90mType /help for commands, Ctrl+C to exit\x1b[0m\n\n";
    }
    
    auto component = m_impl->main_container;
    
    auto event_handler = CatchEvent(component, [this](Event event) {
        if (event == Event::CtrlC) {
            quit();
            return true;
        }
        
        if (event == Event::Tab) {
            handle_tab_completion(m_impl->input_buffer);
            return true;
        }
        
        if (event == Event::ArrowUp) {
            if (!m_history.empty() && m_history_idx > 0) {
                m_history_idx--;
                m_impl->input_buffer = m_history[m_history_idx];
                m_last_completion_prefix.clear();
                m_completion_matches.clear();
            }
            return true;
        }
        
        if (event == Event::ArrowDown) {
            if (m_history_idx < m_history.size() - 1) {
                m_history_idx++;
                m_impl->input_buffer = m_history[m_history_idx];
            } else if (m_history_idx == m_history.size() - 1) {
                m_history_idx = m_history.size();
                m_impl->input_buffer.clear();
            }
            m_last_completion_prefix.clear();
            m_completion_matches.clear();
            return true;
        }
        
        return false;
    });
    
    m_impl->screen.Loop(event_handler);
}

} // namespace agent
#endif
