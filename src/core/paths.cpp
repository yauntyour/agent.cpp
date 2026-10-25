#include "core/paths.hpp"

#include <system_error>

namespace paths
{
    namespace
    {
        fs::path detect_exe_dir()
        {
#ifdef _WIN32
            wchar_t buf[MAX_PATH * 4];
            DWORD len = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
            if (len == 0 || len >= std::size(buf))
                return {};
            std::error_code ec;
            fs::path p(buf);
            fs::path dir = p.parent_path();
            if (dir.empty())
                return {};
            return fs::weakly_canonical(dir, ec);
#elif defined(__APPLE__)
            char buf[PATH_MAX];
            uint32_t size = static_cast<uint32_t>(sizeof(buf));
            if (_NSGetExecutablePath(buf, &size) != 0)
                return {};
            std::error_code ec;
            return fs::weakly_canonical(fs::path(buf).parent_path(), ec);
#else
            char buf[PATH_MAX];
            ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
            if (len <= 0)
                return {};
            buf[len] = '\0';
            std::error_code ec;
            return fs::weakly_canonical(fs::path(buf).parent_path(), ec);
#endif
        }

        fs::path g_exe_dir;
        fs::path g_root;

        void ensure_initialized()
        {
            if (g_exe_dir.empty())
            {
                g_exe_dir = detect_exe_dir();
                if (g_exe_dir.empty())
                {
                    std::error_code ec;
                    g_exe_dir = fs::current_path(ec);
                    if (ec)
                        g_exe_dir = ".";
                }
            }
            if (g_root.empty())
                g_root = g_exe_dir / ".agent";
        }
    } // namespace

    const fs::path &exe_dir()
    {
        ensure_initialized();
        return g_exe_dir;
    }

    const fs::path &agent_root()
    {
        ensure_initialized();
        return g_root;
    }

    void set_agent_root(const fs::path &root)
    {
        ensure_initialized();
        if (root.empty())
        {
            g_root = exe_dir() / ".agent";
            return;
        }
        std::error_code ec;
        fs::path abs = root.is_absolute() ? root : exe_dir() / root;
        g_root = fs::weakly_canonical(abs, ec);
        if (ec)
            g_root = abs.lexically_normal();
    }

    fs::path resolve(const fs::path &p)
    {
        ensure_initialized();
        if (p.empty())
            return g_root;
        if (p.is_absolute())
            return p.lexically_normal();
        return (g_root / p).lexically_normal();
    }

    fs::path resolve(const std::string &p)
    {
        return resolve(fs::path(from_utf8(p)));
    }

    fs::path settings_file()
    {
        return agent_root() / "settings.json";
    }

    bool is_within(const fs::path &child, const fs::path &parent)
    {
        std::error_code ec;
        fs::path c = fs::weakly_canonical(child, ec);
        if (ec)
            c = child.lexically_normal();
        fs::path p = fs::weakly_canonical(parent, ec);
        if (ec)
            p = parent.lexically_normal();
        auto ci = c.begin();
        for (auto pi = p.begin(); pi != p.end(); ++pi, ++ci)
        {
            if (ci == c.end())
                return false;
            if (*ci != *pi)
                return false;
        }
        return true;
    }

    std::string to_utf8(const fs::path &p)
    {
#ifdef _WIN32
        const std::wstring &w = p.native();
        if (w.empty())
            return {};
        int need = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                       nullptr, 0, nullptr, nullptr);
        if (need <= 0)
            return {};
        std::string out(static_cast<size_t>(need), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                            out.data(), need, nullptr, nullptr);
        return out;
#else
        return p.string();
#endif
    }

    fs::path from_utf8(const std::string &s)
    {
#ifdef _WIN32
        if (s.empty())
            return {};
        int need = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (need <= 0)
            return fs::path(s);
        std::wstring w(static_cast<size_t>(need), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), need);
        return fs::path(w);
#else
        return fs::path(s);
#endif
    }
} // namespace paths
