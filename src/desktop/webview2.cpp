#include "desktop/webview2.hpp"

#include "core/base64.hpp"
#include "core/config.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"
#include "desktop/loader_assets.hpp"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <wrl/client.h>

#include "WebView2.h"

#include <functional>
#include <string>

using Microsoft::WRL::ComPtr;

namespace desktop
{
    namespace
    {
        // ==================================================================
        //  WebView2Loader 动态入口
        //  只依赖 DLL，不依赖导入库 —— MinGW / MSVC 都能直接链。
        // ==================================================================
        using CreateEnvFn = HRESULT(STDMETHODCALLTYPE *)(
            PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions *,
            ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *);

        CreateEnvFn g_create_env = nullptr;
        HMODULE g_loader = nullptr;

        std::wstring widen(const std::string &s)
        {
            if (s.empty())
                return {};
            int need = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
            if (need <= 0)
                return {};
            std::wstring w((size_t)need, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), need);
            return w;
        }

        // 把内嵌的 WebView2Loader.dll 落到 <agent_root>/runtime/ 再加载：
        // 发行物因此只有一个 exe，用户不需要额外放 DLL。
        bool extract_embedded_loader(std::wstring &out_path)
        {
            if (embedded::kAssetCount == 0)
                return false;
            const embedded::Asset &a = embedded::kAssets[0];

            std::error_code ec;
            fs::path dir = paths::agent_root() / "runtime";
            fs::create_directories(dir, ec);
            fs::path target = dir / paths::from_utf8(a.path);
            if (fs::exists(target, ec) && fs::file_size(target, ec) == a.size)
            {
                out_path = target.wstring();
                return true;
            }

            std::string b64;
            b64.reserve(a.size * 4 / 3 + 8);
            for (const char *const *p = a.chunks; p && *p; ++p)
                b64 += *p;
            const std::string bytes = base64::from_base64(b64);

            fs::path tmp = target;
            tmp += ".tmp";
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                if (!f)
                {
                    logx::error("webview2", "cannot write " + paths::to_utf8(tmp));
                    return false;
                }
                f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            }
            fs::remove(target, ec);
            fs::rename(tmp, target, ec);
            if (ec)
            {
                logx::error("webview2", "cannot install loader: " + ec.message());
                fs::remove(tmp, ec);
                return false;
            }
            out_path = target.wstring();
            return true;
        }

        bool load_loader()
        {
            if (g_create_env)
                return true;
            std::vector<fs::path> candidates;
            candidates.push_back(paths::exe_dir() / "WebView2Loader.dll");
            candidates.push_back(paths::exe_dir() / "webview2" / "WebView2Loader.dll");
            for (auto &p : candidates)
            {
                std::error_code ec;
                if (!fs::exists(p, ec))
                    continue;
                g_loader = LoadLibraryW(p.wstring().c_str());
                if (g_loader)
                    break;
            }
            if (!g_loader)
            {
                std::wstring embedded_path;
                if (extract_embedded_loader(embedded_path))
                    g_loader = LoadLibraryW(embedded_path.c_str());
            }
            if (!g_loader)
                g_loader = LoadLibraryW(L"WebView2Loader.dll"); // 交给系统搜索
            if (!g_loader)
            {
                logx::warn("webview2", "WebView2Loader.dll not found");
                return false;
            }
            g_create_env = (CreateEnvFn)GetProcAddress(g_loader, "CreateCoreWebView2EnvironmentWithOptions");
            if (!g_create_env)
            {
                logx::error("webview2", "CreateCoreWebView2EnvironmentWithOptions not exported");
                return false;
            }
            return true;
        }

        // ==================================================================
        //  COM 回调实现
        //  MinGW 的 MIDL_INTERFACE 会丢掉 uuid、__uuidof 不可用，
        //  所以这里显式带上接口 IID 做 QueryInterface。
        // ==================================================================
        IID iid_from(const wchar_t *s)
        {
            IID i{};
            IIDFromString(s, &i);
            return i;
        }

        const IID &env_completed_iid()
        {
            static IID g = iid_from(L"{4e8a3389-c9d8-4bd2-b6b5-124fee6cc14d}");
            return g;
        }

        const IID &controller_completed_iid()
        {
            static IID g = iid_from(L"{6c4819f3-c9b7-4260-8127-c9f5bde7f68c}");
            return g;
        }

        const IID &message_received_iid()
        {
            static IID g = iid_from(L"{57213f19-00e6-49fa-8e07-898ea01ecbd2}");
            return g;
        }

        const IID &nav_starting_iid()
        {
            static IID g = iid_from(L"{9adbe429-f36d-432b-9ddc-f8881fbd76e3}");
            return g;
        }

        const IID &nav_completed_iid()
        {
            static IID g = iid_from(L"{d33a35bf-1c49-4f98-93ab-006e0533fe1c}");
            return g;
        }

        const IID &new_window_iid()
        {
            static IID g = iid_from(L"{d4c185fe-c81c-4989-97af-2d3fa7ab5651}");
            return g;
        }

        const IID &controller2_iid()
        {
            static IID g = iid_from(L"{c979903e-d4ca-4228-92eb-47ee3fa96eab}");
            return g;
        }

        const IID &settings3_iid()
        {
            static IID g = iid_from(L"{fdb5ab74-af33-4854-84f0-0a631deb5eba}");
            return g;
        }

        // 引用计数基类：QueryInterface 只认 IUnknown + 自身 IID
        template <class IFace>
        class ComSink : public IFace
        {
        public:
            ULONG STDMETHODCALLTYPE AddRef() override
            {
                return static_cast<ULONG>(InterlockedIncrement(&ref_));
            }
            ULONG STDMETHODCALLTYPE Release() override
            {
                LONG r = InterlockedDecrement(&ref_);
                if (r == 0)
                    delete this;
                return static_cast<ULONG>(r);
            }
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
            {
                if (!ppv)
                    return E_POINTER;
                if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, self_iid_))
                {
                    *ppv = static_cast<IFace *>(this);
                    AddRef();
                    return S_OK;
                }
                *ppv = nullptr;
                return E_NOINTERFACE;
            }

        protected:
            explicit ComSink(const IID &self) : self_iid_(self) {}
            virtual ~ComSink() = default;
            LONG ref_ = 1;
            IID self_iid_;
        };

        // 一次性完成回调：调用完自释放
        class EnvCompleted
            : public ComSink<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>
        {
        public:
            using Fn = std::function<HRESULT(HRESULT, ICoreWebView2Environment *)>;
            explicit EnvCompleted(Fn fn)
                : ComSink(env_completed_iid()), fn_(std::move(fn)) {}
            HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Environment *env) override
            {
                HRESULT hr = fn_(result, env);
                Release();
                return hr;
            }

        private:
            Fn fn_;
        };

        class ControllerCompleted
            : public ComSink<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>
        {
        public:
            using Fn = std::function<HRESULT(HRESULT, ICoreWebView2Controller *)>;
            explicit ControllerCompleted(Fn fn)
                : ComSink(controller_completed_iid()), fn_(std::move(fn)) {}
            HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Controller *controller) override
            {
                HRESULT hr = fn_(result, controller);
                Release();
                return hr;
            }

        private:
            Fn fn_;
        };

        // 常驻事件回调：由 WebView2 持有引用，窗口销毁时随 ComPtr 一起释放
        class MessageReceived
            : public ComSink<ICoreWebView2WebMessageReceivedEventHandler>
        {
        public:
            using Fn = std::function<void(const std::wstring &)>;
            explicit MessageReceived(Fn fn)
                : ComSink(message_received_iid()), fn_(std::move(fn)) {}
            HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *,
                                             ICoreWebView2WebMessageReceivedEventArgs *args) override
            {
                if (!args)
                    return S_OK;
                LPWSTR raw = nullptr;
                if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw)
                {
                    std::wstring msg(raw);
                    CoTaskMemFree(raw);
                    fn_(msg);
                }
                return S_OK;
            }

        private:
            Fn fn_;
        };

        class NavStarting
            : public ComSink<ICoreWebView2NavigationStartingEventHandler>
        {
        public:
            using Fn = std::function<void(ICoreWebView2NavigationStartingEventArgs *, const std::wstring &)>;
            explicit NavStarting(Fn fn)
                : ComSink(nav_starting_iid()), fn_(std::move(fn)) {}
            HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *,
                                             ICoreWebView2NavigationStartingEventArgs *args) override
            {
                if (!args)
                    return S_OK;
                LPWSTR uri = nullptr;
                if (SUCCEEDED(args->get_Uri(&uri)) && uri)
                {
                    std::wstring u(uri);
                    CoTaskMemFree(uri);
                    fn_(args, u);
                }
                return S_OK;
            }

        private:
            Fn fn_;
        };

        class NavCompleted
            : public ComSink<ICoreWebView2NavigationCompletedEventHandler>
        {
        public:
            using Fn = std::function<void(BOOL)>;
            explicit NavCompleted(Fn fn)
                : ComSink(nav_completed_iid()), fn_(std::move(fn)) {}
            HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *,
                                             ICoreWebView2NavigationCompletedEventArgs *args) override
            {
                BOOL ok = FALSE;
                if (args)
                    args->get_IsSuccess(&ok);
                fn_(ok);
                return S_OK;
            }

        private:
            Fn fn_;
        };

        class NewWindow
            : public ComSink<ICoreWebView2NewWindowRequestedEventHandler>
        {
        public:
            using Fn = std::function<void(ICoreWebView2NewWindowRequestedEventArgs *, const std::wstring &)>;
            explicit NewWindow(Fn fn)
                : ComSink(new_window_iid()), fn_(std::move(fn)) {}
            HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *,
                                             ICoreWebView2NewWindowRequestedEventArgs *args) override
            {
                if (!args)
                    return S_OK;
                args->put_Handled(TRUE);
                LPWSTR uri = nullptr;
                if (SUCCEEDED(args->get_Uri(&uri)) && uri)
                {
                    std::wstring u(uri);
                    CoTaskMemFree(uri);
                    fn_(args, u);
                }
                return S_OK;
            }

        private:
            Fn fn_;
        };

        // ==================================================================
        //  窗口
        // ==================================================================
        struct AppState
        {
            HWND hwnd = nullptr;
            ComPtr<ICoreWebView2Controller> controller;
            ComPtr<ICoreWebView2> webview;
            ComPtr<ICoreWebView2WebMessageReceivedEventHandler> on_message;
            ComPtr<ICoreWebView2NavigationStartingEventHandler> on_nav_start;
            ComPtr<ICoreWebView2NavigationCompletedEventHandler> on_nav_done;
            ComPtr<ICoreWebView2NewWindowRequestedEventHandler> on_new_window;
            int min_w = 960;
            int min_h = 640;
            bool ready = false;
            bool shown = false;
            // 托盘
            NOTIFYICONDATAW nid{};
            HICON tray_icon = nullptr;
            bool tray = false;
            bool close_to_tray = true;
            bool quitting = false;
            bool tray_hint_shown = false;
        };

        AppState g_app;
        const wchar_t *kClassName = L"AgentCppWebViewHost";
        HANDLE g_instance_mutex = nullptr;

        constexpr UINT kTrayCallback = WM_APP + 1;
        constexpr UINT kTrayShow = 40001;
        constexpr UINT kTrayOpenData = 40002;
        constexpr UINT kTrayExit = 40003;

        void resize_webview()
        {
            if (!g_app.controller || !g_app.hwnd)
                return;
            RECT rc;
            GetClientRect(g_app.hwnd, &rc);
            if (rc.right <= rc.left || rc.bottom <= rc.top)
                return;
            g_app.controller->put_Bounds(rc);
        }

        void post_window_state()
        {
            if (!g_app.webview || !g_app.hwnd)
                return;
            if (IsIconic(g_app.hwnd))
                return;
            g_app.webview->PostWebMessageAsString(IsZoomed(g_app.hwnd) ? L"win:state:max"
                                                                       : L"win:state:normal");
        }

        void show_window_once()
        {
            if (g_app.shown || !g_app.hwnd)
                return;
            g_app.shown = true;
            KillTimer(g_app.hwnd, 1);
            ShowWindow(g_app.hwnd, SW_SHOW);
            UpdateWindow(g_app.hwnd);
        }

        void restore_main_window()
        {
            if (!g_app.hwnd)
                return;
            if (IsIconic(g_app.hwnd))
                ShowWindow(g_app.hwnd, SW_RESTORE);
            else
                ShowWindow(g_app.hwnd, SW_SHOW);
            SetForegroundWindow(g_app.hwnd);
        }

        // 页面里的拖拽/缩放热区 → 交给系统非客户区循环处理，
        // 这样贴边分屏、双击最大化等原生行为都保留。
        void begin_native_drag(WPARAM hit)
        {
            if (!g_app.hwnd)
                return;
            ReleaseCapture();
            POINT pt{};
            GetCursorPos(&pt);
            SendMessageW(g_app.hwnd, WM_NCLBUTTONDOWN, hit,
                         MAKELPARAM((short)pt.x, (short)pt.y));
        }

        void toggle_maximize()
        {
            if (!g_app.hwnd)
                return;
            ShowWindow(g_app.hwnd, IsZoomed(g_app.hwnd) ? SW_RESTORE : SW_MAXIMIZE);
        }

        void handle_ipc(const std::wstring &msg)
        {
            if (msg == L"win:drag")
            {
                begin_native_drag(HTCAPTION);
            }
            else if (msg == L"win:min")
            {
                ShowWindow(g_app.hwnd, SW_MINIMIZE);
            }
            else if (msg == L"win:max")
            {
                toggle_maximize();
            }
            else if (msg == L"win:close")
            {
                PostMessageW(g_app.hwnd, WM_CLOSE, 0, 0);
            }
            else if (msg.rfind(L"win:resize:", 0) == 0)
            {
                const std::wstring dir = msg.substr(11);
                WPARAM hit = HTBOTTOMRIGHT;
                if (dir == L"l")
                    hit = HTLEFT;
                else if (dir == L"r")
                    hit = HTRIGHT;
                else if (dir == L"t")
                    hit = HTTOP;
                else if (dir == L"b")
                    hit = HTBOTTOM;
                else if (dir == L"tl")
                    hit = HTTOPLEFT;
                else if (dir == L"tr")
                    hit = HTTOPRIGHT;
                else if (dir == L"bl")
                    hit = HTBOTTOMLEFT;
                begin_native_drag(hit);
            }
        }

        bool is_internal_url(const std::wstring &uri)
        {
            static const wchar_t *kAllowed[] = {
                L"http://127.0.0.1", L"http://localhost", L"http://[::1]",
                L"about:", L"data:", L"blob:", L"chrome-error:"};
            for (auto p : kAllowed)
                if (uri.rfind(p, 0) == 0)
                    return true;
            return false;
        }

        void open_external(const std::wstring &uri)
        {
            if (uri.empty())
                return;
            ShellExecuteW(nullptr, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }

        // 托盘实现放在 make_icon 之后，这里先声明给窗口过程用
        void tray_add(const std::wstring &tip);
        void tray_remove();
        void tray_menu();
        void tray_balloon(const wchar_t *title, const wchar_t *text);

        LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
        {
            switch (msg)
            {
            case WM_NCCALCSIZE:
                // wParam == TRUE 时返回 0：客户区铺满整窗，系统标题栏消失
                if (wp)
                    return 0;
                break;
            case WM_GETMINMAXINFO:
            {
                auto *mmi = reinterpret_cast<MINMAXINFO *>(lp);
                mmi->ptMinTrackSize.x = g_app.min_w;
                mmi->ptMinTrackSize.y = g_app.min_h;
                // 最大化时贴合工作区，不盖住任务栏
                MONITORINFO mi{};
                mi.cbSize = sizeof(mi);
                if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
                {
                    mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                    mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                    mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                    mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
                }
                return 0;
            }
            case WM_SIZE:
                resize_webview();
                post_window_state();
                return 0;
            case WM_SETFOCUS:
                if (g_app.controller)
                    g_app.controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                return 0;
            case WM_TIMER:
                if (wp == 1)
                    show_window_once();
                return 0;
            case kTrayCallback:
                if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_LBUTTONDBLCLK)
                    restore_main_window();
                else if (LOWORD(lp) == WM_RBUTTONUP)
                    tray_menu();
                return 0;
            case WM_CLOSE:
                // 关闭 = 收进托盘；托盘菜单里的「退出」才真的结束进程
                if (g_app.tray && g_app.close_to_tray && !g_app.quitting)
                {
                    ShowWindow(hwnd, SW_HIDE);
                    if (!g_app.tray_hint_shown)
                    {
                        g_app.tray_hint_shown = true;
                        tray_balloon(L"agent.cpp 仍在后台运行",
                                     L"点击托盘图标可以重新打开窗口，右键菜单可以退出。");
                    }
                    return 0;
                }
                DestroyWindow(hwnd);
                return 0;
            case WM_DESTROY:
                tray_remove();
                PostQuitMessage(0);
                return 0;
            default:
                break;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        // 运行时画一个圆角图标，省掉外部 .ico 资源
        HICON make_icon(int size)
        {
            HDC screen = GetDC(nullptr);
            HDC mem = CreateCompatibleDC(screen);
            HBITMAP bmp = CreateCompatibleBitmap(screen, size, size);
            HGDIOBJ old = SelectObject(mem, bmp);

            HBRUSH bg = CreateSolidBrush(RGB(99, 102, 241));
            HGDIOBJ oldBrush = SelectObject(mem, bg);
            HPEN pen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
            HGDIOBJ oldPen = SelectObject(mem, pen);
            RoundRect(mem, 0, 0, size, size, size / 4, size / 4);
            SelectObject(mem, oldPen);
            SelectObject(mem, oldBrush);
            DeleteObject(pen);
            DeleteObject(bg);

            SetBkMode(mem, TRANSPARENT);
            SetTextColor(mem, RGB(255, 255, 255));
            HFONT font = CreateFontW(size / 2, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            HGDIOBJ oldFont = SelectObject(mem, font);
            RECT text_rc{0, 0, size, size};
            DrawTextW(mem, L"a", -1, &text_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(mem, oldFont);
            DeleteObject(font);

            SelectObject(mem, old);
            DeleteDC(mem);
            ReleaseDC(nullptr, screen);

            HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
            ICONINFO ii{};
            ii.fIcon = TRUE;
            ii.hbmColor = bmp;
            ii.hbmMask = mask;
            HICON icon = CreateIconIndirect(&ii);
            DeleteObject(bmp);
            DeleteObject(mask);
            return icon;
        }

        // ==================================================================
        //  系统托盘
        // ==================================================================
        void tray_add(const std::wstring &tip)
        {
            if (!g_app.hwnd || g_app.tray)
                return;
            g_app.nid.cbSize = sizeof(NOTIFYICONDATAW);
            g_app.nid.hWnd = g_app.hwnd;
            g_app.nid.uID = 1;
            g_app.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            g_app.nid.uCallbackMessage = kTrayCallback;
            if (!g_app.tray_icon)
                g_app.tray_icon = make_icon(32);
            g_app.nid.hIcon = g_app.tray_icon;
            lstrcpynW(g_app.nid.szTip, tip.c_str(), ARRAYSIZE(g_app.nid.szTip));
            g_app.tray = Shell_NotifyIconW(NIM_ADD, &g_app.nid) != FALSE;
            logx::info("desktop", g_app.tray ? "tray icon installed" : "tray icon not installed");
        }

        void tray_remove()
        {
            if (!g_app.tray)
                return;
            Shell_NotifyIconW(NIM_DELETE, &g_app.nid);
            g_app.tray = false;
            if (g_app.tray_icon)
            {
                DestroyIcon(g_app.tray_icon);
                g_app.tray_icon = nullptr;
            }
        }

        void tray_balloon(const wchar_t *title, const wchar_t *text)
        {
            if (!g_app.tray)
                return;
            NOTIFYICONDATAW nid = g_app.nid;
            nid.uFlags = NIF_INFO;
            nid.dwInfoFlags = NIIF_INFO;
            lstrcpynW(nid.szInfoTitle, title, ARRAYSIZE(nid.szInfoTitle));
            lstrcpynW(nid.szInfo, text, ARRAYSIZE(nid.szInfo));
            Shell_NotifyIconW(NIM_MODIFY, &nid);
        }

        void tray_menu()
        {
            if (!g_app.hwnd)
                return;
            HMENU menu = CreatePopupMenu();
            if (!menu)
                return;
            AppendMenuW(menu, MF_STRING, kTrayShow, L"显示主窗口");
            AppendMenuW(menu, MF_STRING, kTrayOpenData, L"打开数据目录");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, kTrayExit, L"退出");

            POINT pt{};
            GetCursorPos(&pt);
            // 不先抢前台的话，菜单点外面不会消失
            SetForegroundWindow(g_app.hwnd);
            const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                            pt.x, pt.y, 0, g_app.hwnd, nullptr);
            DestroyMenu(menu);
            PostMessageW(g_app.hwnd, WM_NULL, 0, 0);

            if (cmd == kTrayShow)
            {
                restore_main_window();
            }
            else if (cmd == kTrayOpenData)
            {
                ShellExecuteW(nullptr, L"open", paths::agent_root().wstring().c_str(),
                              nullptr, nullptr, SW_SHOWNORMAL);
            }
            else if (cmd == kTrayExit)
            {
                g_app.quitting = true;
                PostMessageW(g_app.hwnd, WM_CLOSE, 0, 0);
            }
        }

        std::wstring user_data_folder()
        {
            fs::path p = paths::agent_root() / "webview2";
            std::error_code ec;
            fs::create_directories(p, ec);
            return p.wstring();
        }

        void set_dwm_attributes(HWND hwnd)
        {
            // DWM 属性只有 Win10 1809+ 才有，取不到就算了
            using DwmSetWindowAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
            HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
            if (!dwm)
                return;
            auto set_attr = (DwmSetWindowAttributeFn)GetProcAddress(dwm, "DwmSetWindowAttribute");
            if (!set_attr)
                return;
            constexpr DWORD kDarkMode = 20;          // DWMWA_USE_IMMERSIVE_DARK_MODE
            constexpr DWORD kCornerPreference = 33;  // DWMWA_WINDOW_CORNER_PREFERENCE
            BOOL dark = TRUE;
            set_attr(hwnd, kDarkMode, &dark, sizeof(dark));
            int corner = 2; // DWMWCP_ROUND
            set_attr(hwnd, kCornerPreference, &corner, sizeof(corner));
        }
    } // namespace

    bool available()
    {
        return load_loader();
    }

    bool open_in_browser(const std::string &url)
    {
        const std::wstring w = widen(url);
        HINSTANCE r = ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<std::intptr_t>(r) > 32;
    }

    void hide_console_if_standalone()
    {
        HWND console = GetConsoleWindow();
        if (!console)
            return;
        // 从 cmd / PowerShell 启动时保留控制台，双击启动才隐藏
        DWORD owners[4] = {0, 0, 0, 0};
        DWORD n = GetConsoleProcessList(owners, 4);
        if (n > 1)
            return;
        ShowWindow(console, SW_HIDE);
    }

    bool single_instance_acquire()
    {
        if (g_instance_mutex)
            return true;
        g_instance_mutex = CreateMutexW(nullptr, TRUE, L"Local\\agentcpp.single.instance");
        if (!g_instance_mutex)
            return true; // 拿不到锁不阻塞启动
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            CloseHandle(g_instance_mutex);
            g_instance_mutex = nullptr;
            return false;
        }
        return true;
    }

    void single_instance_release()
    {
        if (!g_instance_mutex)
            return;
        ReleaseMutex(g_instance_mutex);
        CloseHandle(g_instance_mutex);
        g_instance_mutex = nullptr;
    }

    bool activate_existing()
    {
        HWND hwnd = FindWindowW(kClassName, nullptr);
        if (!hwnd)
            return false;
        if (IsIconic(hwnd))
            ShowWindow(hwnd, SW_RESTORE);
        else if (!IsWindowVisible(hwnd))
            ShowWindow(hwnd, SW_SHOW); // 收在托盘里的窗口
        SetForegroundWindow(hwnd);
        return true;
    }

    void fatal_dialog(const std::string &title, const std::string &text)
    {
        MessageBoxW(nullptr, widen(text).c_str(), widen(title).c_str(), MB_ICONERROR | MB_OK);
    }

    int run(const WindowOptions &opts)
    {
        if (!load_loader())
            return -1;

        {
            using SetDpiCtxFn = BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            auto set_ctx = user32 ? (SetDpiCtxFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext") : nullptr;
            if (set_ctx)
                set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            else
                SetProcessDPIAware();
        }

        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        HINSTANCE inst = GetModuleHandleW(nullptr);
        HBRUSH bg = CreateSolidBrush(RGB(11, 11, 16));

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = wnd_proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
        wc.hbrBackground = bg;
        wc.lpszClassName = kClassName;
        wc.hIcon = make_icon(64);
        wc.hIconSm = wc.hIcon;
        if (!RegisterClassExW(&wc))
        {
            logx::error("webview2", "RegisterClassEx failed");
            DeleteObject(bg);
            return -2;
        }

        const std::wstring title = widen(opts.title);

        // 无边框窗口：WS_POPUP 去掉标题栏，WS_THICKFRAME 保留投影与贴边分屏
        DWORD style = WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU;
        if (opts.resizable)
            style |= WS_THICKFRAME | WS_MAXIMIZEBOX;

        RECT work{0, 0, 1280, 860};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int w = opts.width > 320 ? opts.width : 1280;
        const int h = opts.height > 240 ? opts.height : 860;
        const int x = work.left + ((work.right - work.left) - w) / 2;
        const int y = work.top + ((work.bottom - work.top) - h) / 2;

        HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, kClassName, title.c_str(), style,
                                    x, y, w, h, nullptr, nullptr, inst, nullptr);
        if (!hwnd)
        {
            logx::error("webview2", "CreateWindowEx failed");
            DeleteObject(bg);
            return -3;
        }
        g_app.hwnd = hwnd;
        g_app.min_w = opts.min_width > 0 ? opts.min_width : 960;
        g_app.min_h = opts.min_height > 0 ? opts.min_height : 640;
        g_app.close_to_tray = opts.close_to_tray;

        set_dwm_attributes(hwnd);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (opts.tray)
            tray_add(widen(opts.tray_tooltip.empty() ? opts.title : opts.tray_tooltip));
        // 先藏在后台把 WebView 建好，等首次导航完成再显形，避免白屏闪一下
        SetTimer(hwnd, 1, 4000, nullptr);

        const std::wstring url = widen(opts.url);
        const std::wstring data_dir = user_data_folder();
        const bool devtools = opts.devtools;

        HRESULT hr = g_create_env(
            nullptr, data_dir.c_str(), nullptr,
            new EnvCompleted(
                [hwnd, url, devtools](HRESULT result, ICoreWebView2Environment *env) -> HRESULT
                {
                    if (FAILED(result) || !env)
                    {
                        logx::error("webview2", "CreateCoreWebView2Environment failed");
                        MessageBoxW(hwnd, L"WebView2 环境创建失败，请确认已安装 WebView2 运行时。",
                                    L"agent.cpp", MB_ICONERROR | MB_OK);
                        PostMessageW(hwnd, WM_CLOSE, 0, 0);
                        return result;
                    }
                    env->CreateCoreWebView2Controller(
                        hwnd,
                        new ControllerCompleted(
                            [url, devtools](HRESULT r2, ICoreWebView2Controller *controller) -> HRESULT
                            {
                                if (FAILED(r2) || !controller)
                                {
                                    logx::error("webview2", "CreateCoreWebView2Controller failed");
                                    PostMessageW(g_app.hwnd, WM_CLOSE, 0, 0);
                                    return r2;
                                }
                                g_app.controller = controller;
                                controller->get_CoreWebView2(g_app.webview.GetAddressOf());

                                // 深色底色：页面加载前不闪白
                                ComPtr<ICoreWebView2Controller2> c2;
                                if (g_app.controller &&
                                    SUCCEEDED(g_app.controller->QueryInterface(
                                        controller2_iid(), reinterpret_cast<void **>(c2.GetAddressOf()))) &&
                                    c2)
                                {
                                    COREWEBVIEW2_COLOR bg{255, 11, 11, 16};
                                    c2->put_DefaultBackgroundColor(bg);
                                }

                                ComPtr<ICoreWebView2Settings> settings;
                                if (g_app.webview &&
                                    SUCCEEDED(g_app.webview->get_Settings(settings.GetAddressOf())) && settings)
                                {
                                    settings->put_AreDefaultContextMenusEnabled(FALSE);
                                    settings->put_IsStatusBarEnabled(FALSE);
                                    settings->put_AreDevToolsEnabled(devtools ? TRUE : FALSE);
                                    settings->put_IsZoomControlEnabled(FALSE);
                                    settings->put_IsBuiltInErrorPageEnabled(FALSE);
                                    // 关掉 F5 / Ctrl+P 之类的浏览器快捷键，行为向原生应用看齐
                                    ComPtr<ICoreWebView2Settings3> s3;
                                    if (SUCCEEDED(settings->QueryInterface(
                                            settings3_iid(), reinterpret_cast<void **>(s3.GetAddressOf()))) &&
                                        s3)
                                        s3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
                                }

                                // ---- 页面 → 原生：窗口控制 ----
                                if (!g_app.webview)
                                    return S_OK;
                                g_app.on_message = new MessageReceived(
                                    [](const std::wstring &msg)
                                    { handle_ipc(msg); });
                                EventRegistrationToken tok{};
                                g_app.webview->add_WebMessageReceived(g_app.on_message.Get(), &tok);

                                // ---- 外部链接交给系统浏览器 ----
                                g_app.on_nav_start = new NavStarting(
                                    [](ICoreWebView2NavigationStartingEventArgs *args, const std::wstring &uri)
                                    {
                                        if (is_internal_url(uri))
                                            return;
                                        args->put_Cancel(TRUE);
                                        open_external(uri);
                                    });
                                g_app.webview->add_NavigationStarting(g_app.on_nav_start.Get(), &tok);

                                g_app.on_new_window = new NewWindow(
                                    [](ICoreWebView2NewWindowRequestedEventArgs *, const std::wstring &uri)
                                    { open_external(uri); });
                                g_app.webview->add_NewWindowRequested(g_app.on_new_window.Get(), &tok);

                                g_app.on_nav_done = new NavCompleted(
                                    [](BOOL ok)
                                    {
                                        if (!ok)
                                            logx::warn("webview2", "first navigation reported failure");
                                        g_app.ready = true;
                                        show_window_once();
                                    });
                                g_app.webview->add_NavigationCompleted(g_app.on_nav_done.Get(), &tok);

                                resize_webview();
                                if (g_app.webview)
                                    g_app.webview->Navigate(url.c_str());
                                return S_OK;
                            }));
                    return S_OK;
                }));

        if (FAILED(hr))
        {
            logx::error("webview2", "CreateCoreWebView2EnvironmentWithOptions failed");
            DestroyWindow(hwnd);
            DeleteObject(bg);
            return -4;
        }

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        g_app.on_message.Reset();
        g_app.on_nav_start.Reset();
        g_app.on_nav_done.Reset();
        g_app.on_new_window.Reset();
        if (g_app.controller)
        {
            g_app.controller->Close();
            g_app.controller.Reset();
        }
        g_app.webview.Reset();
        DeleteObject(bg);
        return 0;
    }
} // namespace desktop

#else // 非 Windows：无桌面外壳

namespace desktop
{
    bool available() { return false; }
    int run(const WindowOptions &) { return -1; }

    bool open_in_browser(const std::string &url)
    {
        std::string cmd;
#if defined(__APPLE__)
        cmd = "open '" + url + "' &";
#else
        cmd = "xdg-open '" + url + "' >/dev/null 2>&1 &";
#endif
        return std::system(cmd.c_str()) == 0;
    }

    void hide_console_if_standalone() {}
    bool single_instance_acquire() { return true; }
    void single_instance_release() {}
    bool activate_existing() { return false; }
    void fatal_dialog(const std::string &, const std::string &text)
    {
        std::fprintf(stderr, "%s\n", text.c_str());
    }
} // namespace desktop

#endif
