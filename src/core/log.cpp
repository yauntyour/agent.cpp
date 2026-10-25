#include "core/log.hpp"

#include <deque>

namespace logx
{
    namespace
    {
        std::mutex g_mutex;
        fs::path g_path;
        bool g_ready = false;
        bool g_console = true;
        constexpr std::uintmax_t kMaxBytes = 4ull * 1024 * 1024;

        std::string now_string()
        {
            std::time_t t = std::time(nullptr);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
            return buf;
        }
    } // namespace

    void init(const fs::path &path)
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_path = path;
        g_ready = false;
        if (path.empty())
            return;
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        // 滚动：超过阈值时保留一份 .1 备份
        if (fs::exists(path, ec) && fs::file_size(path, ec) > kMaxBytes && !ec)
        {
            fs::path old = path;
            old += ".1";
            fs::remove(old, ec);
            fs::rename(path, old, ec);
        }
        g_ready = true;
    }

    void set_console(bool enabled)
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_console = enabled;
    }

    void write(const std::string &level, const std::string &scope, const std::string &message)
    {
        std::string line = "[" + now_string() + "] " + level;
        if (!scope.empty())
            line += " [" + scope + "]";
        line += " " + message;

        std::lock_guard<std::mutex> lk(g_mutex);
        if (g_console)
        {
            std::ostream &os = (level == "ERROR" || level == "WARN") ? std::cerr : std::cout;
            os << line << std::endl;
        }
        if (g_ready && !g_path.empty())
        {
            std::ofstream f(g_path, std::ios::app | std::ios::binary);
            if (f)
                f << line << '\n';
        }
    }

    std::string tail(int max_lines)
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (g_path.empty())
            return {};
        std::ifstream f(g_path, std::ios::binary);
        if (!f)
            return {};
        std::deque<std::string> ring;
        std::string line;
        while (std::getline(f, line))
        {
            ring.push_back(line);
            if (max_lines > 0 && static_cast<int>(ring.size()) > max_lines)
                ring.pop_front();
        }
        std::string out;
        for (auto &l : ring)
        {
            out += l;
            out += '\n';
        }
        return out;
    }
} // namespace logx
