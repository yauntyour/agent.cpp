// agent-voice.hpp — 语音服务层（TTS / ASR / 原生录音）
// =============================================================================
// 「服务」是这个项目的一等公民：语音合成、语音识别各自是一份可替换的配置，
// 界面里改完即时生效，不重新编译。
//
//   kind = "openai"   OpenAI 兼容端点（/v1/audio/speech、/v1/audio/transcriptions），
//                     本地 llama.cpp / faster-whisper-server / GPT-SoVITS 的
//                     OpenAI 兼容层都归这一类
//   kind = "http"     自定义 JSON 模板（GPT-SoVITS api_v2 这类自有协议的 TTS）
//   kind = "command"  跑本地命令（piper / bert-vits2 / whisper-cli …），
//                     文本走文件、音频走文件，命令行里不放正文
//   kind = "off"      关闭
//
// 录音在宿主侧完成（Windows MCI，16k/16bit/单声道），页面只发命令，
// 因此不需要浏览器的麦克风权限 —— WebView2 里也不弹权限框。
//
// 所有网络请求都走 libcurl，CA 由 cell::net::ca_bundle() 解析（与模型请求同一套信任链）。
#pragma once

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

namespace app::voice
{
    namespace fs = std::filesystem;

    // ---------------------------------------------------------------------
    //  服务配置
    // ---------------------------------------------------------------------
    struct service
    {
        std::string kind = "off"; // openai | http | command | off
        std::string base;         // openai: https://api.openai.com/v1
        std::string path;         // openai: /audio/speech（补在 base 后面）
        std::string url;          // http: 完整 URL
        std::string body;         // http: JSON 模板（{text} {voice} {model} {lang}）
        std::string model;
        std::string voice;
        std::string format = "mp3"; // mp3 | wav | opus | aac | flac | pcm
        std::string key;
        std::string proxy;
        std::string language = "zh";
        std::string prompt;       // ASR 提示词（专有名词表）
        std::string cmd;          // command: {in} {out} {text} {voice} 占位符
        long timeout_s = 60;

        bool enabled() const { return kind != "off" && kind != ""; }
        std::string endpoint(const std::string &dflt_path) const
        {
            if (kind == "http")
                return url;
            std::string b = base;
            while (!b.empty() && b.back() == '/')
                b.pop_back();
            std::string p = path.empty() ? dflt_path : path;
            if (!p.empty() && p.front() != '/')
                p.insert(p.begin(), '/');
            return b + p;
        }
        nlohmann::json to_json() const
        {
            return {{"kind", kind}, {"base", base}, {"path", path}, {"url", url}, {"body", body},
                    {"model", model}, {"voice", voice}, {"format", format}, {"language", language},
                    {"prompt", prompt}, {"cmd", cmd}, {"timeout_s", timeout_s},
                    {"has_key", !key.empty()}};
        }
        static service from_json(const nlohmann::json &j)
        {
            service s;
            auto str = [&](const char *k, std::string &dst)
            {
                auto it = j.find(k);
                if (it != j.end() && it->is_string())
                    dst = it->get<std::string>();
            };
            str("kind", s.kind);
            str("base", s.base);
            str("path", s.path);
            str("url", s.url);
            str("body", s.body);
            str("model", s.model);
            str("voice", s.voice);
            str("format", s.format);
            str("language", s.language);
            str("prompt", s.prompt);
            str("cmd", s.cmd);
            auto t = j.find("timeout_s");
            if (t != j.end() && t->is_number_integer())
                s.timeout_s = std::clamp<long>(t->get<long>(), 5, 600);
            return s;
        }
    };

    // ---------------------------------------------------------------------
    //  HTTP 基座
    // ---------------------------------------------------------------------
    struct reply
    {
        bool ok = false;
        long status = 0;
        std::string body;
        std::string err;
    };

    namespace detail
    {
        inline size_t write_to_string(char *ptr, size_t size, size_t nmemb, void *ud)
        {
            auto *s = static_cast<std::string *>(ud);
            size_t n = size * nmemb;
            if (s->size() + n > (size_t)256 * 1024 * 1024)
                return 0; // 语音文件不该更大；超了直接断
            s->append(ptr, n);
            return n;
        }

        inline void base_opts(CURL *c, const service &svc, std::string *body)
        {
            curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(c, CURLOPT_TIMEOUT, svc.timeout_s);
            curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
            curl_easy_setopt(c, CURLOPT_USERAGENT, "agent.cpp/1.0");
            curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_to_string);
            curl_easy_setopt(c, CURLOPT_WRITEDATA, body);
            if (!svc.proxy.empty())
                curl_easy_setopt(c, CURLOPT_PROXY, svc.proxy.c_str());
            if (const char *ca = cell::net::ca_bundle(); ca && *ca)
                curl_easy_setopt(c, CURLOPT_CAINFO, ca);
        }

        inline std::string auth_header(const service &svc)
        {
            return svc.key.empty() ? std::string() : ("Authorization: Bearer " + svc.key);
        }

        inline reply finish(CURL *c, reply r)
        {
            curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
            if (r.status >= 200 && r.status < 300)
                r.ok = true;
            else if (r.err.empty())
            {
                std::string msg = cell::net::error_message(r.body);
                r.err = msg.empty() ? std::format("HTTP {}", r.status) : msg;
            }
            return r;
        }

        // text 作为 JSON 字符串内容出现：转义后替换模板里的 {text}
        inline std::string json_escape_text(const std::string &s)
        {
            std::string out;
            out.reserve(s.size() + 8);
            for (unsigned char ch : s)
            {
                switch (ch)
                {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (ch < 0x20)
                        out += std::format("\\u{:04x}", ch);
                    else
                        out.push_back((char)ch);
                }
            }
            return out;
        }

        inline void replace_all(std::string &s, const std::string &from, const std::string &to)
        {
            if (from.empty())
                return;
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos)
            {
                s.replace(pos, from.size(), to);
                pos += to.size();
            }
        }

        inline reply post_json(const service &svc, const std::string &url, const std::string &payload)
        {
            reply r;
            CURL *c = curl_easy_init();
            if (!c)
            {
                r.err = "curl init failed";
                return r;
            }
            detail::base_opts(c, svc, &r.body);
            struct curl_slist *hdrs = nullptr;
            hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
            if (svc.kind != "http")
                hdrs = curl_slist_append(hdrs, "Accept: audio/*");
            if (std::string a = auth_header(svc); !a.empty())
                hdrs = curl_slist_append(hdrs, a.c_str());
            curl_easy_setopt(c, CURLOPT_URL, url.c_str());
            curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
            curl_easy_setopt(c, CURLOPT_POSTFIELDS, payload.data());
            curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)payload.size());
            CURLcode rc = curl_easy_perform(c);
            if (rc != CURLE_OK)
                r.err = curl_easy_strerror(rc);
            curl_slist_free_all(hdrs);
            r = finish(c, std::move(r));
            curl_easy_cleanup(c);
            return r;
        }

        // multipart：一个音频文件 + 若干文本字段（ASR）
        inline reply post_audio(const service &svc, const std::string &url, const fs::path &file,
                                const std::vector<std::pair<std::string, std::string>> &fields)
        {
            reply r;
            CURL *c = curl_easy_init();
            if (!c)
            {
                r.err = "curl init failed";
                return r;
            }
            detail::base_opts(c, svc, &r.body);
            curl_mime *mime = curl_mime_init(c);
            curl_mimepart *part = curl_mime_addpart(mime);
            curl_mime_name(part, "file");
            std::string fname = file.filename().string();
            curl_mime_filename(part, fname.c_str());
            curl_mime_filedata(part, file.string().c_str());
            for (auto &[k, v] : fields)
            {
                if (v.empty())
                    continue;
                curl_mimepart *f = curl_mime_addpart(mime);
                curl_mime_name(f, k.c_str());
                curl_mime_data(f, v.c_str(), CURL_ZERO_TERMINATED);
            }
            struct curl_slist *hdrs = nullptr;
            if (std::string a = auth_header(svc); !a.empty())
                hdrs = curl_slist_append(hdrs, a.c_str());
            curl_easy_setopt(c, CURLOPT_URL, url.c_str());
            if (hdrs)
                curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
            curl_easy_setopt(c, CURLOPT_MIMEPOST, mime);
            CURLcode rc = curl_easy_perform(c);
            if (rc != CURLE_OK)
                r.err = curl_easy_strerror(rc);
            curl_slist_free_all(hdrs);
            curl_mime_free(mime);
            r = finish(c, std::move(r));
            curl_easy_cleanup(c);
            return r;
        }
    } // namespace detail

    // ---------------------------------------------------------------------
    //  通用下载（舞台运行时：Cubism Core 之类不随 exe 分发，由用户点一下取回）
    // ---------------------------------------------------------------------
    inline bool fetch(const std::string &url, std::string &out, std::string &err)
    {
        err.clear();
        out.clear();
        service svc;          // 只借它的超时 / 代理 / CA 设置
        svc.timeout_s = 180;
        CURL *c = curl_easy_init();
        if (!c)
        {
            err = "curl 初始化失败";
            return false;
        }
        detail::base_opts(c, svc, &out);
        curl_easy_setopt(c, CURLOPT_URL, url.c_str());
        CURLcode rc = curl_easy_perform(c);
        reply r = detail::finish(c, reply{});
        curl_easy_cleanup(c);
        if (rc != CURLE_OK)
        {
            err = std::string("下载失败：") + curl_easy_strerror(rc);
            return false;
        }
        if (!r.ok)
        {
            err = r.err.empty() ? std::format("HTTP {}", r.status) : r.err;
            return false;
        }
        if (out.empty())
        {
            err = "下载到空文件";
            return false;
        }
        return true;
    }

    // ---------------------------------------------------------------------
    //  TTS
    // ---------------------------------------------------------------------
    // 把 text 合成到 out（扩展名由调用方按 format 给好）。失败时 err 有话说。
    inline bool tts(const service &svc, const std::string &text, const std::string &voice_override,
                    const fs::path &out, std::string &err)
    {
        err.clear();
        if (!svc.enabled())
        {
            err = "TTS 服务未启用";
            return false;
        }
        if (text.empty())
        {
            err = "空文本";
            return false;
        }
        const std::string voice = voice_override.empty() ? svc.voice : voice_override;

        if (svc.kind == "command")
        {
            if (svc.cmd.empty())
            {
                err = "命令式 TTS 缺少 cmd";
                return false;
            }
            std::error_code ec;
            fs::path dir = out.parent_path();
            fs::create_directories(dir, ec);
            fs::path txt = dir / (out.stem().string() + ".txt");
            {
                std::ofstream f(txt, std::ios::binary | std::ios::trunc);
                if (!f)
                {
                    err = "写不了文本文件";
                    return false;
                }
                f << text;
            }
            std::string cmd = svc.cmd;
            // 路径带空格是常态（Program Files / 中文目录），占位符一律加引号
            detail::replace_all(cmd, "{in}", "\"" + txt.string() + "\"");
            detail::replace_all(cmd, "{out}", "\"" + out.string() + "\"");
            detail::replace_all(cmd, "{voice}", voice);
            // {text} 只做最保守的引号包裹；正文请用 {in}
            detail::replace_all(cmd, "{text}", "\"" + text + "\"");
            std::string output, stderr_out;
            int code = -1;
            bool ran = cell::plat::spawn_cmd(cmd, (double)svc.timeout_s, output, code, stderr_out, dir);
            fs::remove(txt, ec);
            if (!ran)
            {
                err = "无法启动 TTS 命令";
                return false;
            }
            if (code != 0)
            {
                err = std::format("TTS 命令退出码 {}：{}", code,
                                  stderr_out.empty() ? output.substr(0, 300) : stderr_out.substr(0, 300));
                return false;
            }
            if (!fs::is_regular_file(out, ec) || fs::file_size(out, ec) < 128)
            {
                err = "TTS 命令没有产出音频文件";
                return false;
            }
            return true;
        }

        std::string payload;
        std::string url;
        if (svc.kind == "http")
        {
            payload = svc.body;
            if (payload.empty())
            {
                err = "http 式 TTS 缺少 body 模板";
                return false;
            }
            detail::replace_all(payload, "{text}", detail::json_escape_text(text));
            detail::replace_all(payload, "{voice}", detail::json_escape_text(voice));
            detail::replace_all(payload, "{model}", detail::json_escape_text(svc.model));
            detail::replace_all(payload, "{lang}", detail::json_escape_text(svc.language));
            url = svc.endpoint("");
        }
        else // openai
        {
            nlohmann::json j{{"model", svc.model.empty() ? "tts-1" : svc.model},
                             {"input", text},
                             {"voice", voice.empty() ? "alloy" : voice}};
            if (!svc.format.empty())
                j["response_format"] = svc.format;
            payload = j.dump();
            url = svc.endpoint("/audio/speech");
        }
        if (url.empty())
        {
            err = "TTS 服务缺少地址";
            return false;
        }
        reply r = detail::post_json(svc, url, payload);
        if (!r.ok)
        {
            err = r.err.empty() ? "TTS 请求失败" : r.err;
            return false;
        }
        if (r.body.size() < 64)
        {
            err = "TTS 返回的内容不像音频（" + std::to_string(r.body.size()) + " 字节）";
            return false;
        }
        std::error_code ec;
        fs::create_directories(out.parent_path(), ec);
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            err = "写不了音频文件";
            return false;
        }
        f.write(r.body.data(), (std::streamsize)r.body.size());
        return f.good();
    }

    // ---------------------------------------------------------------------
    //  ASR
    // ---------------------------------------------------------------------
    inline bool asr(const service &svc, const fs::path &wav, std::string &text, std::string &err)
    {
        text.clear();
        err.clear();
        if (!svc.enabled())
        {
            err = "ASR 服务未启用";
            return false;
        }
        std::error_code ec;
        if (!fs::is_regular_file(wav, ec))
        {
            err = "录音文件不存在";
            return false;
        }
        if (svc.kind == "command")
        {
            if (svc.cmd.empty())
            {
                err = "命令式 ASR 缺少 cmd";
                return false;
            }
            fs::path prefix = wav.parent_path() / (wav.stem().string() + "-asr");
            std::string cmd = svc.cmd;
            detail::replace_all(cmd, "{in}", "\"" + wav.string() + "\"");
            detail::replace_all(cmd, "{out}", "\"" + prefix.string() + "\"");
            detail::replace_all(cmd, "{lang}", svc.language);
            std::string output, stderr_out;
            int code = -1;
            bool ran = cell::plat::spawn_cmd(cmd, (double)svc.timeout_s, output, code, stderr_out, wav.parent_path());
            if (!ran)
            {
                err = "无法启动 ASR 命令";
                return false;
            }
            // whisper-cli -otxt -of PREFIX 产出 PREFIX.txt；没有就用 stdout
            fs::path txt = prefix;
            txt += ".txt";
            if (fs::is_regular_file(txt, ec))
            {
                std::ifstream f(txt, std::ios::binary);
                text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
                fs::remove(txt, ec);
            }
            else
                text = output;
            text = cell::text::trim(text);
            if (text.empty())
            {
                err = code == 0 ? "ASR 没有识别出内容" : std::format("ASR 命令退出码 {}", code);
                return false;
            }
            return true;
        }

        // openai: multipart /audio/transcriptions
        std::vector<std::pair<std::string, std::string>> fields{
            {"model", svc.model.empty() ? "whisper-1" : svc.model}};
        if (!svc.language.empty())
            fields.emplace_back("language", svc.language);
        if (!svc.prompt.empty())
            fields.emplace_back("prompt", svc.prompt);
        fields.emplace_back("response_format", "json");
        reply r = detail::post_audio(svc, svc.endpoint("/audio/transcriptions"), wav, fields);
        if (!r.ok)
        {
            err = r.err.empty() ? "ASR 请求失败" : r.err;
            return false;
        }
        auto j = nlohmann::json::parse(r.body, nullptr, false);
        if (j.is_object() && j.contains("text") && j["text"].is_string())
            text = j["text"].get<std::string>();
        else
            text = r.body; // 少数实现直接回纯文本
        text = cell::text::trim(text);
        if (text.empty())
        {
            err = "ASR 没有识别出内容";
            return false;
        }
        return true;
    }

    // ---------------------------------------------------------------------
    //  原生录音（Windows MCI waveaudio，16k / 16bit / 单声道）
    // ---------------------------------------------------------------------
    // 页面没有麦克风权限的概念：录什么、录多久都由宿主决定。
    // 录音先落到 %TEMP%（纯 ASCII 路径，MCI 对中文路径不可靠），停止时再搬到目标位置。
    class recorder
    {
    public:
        bool active()
        {
            std::lock_guard<std::mutex> lk(mx_);
            return active_;
        }
#ifdef _WIN32
        bool start(std::string &err)
        {
            std::lock_guard<std::mutex> lk(mx_);
            if (active_)
            {
                err = "已经在录音";
                return false;
            }
            std::error_code ec;
            fs::path tmp = fs::temp_directory_path(ec) / std::format("agent-mic-{}.wav", (int)GetCurrentProcessId());
            tmp_ = tmp;
            fs::remove(tmp_, ec);
            wchar_t buf[MAXERRORLENGTH] = {0};
            auto cmd = [&](const std::wstring &c) -> bool
            {
                MCIERROR rc = mciSendStringW(c.c_str(), nullptr, 0, nullptr);
                if (rc != 0)
                    mciGetErrorStringW(rc, buf, MAXERRORLENGTH);
                return rc == 0;
            };
            close_locked();
            if (!cmd(L"open new type waveaudio alias agentrec"))
            {
                err = std::format("麦克风打不开（{}）", cell::plat::wide_to_utf8(buf));
                return false;
            }
            // 优先 16k/16bit/mono；设备不接受就退回系统默认格式（WAV 头里带着真实参数）
            cmd(L"set agentrec bitspersample 16 channels 1 samplespersec 16000 bytespersec 32000 alignment 2");
            if (!cmd(L"record agentrec"))
            {
                close_locked();
                err = "没有可用的麦克风（系统没检测到录音设备，或被别的程序占用）。"
                      "可以在设置 → 语音识别里用「上传音频转写」。";
                return false;
            }
            active_ = true;
            begun_ = std::chrono::steady_clock::now();
            return true;
        }

        bool stop(const fs::path &dest, int *seconds, std::string &err)
        {
            std::lock_guard<std::mutex> lk(mx_);
            if (!active_)
            {
                err = "没有正在进行的录音";
                return false;
            }
            active_ = false;
            auto dur = std::chrono::steady_clock::now() - begun_;
            if (seconds)
                *seconds = (int)std::chrono::duration_cast<std::chrono::milliseconds>(dur).count() / 1000;
            if (seconds && *seconds < 1)
                *seconds = 1;
            std::string a = "agentrec";
            auto cmd = [&](const std::string &c)
            {
                return mciSendStringW(cell::plat::utf8_to_wide(c).c_str(), nullptr, 0, nullptr) == 0;
            };
            cmd("stop " + a);
            bool saved = cmd("save " + a + " \"" + tmp_.string() + "\"");
            close_locked();
            std::error_code ec;
            if (!saved || !fs::is_regular_file(tmp_, ec) || fs::file_size(tmp_, ec) < 512)
            {
                err = "录音没有落盘（可能是麦克风被占用或没有输入设备）";
                return false;
            }
            fs::create_directories(dest.parent_path(), ec);
            fs::copy_file(tmp_, dest, fs::copy_options::overwrite_existing, ec);
            fs::remove(tmp_, ec);
            if (ec)
            {
                err = "保存录音失败：" + ec.message();
                return false;
            }
            return true;
        }
#else
        bool start(std::string &err)
        {
            err = "当前平台还没有实现宿主侧录音（Windows 用 MCI；其他平台可接 arecord/sox）";
            return false;
        }
        bool stop(const fs::path &, int *, std::string &err)
        {
            err = "当前平台还没有实现宿主侧录音";
            return false;
        }
#endif

    private:
#ifdef _WIN32
        void close_locked()
        {
            mciSendStringW(L"close agentrec", nullptr, 0, nullptr);
        }
#endif
        std::mutex mx_;
        bool active_ = false;
        std::chrono::steady_clock::time_point begun_;
#ifdef _WIN32
        fs::path tmp_;
#endif
    };

    // 音频扩展名 ← 配置里的 format
    inline std::string ext_for(const std::string &format)
    {
        std::string f = format;
        cell::lower_ascii(f);
        if (f == "wav" || f == "mp3" || f == "opus" || f == "aac" || f == "flac" || f == "pcm")
            return f;
        return "mp3";
    }
    inline const char *mime_for(const std::string &ext)
    {
        if (ext == "wav")
            return "audio/wav";
        if (ext == "mp3")
            return "audio/mpeg";
        if (ext == "opus")
            return "audio/ogg";
        if (ext == "aac")
            return "audio/aac";
        if (ext == "flac")
            return "audio/flac";
        return "application/octet-stream";
    }
} // namespace app::voice
