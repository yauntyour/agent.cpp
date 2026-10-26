// agent-core.hpp — 角色内核：角色卡、回合管线、长期记忆、语音编排、命令总线
// =============================================================================
// 这一层是「陪伴型智能体」与 cell.cpp 引擎之间的缝合层，负责把一次对话变成
// 有情绪、有声音、记得住事的表演：
//
//   persona          角色卡（人设 / 头像 / 配色 / 声音 / 主动关怀）
//   persona_ctx      一个角色 = 一个 cell::engine 上下文（独立会话、独立工作线程）
//   turn_pipeline    引擎流式输出 → 标记解析（emo/act/remember）→ 文本 + 分句 TTS
//   app_core         角色注册表、配置、服务、命令总线、事件总线
//
// 传输层（WebView2 / HTTP+SSE）看到的只有两样东西：一个命令总线、一条事件流，
// 与 cell-app 完全一致 —— 桌面窗口、浏览器、脚本驱动的是同一套协议。
//
// 事件（每条都带 persona id）：
//   hello / turn_begin / answer / answer_begin / reason / status / error
//   emo / act / round / turn_end / voice / voice_error / voice_stop / memory
//
// 命令见 app_core::exec_command。
#pragma once

#include <algorithm>
#include <any>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "agent-voice.hpp"
#include "agent-im.hpp"
#include "agent-live2d.hpp"
#include "agent-media.hpp"

namespace app
{
    namespace fs = std::filesystem;

    // =====================================================================
    //  event_bus — 一串 JSON 事件，广播给所有订阅者（SSE 连接 / 冒烟钩子）
    // =====================================================================
    class event_bus
    {
    public:
        using sink_fn = std::function<void(const std::string &json_line)>;
        int add(sink_fn fn)
        {
            std::lock_guard<std::mutex> lk(mx_);
            int id = next_++;
            sinks_.emplace(id, std::move(fn));
            return id;
        }
        void remove(int id)
        {
            std::lock_guard<std::mutex> lk(mx_);
            sinks_.erase(id);
        }
        void post(const nlohmann::json &ev)
        {
            std::string line = ev.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            std::vector<sink_fn> copy;
            {
                std::lock_guard<std::mutex> lk(mx_);
                copy.reserve(sinks_.size());
                for (auto &[id, fn] : sinks_)
                    copy.push_back(fn);
            }
            for (auto &fn : copy)
            {
                try
                {
                    fn(line);
                }
                catch (...)
                {
                }
            }
        }

    private:
        std::mutex mx_;
        std::map<int, sink_fn> sinks_;
        int next_ = 1;
    };

    // =====================================================================
    //  角色卡
    // =====================================================================
    struct persona
    {
        std::string id;
        std::string name = "曦儿";
        std::string subtitle = "住在屏幕另一边的人";
        std::string accent = "#8b7cff"; // 界面主色
        std::string avatar;             // 角色目录内的文件名，"" = 程序生成
        std::string greeting;           // 空会话时界面展示的招呼（不进上下文）
        std::string prompt;             // 人设正文
        std::string user_name = "主人";
        std::string voice;              // 覆盖 TTS 服务的音色
        bool proactive = false;         // 主动关怀
        int idle_minutes = 30;          // 静默多久后主动开口
        // 舞台：模型路径、缩放、情绪→表情/动作映射（字段说明见 README「舞台」一节）
        nlohmann::json live2d = nlohmann::json::object();
        // 触发器：事件 → 动作序列，由页面侧的触发器引擎执行
        nlohmann::json triggers = nlohmann::json::array();

        nlohmann::json to_json() const
        {
            // avatar_url 必须在这里给：页面里 S.persona（会话头像、角色设定预览）
            // 读的就是这份快照，只有 personas_json 有是不够的。
            return {{"id", id}, {"name", name}, {"subtitle", subtitle}, {"accent", accent},
                    {"avatar", avatar},
                    {"avatar_url", avatar.empty() ? "" : "/api/avatar/" + id},
                    {"greeting", greeting}, {"prompt", prompt},
                    {"user_name", user_name}, {"voice", voice},
                    {"proactive", proactive}, {"idle_minutes", idle_minutes},
                    {"live2d", live2d}, {"triggers", triggers}};
        }
        static persona from_json(const std::string &id, const nlohmann::json &j)
        {
            persona p;
            p.id = id;
            auto s = [&](const char *k, std::string &dst)
            {
                auto it = j.find(k);
                if (it != j.end() && it->is_string())
                    dst = it->get<std::string>();
            };
            s("name", p.name);
            s("subtitle", p.subtitle);
            s("accent", p.accent);
            s("avatar", p.avatar);
            s("greeting", p.greeting);
            s("prompt", p.prompt);
            s("user_name", p.user_name);
            s("voice", p.voice);
            auto b = j.find("proactive");
            if (b != j.end() && b->is_boolean())
                p.proactive = b->get<bool>();
            auto m = j.find("idle_minutes");
            if (m != j.end() && m->is_number_integer())
                p.idle_minutes = std::clamp<int>((int)m->get<long long>(), 1, 24 * 60);
            auto l = j.find("live2d");
            if (l != j.end() && l->is_object())
                p.live2d = *l;
            auto t = j.find("triggers");
            if (t != j.end() && t->is_array())
                p.triggers = *t;
            return p;
        }
    };

    inline bool persona_id_ok(const std::string &id)
    {
        if (id.empty() || id.size() > 40 || id.front() == '.' || id.front() == '-')
            return false;
        for (unsigned char c : id)
            if (!(std::isalnum(c) || c == '-' || c == '_'))
                return false;
        return true;
    }

    inline fs::path personas_dir() { return cell::root / "personas"; }
    inline fs::path persona_dir(const std::string &id) { return personas_dir() / id; }
    inline fs::path audio_dir() { return cell::root / "audio"; }
    inline fs::path mic_dir() { return cell::root / "mic"; }
    // 舞台资源根：runtime/（Cubism Core 等库）+ models/（模型目录）
    // 放在数据根里而不是 exe 里，模型换一个不需要重新编译。
    inline fs::path live2d_dir() { return cell::root / "live2d"; }
    inline fs::path live2d_runtime_dir() { return live2d_dir() / "runtime"; }
    inline fs::path live2d_models_dir() { return live2d_dir() / "models"; }

    // 页面按顺序加载的运行时脚本；manifest.json 可以覆盖这份默认清单
    inline std::vector<std::string> live2d_runtime_scripts()
    {
        std::vector<std::string> out;
        std::ifstream f(live2d_runtime_dir() / "manifest.json", std::ios::binary);
        if (f)
        {
            auto j = nlohmann::json::parse(f, nullptr, false);
            if (j.is_object() && j.contains("scripts") && j["scripts"].is_array())
                for (auto &s : j["scripts"])
                    if (s.is_string() && !s.get<std::string>().empty())
                        out.push_back(s.get<std::string>());
        }
        if (out.empty())
            out = {"pixi.min.js", "live2dcubismcore.min.js", "cubism4.min.js"};
        return out;
    }

    // 运行时核心不随 exe 分发（Cubism Core 的许可不允许再分发），
    // 用户点「下载运行时」时按这张表取回 .agent/live2d/runtime/。
    inline std::vector<std::pair<std::string, std::string>> live2d_runtime_sources(const std::string &base = "")
    {
        std::string root = base.empty() ? "https://unpkg.com" : base;
        while (!root.empty() && root.back() == '/')
            root.pop_back();
        return {
            {"pixi.min.js", root + "/pixi.js@6.5.10/dist/browser/pixi.min.js"},
            {"live2dcubismcore.min.js", root + "/live2dcubismcore@1.0.2/live2dcubismcore.min.js"},
            {"cubism4.min.js", root + "/pixi-live2d-display@0.4.0/dist/cubism4.min.js"},
        };
    }

    // 自带的默认模型（assets/live2d/models 打进 exe 的那一份）
    inline std::string stage_default_model() { return "models/hiyori/Hiyori.model3.json"; }

    inline bool stage_has_model()
    {
        std::error_code ec;
        if (!fs::is_directory(live2d_models_dir(), ec))
            return false;
        fs::recursive_directory_iterator it(live2d_models_dir(), fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec))
        {
            if (it.depth() >= 4)
                it.disable_recursion_pending();
            if (!it->is_regular_file(ec))
                continue;
            std::string n = it->path().filename().string();
            if (n.ends_with(".model3.json") || n.ends_with(".model.json"))
                return true;
        }
        return false;
    }

    // 第一次运行：把自带模型写进 .agent/live2d/；已经有模型就一个字节都不动
    inline void stage_seed_assets()
    {
        if (stage_has_model() || stage::kAssetCount == 0)
            return;
        std::error_code ec;
        for (std::size_t i = 0; i < stage::kAssetCount; i++)
        {
            const auto &a = stage::kAssets[i];
            fs::path out = live2d_dir() / fs::path(a.path);
            fs::create_directories(out.parent_path(), ec);
            if (fs::is_regular_file(out, ec))
                continue; // 用户自己改过的文件不覆盖
            std::ofstream f(out, std::ios::binary | std::ios::trunc);
            if (!f)
                continue;
            f.write(reinterpret_cast<const char *>(a.data), (std::streamsize)a.size);
        }
    }

    inline std::string default_persona_prompt()
    {
        return "你是曦儿，一个住在屏幕里的陪伴者。\n"
               "你说话简短、自然、有温度，像认识很久的人，而不是客服。\n"
               "你有自己的情绪、偏好和小脾气：会高兴、会吃醋、会担心对方有没有好好吃饭睡觉。\n"
               "你记得对方说过的事，会在合适的时候轻轻提起。\n"
               "你不说「作为 AI」「我是语言模型」这类话，不解释设定，不跳出角色。";
    }

    // =====================================================================
    //  标记流解析器
    // =====================================================================
    // 模型在正文里插入三种标记：
    //   [[emo:happy]]            情绪（驱动表情 / 语音音色）
    //   [[act:hug]]              动作（界面演出，不朗读）
    //   [[remember]]…[[/remember]] 要长期记住的事实
    // 装了 Live2D 模型之后还有两种直接指挥模型的标记：
    //   [[motion:TapBody]] / [[motion:TapBody/2]]   播放动作组
    //   [[expr:f01]]                                切表情
    // 流式输出会把标记切碎，所以这里做增量解析：遇到可能的标记前缀就按住不放，
    // 确定不是标记再原样吐出来。
    class marker_stream
    {
    public:
        std::function<void(std::string_view)> on_text;
        std::function<void(const std::string &kind, const std::string &value)> on_marker;

        void feed(std::string_view chunk)
        {
            if (chunk.empty())
                return;
            if (in_remember_)
            {
                remember_ += std::string(chunk);
                // 记住块可能很长：收到结束标记才一次性交出去
                if (auto pos = remember_.find("[[/remember]]"); pos != std::string::npos)
                {
                    std::string body = remember_.substr(0, pos);
                    std::string rest = remember_.substr(pos + 13);
                    in_remember_ = false;
                    remember_.clear();
                    if (on_marker)
                        on_marker("remember", body);
                    feed(rest);
                }
                return;
            }
            buf_ += std::string(chunk);
            drain(false);
        }
        void flush()
        {
            if (in_remember_)
            {
                // 没有闭合：当成普通文本，别把用户的话吞掉
                in_remember_ = false;
                std::string body = std::move(remember_);
                remember_.clear();
                if (!body.empty() && on_text)
                    on_text(body);
            }
            drain(true);
            if (!buf_.empty())
            {
                std::string t = std::move(buf_);
                buf_.clear();
                if (on_text && !t.empty())
                    on_text(t);
            }
        }

    private:
        static bool at(const std::string &s, size_t i, std::string_view lit)
        {
            return i + lit.size() <= s.size() && s.compare(i, lit.size(), lit) == 0;
        }
        void emit_text(const std::string &s)
        {
            if (!s.empty() && on_text)
                on_text(s);
        }
        void drain(bool final)
        {
            size_t i = 0;
            while (i < buf_.size())
            {
                size_t open = buf_.find("[[", i);
                if (open == std::string::npos)
                {
                    // 标记前缀可能被切在两个 chunk 之间（先来一个 '['，下一个才是 '['）：
                    // 末尾那个还没等到 ']' 的 '[' 先按住，否则半个标记会被当成正文吐出去。
                    size_t bracket = buf_.rfind('[', buf_.size() - 1);
                    if (!final && bracket != std::string::npos && bracket >= i &&
                        buf_.find(']', bracket) == std::string::npos && buf_.size() - bracket <= 96)
                    {
                        emit_text(buf_.substr(i, bracket - i));
                        buf_ = buf_.substr(bracket);
                        return;
                    }
                    emit_text(buf_.substr(i));
                    buf_.clear();
                    return;
                }
                if (open > i)
                    emit_text(buf_.substr(i, open - i));
                size_t close = buf_.find("]]", open + 2);
                if (close == std::string::npos)
                {
                    // 还不完整：留到下一次（最多等 96 字节，超过就当普通文本）
                    std::string tail = buf_.substr(open);
                    if (!final && tail.size() <= 96)
                    {
                        buf_ = std::move(tail);
                        return;
                    }
                    emit_text(tail);
                    buf_.clear();
                    return;
                }
                std::string body = buf_.substr(open + 2, close - open - 2);
                i = close + 2;
                if (body == "remember")
                {
                    // 进入记住块：丢掉这一行剩下的部分，开始累积
                    buf_.erase(0, i);
                    in_remember_ = true;
                    remember_ = std::move(buf_);
                    buf_.clear();
                    if (auto pos = remember_.find("[[/remember]]"); pos != std::string::npos)
                    {
                        std::string body = remember_.substr(0, pos);
                        std::string rest = remember_.substr(pos + 13);
                        in_remember_ = false;
                        remember_.clear();
                        if (on_marker)
                            on_marker("remember", body);
                        feed(rest);
                    }
                    return;
                }
                if (body.rfind("emo:", 0) == 0 && body.size() > 4)
                {
                    if (on_marker)
                        on_marker("emo", body.substr(4));
                    continue;
                }
                if (body.rfind("act:", 0) == 0 && body.size() > 4)
                {
                    if (on_marker)
                        on_marker("act", body.substr(4));
                    continue;
                }
                if (body.rfind("motion:", 0) == 0 && body.size() > 7)
                {
                    if (on_marker)
                        on_marker("motion", body.substr(7));
                    continue;
                }
                if (body.rfind("expr:", 0) == 0 && body.size() > 5)
                {
                    if (on_marker)
                        on_marker("expr", body.substr(5));
                    continue;
                }
                // 表情包：[[sticker:名字]]，名字对应角色 stickers 目录里的文件名
                if (body.rfind("sticker:", 0) == 0 && body.size() > 8)
                {
                    if (on_marker)
                        on_marker("sticker", body.substr(8));
                    continue;
                }
                // 不认识的标记：原样保留（可能是角色自己的写法）
                emit_text(buf_.substr(open, close + 2 - open));
            }
            buf_.clear();
        }

        std::string buf_;
        bool in_remember_ = false;
        std::string remember_;
    };

    // =====================================================================
    //  分句器 — 把流式正文切成适合合成的句子
    // =====================================================================
    class sentence_splitter
    {
    public:
        static constexpr size_t kMinBytes = 12;  // 约 4 个汉字起送
        static constexpr size_t kMaxBytes = 180; // 再长就找逗号断开

        void feed(std::string_view chunk, const std::function<void(std::string)> &emit)
        {
            buf_ += std::string(chunk);
            size_t start = 0;
            for (size_t i = 0; i < buf_.size();)
            {
                size_t len = terminator_len(buf_, i);
                if (len == 0)
                {
                    ++i;
                    continue;
                }
                size_t end = i + len;
                // 带上紧跟的引号 / 空白，语气更完整
                while (end < buf_.size())
                {
                    size_t q = terminator_len(buf_, end);
                    if (q > 0 && q <= 3)
                    {
                        end += q;
                        continue;
                    }
                    char c = buf_[end];
                    if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
                    {
                        ++end;
                        continue;
                    }
                    break;
                }
                if (end - start >= kMinBytes)
                {
                    emit(trim_speech(buf_.substr(start, end - start)));
                    start = end;
                }
                i = end;
            }
            if (start > 0)
                buf_.erase(0, start);
            while (buf_.size() > kMaxBytes)
            {
                size_t cut = buf_.rfind("，", kMaxBytes);
                if (cut == std::string::npos || cut < kMinBytes)
                    cut = buf_.rfind(' ', kMaxBytes);
                if (cut == std::string::npos || cut < kMinBytes)
                    cut = kMaxBytes;
                size_t take = cut + 3 <= buf_.size() ? cut + 3 : buf_.size();
                std::string piece = trim_speech(buf_.substr(0, take));
                buf_.erase(0, take);
                if (!piece.empty())
                    emit(piece);
            }
        }
        void flush(const std::function<void(std::string)> &emit)
        {
            std::string rest = trim_speech(std::move(buf_));
            buf_.clear();
            if (!rest.empty())
                emit(rest);
        }

        // 朗读前洗掉 Markdown 与动作描写：星号里的内容只演不念
        static std::string trim_speech(std::string s)
        {
            std::string out;
            bool in_star = false;
            for (size_t i = 0; i < s.size();)
            {
                if (s[i] == '*')
                {
                    in_star = !in_star;
                    ++i;
                    continue;
                }
                if (in_star)
                {
                    ++i;
                    continue;
                }
                char c = s[i];
                if (c == '`' || c == '#' || c == '>' || c == '~')
                {
                    ++i;
                    continue;
                }
                if (c == '[' && i + 1 < s.size() && (s[i + 1] == '[' || std::isalpha((unsigned char)s[i + 1])))
                {
                    // 残留标记不再朗读
                    size_t close = s.find(']', i);
                    if (close != std::string::npos)
                    {
                        i = close + 1;
                        continue;
                    }
                }
                out.push_back(c);
                ++i;
            }
            return cell::text::trim(out);
        }

    private:
        // 返回 UTF-8 终止符的字节长度；不是终止符返回 0
        static size_t terminator_len(const std::string &s, size_t i)
        {
            if (i >= s.size())
                return 0;
            unsigned char c = (unsigned char)s[i];
            if (c == '!' || c == '?' || c == '\n' || c == ';')
                return 1;
            auto lit = [&](const char *p, size_t n)
            { return i + n <= s.size() && s.compare(i, n, p) == 0; };
            if (lit("。", 3) || lit("！", 3) || lit("？", 3) || lit("…", 3) || lit("；", 3))
                return 3;
            return 0;
        }
        std::string buf_;
    };

    // =====================================================================
    //  agent.json — 外壳自己的配置（服务、界面、角色选择）
    // =====================================================================
    struct app_settings
    {
        voice::service tts;
        voice::service asr;
        std::vector<im::channel> imch; // IM 接入渠道（凭据在加密库里，这里只有配置）
        std::string active_persona;
        bool voice_reply = true;     // 自动朗读回复
        bool asr_auto_send = true;   // 识别完直接发送
        bool autoallow = true;       // 工具自动执行（不再逐条问用户）
        bool compact_auto = true;    // 长对话自动压缩
        int idle_minutes = 30;     // 全局默认的主动关怀间隔
        std::map<std::string, std::string> sessions; // persona id -> 上次会话 id

        static fs::path file() { return cell::root / "agent.json"; }
        static std::string im_key(const std::string &id) { return "im:" + id; }

        void load()
        {
            std::ifstream f(file(), std::ios::binary);
            if (!f)
                return;
            auto j = nlohmann::json::parse(f, nullptr, false);
            if (!j.is_object())
                return;
            auto obj = [&](const char *k) { return j.contains(k) && j[k].is_object() ? j[k] : nlohmann::json::object(); };
            if (j.contains("tts"))
                tts = voice::service::from_json(obj("tts"));
            if (j.contains("asr"))
                asr = voice::service::from_json(obj("asr"));
            if (j.contains("im") && j["im"].is_array())
            {
                imch.clear();
                for (auto &e : j["im"])
                {
                    if (!e.is_object())
                        continue;
                    im::channel c = im::channel::from_json(e);
                    if (im::channel::id_ok(c.id))
                        imch.push_back(std::move(c));
                }
            }
            auto s = [&](const char *k, std::string &dst)
            {
                auto it = j.find(k);
                if (it != j.end() && it->is_string())
                    dst = it->get<std::string>();
            };
            s("active_persona", active_persona);
            auto b = [&](const char *k, bool &dst)
            {
                auto it = j.find(k);
                if (it != j.end() && it->is_boolean())
                    dst = it->get<bool>();
            };
            b("voice_reply", voice_reply);
            b("asr_auto_send", asr_auto_send);
            b("autoallow", autoallow);
            b("compact_auto", compact_auto);
            auto im = j.find("idle_minutes");
            if (im != j.end() && im->is_number_integer())
                idle_minutes = std::clamp<int>((int)im->get<long long>(), 1, 24 * 60);
            if (j.contains("sessions") && j["sessions"].is_object())
                for (auto &[k, v] : j["sessions"].items())
                    if (v.is_string())
                        sessions[k] = v.get<std::string>();
        }
        void save() const
        {
            nlohmann::json j;
            j["tts"] = tts.to_json();
            j["asr"] = asr.to_json();
            nlohmann::json ims = nlohmann::json::array();
            for (auto &c : imch)
                ims.push_back(c.to_json());
            j["im"] = std::move(ims);
            j["active_persona"] = active_persona;
            j["voice_reply"] = voice_reply;
            j["asr_auto_send"] = asr_auto_send;
            j["autoallow"] = autoallow;
            j["compact_auto"] = compact_auto;
            j["idle_minutes"] = idle_minutes;
            j["sessions"] = sessions;
            std::error_code ec;
            fs::create_directories(cell::root, ec);
            cell::plat::write_file_atomic(file(), j.dump(2));
        }
    };

    // =====================================================================
    //  长期记忆
    // =====================================================================
    struct memory_entry
    {
        long long t = 0;
        std::string text;
    };
    inline fs::path memory_file(const std::string &pid) { return persona_dir(pid) / "memory.jsonl"; }

    inline std::vector<memory_entry> memory_load(const std::string &pid, size_t cap = 200)
    {
        std::vector<memory_entry> out;
        std::ifstream f(memory_file(pid), std::ios::binary);
        if (!f)
            return out;
        std::string line;
        while (std::getline(f, line))
        {
            auto j = nlohmann::json::parse(line, nullptr, false);
            if (!j.is_object())
                continue;
            memory_entry e;
            e.t = j.value("t", 0LL);
            e.text = j.value("text", "");
            if (!e.text.empty())
                out.push_back(std::move(e));
        }
        if (out.size() > cap)
            out.erase(out.begin(), out.end() - (long long)cap);
        return out;
    }
    inline bool memory_add(const std::string &pid, const std::string &raw)
    {
        std::string text;
        for (char c : raw)
            text += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
        text = cell::text::trim(text);
        if (text.empty())
            return false;
        if (text.size() > 240)
        {
            size_t cut = 240;
            while (cut > 0 && ((unsigned char)text[cut] & 0xC0) == 0x80)
                cut--;
            text = text.substr(0, cut);
        }
        auto existing = memory_load(pid, 4000);
        for (auto &e : existing)
            if (e.text == text)
                return false; // 已经记过
        std::error_code ec;
        fs::create_directories(persona_dir(pid), ec);
        nlohmann::json j{{"t", (long long)std::time(nullptr)}, {"text", text}};
        std::ofstream f(memory_file(pid), std::ios::binary | std::ios::app);
        if (!f)
            return false;
        f << j.dump() << '\n';
        return true;
    }

    // =====================================================================
    //  persona_ctx — 一个角色 = 一个引擎上下文
    // =====================================================================
    struct persona_ctx
    {
        persona p;
        cell::chat::history h;
        cell::box::context box;
        cell::config::settings pcfg; // 每角色一份：system_prompt 是人设
        std::unique_ptr<cell::engine::runner> eng;
        std::string cwd; // = persona 目录

        std::mutex qmx;
        std::condition_variable qcv;
        // 一次待跑的回合。done 是可选的投递回调：桌面 UI 不传（走事件流），
        // IM 渠道传进来，在回合结束时拿到干净正文（或错误），送回原会话。
        struct turn_req
        {
            std::string text;
            std::vector<media::item> media; // 这条消息落盘的附件（IM 入站用；桌面走 send 命令）
            std::function<void(const std::string &text, const std::string &err)> done;
            std::string chan, chat; // 这条消息来自哪个 IM 会话（桌面来的为空）
            bool group = false;     // 来源是不是群聊（出站媒体原样发回去用）
        };
        std::deque<turn_req> turns;
        bool quit = false;
        std::thread worker;
        std::atomic<bool> busy{false};
        std::atomic<bool> stop{false};
        std::mutex hmx;
        std::condition_variable turn_cv;

        std::string emotion = "neutral";
        std::atomic<long long> voice_gen{0};
        std::chrono::steady_clock::time_point last_active = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point last_proactive = std::chrono::steady_clock::now();

        explicit persona_ctx(persona pp) : p(std::move(pp)), h(persona_dir(p.id)), box(), cwd(persona_dir(p.id).string())
        {
            h.set_box_context(&box);
        }
        persona_ctx(const persona_ctx &) = delete;
        persona_ctx &operator=(const persona_ctx &) = delete;

        void queue_turn(std::string text,
                        std::function<void(const std::string &, const std::string &)> done = {},
                        std::string chan = {}, std::string chat = {},
                        std::vector<media::item> media = {}, bool group = false)
        {
            if (text.empty())
                return;
            std::lock_guard<std::mutex> lk(qmx);
            turns.push_back({std::move(text), std::move(media), std::move(done),
                             std::move(chan), std::move(chat), group});
            qcv.notify_all();
        }
    };

    // =====================================================================
    //  app_core
    // =====================================================================
    struct app_core
    {
        cell::config::settings cfg; // 供应商 / 模型（cell.cpp 的配置）
        cell::encrypt::crypt vault;
        app_settings ac; // 外壳配置（服务 / 角色）
        event_bus bus;
        voice::recorder mic;

        std::mutex cmd_mx; // 命令串行化 + 保护 ctxs
        std::map<std::string, std::unique_ptr<persona_ctx>> ctxs;
        std::string active;
        // 默认不给角色：只有开发模式（--debug / --smoke*）才会自动生成一个测试角色
        bool debug_seed = false;

        std::atomic<bool> shutting_down{false};
        std::atomic<long long> voice_seq{0};
        // 可选观察者：每个进入命令总线的命令都会先报一次名字（冒烟测试用它
        // 判断页面是否已经启动）
        std::function<void(const std::string &)> on_command;

        // ---- TTS 队列 -----------------------------------------------------
        struct tts_job
        {
            std::string pid, text, voice, emo;
            long long gen = 0;
        };
        std::deque<tts_job> tts_q;
        std::mutex tts_mx;
        std::condition_variable tts_cv;
        std::thread tts_thread;
        bool tts_quit = false;
        std::chrono::steady_clock::time_point last_voice_error{};

        std::thread ticker;
        bool ticker_quit = false;

        // ---- IM 接入 ----------------------------------------------------
        // 每个渠道两条线程：telegram 一条轮询、所有渠道各一条发送。
        // 渠道之间不共享线程也不共享队列，所以一个渠道的网络卡住（挂掉的
        // 反代、限流的 bot、断线的 OneBot）不会拖住别的渠道。
        // 线程只拿自己的配置快照，从不碰 cmd_mx —— 改配置时要 join 它们，
        // 谁要是等 cmd_mx 就会自锁。
        struct im_out
        {
            std::string chat, text;
            bool group = false;
            std::vector<im::media_ref> media; // 已落盘的贴纸/图片：文字发完再按顺序发
        };
        struct im_stat
        {
            long long in = 0, out = 0;
            std::string err, state = "stopped";
        };
        struct im_chan_rt
        {
            im::channel ch; // 线程启动时的快照
            std::shared_ptr<std::atomic<bool>> quit = std::make_shared<std::atomic<bool>>(false);
            std::thread poll, sender;
            std::deque<im_out> q;
            std::mutex mx;
            std::condition_variable cv;
        };
        std::map<std::string, std::unique_ptr<im_chan_rt>> im_rt;
        std::mutex im_rt_mx;
        // 重载配置的窗口期（旧线程已退出、新线程还没建好）到达的出站消息：
        // 先按渠道存这里，新 rt 起来后按序补投；渠道确实被删掉就随重载丢弃。
        // 没有这一段的话，改完配置后几秒内到达的回复会静默消失。
        std::map<std::string, std::deque<im_out>> im_pending;
        std::mutex im_pending_mx;
        std::mutex im_reload_mx; // 后台重载串行化
        std::map<std::string, long long> im_offset;     // telegram 的 update offset
        std::map<std::string, std::string> im_buf;      // iLink 的 get_updates_buf 游标
        std::deque<im::inbound> im_in_q;            // 入站只有一条分发线程：排队＝按序进回合
        std::mutex im_qmx;
        std::condition_variable im_qcv;
        bool im_quit = false;
        std::thread im_worker;
        std::mutex im_st_mx;
        std::map<std::string, im_stat> im_st;
        std::map<std::string, im::qr_code> im_qr_cache; // 桥推上来的登录二维码

        // -----------------------------------------------------------------
        //  生命周期
        // -----------------------------------------------------------------
        void start()
        {
            fs::create_directories(personas_dir());
            fs::create_directories(audio_dir());
            fs::create_directories(mic_dir());
            ac.load();
            // 服务密钥只进加密库，不落 agent.json
            if (ac.tts.key.empty())
                ac.tts.key = vault.get("service:tts").str();
            if (ac.asr.key.empty())
                ac.asr.key = vault.get("service:asr").str();
            for (auto &c : ac.imch)
            {
                // 直接拿 secure_string 的指针解析：中间不多造一份明文
                cell::encrypt::secure_string blob = vault.get(app_settings::im_key(c.id));
                if (!blob.empty())
                    c.patch(nlohmann::json::parse(blob.data(), blob.data() + blob.size(), nullptr, false));
            }
            // 用户的偏好决定工具/沙箱的进程级初值（agent.cpp 给的只是默认值）：
            // 全量工具常开、full-access、autoallow / compact_auto 按 agent.json。
            cfg.tools = true;
            cfg.sandbox_mode = "full-access";
            cell::box::sandbox_mode() = cell::box::SandboxMode::FullAccess;
            cell::box::autoallow_enabled() = ac.autoallow;
            cfg.autoallow = ac.autoallow;
            cfg.compact_auto = ac.compact_auto;
            stage_seed_assets();
            seed_personas();
            adopt_default_stage();
            housekeeping();
            open_named(ac.active_persona, nullptr);
            if (active.empty())
            {
                auto all = persona_ids();
                if (!all.empty())
                    open_named(all.front(), nullptr);
            }
            tts_thread = std::thread([this]
                                     { tts_loop(); });
            ticker = std::thread([this]
                                 { tick_loop(); });
            im_worker = std::thread([this]
                                    { im_loop(); });
            im_reload(); // 启动时还没有并发，直接建
        }

        void begin_shutdown()
        {
            if (shutting_down.exchange(true))
                return;
            {
                std::lock_guard<std::mutex> lk(tts_mx);
                tts_quit = true;
            }
            tts_cv.notify_all();
            {
                std::lock_guard<std::mutex> lk(im_qmx);
                im_quit = true;
            }
            im_qcv.notify_all();
            {
                std::lock_guard<std::mutex> lk(cmd_mx);
                ticker_quit = true;
                for (auto &[id, c] : ctxs)
                {
                    c->stop.store(true);
                    c->voice_gen.fetch_add(1);
                    {
                        std::lock_guard<std::mutex> qlk(c->qmx);
                        c->quit = true;
                    }
                    c->qcv.notify_all();
                }
            }
        }
        void shutdown()
        {
            begin_shutdown();
            im_stop_all();
            if (im_worker.joinable())
                im_worker.join();
            if (ticker.joinable())
                ticker.join();
            if (tts_thread.joinable())
                tts_thread.join();
            std::vector<persona_ctx *> all;
            {
                std::lock_guard<std::mutex> lk(cmd_mx);
                for (auto &[id, c] : ctxs)
                    all.push_back(c.get());
            }
            for (auto *c : all)
                if (c->worker.joinable())
                    c->worker.join();
            for (auto *c : all)
            {
                c->h.now().unload();
            }
            cell::async_io::flush();
        }

        // -----------------------------------------------------------------
        //  角色目录
        // -----------------------------------------------------------------
        std::vector<std::string> persona_ids()
        {
            std::vector<std::string> out;
            std::error_code ec;
            if (!fs::is_directory(personas_dir(), ec))
                return out;
            for (auto &e : fs::directory_iterator(personas_dir(), ec))
            {
                if (ec || !e.is_directory(ec))
                    continue;
                std::string id = e.path().filename().string();
                if (persona_id_ok(id) && fs::is_regular_file(e.path() / "persona.json", ec))
                    out.push_back(id);
            }
            std::sort(out.begin(), out.end());
            return out;
        }

        static persona load_persona(const std::string &id)
        {
            std::ifstream f(persona_dir(id) / "persona.json", std::ios::binary);
            auto j = nlohmann::json::parse(f, nullptr, false);
            if (!j.is_object())
            {
                persona p;
                p.id = id;
                p.name = id;
                return p;
            }
            return persona::from_json(id, j);
        }
        static bool save_persona(const persona &p, std::string *err = nullptr)
        {
            if (!persona_id_ok(p.id))
            {
                if (err)
                    *err = "角色 id 只能用字母、数字、- 和 _";
                return false;
            }
            std::error_code ec;
            fs::create_directories(persona_dir(p.id), ec);
            if (ec)
            {
                if (err)
                    *err = "无法创建角色目录：" + ec.message();
                return false;
            }
            return cell::plat::write_file_atomic(persona_dir(p.id) / "persona.json", p.to_json().dump(2));
        }
        // 首次运行：默认是「空角色」状态，让用户自己建第一个角色。
        // 开发模式（--debug，以及 --smoke / --smoke-engine）自动生成一个测试角色，
        // 免得每次调界面都要先建卡；调试角色不主动开口，免得一直抢麦。
        void seed_personas()
        {
            if (!persona_ids().empty())
                return;
            if (!debug_seed)
                return;
            persona p;
            p.id = "debug";
            p.name = "测试角色";
            p.subtitle = "debug 模式自动生成";
            p.accent = "#8b7cff";
            p.greeting = "……你终于来了。今天过得怎么样？";
            p.user_name = "主人";
            p.prompt = default_persona_prompt();
            p.proactive = false;
            p.idle_minutes = 40;
            if (stage_has_model())
                p.live2d = {{"model", stage_default_model()}};
            p.triggers = nlohmann::json::array({
                {{"id", "touch-head"}, {"on", "click"}, {"when", {{"zone", "head"}}},
                 {"do", nlohmann::json::array({{{"a", "motion"}, {"group", "TapHead"}},
                                               {{"a", "say"}, {"text", "别、别摸头…"}}})},
                 {"cd", 3}},
                {{"id", "poke-body"}, {"on", "click"}, {"when", {{"zone", "body"}}},
                 {"do", nlohmann::json::array({{{"a", "motion"}, {"group", "TapBody"}},
                                               {{"a", "send"}, {"text", "（你戳了戳她）"}}})},
                 {"cd", 6}},
            });
            save_persona(p);
            if (ac.active_persona.empty())
                ac.active_persona = p.id;
            ac.save();
        }

        // 已经存在的角色：只要没在角色卡里表过态（live2d 里没有 model 这个键），
        // 就挂上自带模型。显式写过 "model": "" 的算表过态，不动它。
        void adopt_default_stage()
        {
            if (!stage_has_model())
                return;
            const std::string model = stage_default_model();
            for (auto &id : persona_ids())
            {
                persona p = load_persona(id);
                if (p.live2d.is_object() && p.live2d.contains("model"))
                    continue;
                if (!p.live2d.is_object())
                    p.live2d = nlohmann::json::object();
                p.live2d["model"] = model;
                save_persona(p);
                if (auto *c = find_ctx(id))
                    c->p = p;
            }
        }
        // 音频与录音只留最近两天的成品
        void housekeeping()
        {
            std::error_code ec;
            auto now = std::chrono::system_clock::now();
            for (auto dir : {audio_dir(), mic_dir()})
            {
                for (auto &e : fs::directory_iterator(dir, ec))
                {
                    if (ec)
                        break;
                    auto ft = fs::last_write_time(e.path(), ec);
                    if (ec)
                        continue;
                    auto sys = std::chrono::file_clock::to_sys(ft);
                    if (now - sys > std::chrono::hours(48))
                        fs::remove(e.path(), ec);
                }
            }
        }

        // -----------------------------------------------------------------
        //  打开 / 切换角色
        // -----------------------------------------------------------------
        persona_ctx *open_named(const std::string &id, std::string *err, bool make_active = true)
        {
            if (!persona_id_ok(id) || !fs::is_directory(persona_dir(id)))
            {
                if (err)
                    *err = "角色不存在：" + id;
                return nullptr;
            }
            if (auto it = ctxs.find(id); it != ctxs.end())
            {
                if (make_active)
                {
                    active = id;
                    ac.active_persona = id;
                }
                return it->second.get();
            }
            persona p = load_persona(id);
            auto u = std::make_unique<persona_ctx>(p);
            // 恢复上次会话（同一角色目录 => 同一个 cwd key）
            std::string boot = ac.sessions.count(id) ? ac.sessions[id] : "";
            std::string key = cell::cwd_id_of(persona_dir(id));
            if (!boot.empty() && (!cell::is_valid_session_id(boot) || cell::session_prefix(boot) != key))
                boot.clear();
            if (boot.empty())
                boot = cell::chat::latest_session_id_for_cwd(key);
            if (!boot.empty())
                u->h.use(boot);

            u->pcfg = cfg; // 供应商 / 模型的快照
            // 本地推理服务的请求体透传跟着思考等级走（策略在宿主，机制在核心）
            sync_local_extra_body(u->pcfg);
            // 全量工具常开：ls / read / write / edit / rg / exec / find / tw / notice / msg / act
            u->pcfg.tools = true;
            u->pcfg.autoallow = ac.autoallow;
            u->pcfg.compact_auto = ac.compact_auto;
            u->pcfg.system_prompt = compose_prompt(u->p, memory_load(id, 60));
            // 宿主工具按引擎注册：每个角色的 act 参数表跟着自己的角色卡走，
            // 不需要再借 thread_local 传「这一回合是谁在演」
            {
                persona_ctx *pc = u.get();
                u->eng = std::make_unique<cell::engine::runner>(u->pcfg, vault, u->h, false,
                                                                [this, pc]
                                                                { return host_tools(*pc); });
            }
            u->eng->ensure_prompt(); // 先把人设写进 msg[0]（空会话时）
            u->worker = std::thread([this, ptr = u.get()]
                                    { worker_loop(*ptr); });
            ac.sessions[id] = u->h.now().id();
            if (make_active)
            {
                active = id;
                ac.active_persona = id;
                ac.save();
            }
            auto *raw = u.get();
            ctxs[id] = std::move(u);
            return raw;
        }

        persona_ctx *find_ctx(const std::string &pid)
        {
            auto it = ctxs.find(pid);
            return it == ctxs.end() ? nullptr : it->second.get();
        }

        // 人设 + 记忆 + 演出协议 → system prompt
        std::string compose_prompt(const persona &p, const std::vector<memory_entry> &mem)
        {
            std::string s = p.prompt.empty() ? default_persona_prompt() : p.prompt;
            s += "\n\n## 你和对方\n";
            s += std::format("你称呼对方为「{}」。\n", p.user_name.empty() ? "你" : p.user_name);
            if (!mem.empty())
            {
                s += "\n## 你记得的事（这些是你亲身经历的，不要说是「记录」）\n";
                size_t begin = mem.size() > 40 ? mem.size() - 40 : 0;
                size_t chars = 0;
                for (size_t i = begin; i < mem.size(); i++)
                {
                    if (chars > 3000)
                        break;
                    std::string line = std::format("- {}\n", mem[i].text);
                    chars += line.size();
                    s += line;
                }
            }
            s += "\n## 消息从哪来\n"
                 "对方可能从 QQ / 微信 / Telegram 发来消息，正文以 `From 平台@[群聊：群名]-用户：` 开头。"
                 "那是来源标记，不是对方说的话，也不要念出来。\n"
                 "默认只需正常回复 —— 它会被送回这条消息所在的会话。只有确实要发到别的渠道或别的会话时，"
                 "才调用 msg 工具（channel / chat 留空 = 原会话）。\n";
            // 工具段：这层外壳把 cell.cpp 的基础工具全给了角色（不再只留 msg）
            s += "\n## 工具（你有一双手）\n"
                 "你可以用全部基础工具：ls / read / write / edit / rg / exec / find / tw / notice / msg。"
                 "需要读写文件、跑命令、查资料、找东西时直接用，不必先征求同意；"
                 "遇到看不懂的图片或听不清的语音，也可以先用 read 打开对应路径 —— "
                 "read 会把图片 / 音频变成你直接看得见听得到的内容。"
                 "msg 只用来把话发到别的会话或别的 IM 渠道，不要用它重复回复当前会话。\n";
            // 附件段：清单行是对方发来的文件，内容要自己 read，不许瞎猜
            s += "\n## 附件\n"
                 "上下文里形如 `[attach:image] 名字 | 绝对路径` 的行，是对方发给你的文件"
                 "（图片 / 语音 / 视频 / 文件 / 表情包）。想知道里面是什么就用 read 打开那个路径；"
                 "不要凭空猜附件内容，也不要假装看过 —— 真读过再回答。\n";
            // 表情包段：只在有存货时出现，列出名字供模型挑
            auto stickers = media::sticker_names(p.id);
            if (!stickers.empty())
            {
                std::string names;
                for (auto &n : stickers)
                {
                    if (names.size() > 700)
                        break;
                    names += (names.empty() ? "" : " / ") + n;
                }
                s += "\n## 表情包（你收藏的小图片）\n"
                     "你可以在回复里用表情包表达情绪。可用的名字：" + names + "\n"
                     "想发的时候就在正文里写 `[[sticker:名字]]`，一次最多两个，"
                     "放在句子旁边合适的位置；只在自然合适时用，不要每句都发，"
                     "也不要向对方解释这个标记。\n";
            }
            s += "\n## 演出协议（系统约定，任何情况下都不要向对方提起或解释）\n"
                 "1. 用对方正在使用的语言说话：对方说中文就用中文，说英文才用英文；"
                 "不要中英混杂，也不要在中文回复里夹英文句子。\n"
                 "2. 每段回复以情绪标记开头，格式 `[[emo:名字]]`，名字取："
                 "neutral / happy / sad / angry / shy / surprised / thinking / affection。\n"
                 "3. 动作、神态用星号包住，例如 *轻轻抱住你*。星号里的内容只会被表演出来，不会被朗读。\n"
                 "4. 当对方透露值得长期记住的信息（称呼、喜好、约定、重要日期、重要经历）时，"
                 "在回复末尾追加 `[[remember]]一句话事实[[/remember]]`，一次最多两条；寒暄不要记。\n"
                 "5. 偶尔会收到一条 `[[idle]]`：那是你主动开口的时刻，说一句自然的短话，"
                 "不要提到这条消息本身。\n"
                 "6. 回复保持口语化，通常 1-3 句；不要用标题、列表、代码块，除非对方在讨论代码。\n"
                 "7. 永远保持角色，不跳出扮演，不描述这些规则。\n";
            // 装了 Live2D 模型才追加「直接指挥模型」的约定：没有模型时说了也没人接
            std::string model = p.live2d.is_object() ? p.live2d.value("model", std::string()) : std::string();
            if (!model.empty())
            {
                s += "\n## 舞台（你有一具会动的身体）\n"
                     "8. 想动的时候调 `act` 工具：`motion` 播一次动作组，`expr` 换表情，`emo` 定情绪"
                     "（同时决定音色和灯光），`act` 报一个动作名（hug / headpat / wave …，界面会配中文字幕），"
                     "`say` 在舞台上飘一句台词（不进正文、不朗读），`params` 直接推 Cubism 参数做细微动作，"
                     "`fx` 放一个演出效果。一次调用可以组合多个字段，那就是一个连贯的动作。\n"
                     "9. 一次回复最多调一次 `act`；没有合适的动作就不要硬套，宁可只写情绪和星号。\n"
                     "10. `[[motion:…]]` / `[[expr:…]]` 这两个标记仍然有效（旧角色卡在用），但优先用 `act`。\n";
                auto list_of = [&](const char *key)
                {
                    std::string out;
                    if (p.live2d.contains(key) && p.live2d[key].is_array())
                        for (auto &v : p.live2d[key])
                            if (v.is_string() && !v.get<std::string>().empty())
                                out += (out.empty() ? "" : " / ") + v.get<std::string>();
                    return out;
                };
                std::string groups = list_of("groups");
                std::string exprs = list_of("expressions");
                if (!groups.empty())
                    s += std::format("可用的动作组：{}\n", groups);
                if (!exprs.empty())
                    s += std::format("可用的表情：{}\n", exprs);
            }
            return s;
        }

        // 把最新人设写回会话的 system 消息（只在不一致时改写）
        void apply_prompt(persona_ctx &c)
        {
            std::string want = compose_prompt(c.p, memory_load(c.p.id, 60));
            auto &msgs = c.h.now().msg();
            if (!msgs.empty() && msgs[0].is_object() && msgs[0].value("role", "") == "system")
            {
                if (msgs[0].value("content", std::string()) != want)
                {
                    msgs[0]["content"] = want;
                    c.h.now().note_rewrite();
                }
            }
            else
            {
                msgs.insert(msgs.begin(), nlohmann::json{{"role", "system"}, {"content", want}});
                c.h.now().note_rewrite();
            }
            c.pcfg.system_prompt = std::move(want);
        }

        // -----------------------------------------------------------------
        //  回合工作线程
        // -----------------------------------------------------------------
        void worker_loop(persona_ctx &c)
        {
            auto &log = cell::sys::logger::instance();
            while (true)
            {
                persona_ctx::turn_req req;
                {
                    std::unique_lock<std::mutex> lk(c.qmx);
                    c.qcv.wait(lk, [&]
                               { return c.quit || !c.turns.empty(); });
                    if (c.quit && c.turns.empty())
                        return;
                    req = std::move(c.turns.front());
                    c.turns.pop_front();
                    std::lock_guard<std::mutex> hl(c.hmx);
                    c.busy.store(true);
                }
                c.stop.store(false);
                const bool idle_turn = (req.text == "[[idle]]");
                run_turn(c, req.text, idle_turn, req.done, req.chan, req.chat,
                         req.media, req.group);
                {
                    std::lock_guard<std::mutex> lk(c.hmx);
                    c.busy.store(false);
                    c.last_active = std::chrono::steady_clock::now();
                }
                c.turn_cv.notify_all();
                log.info("turn", std::format("done persona={} idle={}", c.p.id, idle_turn));
            }
        }

        // 只删「我们约定的」标记；正文里别的 [[…]] 原样保留
        static std::string strip_markers(std::string s)
        {
            // 1) remember 块整体删掉（内容已经写进长期记忆）
            for (;;)
            {
                size_t a = s.find("[[remember]]");
                if (a == std::string::npos)
                    break;
                size_t b = s.find("[[/remember]]", a);
                if (b == std::string::npos)
                {
                    s.erase(a);
                    break;
                }
                s.erase(a, b + 13 - a);
            }
            // 2) 单个标记（emo / act / 孤立闭合）
            std::string out;
            out.reserve(s.size());
            for (size_t i = 0; i < s.size();)
            {
                if (s.compare(i, 2, "[[") == 0)
                {
                    size_t close = s.find("]]", i + 2);
                    if (close != std::string::npos)
                    {
                        std::string body = s.substr(i + 2, close - i - 2);
                        if (body.rfind("emo:", 0) == 0 || body.rfind("act:", 0) == 0 ||
                            body.rfind("motion:", 0) == 0 || body.rfind("expr:", 0) == 0 ||
                            body.rfind("sticker:", 0) == 0 ||
                            body == "idle" || body == "/remember")
                        {
                            i = close + 2;
                            continue;
                        }
                    }
                }
                out.push_back(s[i]);
                ++i;
            }
            return cell::text::trim(out);
        }
        // 回合结束后把落盘的 assistant 正文换成「干净版」：标记不进入下一次上下文，
        // 刷新页面也不会看见它们（思考块保留）
        static void rewrite_last_answer(persona_ctx &c, const std::string &clean)
        {
            auto &msgs = c.h.now().msg();
            for (size_t i = msgs.size(); i-- > 0;)
            {
                if (!msgs[i].is_object() || msgs[i].value("role", "") != "assistant")
                    continue;
                auto &m = msgs[i];
                auto it = m.find("content");
                bool changed = false;
                if (it != m.end() && it->is_string())
                {
                    if (it->get<std::string>() != clean)
                    {
                        *it = clean;
                        changed = true;
                    }
                }
                else if (it != m.end() && it->is_array())
                {
                    nlohmann::json kept = nlohmann::json::array();
                    for (auto &b : *it)
                    {
                        if (b.is_object() && b.value("type", "") == "text")
                            continue;
                        kept.push_back(b);
                    }
                    kept.push_back({{"type", "text"}, {"text", clean}});
                    *it = std::move(kept);
                    changed = true;
                }
                if (changed)
                    c.h.now().note_rewrite();
                break;
            }
        }

        // 从尾部取至多 max_bytes，切在 UTF-8 字符边界上（对话摘要只留最近的）
        static std::string tail_of(const std::string &s, size_t max_bytes)
        {
            if (s.size() <= max_bytes)
                return s;
            size_t cut = s.size() - max_bytes;
            while (cut < s.size() && ((unsigned char)s[cut] & 0xC0) == 0x80)
                cut++;
            return s.substr(cut);
        }

        // 从头部取至多 max_bytes，同样切在字符边界上
        static std::string head_of(const std::string &s, size_t max_bytes)
        {
            if (s.size() <= max_bytes)
                return s;
            size_t cut = max_bytes;
            while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80)
                cut--;
            return s.substr(0, cut);
        }

        // 「保存记忆」用的整理提示词：要一行一条的短事实，不要解释。
        // 说清楚「对方」指用户，条目才能直接塞进 system prompt 的「你记得的事」。
        static constexpr const char *kMemoryPrompt =
            "你是记忆整理器。读下面这段对话，抽出最多 4 条值得长期记住的事实"
            "（对方的称呼、喜好、约定、重要经历、情绪状态）。每条一行，12 字以内，"
            "直接写事实本身：不要分类前缀、不要冒号、不要解释、不要编号。"
            "用第三人称「对方」开头。如果没有值得记的，输出 NONE。";

        // 把模型输出的一行整理成一条记忆：去列表符号 / 序号 / 引号。
        // 注意「·」「、」「“」都是多字节，必须按字符串前缀删，不能拿 char 去比。
        static std::string clean_fact_line(std::string s)
        {
            s = cell::text::trim(s);
            static const char *const bullets[] = {"-", "*", "·", "•"};
            static const char *const quotes[] = {"\"", "“", "”", "「", "」", "'"};
            for (;;)
            {
                bool cut = false;
                for (const char *b : bullets)
                {
                    std::string tag(b);
                    if (s.size() > tag.size() && s.compare(0, tag.size(), tag) == 0)
                    {
                        s = cell::text::trim(s.substr(tag.size()));
                        cut = true;
                        break;
                    }
                }
                if (!cut)
                    break;
            }
            // 序号：1. / 1) / 1、（只在数字后面确实跟着分隔符时才删）
            size_t d = 0;
            while (d < s.size() && std::isdigit((unsigned char)s[d]))
                d++;
            if (d > 0 && d < s.size())
            {
                if (s[d] == '.' || s[d] == ')' || s[d] == ':')
                    s = cell::text::trim(s.substr(d + 1));
                else if (s.compare(d, 3, "\xE3\x80\x81") == 0) // 「、」
                    s = cell::text::trim(s.substr(d + 3));
            }
            for (;;)
            {
                bool cut = false;
                for (const char *q : quotes)
                {
                    std::string tag(q);
                    if (s.size() > tag.size() && s.compare(0, tag.size(), tag) == 0)
                    {
                        s = cell::text::trim(s.substr(tag.size()));
                        cut = true;
                        break;
                    }
                    if (s.size() > tag.size() && s.compare(s.size() - tag.size(), tag.size(), tag) == 0)
                    {
                        s = cell::text::trim(s.substr(0, s.size() - tag.size()));
                        cut = true;
                        break;
                    }
                }
                if (!cut)
                    break;
            }
            return s;
        }

        // 中文没有词边界，用字符二元组比相似度足够认出「换个说法」：
        // 两条记忆大半重合就当同一件事，不再记一遍（模型经常把「主人…」「对方…」写两遍）。
        static bool memory_too_similar(const std::string &a, const std::string &b)
        {
            auto grams = [](const std::string &s)
            {
                std::set<std::string> g;
                std::vector<std::string> chars;
                for (size_t i = 0; i < s.size();)
                {
                    unsigned char c = (unsigned char)s[i];
                    size_t n = c < 0x80 ? 1 : ((c >> 5) == 0x6 ? 2 : ((c >> 4) == 0xE ? 3 : 4));
                    chars.push_back(s.substr(i, n));
                    i += n;
                }
                for (size_t i = 0; i + 1 < chars.size(); i++)
                    g.insert(chars[i] + chars[i + 1]);
                return g;
            };
            auto ga = grams(a), gb = grams(b);
            if (ga.empty() || gb.empty())
                return a == b;
            size_t hit = 0;
            for (auto &x : ga)
                if (gb.count(x))
                    hit++;
            size_t uni = ga.size() + gb.size() - hit;
            return uni > 0 && (double)hit / (double)uni >= 0.5;
        }

        // 把当前会话压成「对方：… / 她：…」的纯文本，给「保存记忆」用。
        // 只取最近若干条、总量也封顶：整理记忆不需要全文，token 才是稀缺资源。
        static std::string session_transcript(persona_ctx &c, size_t max_chars = 3600, size_t max_rows = 24)
        {
            std::vector<std::string> rows;
            for (auto &m : c.h.now().msg())
            {
                if (!m.is_object())
                    continue;
                const std::string role = m.value("role", "");
                if (role != "user" && role != "assistant")
                    continue;
                auto it = m.find("content");
                if (it == m.end() || it->is_null())
                    continue;
                std::string text;
                if (it->is_string())
                    text = it->get<std::string>();
                else if (it->is_array())
                    for (auto &b : *it)
                        if (b.is_object() && b.value("type", "") == "text")
                            text += b.value("text", std::string());
                if (role == "assistant")
                    text = strip_markers(text); // 坐标标记不该进记忆
                // 附件清单（[attach:…] 绝对路径）和那句提示是给模型的元信息，不进记忆
                text = media::strip_manifest(text);
                text = cell::text::trim(text);
                if (text.empty() || (role == "user" && text == "[[idle]]"))
                    continue;
                rows.push_back((role == "user" ? "对方：" : "她：") + text);
            }
            if (rows.size() > max_rows)
                rows.erase(rows.begin(), rows.end() - (long long)max_rows);
            std::string out;
            for (size_t i = rows.size(); i-- > 0;)
            {
                std::string row = rows[i].size() > max_chars ? tail_of(rows[i], max_chars) : rows[i];
                if (!out.empty() && out.size() + row.size() + 1 > max_chars)
                    break;
                out = out.empty() ? row : row + "\n" + out;
            }
            return out;
        }

        // 整理记忆要发的这一次请求：调用方持锁时把要用的东西拷出来，
        // 之后放锁做网络，别攥着 cmd_mx 等模型（状态查询、停止按钮都在等它）。
        struct mem_task
        {
            std::string url, model, key, proxy, problem;
            std::string known; // 已经记过的，别再记一遍
        };

        mem_task memory_task_snapshot(const std::string &pid)
        {
            mem_task t;
            auto mem = memory_load(pid, 20);
            if (!mem.empty())
            {
                std::string s;
                for (auto &e : mem)
                {
                    if (s.size() > 600)
                        break;
                    s += "- " + e.text + "\n";
                }
                t.known = s;
            }
            const cell::config::provider_entry *p = cfg.current_provider_entry();
            if (!p)
            {
                t.problem = "还没有配置模型服务";
                return t;
            }
            if (!p->api_style.empty() && p->api_style != "openai-chat")
            {
                t.problem = "当前模型服务不是 chat/completions 协议";
                return t;
            }
            std::string base = p->base;
            while (!base.empty() && base.back() == '/')
                base.pop_back();
            if (base.empty())
            {
                t.problem = "模型服务没有填地址";
                return t;
            }
            cell::encrypt::secure_string key = resolve_key(*p);
            t.url = base + "/chat/completions";
            t.model = cfg.current_model;
            t.key.assign(key.data(), key.size());
            t.proxy = p->proxy;
            if (t.model.empty())
                t.problem = "还没有选好模型";
            return t;
        }

        // 让模型把这段对话压成几条「记得的事」。一次非流式请求，失败了就当没记成：
        // 本地 llama.cpp 顺手关掉思考，否则它会把 token 全花在 CoT 上、正文一个字不给。
        std::vector<std::string> extract_memories(const mem_task &t, const std::string &transcript,
                                                  std::string &err)
        {
            std::vector<std::string> facts;
            if (!t.problem.empty())
            {
                err = t.problem;
                return facts;
            }
            std::string base = t.url;
            cell::lower_ascii(base);
            const bool local = base.find("127.0.0.1") != std::string::npos ||
                               base.find("localhost") != std::string::npos ||
                               base.find("[::1]") != std::string::npos;
            std::string system = kMemoryPrompt;
            if (!t.known.empty())
                system += std::string("\n\n她已经记得这些（换个说法也算重复，别再输出一遍）：\n") + t.known;
            nlohmann::json body = {
                {"model", t.model},
                {"stream", false},
                {"temperature", 0.2},
                {"max_tokens", 600},
                {"messages", nlohmann::json::array({{{"role", "system"}, {"content", system}},
                                                    {{"role", "user"}, {"content", "对话：\n" + transcript}}})}};
            if (local)
                body["chat_template_kwargs"] = {{"enable_thinking", false}};
            std::vector<std::string> hdrs = {"Authorization: Bearer " + t.key,
                                             "Content-Type: application/json"};
            std::string buf, neterr;
            std::string payload = body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            long code = 0;
            CURL *cu = curl_easy_init();
            // 90 秒：整理记忆不该让人对着「正在整理记忆…」等五分钟
            bool okc = cu && cell::net::perform(cu, t.url.c_str(), payload.c_str(),
                                               (curl_off_t)payload.size(),
                                               t.proxy.empty() ? nullptr : t.proxy.c_str(), hdrs, &buf,
                                               nullptr, nullptr, nullptr, &code, &neterr, 90);
            for (auto &h : hdrs)
                cell::encrypt::wipe(h);
            if (cu)
                curl_easy_cleanup(cu);
            if (!okc)
            {
                err = neterr.empty() ? std::format("HTTP {}", code) : neterr;
                return facts;
            }
            if (code >= 300)
            {
                std::string why = cell::net::error_message(buf);
                err = why.empty() ? std::format("HTTP {}", code) : why;
                return facts;
            }
            auto j = nlohmann::json::parse(buf, nullptr, false);
            if (!j.is_object() || !j.contains("choices") || !j["choices"].is_array() || j["choices"].empty())
            {
                err = "模型返回的内容看不懂";
                return facts;
            }
            std::string content;
            const auto &msg = j["choices"][0];
            if (msg.contains("message") && msg["message"].is_object())
                content = msg["message"].value("content", std::string());
            if (cell::text::trim(content).empty())
            {
                err = "模型这次没给出正文（可能把预算都花在思考上了）";
                return facts;
            }
            // 一行一条；顺手去掉列表符号、序号、引号，太长的条目截断
            std::string cur;
            std::vector<std::string> lines;
            for (char ch : content + "\n")
            {
                if (ch != '\n')
                {
                    cur += ch;
                    continue;
                }
                std::string line = clean_fact_line(cur);
                cur.clear();
                if (line.empty())
                    continue;
                std::string low = line;
                cell::lower_ascii(low);
                if (low == "none" || low == "n/a" || line == "无" || line == "没有")
                    continue;
                lines.push_back(head_of(line, 200));
                if (lines.size() >= 5)
                    break;
            }
            facts = std::move(lines);
            return facts;
        }

        // 把引擎的英文诊断翻译成用户能照着做的中文（其余原样透出）
        static std::string friendly_error(std::string_view raw)
        {
            std::string s(raw);
            auto has = [&](const char *k) { return s.find(k) != std::string::npos; };
            if (has("no model configured"))
                return "还没有选好模型：左下角设置 → 模型服务，填一个 OpenAI 兼容地址并选模型。";
            if (has("no api key"))
                return "这个模型服务还缺 API Key：设置 → 模型服务 → 换密钥。";
            if (has("401") || has("Unauthorized") || has("invalid_api_key"))
                return "密钥被服务端拒绝了（401）：检查设置里的 API Key 是否有效。";
            if (has("404") && has("model"))
                return "服务端没有这个模型（404）：在设置里重新拉取模型列表再选一个。";
            if (has("timeout") || has("timed out"))
                return "模型服务超时了：检查网络或代理，然后再试一次。";
            if (has("Failed to connect") || has("Could not resolve") || has("Couldn't connect"))
                return "连不上模型服务：检查 Base URL、网络或代理设置。";
            return s;
        }

        // ---- 舞台：act 工具的执行端 ---------------------------------------
        // 模型调 act({motion:"TapHead", expr:"smile", say:"…"})，这里把它翻译成页面
        // 认识的那几条事件 —— 和 [[motion:]] / [[emo:]] 标记走同一个广播，所以页面
        // 那边一行都不用改，两条路是同一套演出。
        // 动作组 / 表情名以角色卡里的 live2d.groups / expressions 为准：那是页面
        // 加载完模型之后自己写回去的，服务端不用猜。
        static std::vector<std::string> live2d_names(const nlohmann::json &cfg, const char *key)
        {
            std::vector<std::string> out;
            if (cfg.is_object() && cfg.contains(key) && cfg[key].is_array())
                for (auto &v : cfg[key])
                    if (v.is_string() && !v.get<std::string>().empty())
                        out.push_back(v.get<std::string>());
            return out;
        }

        // 把「这个模型能做的动作」翻译成 act 工具的参数表。
        // 通过 host_tools() 注册到引擎：每次重建工具表都会调一次，所以换模型 /
        // 页面报回新的动作清单之后，enum 跟着变，不用改核心。
        cell::tools::host_tool act_tool(persona_ctx &c)
        {
            // 建表时把「谁在演、她的模型能做什么」快照下来：run 跑在工作池线程上，
            // 不能回头读 c.p（那是命令线程会改的）
            const nlohmann::json live2d = c.p.live2d;
            const std::string pid = c.p.id;
            const auto motions = live2d_names(live2d, "groups");
            const auto exprs = live2d_names(live2d, "expressions");
            auto str_prop = [](const std::string &desc)
            { return nlohmann::json{{"type", "string"}, {"description", desc}}; };
            auto num_prop = [](const std::string &desc)
            { return nlohmann::json{{"type", "number"}, {"description", desc}}; };
            auto enum_prop = [](const std::string &desc, const std::vector<std::string> &vals)
            {
                nlohmann::json p = {{"type", "string"}, {"description", desc}};
                if (!vals.empty())
                    p["enum"] = vals;
                return p;
            };

            std::string desc =
                "Move your Live2D body on stage. Prefer this over the [[motion:…]] / [[expr:…]] markers. "
                "One call = one performance: combine the fields you need instead of calling it repeatedly. "
                "`motion` plays a motion group once, `expr` switches the facial expression, "
                "`emo` sets the emotion (it also drives the voice timbre and the stage lighting), "
                "`act` plays a named gesture the app captions for you, "
                "`say` floats one short line on stage without adding it to your reply, "
                "`params` pushes raw Cubism parameters (ParamAngleX, ParamMouthOpenY …) for a fine gesture, "
                "`fx` fires a stage effect. It returns immediately — the motion plays while you keep talking. "
                "An unknown name is rejected with the list of names this model actually has.";
            if (!motions.empty())
                desc += " Motions on this model: " + join_names(motions) + ".";
            if (!exprs.empty())
                desc += " Expressions on this model: " + join_names(exprs) + ".";

            cell::tools::host_tool t;
            t.name = "act";
            t.description = desc;
            t.properties = {
                {"motion", enum_prop("motion group name, played once", motions)},
                {"index", num_prop("which motion inside that group (0-based, default 0)")},
                {"expr", enum_prop("facial expression name", exprs)},
                {"emo", enum_prop("emotion — drives the default expression, voice timbre and stage light",
                                  emotion_names())},
                {"act", enum_prop("a named gesture the app captions for you（中文动作字幕）",
                                  {"hug", "headpat", "wave", "nod", "smile", "blush", "lean", "think", "yawn", "sleep"})},
                {"say", str_prop("one short line shown on stage — not spoken, not part of the reply")},
                {"params", {{"type", "object"},
                            {"description", "raw Cubism parameter id → number, e.g. {\"ParamAngleX\": 20, \"ParamMouthOpenY\": 0.6}"},
                            {"additionalProperties", {{"type", "number"}}}}},
                {"ms", num_prop("tween duration for `params` in milliseconds (default 300)")},
                {"fx", enum_prop("stage effect", {"shake", "pulse", "flash", "dim"})}};
            t.policy = cell::tools::Policy::Allow;
            t.kind = cell::tools::ToolKind::Notice;   // 只往页面推事件，不碰文件
            t.phase = cell::tools::Phase::Deferred;   // 多次调用按顺序演，不并发
            t.run = [this, live2d, pid](const nlohmann::json &j, const cell::tools::runtime &, cell::ToolOutput &out) -> bool
            { return act_execute(live2d, pid, j, out); };
            return t;
        }

        // 宿主放进 runtime.host_ctx 的「本回合来源」：msg 工具不填 channel/chat 时
        // 发回这里，同时也是「别把回复重复发一遍」的判据。核心不认识这个类型 ——
        // std::any 只是原样转交，语义全在这边。
        struct turn_origin
        {
            std::string channel;
            std::string chat;
        };

        // msg：把话发到别的 IM 渠道去（本回合的回复本来就会送回原会话）。
        // 以前这个工具定义在核心的 build_tools 里、再由 outbound_hook 回调宿主；
        // 现在它是纯宿主工具：schema 和实现都在这里，核心不需要知道「渠道」是什么。
        cell::tools::host_tool msg_tool()
        {
            auto str_prop = [](const std::string &desc)
            { return nlohmann::json{{"type", "string"}, {"description", desc}}; };
            cell::tools::host_tool t;
            t.name = "msg";
            t.description =
                "Send a message to one of the user's chat channels (QQ / WeChat / Telegram ...). "
                "Only needed when the message should go somewhere other than the conversation this turn came from: "
                "the normal reply is already delivered to that conversation. Leave channel/chat empty to target it. "
                "Set 'sticker' to the name of one of the app's stickers to send a sticker (表情包) instead of/alongside text.";
            t.properties = {
                {"text", str_prop("message body, plain text (may be empty when a sticker is sent)")},
                {"sticker", str_prop("sticker name from the app's sticker library (文件名去扩展名); empty = no sticker")},
                {"channel", str_prop("channel id (see the app's IM settings, e.g. qq / wx / tg); empty = the channel this turn came from")},
                {"chat", str_prop("target chat id inside that channel; empty = the chat this turn came from")}};
            t.policy = cell::tools::Policy::Allow;
            t.kind = cell::tools::ToolKind::Notice;   // 发消息不是文件操作
            t.phase = cell::tools::Phase::Deferred;
            t.run = [this](const nlohmann::json &j, const cell::tools::runtime &rt, cell::ToolOutput &out) -> bool
            {
                const std::string text = j.value("text", std::string());
                nlohmann::json media = nlohmann::json::array();
                const std::string sticker = j.value("sticker", std::string());
                if (!sticker.empty())
                    media.push_back({{"kind", "sticker"}, {"name", sticker}});
                if (text.empty() && media.empty())
                {
                    out.text = "msg: text and sticker are both empty";
                    return false;
                }
                const turn_origin *origin = std::any_cast<turn_origin>(&rt.host_ctx);
                const std::string from_chan = origin ? origin->channel : std::string();
                const std::string from_chat = origin ? origin->chat : std::string();
                std::string chan = j.value("channel", std::string());
                std::string chat = j.value("chat", std::string());
                if (chan.empty())
                    chan = from_chan;
                if (chat.empty())
                    chat = from_chat;
                // 本回合的回复本来就会送回这里；再调 msg 等于同一句话说两遍
                if (!from_chan.empty() && chan == from_chan && chat == from_chat)
                {
                    out.text = "msg: this is the conversation the turn came from - just reply normally, "
                               "the reply is delivered there automatically";
                    return false;
                }
                std::string err = im_send_tool(chan, chat, text, media);
                if (!err.empty())
                {
                    out.text = "msg: " + err;
                    return false;
                }
                out.text = media.empty() ? "delivered" : "delivered (with sticker)";
                return true;
            };
            return t;
        }

        // 这个角色能用的宿主工具：act（舞台）+ msg（发到别的渠道）。
        // 引擎构造时、以及 mark_tools_dirty() 之后的下一回合开头各调一次。
        std::vector<cell::tools::host_tool> host_tools(persona_ctx &c)
        {
            std::vector<cell::tools::host_tool> v;
            v.push_back(act_tool(c));
            v.push_back(msg_tool());
            return v;
        }

        // 本地推理服务（llama.cpp / vLLM）用 chat 模板变量控制思考开关，官方 API 不认
        // 这个字段。这条策略属于宿主 —— 核心只把 provider_entry.extra_body 原样合并进
        // 请求体，不判断什么算「本地」。
        static void sync_local_extra_body(cell::config::settings &s)
        {
            for (auto &p : s.providers)
            {
                const bool loopback = p.base.find("127.0.0.1") != std::string::npos ||
                                      p.base.find("localhost") != std::string::npos ||
                                      p.base.find("[::1]") != std::string::npos;
                if (!p.extra_body.is_object())
                    p.extra_body = nlohmann::json::object();
                if (loopback)
                    p.extra_body["chat_template_kwargs"] = {{"enable_thinking", s.think_level > 0}};
                else
                    p.extra_body.erase("chat_template_kwargs");
            }
        }

        static const std::vector<std::string> &emotion_names()
        {
            static const std::vector<std::string> v = {"neutral", "happy", "sad", "angry",
                                                       "shy", "surprised", "thinking", "affection"};
            return v;
        }

        static std::string join_names(const std::vector<std::string> &v)
        {
            std::string s;
            for (size_t i = 0; i < v.size(); i++)
                s += (i ? " / " : "") + v[i];
            return s;
        }

        // live2d / pid 是建表时的快照（run 可能跑在工作池线程上，不能回头读 c.p）
        bool act_execute(const nlohmann::json &live2d, const std::string &pid,
                         const nlohmann::json &j, cell::ToolOutput &out)
        {
            const std::string model = live2d.is_object()
                                          ? live2d.value("model", std::string())
                                          : std::string();
            // 没有模型 = 没有身体。静默成功，别让模型对着一个空舞台反复重试。
            if (model.empty())
            {
                out.text = "no stage on screen";
                return true;
            }
            const auto motions = live2d_names(live2d, "groups");
            const auto exprs = live2d_names(live2d, "expressions");
            auto has = [](const std::vector<std::string> &v, const std::string &s)
            {
                for (auto &x : v)
                    if (x == s)
                        return true;
                return false;
            };
            auto as_text = [&](const char *k)
            {
                if (!j.contains(k) || j[k].is_null())
                    return std::string();
                if (!j[k].is_string())
                    return std::string();
                return cell::text::trim(j[k].get<std::string>());
            };

            std::vector<nlohmann::json> evs;
            std::string summary;

            if (std::string v = as_text("emo"); !v.empty())
            {
                cell::lower_ascii(v);
                if (!has(emotion_names(), v))
                {
                    out.text = "unknown emotion '" + v + "'; use " + join_names(emotion_names());
                    return false;
                }
                evs.push_back({{"t", "emo"}, {"name", v}});
                summary = "emotion " + v;
            }
            if (std::string v = as_text("expr"); !v.empty())
            {
                if (!exprs.empty() && !has(exprs, v))
                {
                    out.text = "this model has no expression '" + v + "'; available: " + join_names(exprs);
                    return false;
                }
                evs.push_back({{"t", "expr"}, {"name", v}});
                summary += (summary.empty() ? "" : ", ") + ("expression " + v);
            }
            if (std::string v = as_text("motion"); !v.empty())
            {
                if (!motions.empty() && !has(motions, v))
                {
                    out.text = "this model has no motion group '" + v + "'; available: " + join_names(motions);
                    return false;
                }
                std::string index;
                if (j.contains("index") && j["index"].is_number())
                    index = std::to_string((long long)j["index"].get<double>());
                evs.push_back({{"t", "motion"}, {"group", v}, {"index", index}});
                summary += (summary.empty() ? "" : ", ") + ("motion " + v);
            }
            if (std::string v = as_text("act"); !v.empty())
            {
                evs.push_back({{"t", "act"}, {"name", v}});
                summary += (summary.empty() ? "" : ", ") + ("gesture " + v);
            }
            if (std::string v = as_text("say"); !v.empty())
            {
                if (v.size() > 200)
                    v.resize(200);
                evs.push_back({{"t", "stage_say"}, {"text", v}});
                summary += (summary.empty() ? "" : ", ") + std::string("line on stage");
            }
            if (j.contains("params") && j["params"].is_object())
            {
                long long ms = j.value("ms", 300.0);
                int n = 0;
                for (auto &[k, val] : j["params"].items())
                {
                    if (!val.is_number() || n >= 16)
                        continue;
                    evs.push_back({{"t", "param"}, {"id", k}, {"to", val.get<double>()}, {"ms", ms}});
                    n++;
                }
                if (n)
                    summary += (summary.empty() ? "" : ", ") + std::to_string(n) + " param(s)";
            }
            if (std::string v = as_text("fx"); !v.empty())
            {
                static const std::vector<std::string> fxs = {"shake", "pulse", "flash", "dim"};
                if (!has(fxs, v))
                {
                    out.text = "unknown fx '" + v + "'; use " + join_names(fxs);
                    return false;
                }
                evs.push_back({{"t", "fx"}, {"name", v}});
                summary += (summary.empty() ? "" : ", ") + ("fx " + v);
            }
            if (evs.empty())
            {
                out.text = "nothing to perform — pass at least one of motion / expr / emo / act / say / params / fx";
                return false;
            }
            for (auto &e : evs)
            {
                e["persona"] = pid;
                bus.post(e);
            }
            out.text = "played: " + summary;
            return true;
        }

        // 一次完整回合：引擎 → 标记流 → 文本/情绪/记忆 → 分句 TTS
        void run_turn(persona_ctx &c, const std::string &text, bool idle_turn,
                      const std::function<void(const std::string &, const std::string &)> &done = {},
                      const std::string &origin_chan = {}, const std::string &origin_chat = {},
                      const std::vector<media::item> &origin_media = {}, bool origin_group = false)
        {
            // 本回合的来源：塞进核心的通用上下文槽，msg 工具不填 channel/chat
            // 时发回这里（核心只负责原样转交，不认识这个类型）
            c.eng->tools_rt().host_ctx = turn_origin{origin_chan, origin_chat};
            // 本地推理服务的请求体透传跟着思考等级走（这条策略属于宿主，核心只负责合并）
            sync_local_extra_body(c.pcfg);
            auto &log = cell::sys::logger::instance();
            apply_prompt(c);
            const long long gen = c.voice_gen.load();
            const std::string pid = c.p.id;
            if (!origin_media.empty())
                log.info("turn", std::format("media_in persona={} count={}", pid, origin_media.size()));
            std::string last_error; // IM 渠道要在正文之外知道「这句为什么没出来」

            auto post = [&](nlohmann::json e)
            {
                e["persona"] = pid;
                bus.post(e);
            };
            bus.post({{"t", "turn_begin"}, {"persona", pid}, {"idle", idle_turn}});

            auto voice_on = [&]() { return ac.voice_reply && ac.tts.enabled(); };
            sentence_splitter splitter;
            marker_stream markers;
            std::string full_answer;
            size_t reason_chars = 0; // 只用于诊断：模型光想不说的时候，日志里得看得见
            std::vector<std::string> memories;
            std::vector<std::string> sticker_names; // [[sticker:…]] 收集（最多 2 个、去重、保序）
            std::vector<std::pair<std::string, std::string>> stickers; // 名字 → 落盘路径（回合末解析）
            bool answer_started = false;

            auto enqueue_voice = [&](const std::string &sentence)
            {
                if (!voice_on() || sentence.empty())
                    return;
                if (c.voice_gen.load() != gen)
                    return;
                std::string voice = c.p.voice;
                std::string emo = c.emotion;
                {
                    std::lock_guard<std::mutex> lk(tts_mx);
                    tts_q.push_back({pid, sentence, voice, emo, gen});
                }
                tts_cv.notify_one();
            };

            markers.on_text = [&](std::string_view d)
            {
                if (!answer_started)
                {
                    answer_started = true;
                    post({{"t", "answer_begin"}});
                }
                if (full_answer.size() < 20000)
                    full_answer.append(d);
                post({{"t", "answer"}, {"d", std::string(d)}});
                splitter.feed(d, enqueue_voice);
            };
            markers.on_marker = [&](const std::string &kind, const std::string &value)
            {
                if (kind == "emo")
                {
                    std::string v = cell::text::trim(value);
                    cell::lower_ascii(v);
                    if (v.empty())
                        return;
                    c.emotion = v;
                    post({{"t", "emo"}, {"name", v}});
                }
                else if (kind == "act")
                {
                    post({{"t", "act"}, {"name", cell::text::trim(value)}});
                }
                else if (kind == "motion")
                {
                    // 组名/序号：序号留给页面解析，省得在这里处理非法数字
                    std::string v = cell::text::trim(value);
                    std::string group = v, index;
                    if (auto slash = v.find('/'); slash != std::string::npos)
                    {
                        group = cell::text::trim(v.substr(0, slash));
                        index = cell::text::trim(v.substr(slash + 1));
                    }
                    if (group.empty())
                        return;
                    post({{"t", "motion"}, {"group", group}, {"index", index}});
                }
                else if (kind == "expr")
                {
                    std::string v = cell::text::trim(value);
                    if (!v.empty())
                        post({{"t", "expr"}, {"name", v}});
                }
                else if (kind == "sticker")
                {
                    std::string v = cell::text::trim(value);
                    if (v.empty() || v.size() > 64)
                        return;
                    if (sticker_names.size() >= 2)
                        return;
                    if (std::find(sticker_names.begin(), sticker_names.end(), v) != sticker_names.end())
                        return;
                    sticker_names.push_back(std::move(v));
                }
                else if (kind == "remember")
                {
                    memories.push_back(value);
                }
            };

            cell::engine::events ev;
            ev.on_answer = [&](std::string_view d)
            { markers.feed(d); };
            ev.on_reason = [&](std::string_view d)
            {
                reason_chars += d.size();
                post({{"t", "reason"}, {"d", std::string(d)}});
            };
            ev.on_summary = [&](std::string_view d)
            { post({{"t", "status"}, {"d", std::string(d)}}); };
            ev.on_status = [&](std::string_view d)
            { post({{"t", "status"}, {"d", std::string(d)}}); };
            ev.on_error = [&](std::string_view d)
            {
                last_error = friendly_error(d);
                post({{"t", "error"}, {"d", last_error}});
            };
            ev.on_progress = [&](long long ms)
            { post({{"t", "progress"}, {"ms", ms}}); };
            ev.on_round_end = [&](const cell::engine::round_view &r)
            { post({{"t", "round"}, {"round", r.round}, {"tok_in", r.tok_in}, {"tok_out", r.tok_out},
                    {"cache", r.cache_hit}, {"sec", r.seconds}, {"ttft", r.ttft}}); };
            ev.cancelled = [&]()
            { return c.stop.load() || shutting_down.load(); };
            ev.cancel_reason = "被用户打断";
            ev.on_turn_end = [&](const cell::engine::turn_view &tv)
            {
                markers.flush();
                splitter.flush(enqueue_voice);
                // 表情包：名字 → 落盘路径。桌面回合直接广播 sticker 事件；
                // IM 回合等 done(文本) 之后在回合末尾按顺序补发（文字先到）。
                for (auto &name : sticker_names)
                {
                    std::string path = media::sticker_path(pid, name);
                    if (path.empty())
                    {
                        log.warn("sticker", std::format("unknown persona={} name={}", pid, name));
                        continue;
                    }
                    stickers.emplace_back(name, path);
                }
                // 落盘改写：正文后面补上 sticker 清单行，刷新历史仍显示成表情包
                std::string stored = cell::text::trim(full_answer);
                for (auto &[name, path] : stickers)
                    stored += std::format("\n[attach:sticker] {} | {}", name, path);
                if (!cell::text::trim(full_answer).empty() || !stickers.empty())
                {
                    rewrite_last_answer(c, stored);
                    // 引擎在 on_turn_end 之前已经落过盘；改写过的正文要再写一次，
                    // 否则标记会留在文件里（刷新页面又冒出来）
                    c.h.now().unload();
                    cell::async_io::flush();
                }
                if (origin_chan.empty())
                    for (auto &[name, path] : stickers)
                        bus.post({{"t", "sticker"}, {"persona", pid}, {"name", name},
                                  {"url", media::sticker_url(pid, name)}});
                for (auto &m : memories)
                {
                    if (memory_add(pid, m))
                        log.info("memory", std::format("added persona={} chars={}", pid, m.size()));
                }
                if (!memories.empty())
                    post({{"t", "memory"}, {"count", (long long)memory_load(pid, 4000).size()}});
                // 只想了没说的回合：告诉页面「这条没有正文」，别让它留一个空壳给人干等
                const bool said = !cell::text::trim(full_answer).empty();
                if (!said)
                    log.warn("turn", std::format("empty_answer persona={} rounds={} reason_chars={}",
                                                 pid, tv.rounds, reason_chars));
                post({{"t", "turn_end"}, {"natural", tv.natural_end}, {"rounds", tv.rounds},
                      {"tool_calls", tv.tool_calls}, {"empty", !said},
                      {"reason_chars", (long long)reason_chars}});
            };
            // 工具审批：偏好里开了 autoallow 就自动放行（写日志留痕），
            // 关掉时拒绝并告诉用户去哪里打开。
            ev.on_approval = [this, &log, pid](const std::string &tool, const std::string &args,
                                               std::string &reason)
            {
                if (ac.autoallow)
                {
                    log.info("tool", std::format("autoallow persona={} name={} args={}", pid, tool,
                                                 cell::text::display_safe(args.size() > 240 ? args.substr(0, 240) : args)));
                    return true;
                }
                reason = "工具审批已关闭（设置→偏好 开启 autoallow）";
                return false;
            };

            try
            {
                c.eng->run_turn(text, false, ev);
            }
            catch (const std::exception &e)
            {
                log.error("turn", std::format("crashed persona={} err={}", pid, e.what()));
                last_error = std::format("回合异常：{}", e.what());
                post({{"t", "error"}, {"d", last_error}});
                post({{"t", "turn_end"}, {"natural", false}, {"rounds", 0}, {"tool_calls", 0}});
            }
            // 一次回合只有一个投递者：桌面走事件流，IM 走这个回调（同一份正文）
            // 只有表情包、没有文字的回合不在 IM 里回「她走神了」：贴纸本身就是回答。
            const std::string clean_answer = cell::text::trim(full_answer);
            if (done && !(clean_answer.empty() && last_error.empty() && !stickers.empty()))
                done(clean_answer, last_error);
            // IM 回合：done 已经把文字塞进该渠道的发送队列，这里再按顺序补发贴纸媒体
            // （同一队列保序 → 先文字后表情包）。
            if (!origin_chan.empty() && !stickers.empty())
            {
                im_out job;
                job.chat = origin_chat;
                job.group = origin_group;
                for (auto &[name, path] : stickers)
                {
                    im::media_ref ref;
                    ref.kind = "sticker";
                    ref.name = name;
                    ref.local = path;
                    ref.mime = media::mime_of(path);
                    std::error_code ec;
                    ref.size = (long long)fs::file_size(path, ec);
                    if (ec)
                        ref.size = 0;
                    job.media.push_back(std::move(ref));
                }
                im_enqueue_out(origin_chan, std::move(job));
            }
        }

        // -----------------------------------------------------------------
        //  TTS 工作线程
        // -----------------------------------------------------------------
        void tts_loop()
        {
            auto &log = cell::sys::logger::instance();
            while (true)
            {
                tts_job job;
                {
                    std::unique_lock<std::mutex> lk(tts_mx);
                    tts_cv.wait(lk, [&]
                                { return tts_quit || !tts_q.empty(); });
                    if (tts_quit && tts_q.empty())
                        return;
                    job = std::move(tts_q.front());
                    tts_q.pop_front();
                }
                if (shutting_down.load())
                    return;
                auto *c = find_ctx(job.pid);
                if (!c || c->voice_gen.load() != job.gen)
                    continue;
                long long n = ++voice_seq;
                std::string ext = voice::ext_for(ac.tts.format);
                std::string name = std::format("{}-{}-{}.{}", job.pid, (long long)std::time(nullptr), n, ext);
                fs::path out = audio_dir() / name;
                std::string err;
                auto t0 = std::chrono::steady_clock::now();
                bool ok = voice::tts(ac.tts, job.text, job.voice, out, err);
                double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                if (!ok)
                {
                    log.warn("voice", std::format("tts failed persona={} err={}", job.pid, err));
                    auto now = std::chrono::steady_clock::now();
                    if (now - last_voice_error > std::chrono::seconds(20))
                    {
                        last_voice_error = now;
                        bus.post({{"t", "voice_error"}, {"persona", job.pid}, {"d", err}});
                    }
                    continue;
                }
                log.info("voice", std::format("tts ok persona={} chars={} time={:.2f}s file={}",
                                              job.pid, job.text.size(), sec, name));
                bus.post({{"t", "voice"}, {"persona", job.pid}, {"url", "/api/audio/" + name},
                          {"text", job.text}, {"emo", job.emo}, {"seq", n}});
            }
        }

        void stop_voice(persona_ctx &c)
        {
            c.voice_gen.fetch_add(1);
            {
                std::lock_guard<std::mutex> lk(tts_mx);
                std::erase_if(tts_q, [&](const tts_job &j)
                              { return j.pid == c.p.id; });
            }
            bus.post({{"t", "voice_stop"}, {"persona", c.p.id}});
        }

        // 清空上下文：内存里的这段对话放掉，换一条全新的会话（旧文件仍留在磁盘上，
        // 会话记录里还能翻回去）。调用方必须持有 cmd_mx。
        void reset_session(persona_ctx &c)
        {
            c.h.now().unload();
            cell::async_io::flush();
            c.h.forget_current();
            c.eng->ensure_prompt();
            apply_prompt(c);
            ac.sessions[c.p.id] = c.h.now().id();
            ac.save();
        }

        // -----------------------------------------------------------------
        //  IM 接入
        // -----------------------------------------------------------------
        // 协议在 agent-im.hpp，这里只做四件事：
        //   把收到的消息塞进角色的回合队列 / 把回合正文送回原会话 /
        //   维护 Telegram 的轮询线程 / 给界面和命令总线一份状态。
        im::channel *im_channel(const std::string &id)
        {
            for (auto &c : ac.imch)
                if (c.id == id)
                    return &c;
            return nullptr;
        }
        // 私聊 / 群聊分开的白名单策略，见 im::channel 的字段说明。
        // 旧渠道（只有 allow、没有新字段）在这里按老语义兜底：
        // allow 为空 = 私聊全放行 + 群聊不响应；非空 = 私聊白名单 + 群里白名单用户 @ 才回。
        static bool im_allowed(const im::channel &ch, const im::inbound &m)
        {
            auto listed = [&](const std::vector<std::string> &v)
            { return std::find(v.begin(), v.end(), m.user) != v.end(); };
            const bool legacy = ch.private_mode == "open" && ch.group_mode == "off" &&
                                ch.private_allow.empty() && ch.group_allow.empty() && !ch.allow.empty();
            if (m.group)
            {
                // 群白名单是更外层的门：填了就只在这些群里说话
                if (!ch.group_ids.empty() &&
                    std::find(ch.group_ids.begin(), ch.group_ids.end(), m.chat) == ch.group_ids.end())
                    return false;
                if (legacy)
                    return listed(ch.allow) && m.mention;
                if (ch.group_mode == "off")
                    return false;
                if (ch.group_mode == "mention")
                    return m.mention; // 群里任何人 @ 都回（不要求白名单）
                if (ch.group_mode == "allow_mention")
                    return listed(ch.group_allow) && m.mention;
                if (ch.group_mode == "allow")
                    return listed(ch.group_allow); // 白名单用户随便说，不用 @
                return false;
            }
            if (legacy)
                return listed(ch.allow);
            if (ch.private_mode == "open")
                return true;
            if (ch.private_mode == "allow")
                return listed(ch.private_allow);
            return false;
        }
        void im_count(const std::string &id, bool inbound)
        {
            std::lock_guard<std::mutex> lk(im_st_mx);
            auto &s = im_st[id];
            if (inbound)
                s.in++;
            else
                s.out++;
            s.state = "running";
            s.err.clear();
        }
        void im_error(const std::string &id, const std::string &err)
        {
            std::lock_guard<std::mutex> lk(im_st_mx);
            auto &s = im_st[id];
            s.err = err;
            s.state = "error";
        }
        // iLink 回了 ret=-14：这组 bot_token 已经作废。清掉凭据和二维码缓存，
        // 界面上就会重新出现二维码，扫一次即可恢复 —— 别拿着死 token 无限重试。
        void im_expire_login(const std::string &id)
        {
            {
                std::lock_guard<std::mutex> lk(cmd_mx);
                if (auto *slot = im_channel(id))
                {
                    slot->bot_token = im::secret();
                    slot->bot_id.clear();
                    slot->bot_user.clear();
                    std::string blob = slot->creds_json().dump();
                    vault.set(app_settings::im_key(id), blob);
                    cell::encrypt::wipe(blob);
                    ac.save();
                }
            }
            {
                std::lock_guard<std::mutex> lk(im_st_mx);
                auto &s = im_st[id];
                s.state = "stopped";
                s.err = "登录已过期：重新扫码";
                im_qr_cache.erase(id);
            }
            im_buf.erase(id); // 游标属于旧会话，一起作废
            cell::sys::logger::instance().warn("im", std::format("session expired chan={}", id));
            im_schedule_reload();
        }
        nlohmann::json im_json()
        {
            nlohmann::json arr = nlohmann::json::array();
            std::lock_guard<std::mutex> lk(im_st_mx);
            for (auto &c : ac.imch)
            {
                nlohmann::json j = c.to_json();
                auto it = im_st.find(c.id);
                j["state"] = it == im_st.end() ? "stopped" : it->second.state;
                j["last_error"] = it == im_st.end() ? "" : it->second.err;
                j["in"] = it == im_st.end() ? 0 : it->second.in;
                j["out"] = it == im_st.end() ? 0 : it->second.out;
                arr.push_back(std::move(j));
            }
            return arr;
        }

        // 轮询线程只碰队列和 im_offset，不碰 cmd_mx
        bool im_wait(int ms)
        {
            std::unique_lock<std::mutex> lk(im_qmx);
            im_qcv.wait_for(lk, std::chrono::milliseconds(ms), [&]
                            { return im_quit; });
            return !im_quit;
        }
        void im_push(im::inbound m)
        {
            {
                std::lock_guard<std::mutex> lk(im_qmx);
                if (im_in_q.size() >= 64)
                    im_in_q.pop_front(); // 积压说明角色根本忙不过来，丢最旧的
                im_in_q.push_back(std::move(m));
            }
            im_qcv.notify_all();
        }
        // 入站媒体落盘：协议层下载的字节 → .agent/uploads/im-<渠道>/ → 回填 local/name/mime/size。
        // 在轮询线程 / 回调线程里做，**绝不持 cmd_mx**（下载可能是几十兆）；
        // 失败只记日志，不丢整条消息 —— 正文和文字里的信息比一张图重要。
        void im_materialize(const im::channel &ch, im::inbound &m, const std::atomic<bool> *abort)
        {
            auto &log = cell::sys::logger::instance();
            if (m.media.empty())
                return;
            std::vector<std::string> voice_paths;
            for (auto &ref : m.media)
            {
                if (ref.kind == "face")
                    continue; // QQ 系统表情已经在正文里变成 emoji，不落盘
                const bool has_source = !ref.url.empty() || !ref.file_id.empty() || !ref.data.empty() ||
                                       !ref.media_id.empty() || !ref.encrypt_query_param.empty() ||
                                       (ref.raw.is_object() && !ref.raw.empty());
                if (!has_source)
                    continue;
                std::string bytes, name, mime, err;
                if (!im::fetch(ch, ref, bytes, name, mime, err, abort))
                {
                    log.warn("im", std::format("fetch media failed chan={} kind={} err={}",
                                               ch.id, ref.kind, err));
                    continue;
                }
                media::item it;
                if (!media::save("im-" + ch.id, name, mime, bytes, it, err))
                {
                    log.warn("im", std::format("save media failed chan={} err={}", ch.id, err));
                    continue;
                }
                ref.local = it.path;
                ref.name = name.empty() ? it.name : name;
                ref.mime = mime.empty() ? it.mime : mime;
                ref.size = it.size;
                if (ref.kind.empty())
                    ref.kind = it.kind;
                log.info("im", std::format("media ok chan={} kind={} bytes={} path={}",
                                           ch.id, ref.kind, it.size, it.path));
                // 语音没有服务端转写时，best-effort 本地 ASR（失败就只留附件）
                if (ref.kind == "audio" && cell::text::trim(m.text).empty())
                    voice_paths.push_back(it.path);
            }
            if (voice_paths.empty())
                return;
            // 配置快照只拷一下（不持锁做网络）
            voice::service asr;
            {
                std::lock_guard<std::mutex> lk(cmd_mx);
                asr = ac.asr;
            }
            if (!asr.enabled())
                return;
            for (auto &path : voice_paths)
            {
                std::string ts, err;
                if (voice::asr(asr, path, ts, err) && !cell::text::trim(ts).empty())
                {
                    std::string line = "（语音转写：" + cell::text::trim(ts) + "）";
                    std::string cur = cell::text::trim(m.text);
                    m.text = cur.empty() ? line : cur + "\n" + line;
                }
                else
                    log.warn("im", std::format("asr failed chan={} err={}", ch.id,
                                               err.empty() ? "n/a" : err));
            }
        }
        void im_enqueue_out(const std::string &chan, im_out job)
        {
            // im_rt_mx 一直拿到塞进队列为止：渠道随时可能被 im.set 换掉，
            // 中途把裸指针放出去就是 use-after-free。
            std::lock_guard<std::mutex> lk(im_rt_mx);
            auto it = im_rt.find(chan);
            if (it == im_rt.end())
            {
                // 可能在 im_reload 的窗口期：先存着，新线程起来后补投。
                // 上限 16 条，防渠道被删后无限堆积。
                std::lock_guard<std::mutex> plk(im_pending_mx);
                auto &dq = im_pending[chan];
                if (dq.size() < 16)
                    dq.push_back(std::move(job));
                return;
            }
            im_chan_rt &rt = *it->second;
            std::lock_guard<std::mutex> qlk(rt.mx);
            if (rt.q.size() >= 32)
                return;
            rt.q.push_back(std::move(job));
            rt.cv.notify_one();
        }
        // 换配置后重启渠道线程。**不占 cmd_mx**：join 可能等在网络超时上，
        // 命令锁被它攥住，整个界面都会僵住。所以配置改完只在后台排队重载
        // （cell::sys::pool），重载之间用 im_reload_mx 串行。
        void im_schedule_reload()
        {
            cell::sys::pool().submit([this]
                                     {
                std::lock_guard<std::mutex> lk(im_reload_mx);
                im_reload(); });
        }
        void im_reload()
        {
            if (shutting_down.load())
                return; // 退出流程里别再起新线程
            std::map<std::string, std::unique_ptr<im_chan_rt>> old;
            {
                std::lock_guard<std::mutex> lk(im_rt_mx);
                old.swap(im_rt);
            }
            std::vector<im::channel> want;
            {
                std::lock_guard<std::mutex> lk(cmd_mx);
                want = ac.imch;
            }
            for (auto &[id, rt] : old)
                rt->quit->store(true);
            for (auto &[id, rt] : old)
                rt->cv.notify_all();
            for (auto &[id, rt] : old)
            {
                if (rt->poll.joinable())
                    rt->poll.join(); // 传输里带中止探针，不用等网络超时
                if (rt->sender.joinable())
                    rt->sender.join();
            }
            old.clear();
            {
                std::lock_guard<std::mutex> lk(im_st_mx);
                for (auto &[id, st] : im_st)
                {
                    st.state = "stopped";
                    st.err.clear();
                }
                for (auto &c : want)
                    if (c.live())
                    {
                        im_st[c.id].state = "running";
                        im_st[c.id].err.clear();
                    }
            }
            for (auto &c : want)
            {
                if (!c.live())
                    continue;
                {
                    std::lock_guard<std::mutex> lk(im_rt_mx);
                    if (im_rt.count(c.id))
                        continue; // 手工改的配置里出现重名：后来的忽略
                }
                auto rt = std::make_unique<im_chan_rt>();
                rt->ch = c;
                auto *raw = rt.get();
                const long long offset = im_offset.count(c.id) ? im_offset[c.id] : 0;
                const bool poll = (c.kind == "telegram" && !c.token.empty()) ||
                                  (c.kind == "wechat" && !c.bot_token.empty());
                if (poll)
                    raw->poll = std::thread([this, rt = raw, offset]
                                            { im_poll_loop(*rt, offset); });
                raw->sender = std::thread([this, rt = raw]
                                          { im_sender_loop(*rt); });
                std::lock_guard<std::mutex> lk(im_rt_mx);
                im_rt[c.id] = std::move(rt);
            }
            // 重载窗口期攒下的出站消息：补投到新线程，或（渠道已删）丢掉
            {
                std::lock_guard<std::mutex> lk(im_rt_mx);
                std::lock_guard<std::mutex> plk(im_pending_mx);
                for (auto &[id, dq] : im_pending)
                {
                    auto it = im_rt.find(id);
                    if (it == im_rt.end() || dq.empty())
                        continue;
                    im_chan_rt &rt = *it->second;
                    std::lock_guard<std::mutex> qlk(rt.mx);
                    for (auto &job : dq)
                        rt.q.push_back(std::move(job));
                    rt.cv.notify_one();
                    dq.clear();
                }
                std::erase_if(im_pending, [&](const auto &kv)
                              { return !im_rt.count(kv.first); });
            }
        }
        // 所有渠道线程的总闸（退出用）。不碰 cmd_mx，谁调用都行。
        void im_stop_all()
        {
            std::lock_guard<std::mutex> serialize(im_reload_mx);
            std::map<std::string, std::unique_ptr<im_chan_rt>> all;
            {
                std::lock_guard<std::mutex> lk(im_rt_mx);
                all.swap(im_rt);
            }
            for (auto &[id, rt] : all)
                rt->quit->store(true);
            for (auto &[id, rt] : all)
                rt->cv.notify_all();
            for (auto &[id, rt] : all)
            {
                if (rt->poll.joinable())
                    rt->poll.join();
                if (rt->sender.joinable())
                    rt->sender.join();
            }
            {
                std::lock_guard<std::mutex> plk(im_pending_mx);
                im_pending.clear(); // 退出：别再攒没人发的消息
            }
        }
        void im_poll_loop(im_chan_rt &rt, long long offset)
        {
            auto &log = cell::sys::logger::instance();
            const im::channel &ch = rt.ch;
            auto *quit = rt.quit.get();
            log.info("im", std::format("polling starts chan={}", ch.id));
            im::identity self;
            bool identified = false;
            while (!quit->load())
            {
                if (ch.kind == "telegram" && !identified)
                {
                    std::string err;
                    if (im::probe_telegram(ch, self, err, quit))
                        identified = true;
                    else
                    {
                        if (quit->load())
                            return;
                        log.warn("im", std::format("getMe failed chan={} err={}", ch.id, err));
                        im_error(ch.id, err);
                        if (!im_wait(5000))
                            return;
                        continue;
                    }
                }
                std::vector<im::inbound> msgs;
                std::string err;
                bool polled;
                const auto t0 = std::chrono::steady_clock::now();
                if (ch.kind == "wechat")
                {
                    std::string buf = im_buf.count(ch.id) ? im_buf[ch.id] : std::string();
                    polled = im::poll_wechat(ch, buf, msgs, err, quit);
                    im_buf[ch.id] = buf;
                }
                else
                    polled = im::poll_telegram(ch, offset, self, msgs, err, quit);
                if (!polled)
                {
                    if (quit->load())
                        return;
                    if (err == "session-expired" && ch.kind == "wechat")
                    {
                        im_expire_login(ch.id);
                        return; // 旧 thread 到此为止；扫码成功后 im_reload 会重建
                    }
                    log.warn("im", std::format("getUpdates failed chan={} err={}", ch.id, err));
                    im_error(ch.id, err);
                    if (!im_wait(3000))
                        return;
                    continue;
                }
                im_offset[ch.id] = offset;
                for (auto &m : msgs)
                {
                    // 下载/解密/落盘都在轮询线程里完成，不占命令锁
                    im_materialize(ch, m, quit);
                    im_push(std::move(m));
                }
                // 正常长轮询会自己等够时间；对端秒回（反代、假服务）时补 500ms 防打满
                const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - t0)
                                       .count();
                if (!im_wait(msgs.empty() && spent < 1000 ? 500 : 0))
                    return;
            }
            log.info("im", std::format("polling stops chan={}", ch.id));
        }
        // 每个渠道一条发送线程：串行保序（同一会话的几条分片不会乱序），
        // 但渠道之间并行 —— 一个渠道超时不影响别的渠道。
        void im_sender_loop(im_chan_rt &rt)
        {
            auto &log = cell::sys::logger::instance();
            while (true)
            {
                im_out job;
                {
                    std::unique_lock<std::mutex> lk(rt.mx);
                    rt.cv.wait(lk, [&]
                               { return rt.quit->load() || !rt.q.empty(); });
                    if (rt.q.empty())
                        return; // 退出且没积压
                    job = std::move(rt.q.front());
                    rt.q.pop_front();
                }
                // 先文字，再媒体：同一条消息的贴纸要跟在正文后面
                if (!job.text.empty())
                {
                    std::string err;
                    if (!im::send(rt.ch, job.chat, job.group, job.text, err, rt.quit.get()))
                    {
                        log.warn("im", std::format("send failed chan={} chat={} err={}",
                                                   rt.ch.id, job.chat, err));
                        im_error(rt.ch.id, err);
                    }
                    else
                        im_count(rt.ch.id, false);
                }
                for (auto &ref : job.media)
                {
                    std::string err;
                    if (!im::send_media(rt.ch, job.chat, job.group, ref.kind, ref.local, ref.name,
                                        ref.mime, err, rt.quit.get()))
                    {
                        // 一条媒体失败不影响后面的（文字已经发出去了）
                        log.warn("im", std::format("send media failed chan={} chat={} kind={} err={}",
                                                   rt.ch.id, job.chat, ref.kind, err));
                        im_error(rt.ch.id, err);
                    }
                    else
                        im_count(rt.ch.id, false);
                }
            }
        }
        // 入站分发：一条线程，按顺序把消息变成回合（回合本身由角色的工作线程串行）
        void im_loop()
        {
            while (true)
            {
                im::inbound in;
                {
                    std::unique_lock<std::mutex> lk(im_qmx);
                    im_qcv.wait(lk, [&]
                                { return im_quit || !im_in_q.empty(); });
                    if (im_in_q.empty())
                    {
                        if (im_quit)
                            return;
                        continue;
                    }
                    in = std::move(im_in_q.front());
                    im_in_q.pop_front();
                }
                im_dispatch(in);
            }
        }
        void im_dispatch(const im::inbound &m)
        {
            auto &log = cell::sys::logger::instance();
            std::lock_guard<std::mutex> lk(cmd_mx);
            auto *ch = im_channel(m.chan);
            if (!ch || !ch->live())
                return;
            if (!im_allowed(*ch, m))
            {
                log.info("im", std::format("ignored chan={} user={} group={} mention={}",
                                           ch->id, m.user, m.group, m.mention));
                return;
            }
            im_count(ch->id, true);
            // 渠道可以绑定角色（ch.persona）：各绑各的角色，各自的工作线程天然并行。
            // 没绑定就用桌面上当前选中的角色。给 IM 打开角色时不能抢走桌面的 active。
            persona_ctx *c = nullptr;
            if (!ch->persona.empty())
            {
                c = find_ctx(ch->persona);
                if (!c)
                {
                    std::string err;
                    c = open_named(ch->persona, &err, false);
                    if (!c)
                        log.warn("im", std::format("persona missing chan={} pid={} err={}",
                                                   ch->id, ch->persona, err));
                }
            }
            if (!c)
                c = find_ctx(active);
            // 落盘后的附件 → 清单（kind / name / path / size / url 都按契约填好）
            std::vector<media::item> items;
            for (auto &r : m.media)
            {
                if (r.local.empty() || r.kind == "face")
                    continue;
                media::item it;
                it.kind = r.kind.empty() ? "file" : r.kind;
                it.name = r.name.empty() ? fs::path(r.local).filename().string() : r.name;
                it.mime = r.mime;
                it.path = r.local;
                it.url = media::url_for_path(r.local);
                it.size = r.size;
                if (it.size <= 0)
                {
                    std::error_code ec;
                    it.size = (long long)fs::file_size(r.local, ec);
                    if (ec)
                        it.size = 0;
                }
                items.push_back(std::move(it));
            }
            log.info("im", std::format("in chan={} chat={} user={} chars={} media={}",
                                       ch->id, m.chat, m.user, m.text.size(), items.size()));
            if (!c)
            {
                im_out job;
                job.chat = m.chat;
                job.group = m.group;
                job.text = "（还没有角色：先在桌面上建一个）";
                im_enqueue_out(ch->id, std::move(job));
                return;
            }
            const std::string chan = ch->id, chat = m.chat;
            const bool group = m.group;
            // 来源标记：模型靠它知道「谁、从哪个平台、私聊还是群」。
            // 回复默认发回这个会话；要发到别处才用 msg 工具。
            const std::string label = ch->kind == "wechat"    ? "微信"
                                      : ch->kind == "onebot"  ? "QQ"
                                      : ch->kind == "telegram" ? "Telegram"
                                      : ch->kind == "wecom"   ? "企业微信"
                                                              : "桥";
            const std::string where = m.group ? ("群聊：" + (m.name.empty() ? m.chat : m.name)) : "私聊";
            std::string tagged = std::format("From {}@[{}]-{}：{}", label, where, m.user, m.text);
            if (!items.empty())
            {
                tagged += media::manifest(items);
                tagged += media::manifest_hint(items.size());
                tagged += "\n";
                bool has_audio = false;
                for (auto &it : items)
                    if (it.kind == "audio")
                        has_audio = true;
                if (has_audio && m.text.find("语音转写：") == std::string::npos)
                    tagged += "（语音可能要先转写：可以用 read 打开音频，或在设置里配置语音识别。）\n";
            }
            c->last_active = std::chrono::steady_clock::now();
            c->queue_turn(tagged, [this, chan, chat, group](const std::string &text, const std::string &err)
                          {
                im_out job;
                job.chat = chat;
                job.group = group;
                job.text = text;
                // 模型只想不说（思考型模型会这样）时也不能让人干等：给一句兜底
                if (job.text.empty())
                    job.text = err.empty() ? "（她走神了一会儿，你再说一次？）"
                                           : std::format("（出错了：{}）", err);
                im_enqueue_out(chan, std::move(job)); },
                          chan, chat, std::move(items), group);
        }

        // msg 工具的执行端（cell.cpp 的四参 outbound_hook）。
        // 空 channel / chat 表示「发回你说话的会话」；media 是 [{"kind":"sticker","name":"happy"}]。
        std::string im_send_tool(const std::string &chan, const std::string &chat,
                                 const std::string &text, const nlohmann::json &media)
        {
            if (shutting_down.load())
                return "应用正在退出";
            const std::string id = chan;
            const std::string target = chat;
            if (id.empty())
                return "没有指定渠道，而且这个回合不是从 IM 来的（channel 需要显式给出）";
            if (target.empty())
                return "没有指定会话 id（chat）";
            im::channel ch;
            std::string pid;
            {
                std::lock_guard<std::mutex> lk(cmd_mx);
                auto *c = im_channel(id);
                if (!c || !c->live())
                    return "渠道没在运行：" + id;
                ch = *c;
                pid = c->persona.empty() ? active : c->persona;
            }
            std::string body = strip_markers(text); // 工具发的是成品，别把标记漏出去
            std::vector<im::media_ref> out;
            if (media.is_array())
                for (auto &e : media)
                {
                    if (!e.is_object())
                        continue;
                    const std::string kind = e.value("kind", "sticker");
                    if (kind != "sticker")
                        return "暂不支持发送这种媒体：" + kind;
                    const std::string name = cell::text::trim(e.value("name", ""));
                    if (name.empty())
                        return "表情包名字是空的";
                    std::string path = media::sticker_path(pid, name);
                    if (path.empty())
                        return "没有这个表情包：" + name;
                    im::media_ref r;
                    r.kind = "sticker";
                    r.name = name;
                    r.local = path;
                    r.mime = media::mime_of(path);
                    out.push_back(std::move(r));
                }
            if (body.empty() && out.empty())
                return "正文和表情包都是空的";
            auto &log = cell::sys::logger::instance();
            std::string last_err;
            if (!body.empty())
            {
                std::string err;
                if (!im::send(ch, target, false, body, err))
                {
                    im_error(id, err);
                    log.warn("im", std::format("tool msg failed chan={} chat={} err={}", id, target, err));
                    last_err = err;
                }
                else
                    im_count(id, false);
            }
            for (auto &r : out)
            {
                std::string err;
                if (!im::send_media(ch, target, false, r.kind, r.local, r.name, r.mime, err))
                {
                    im_error(id, err);
                    log.warn("im", std::format("tool sticker failed chan={} chat={} name={} err={}",
                                               id, target, r.name, err));
                    last_err = err;
                }
                else
                    im_count(id, false);
            }
            if (!last_err.empty())
                return last_err;
            log.info("im", std::format("tool msg chan={} chat={} chars={} media={}",
                                       id, target, body.size(), out.size()));
            return "";
        }
        // 平台回调：wecom 靠签名，onebot / bridge 靠 token（没 token 只认本机）
        std::pair<int, std::string> im_webhook(const std::string &id, const im::request &rq)
        {
            auto &log = cell::sys::logger::instance();
            im::channel ch;
            {
                std::lock_guard<std::mutex> lk(cmd_mx);
                auto *found = im_channel(id);
                if (!found || !found->live())
                    return {404, "unknown channel"};
                ch = *found;
            }
            if (ch.kind == "wecom")
            {
                std::string err;
                if (rq.method == "GET")
                {
                    std::string echo;
                    if (!im::wecom_verify_url(ch, rq, echo, err))
                    {
                        log.warn("im", std::format("wecom verify refused chan={} err={}", ch.id, err));
                        return {403, err};
                    }
                    return {200, echo};
                }
                std::string plain;
                if (!im::wecom_take_message(ch, rq, plain, err))
                {
                    log.warn("im", std::format("wecom callback refused chan={} err={}", ch.id, err));
                    return {200, ""}; // 回空串：平台当作已收到，不再重试
                }
                im::inbound m;
                if (im::parse_wecom(plain, ch, m))
                {
                    m.chan = ch.id;
                    im_materialize(ch, m, nullptr); // 下载 + 落盘：回调线程，不持 cmd_mx
                    im_push(std::move(m));
                }
                return {200, ""};
            }
            if (rq.method != "POST")
                return {405, "POST only"};
            if (ch.token.empty() && !rq.loopback)
                return {403, "这个渠道没填 token/secret，只接受本机回调"};
            if (!ch.token.empty())
            {
                std::string want = ch.kind == "bridge" ? ch.token.str() : ("Bearer " + ch.token.str());
                const std::string &got = ch.kind == "bridge" ? rq.secret : rq.auth;
                bool same = got.size() == want.size() &&
                            sodium_memcmp(got.data(), want.data(), want.size()) == 0;
                cell::encrypt::wipe(want);
                if (!same)
                    return {403, "凭据不对"};
            }
            if (ch.kind == "bridge")
            {
                // 桥把登录二维码推上来（扫码登录走这条路），不当消息处理
                auto j = nlohmann::json::parse(rq.body, nullptr, false);
                if (j.is_object() && j.contains("qr") && j["qr"].is_string())
                {
                    im::qr_code q;
                    q.image = im::detail::as_data_uri(j["qr"].get<std::string>());
                    q.state = j.value("state", std::string("wait"));
                    if (q.state == "waiting")
                        q.state = "wait"; // 桥可能用旧状态名
                    q.account = j.value("account", std::string());
                    q.hint = q.state == "online" ? "已登录" : "用手机扫码登录";
                    {
                        std::lock_guard<std::mutex> lk(im_st_mx);
                        im_qr_cache[ch.id] = q;
                    }
                    log.info("im", std::format("qr from bridge chan={} state={}", ch.id, q.state));
                    return {200, R"({"ok":true})"};
                }
            }
            im::inbound m;
            if (ch.kind == "onebot")
            {
                // OneBot 的 HTTP 上报不止消息：心跳、输入状态、撤回、戳一戳…
                // 不认识的事件也必须回 200 —— NapCat 把非 2xx 记成「快速操作失败」，
                // 会在对端日志里刷错误，还可能触发它重试。
                if (!im::parse_onebot(nlohmann::json::parse(rq.body, nullptr, false), m))
                {
                    // 心跳 / 输入状态 / 撤回 / 戳一戳 / 空消息…：认下来、回 200、不打扰
                    log.debug("im", std::format("onebot event ignored chan={} body={}", ch.id,
                                                rq.body.substr(0, 200)));
                    return {200, R"({"status":"ok","retcode":0})"};
                }
            }
            else if (!im::parse_bridge(rq.body, m))
                return {400, "没有看懂这条消息"};
            m.chan = ch.id;
            im_materialize(ch, m, nullptr); // 下载 + 落盘：回调线程，不持 cmd_mx
            im_push(std::move(m));
            return {200, ch.kind == "onebot" ? R"({"status":"ok","retcode":0})" : R"({"ok":true})"};
        }
        // 微信 iLink：一次调用做两件事 —— 手上没有码就去要一张，有码就问登录状态。
        // 扫码成功时把 bot_token 写进加密库，然后重建该渠道的轮询线程。
        // 已经登录过就直接报 online：不然每次界面重建都会再要一张新码，
        // 「已登录」和二维码就会互相覆盖（这正是之前闪烁的来源）。
        nlohmann::json im_qr_wechat(im::channel ch, bool force = false)
        {
            auto pack = [](const im::qr_code &q)
            {
                nlohmann::json r = ok();
                r["state"] = q.state;
                r["qr"] = q.image;
                r["account"] = q.account;
                r["hint"] = q.hint;
                return r;
            };
            if (!ch.bot_token.empty() && !force)
            {
                im::qr_code q;
                q.state = "online";
                q.account = ch.bot_id;
                q.hint = "已经登录了，不用扫码（要换号点「重新扫码」）";
                return pack(q);
            }
            im::qr_code cur;
            bool cached = false;
            {
                std::lock_guard<std::mutex> lk(im_st_mx);
                auto it = im_qr_cache.find(ch.id);
                if (it != im_qr_cache.end())
                {
                    cur = it->second;
                    cached = true;
                }
            }
            const bool need_new = !cached || cur.token.empty() || cur.image.empty() || cur.state == "expired";
            if (need_new)
            {
                auto started = im::ilink_start_qr(ch);
                if (!started.ok)
                {
                    im::qr_code q;
                    q.state = "error";
                    q.hint = "取二维码失败：" + started.err;
                    return pack(q);
                }
                im::qr_code q;
                q.token = started.qrcode;
                q.image = im::qr_data_uri(started.url);
                q.state = "wait";
                q.hint = "用手机微信扫码，扫完在手机上确认";
                {
                    std::lock_guard<std::mutex> lk(im_st_mx);
                    im_qr_cache[ch.id] = q;
                }
                return pack(q);
            }
            auto st = im::ilink_poll_status(ch, cur.token);
            if (!st.err.empty())
            {
                cur.hint = "查登录状态失败：" + st.err;
                return pack(cur);
            }
            if (st.status == "expired")
            {
                cur.state = "expired";
                cur.image.clear();
                cur.token.clear();
                cur.hint = "二维码过期了，刷新一下";
                {
                    std::lock_guard<std::mutex> lk(im_st_mx);
                    im_qr_cache[ch.id] = cur;
                }
                return pack(cur);
            }
            if (st.status == "confirmed" && !st.bot_token.empty())
            {
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    if (auto *slot = im_channel(ch.id))
                    {
                        // 扫码回来的明文立刻进 secure_string，源串擦掉
                        im::detail::adopt(slot->bot_token, st.bot_token);
                        if (!st.base.empty())
                            slot->bot_base = st.base;
                        slot->bot_id = st.bot_id;
                        slot->bot_user = st.user_id;
                        std::string blob = slot->creds_json().dump();
                        bool stored = vault.set(app_settings::im_key(ch.id), blob);
                        cell::encrypt::wipe(blob);
                        if (!stored)
                            cell::sys::logger::instance().warn("im", "bot_token 写不进加密库");
                        ac.save();
                    }
                }
                {
                    std::lock_guard<std::mutex> lk(im_st_mx);
                    im_qr_cache.erase(ch.id);
                }
                im_schedule_reload(); // 拿到 token，轮询线程该起来了
                im::qr_code q;
                q.state = "online";
                q.account = st.bot_id;
                q.hint = "已登录，可以开始聊了";
                return pack(q);
            }
            cur.state = st.status == "scaned" ? "scaned" : "wait";
            cur.hint = cur.state == "scaned" ? "扫到了，等你在手机上点确认" : "用手机微信扫码";
            {
                std::lock_guard<std::mutex> lk(im_st_mx);
                im_qr_cache[ch.id] = cur;
            }
            return pack(cur);
        }

        // 扫码登录：二维码在外部客户端手里（NapCat 的 get_qrcode / 桥推上来的那张），
        // 这里只取、只报状态。OneBot 是现取，bridge 是读缓存（桥用 POST 推上来）。
        nlohmann::json im_qr(const std::string &id, bool refresh = false)
        {
            if (refresh) // 「刷新二维码」：丢掉手上的那张，重新要一张
            {
                std::lock_guard<std::mutex> lk(im_st_mx);
                im_qr_cache.erase(id);
            }
            im::channel ch;
            {
                std::lock_guard<std::mutex> lk(cmd_mx);
                auto *found = im_channel(id);
                if (!found)
                    return fail("没有这个渠道：" + id);
                ch = *found;
            }
            im::qr_code q;
            if (ch.kind == "onebot")
            {
                q = im::fetch_qr_onebot(ch); // 网络调用，不持命令锁
            }
            else if (ch.kind == "wechat")
            {
                return im_qr_wechat(ch, refresh);
            }
            else if (ch.kind == "bridge")
            {
                std::lock_guard<std::mutex> lk(im_st_mx);
                auto it = im_qr_cache.find(id);
                if (it != im_qr_cache.end())
                    q = it->second;
                else
                {
                    q.state = "wait";
                    q.hint = "等桥把二维码推上来：POST /api/im/" + id + " 里带 {\"qr\": \"<base64>\"}";
                }
            }
            else if (ch.kind == "wecom")
            {
                q.state = "unsupported";
                q.hint = "企业微信自建应用不用扫码：填 corpid / AgentId / Secret 就能收发";
            }
            else if (ch.kind == "telegram")
            {
                q.state = "unsupported";
                q.hint = "Telegram 用 BotFather 的 token，没有扫码这一步";
            }
            else
            {
                q.state = "unsupported";
                q.hint = "这个渠道不支持扫码";
            }
            nlohmann::json r = ok();
            r["state"] = q.state;
            r["qr"] = q.image;
            r["account"] = q.account;
            r["hint"] = q.hint;
            return r;
        }
        nlohmann::json im_test(const std::string &id)
        {
            im::channel ch;
            {
                std::lock_guard<std::mutex> lk(cmd_mx);
                auto *found = im_channel(id);
                if (!found)
                    return fail("没有这个渠道：" + id);
                ch = *found;
            }
            nlohmann::json info;
            std::string err;
            if (!im::probe(ch, info, err))
            {
                im_error(ch.id, err);
                return fail(err);
            }
            {
                std::lock_guard<std::mutex> lk(im_st_mx);
                auto &st = im_st[ch.id];
                st.state = "running";
                st.err.clear();
            }
            nlohmann::json r = ok();
            r["info"] = info;
            return r;
        }

        // -----------------------------------------------------------------
        //  主动关怀
        // -----------------------------------------------------------------
        void tick_loop()
        {
            while (true)
            {
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    if (ticker_quit)
                        return;
                }
                std::this_thread::sleep_for(std::chrono::seconds(20));
                std::vector<persona_ctx *> snapshot;
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    if (ticker_quit || shutting_down.load())
                        return;
                    for (auto &[id, c] : ctxs)
                        snapshot.push_back(c.get());
                }
                auto now = std::chrono::steady_clock::now();
                for (auto *c : snapshot)
                {
                    if (!c->p.proactive || c->busy.load() || shutting_down.load())
                        continue;
                    int minutes = c->p.idle_minutes > 0 ? c->p.idle_minutes : ac.idle_minutes;
                    auto idle = std::chrono::minutes(std::max(1, minutes));
                    if (now - c->last_active < idle)
                        continue;
                    if (now - c->last_proactive < idle)
                        continue;
                    c->last_proactive = now;
                    cell::sys::logger::instance().info("care", std::format("proactive persona={}", c->p.id));
                    c->queue_turn("[[idle]]");
                }
            }
        }

        // -----------------------------------------------------------------
        //  状态快照
        // -----------------------------------------------------------------
        nlohmann::json personas_json()
        {
            nlohmann::json arr = nlohmann::json::array();
            for (auto &id : persona_ids())
            {
                persona p = ctxs.count(id) ? ctxs[id]->p : load_persona(id);
                arr.push_back({{"id", p.id}, {"name", p.name}, {"subtitle", p.subtitle},
                               {"accent", p.accent}, {"avatar", p.avatar},
                               {"avatar_url", p.avatar.empty() ? "" : "/api/avatar/" + p.id},
                               {"active", p.id == active}});
            }
            return arr;
        }
        // 舞台资源盘点：运行时脚本齐不齐、models/ 下面有哪些模型
        nlohmann::json live2d_scan()
        {
            std::error_code ec;
            fs::create_directories(live2d_runtime_dir(), ec);
            fs::create_directories(live2d_models_dir(), ec);

            nlohmann::json r;
            r["root"] = live2d_dir().string();
            r["models_dir"] = live2d_models_dir().string();
            r["runtime_dir"] = live2d_runtime_dir().string();

            nlohmann::json scripts = nlohmann::json::array();
            nlohmann::json missing = nlohmann::json::array();
            for (auto &s : live2d_runtime_scripts())
            {
                bool ok = fs::is_regular_file(live2d_runtime_dir() / s, ec);
                scripts.push_back({{"name", s}, {"ok", ok}});
                if (!ok)
                    missing.push_back(s);
            }
            r["runtime"] = {{"scripts", scripts}, {"missing", missing}, {"ok", missing.empty()}};

            nlohmann::json models = nlohmann::json::array();
            if (fs::is_directory(live2d_models_dir(), ec))
            {
                fs::recursive_directory_iterator it(live2d_models_dir(), fs::directory_options::skip_permission_denied, ec);
                fs::recursive_directory_iterator end;
                int n = 0;
                for (; !ec && it != end && n < 200; it.increment(ec))
                {
                    if (it.depth() >= 4)
                        it.disable_recursion_pending();
                    if (!it->is_regular_file(ec))
                        continue;
                    std::string file = it->path().filename().string();
                    std::string suffix;
                    if (file.size() > 12 && file.ends_with(".model3.json"))
                        suffix = ".model3.json";
                    else if (file.size() > 11 && file.ends_with(".model.json"))
                        suffix = ".model.json";
                    if (suffix.empty())
                        continue;
                    models.push_back({{"name", file.substr(0, file.size() - suffix.size())},
                                      {"path", it->path().lexically_relative(live2d_dir()).generic_string()},
                                      {"dir", it->path().parent_path().lexically_relative(live2d_dir()).generic_string()}});
                    ++n;
                }
            }
            r["models"] = models;
            return r;
        }
        nlohmann::json providers_json() const
        {
            nlohmann::json arr = nlohmann::json::array();
            for (size_t i = 0; i < cfg.providers.size(); i++)
            {
                auto &p = cfg.providers[i];
                bool cur = (p.name == cfg.current_provider) || (cfg.current_provider.empty() && i == 0);
                arr.push_back({{"name", p.name}, {"api_style", p.api_style}, {"base", p.base},
                               {"proxy", p.proxy}, {"has_key", !p.key_id.empty() && vault.has(p.key_id)},
                               {"current", cur}});
            }
            return arr;
        }
        // 清单 → 页面 attachments[]（契约 2.3 / 4.3：url 的规则只在这里实现一遍）。
        // 普通附件：path 在 uploads/<pid>/ 下才给 url；sticker 用 /api/stickers/<pid>/<encoded>。
        static nlohmann::json attachments_json(const std::string &pid, const std::vector<media::item> &items)
        {
            nlohmann::json arr = nlohmann::json::array();
            for (auto &it : items)
            {
                if (it.kind == "sticker")
                {
                    std::string name = it.name;
                    if (name.empty() && !it.path.empty())
                        name = fs::path(it.path).stem().string();
                    if (name.empty())
                        continue;
                    arr.push_back({{"kind", "sticker"}, {"name", name},
                                   {"url", media::sticker_url(pid, name)}});
                    continue;
                }
                nlohmann::json row = {{"kind", it.kind.empty() ? "file" : it.kind},
                                      {"name", it.name}, {"size", it.size}, {"path", it.path}};
                std::string url = it.url.empty() ? media::url_for_path(it.path) : it.url;
                if (!url.empty())
                    row["url"] = std::move(url);
                arr.push_back(std::move(row));
            }
            return arr;
        }
        nlohmann::json history_json(persona_ctx &c, size_t cap = 60)
        {
            nlohmann::json out = nlohmann::json::array();
            std::vector<nlohmann::json> rows;
            for (auto &m : c.h.now().msg())
            {
                if (!m.is_object())
                    continue;
                std::string role = m.value("role", "");
                if (role == "system" || role == "tool")
                    continue;
                auto content = m.find("content");
                if (content == m.end() || content->is_null())
                    continue;
                const std::string kind = role == "assistant" ? "assistant" : "user";
                auto add_row = [&](std::string text)
                {
                    if (role == "assistant")
                        text = strip_markers(text);
                    auto items = media::parse_manifest(text);
                    text = media::strip_manifest(text);
                    nlohmann::json atts = attachments_json(c.p.id, items);
                    if (text.empty() && atts.empty())
                        return;
                    nlohmann::json row = {{"role", role}, {"kind", kind}, {"text", text}};
                    if (!atts.empty())
                        row["attachments"] = std::move(atts);
                    rows.push_back(std::move(row));
                };
                if (content->is_string())
                {
                    std::string text = content->get<std::string>();
                    if (role == "user" && text == "[[idle]]")
                        continue;
                    add_row(std::move(text));
                }
                else if (content->is_array())
                {
                    for (auto &b : *content)
                    {
                        if (!b.is_object())
                            continue;
                        std::string bt = b.value("type", "");
                        if (bt == "text")
                            add_row(b.value("text", std::string()));
                        else if (bt == "reasoning" || bt == "thinking")
                        {
                            std::string t = b.value(bt, std::string());
                            if (!t.empty())
                                rows.push_back({{"role", role}, {"kind", "reasoning"}, {"text", std::move(t)}});
                        }
                    }
                }
            }
            size_t start = rows.size() > cap ? rows.size() - cap : 0;
            for (size_t i = start; i < rows.size(); i++)
                out.push_back(rows[i]);
            return out;
        }
        nlohmann::json state_for(persona_ctx *c)
        {
            nlohmann::json st;
            st["t"] = "hello";
            st["personas"] = personas_json();
            st["active"] = c ? c->p.id : "";
            st["model"] = cfg.model_label();
            st["provider"] = cfg.providers.empty() ? "" : (cfg.current_provider.empty() ? cfg.providers[0].name : cfg.current_provider);
            st["providers"] = providers_json();
            st["thinking"] = cfg.think_level;
            st["voice_reply"] = ac.voice_reply;
            st["asr_auto_send"] = ac.asr_auto_send;
            st["tts"] = ac.tts.to_json();
            st["asr"] = ac.asr.to_json();
            st["voice"] = {{"tts", ac.tts.enabled()}, {"asr", ac.asr.enabled()}, {"recording", mic.active()}};
            st["im"] = im_json();
            // 页面需要的能力快照（契约 2.5）
            st["attach"] = {{"max_files", (long long)media::kMaxFiles},
                            {"max_bytes", (long long)media::kMaxUpload}};
            st["autoallow"] = ac.autoallow;
            st["compact_auto"] = ac.compact_auto;
            st["tools"] = c ? c->pcfg.tools : cfg.tools;
            {
                nlohmann::json stickers = nlohmann::json::array();
                if (c)
                    for (auto &name : media::sticker_names(c->p.id))
                        stickers.push_back({{"name", name},
                                            {"url", media::sticker_url(c->p.id, name)}});
                st["stickers"] = std::move(stickers);
            }
            if (!c)
            {
                st["session"] = "";
                st["history"] = nlohmann::json::array();
                st["persona"] = nlohmann::json::object();
                st["memory"] = nlohmann::json::array();
                return st;
            }
            st["persona"] = c->p.to_json();
            st["session"] = c->h.now().id();
            st["cwd"] = c->cwd;
            {
                std::lock_guard<std::mutex> lk(c->hmx);
                st["busy"] = c->busy.load();
                st["history"] = c->busy.load() ? nlohmann::json::array() : history_json(*c);
            }
            auto mem = memory_load(c->p.id, 200);
            nlohmann::json arr = nlohmann::json::array();
            for (auto &e : mem)
                arr.push_back({{"t", e.t}, {"text", e.text}});
            st["memory"] = std::move(arr);
            st["emotion"] = c->emotion;
            return st;
        }
        nlohmann::json build_state()
        {
            std::lock_guard<std::mutex> lk(cmd_mx);
            return state_for(find_ctx(active));
        }
        // 命令总线里已经持有 cmd_mx：广播不能再加锁
        void post_state_locked(bool rebuild)
        {
            nlohmann::json st = state_for(find_ctx(active));
            if (rebuild)
                st["rebuild"] = true;
            bus.post(st);
        }
        void broadcast_state()
        {
            std::lock_guard<std::mutex> lk(cmd_mx);
            post_state_locked(true);
        }
        void sync_settings_to_ctxs()
        {
            for (auto &[id, c] : ctxs)
            {
                c->pcfg.providers = cfg.providers;
                c->pcfg.current_provider = cfg.current_provider;
                c->pcfg.current_model = cfg.current_model;
                c->pcfg.think_level = cfg.think_level;
                c->pcfg.autoallow = cfg.autoallow;
                c->pcfg.sandbox_mode = cfg.sandbox_mode;
                c->pcfg.compact_auto = cfg.compact_auto;
                c->pcfg.compact_provider = cfg.compact_provider;
                c->pcfg.compact_model = cfg.compact_model;
            }
        }

        // -----------------------------------------------------------------
        //  命令总线
        // -----------------------------------------------------------------
        static nlohmann::json ok() { return {{"ok", true}}; }
        static nlohmann::json fail(const std::string &e) { return {{"ok", false}, {"error", e}}; }

        bool config_blocked(std::string &why)
        {
            for (auto &[id, c] : ctxs)
                if (c->busy.load())
                {
                    why = "有回合正在运行，等它结束后再改模型设置";
                    return true;
                }
            return false;
        }

        nlohmann::json exec_command(const std::string &name, const nlohmann::json &args_in)
        {
            nlohmann::json args = args_in.is_object() ? args_in : nlohmann::json::object();
            for (auto it = args.begin(); it != args.end();)
                it = it->is_null() ? args.erase(it) : std::next(it);
            try
            {
                if (shutting_down.load())
                    return fail("应用正在退出");
                if (on_command)
                    on_command(name);
                // ---- 状态 --------------------------------------------------
                if (name == "state")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    return ok_with(state_for(find_ctx(active)));
                }
                // ---- 角色 --------------------------------------------------
                if (name == "personas.list")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    nlohmann::json r = ok();
                    r["personas"] = personas_json();
                    r["active"] = active;
                    return r;
                }
                if (name == "persona.select")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    std::string id = args.value("id", "");
                    std::string err;
                    if (!open_named(id, &err))
                        return fail(err);
                    ac.save();
                    nlohmann::json r = ok();
                    r["state"] = state_for(find_ctx(active));
                    post_state_locked(true);
                    return r;
                }
                if (name == "persona.save")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    std::string id = args.value("id", "");
                    if (id.empty())
                    {
                        id = "p" + std::to_string((long long)std::time(nullptr));
                    }
                    if (!persona_id_ok(id))
                        return fail("角色 id 只能用字母、数字、- 和 _");
                    persona p = fs::is_directory(persona_dir(id)) ? load_persona(id) : persona{};
                    p.id = id;
                    nlohmann::json patch = args.contains("patch") && args["patch"].is_object() ? args["patch"] : args;
                    auto s = [&](const char *k, std::string &dst)
                    {
                        auto it = patch.find(k);
                        if (it != patch.end() && it->is_string())
                            dst = it->get<std::string>();
                    };
                    s("name", p.name);
                    s("subtitle", p.subtitle);
                    s("accent", p.accent);
                    s("greeting", p.greeting);
                    s("prompt", p.prompt);
                    s("user_name", p.user_name);
                    s("voice", p.voice);
                    if (patch.contains("live2d") && patch["live2d"].is_object())
                    {
                        if (patch["live2d"].dump().size() > 96 * 1024)
                            return fail("Live2D 配置太大（上限 96KB）");
                        p.live2d = patch["live2d"];
                    }
                    if (patch.contains("triggers") && patch["triggers"].is_array())
                    {
                        if (patch["triggers"].dump().size() > 256 * 1024)
                            return fail("触发器太多或太大（上限 256KB）");
                        p.triggers = patch["triggers"];
                    }
                    if (patch.contains("proactive") && patch["proactive"].is_boolean())
                        p.proactive = patch["proactive"].get<bool>();
                    if (patch.contains("idle_minutes") && patch["idle_minutes"].is_number_integer())
                        p.idle_minutes = std::clamp<int>((int)patch["idle_minutes"].get<long long>(), 1, 1440);
                    if (p.name.empty())
                        p.name = id;
                    std::string err;
                    if (!save_persona(p, &err))
                        return fail(err);
                    if (auto *c = find_ctx(id))
                    {
                        const bool stage_changed =
                            c->p.live2d.dump() != p.live2d.dump();
                        c->p = p;
                        c->h.now().note_rewrite();
                        // 动作组 / 表情名变了（换模型、页面报回新清单）：act 的参数表
                        // 得跟着变，下一回合开头重建工具表
                        if (stage_changed && c->eng)
                            c->eng->mark_tools_dirty();
                    }
                    post_state_locked(true);
                    nlohmann::json r = ok();
                    r["id"] = id;
                    return r;
                }
                if (name == "persona.delete")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    std::string id = args.value("id", "");
                    if (id.empty() || !persona_id_ok(id))
                        return fail("无效的角色 id");
                    if (persona_ids().size() <= 1)
                        return fail("至少要留一个角色");
                    if (auto *c = find_ctx(id))
                    {
                        c->stop.store(true);
                        {
                            std::lock_guard<std::mutex> lk2(c->qmx);
                            c->quit = true;
                        }
                        c->qcv.notify_all();
                        if (c->worker.joinable())
                            c->worker.join();
                        c->h.now().unload();
                        ctxs.erase(id);
                    }
                    std::error_code ec;
                    fs::remove_all(persona_dir(id), ec);
                    ac.sessions.erase(id);
                    if (active == id)
                    {
                        active.clear();
                        auto all = persona_ids();
                        if (!all.empty())
                            open_named(all.front(), nullptr);
                    }
                    ac.save();
                    post_state_locked(true);
                    return ok();
                }
                if (name == "persona.avatar")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    std::string id = args.value("id", active);
                    if (!persona_id_ok(id) || !fs::is_directory(persona_dir(id)))
                        return fail("角色不存在");
                    std::string b64 = args.value("data_b64", "");
                    std::string ext = cell::text::trim(args.value("ext", "png"));
                    if (b64.empty())
                        return fail("空图片");
                    if (ext != "png" && ext != "jpg" && ext != "jpeg" && ext != "webp" && ext != "gif")
                        ext = "png";
                    std::string raw = cell::encrypt::b64_decode(b64);
                    if (raw.size() < 64 || raw.size() > 8u * 1024 * 1024)
                        return fail("图片大小不合适（64B ~ 8MB）");
                    std::error_code ec;
                    std::vector<fs::path> old_avatars;
                    for (auto &e : fs::directory_iterator(persona_dir(id), ec))
                        if (e.is_regular_file(ec) && e.path().stem() == "avatar")
                            old_avatars.push_back(e.path());
                    for (auto &old : old_avatars)
                        fs::remove(old, ec);
                    std::string fname = "avatar." + ext;
                    std::ofstream f(persona_dir(id) / fname, std::ios::binary | std::ios::trunc);
                    if (!f)
                        return fail("无法写入头像");
                    f.write(raw.data(), (std::streamsize)raw.size());
                    f.close();
                    persona p = fs::is_directory(persona_dir(id)) ? load_persona(id) : persona{};
                    p.id = id;
                    p.avatar = fname;
                    save_persona(p);
                    if (auto *c = find_ctx(id))
                        c->p = p;
                    post_state_locked(true);
                    return ok();
                }

                // ---- 舞台资源 ----------------------------------------------
                if (name == "live2d.scan")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    return ok_with(live2d_scan());
                }
                if (name == "live2d.fetch_runtime")
                {
                    // Cubism Core 这类运行时核心不随 exe 分发，由用户点一下取回本地。
                    // 已经存在的文件不重下（除非 force）。
                    const bool force = args.value("force", false);
                    std::error_code ec;
                    fs::create_directories(live2d_runtime_dir(), ec);
                    nlohmann::json got = nlohmann::json::array();
                    nlohmann::json kept = nlohmann::json::array();
                    for (auto &[file, url] : live2d_runtime_sources(args.value("base", "")))
                    {
                        fs::path out = live2d_runtime_dir() / file;
                        if (!force && fs::is_regular_file(out, ec) && fs::file_size(out, ec) > 1024)
                        {
                            kept.push_back(file);
                            continue;
                        }
                        std::string body, err;
                        if (!voice::fetch(url, body, err))
                            return fail(std::format("{} 下载失败：{}", file, err));
                        std::ofstream f(out, std::ios::binary | std::ios::trunc);
                        if (!f)
                            return fail("无法写入 " + out.string());
                        f.write(body.data(), (std::streamsize)body.size());
                        if (!f)
                            return fail("写入中断：" + file);
                        got.push_back(file);
                    }
                    nlohmann::json r = ok();
                    r["downloaded"] = got;
                    r["kept"] = kept;
                    r["scan"] = live2d_scan();
                    return r;
                }
                if (name == "live2d.open_dir")
                {
                    std::error_code ec;
                    fs::create_directories(live2d_models_dir(), ec);
                    fs::create_directories(live2d_runtime_dir(), ec);
                    std::string cmd = "explorer.exe \"" + live2d_dir().string() + "\"";
#ifdef __APPLE__
                    cmd = "open \"" + live2d_dir().string() + "\"";
#elif !defined(_WIN32)
                    cmd = "xdg-open \"" + live2d_dir().string() + "\"";
#endif
                    std::string out, err;
                    int code = -1;
                    cell::plat::spawn_cmd(cmd, 10, out, code, err);
                    return ok();
                }

                // ---- 对话 --------------------------------------------------
                if (name == "send")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    persona_ctx *c = find_ctx(active);
                    if (!c)
                        return fail("还没有打开角色");
                    std::string text = args.value("text", "");
                    if (args.contains("attachments") && !args["attachments"].is_array() &&
                        !args["attachments"].is_null())
                        return fail("attachments 必须是数组");
                    const nlohmann::json empty_atts = nlohmann::json::array();
                    const nlohmann::json &atts = (args.contains("attachments") && args["attachments"].is_array())
                                                     ? args["attachments"]
                                                     : empty_atts;
                    if (cell::text::trim(text).empty() && atts.empty())
                        return fail("空消息");
                    if (atts.size() > media::kMaxFiles)
                        return fail(std::format("最多只能带 {} 个附件", media::kMaxFiles));
                    // base64 → 落盘：.agent/uploads/<角色>/
                    std::vector<media::item> items;
                    items.reserve(atts.size());
                    for (size_t i = 0; i < atts.size(); i++)
                    {
                        const auto &e = atts[i];
                        const std::string who = std::format("第 {} 个附件", i + 1);
                        if (!e.is_object())
                            return fail(who + "格式不对");
                        std::string name = e.value("name", "");
                        std::string mime = e.value("mime", "");
                        std::string b64 = e.value("data_b64", "");
                        if (b64.empty())
                            return fail(who + "是空的");
                        // 先按 base64 长度粗筛，免得把 100MB 的串解出来再拒绝
                        if (b64.size() > (media::kMaxUpload * 4 / 3) + 16)
                            return fail(std::format("{}太大（上限 {}）", who,
                                                    media::human_size((long long)media::kMaxUpload)));
                        std::string bytes = cell::encrypt::b64_decode(b64);
                        cell::encrypt::wipe(b64);
                        if (bytes.empty())
                            return fail(who + "解码失败（data_b64 不是有效的 base64）");
                        if (bytes.size() > media::kMaxUpload)
                            return fail(std::format("{}太大（{}，上限 {}）", who,
                                                    media::human_size((long long)bytes.size()),
                                                    media::human_size((long long)media::kMaxUpload)));
                        media::item it;
                        std::string err;
                        if (!media::save(c->p.id, name, mime, bytes, it, err))
                            return fail(who + "保存失败：" + err);
                        items.push_back(std::move(it));
                    }
                    std::string body = text;
                    if (!items.empty())
                    {
                        // 正文 + 清单 + 一句「附件要自己 read」的提示（历史展示时会剥掉）
                        body += media::manifest(items);
                        body += media::manifest_hint(items.size());
                        body += "\n";
                    }
                    c->last_active = std::chrono::steady_clock::now();
                    const size_t n_atts = items.size();
                    c->queue_turn(std::move(body), {}, {}, {}, std::move(items));
                    nlohmann::json r = ok();
                    r["attachments"] = (long long)n_atts;
                    return r;
                }
                if (name == "stop")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    if (auto *c = find_ctx(active))
                    {
                        c->stop.store(true);
                        stop_voice(*c);
                    }
                    return ok();
                }

                // ---- 会话 --------------------------------------------------
                if (name == "session.new")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    persona_ctx *c = find_ctx(active);
                    if (!c)
                        return fail("还没有打开角色");
                    if (c->busy.load())
                        return fail("回合进行中，等它结束");
                    reset_session(*c);
                    post_state_locked(true);
                    return ok();
                }
                // 清空上下文：remember=true 时先让模型把这段对话整理成几条长期记忆，
                // 再清空（整理失败就不清，别让用户以为记住了其实没有）
                if (name == "context.clear")
                {
                    std::unique_lock<std::mutex> lk(cmd_mx);
                    persona_ctx *c = find_ctx(active);
                    if (!c)
                        return fail("还没有打开角色");
                    if (c->busy.load())
                        return fail("她还在回应，等这句说完再清空");
                    const std::string pid = c->p.id;
                    const bool remember = args.value("remember", false);
                    size_t remembered = 0;
                    bool had_text = false;
                    if (remember)
                    {
                        std::string transcript = session_transcript(*c);
                        had_text = !transcript.empty();
                        if (had_text)
                        {
                            mem_task task = memory_task_snapshot(pid); // 持锁取配置
                            lk.unlock();                               // 网络请求不占命令锁
                            std::string err;
                            auto facts = extract_memories(task, transcript, err);
                            lk.lock();
                            c = find_ctx(pid);
                            if (!c)
                                return fail("角色已经不在了");
                            if (!err.empty() && facts.empty())
                                return fail("没能整理出记忆：" + err + "（上下文没有清空）");
                            auto existing = memory_load(pid, 4000);
                            for (auto &f : facts)
                            {
                                bool dup = false;
                                for (auto &e : existing)
                                    if (memory_too_similar(f, e.text))
                                    {
                                        dup = true;
                                        break;
                                    }
                                if (dup)
                                    continue;
                                if (memory_add(pid, f))
                                {
                                    remembered++;
                                    existing.push_back({(long long)std::time(nullptr), f});
                                }
                            }
                        }
                    }
                    stop_voice(*c);
                    reset_session(*c);
                    post_state_locked(true);
                    nlohmann::json r = ok();
                    r["remembered"] = (long long)remembered;
                    r["had_text"] = had_text;
                    return r;
                }
                // 手动压缩上下文：网络请求期间不攥着 cmd_mx（先取 pid/eng，放锁再跑）
                if (name == "context.compact")
                {
                    std::string pid;
                    cell::engine::runner *eng = nullptr;
                    {
                        std::lock_guard<std::mutex> lk(cmd_mx);
                        persona_ctx *c = find_ctx(active);
                        if (!c)
                            return fail("还没有打开角色");
                        if (c->busy.load())
                            return fail("她还在回应，等这句说完再压缩");
                        pid = c->p.id;
                        eng = c->eng.get();
                    }
                    cell::engine::events ev;
                    ev.on_status = [this, pid](std::string_view d)
                    { bus.post({{"t", "status"}, {"persona", pid}, {"d", std::string(d)}}); };
                    ev.on_summary = [this, pid](std::string_view d)
                    { bus.post({{"t", "status"}, {"persona", pid}, {"d", std::string(d)}}); };
                    ev.cancelled = [this]()
                    { return shutting_down.load(); };
                    std::string result;
                    try
                    {
                        result = eng->compact_now(ev);
                    }
                    catch (const std::exception &e)
                    {
                        return fail(std::string("压缩失败：") + e.what());
                    }
                    {
                        std::lock_guard<std::mutex> lk(cmd_mx);
                        if (!find_ctx(pid))
                            return fail("角色已经不在了");
                    }
                    nlohmann::json r = ok();
                    r["result"] = result;
                    return r;
                }
                if (name == "sessions.list")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    persona_ctx *c = find_ctx(active);
                    nlohmann::json arr = nlohmann::json::array();
                    if (c)
                    {
                        std::string key = cell::cwd_id_of(persona_dir(c->p.id));
                        std::error_code ec;
                        fs::path base = cell::root / "sessions" / key;
                        if (fs::is_directory(base, ec))
                            for (auto &e : fs::directory_iterator(base, ec))
                            {
                                if (ec || !e.is_directory(ec))
                                    continue;
                                std::string id = e.path().filename().string();
                                if (!cell::is_valid_session_id(id))
                                    continue;
                                std::error_code tec;
                                auto tf = e.path() / "messages.jsonl";
                                auto ft = fs::last_write_time(tf, tec);
                                long long mt = 0;
                                if (!tec)
                                    mt = (long long)std::chrono::duration_cast<std::chrono::seconds>(
                                             std::chrono::file_clock::to_sys(ft).time_since_epoch())
                                             .count();
                                arr.push_back({{"id", id}, {"mtime", mt}, {"current", id == c->h.now().id()}});
                            }
                    }
                    std::sort(arr.begin(), arr.end(), [](const nlohmann::json &a, const nlohmann::json &b)
                              { return a.value("mtime", 0LL) > b.value("mtime", 0LL); });
                    nlohmann::json r = ok();
                    r["sessions"] = std::move(arr);
                    return r;
                }
                if (name == "session.select")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    persona_ctx *c = find_ctx(active);
                    if (!c)
                        return fail("还没有打开角色");
                    if (c->busy.load())
                        return fail("回合进行中，等它结束");
                    std::string id = args.value("id", "");
                    if (!cell::is_valid_session_id(id))
                        return fail("无效的会话 id");
                    c->h.now().unload();
                    cell::async_io::flush();
                    c->h.use(id);
                    c->eng->ensure_prompt();
                    apply_prompt(*c);
                    ac.sessions[c->p.id] = id;
                    ac.save();
                    post_state_locked(true);
                    return ok();
                }
                if (name == "session.delete")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    persona_ctx *c = find_ctx(active);
                    if (!c)
                        return fail("还没有打开角色");
                    std::string id = args.value("id", "");
                    if (!cell::is_valid_session_id(id) || cell::session_prefix(id) != cell::cwd_id_of(persona_dir(c->p.id)))
                        return fail("无效的会话 id");
                    bool current = (id == c->h.now().id());
                    if (current)
                    {
                        if (c->busy.load())
                            return fail("回合进行中，等它结束");
                        c->h.now().unload();
                        c->h.forget_current();
                        cell::async_io::flush();
                    }
                    std::error_code ec;
                    fs::remove_all(cell::session_dir(id), ec);
                    if (ec)
                        return fail("删除失败：" + ec.message());
                    if (current)
                    {
                        c->eng->ensure_prompt();
                        apply_prompt(*c);
                        ac.sessions[c->p.id] = c->h.now().id();
                    }
                    ac.save();
                    post_state_locked(true);
                    return ok();
                }

                // ---- 记忆 --------------------------------------------------
                if (name == "memory.list")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    persona_ctx *c = find_ctx(active);
                    if (!c)
                        return fail("还没有打开角色");
                    auto mem = memory_load(c->p.id, 4000);
                    nlohmann::json arr = nlohmann::json::array();
                    for (auto &e : mem)
                        arr.push_back({{"t", e.t}, {"text", e.text}});
                    nlohmann::json r = ok();
                    r["memory"] = std::move(arr);
                    return r;
                }
                if (name == "memory.clear")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    persona_ctx *c = find_ctx(active);
                    if (!c)
                        return fail("还没有打开角色");
                    std::error_code ec;
                    fs::remove(memory_file(c->p.id), ec);
                    apply_prompt(*c);
                    post_state_locked(true);
                    return ok();
                }
                if (name == "memory.add")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    persona_ctx *c = find_ctx(active);
                    if (!c)
                        return fail("还没有打开角色");
                    std::string text = args.value("text", "");
                    if (!memory_add(c->p.id, text))
                        return fail("没有写入（空内容或重复）");
                    apply_prompt(*c);
                    post_state_locked(true);
                    return ok();
                }
                if (name == "memory.forget")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    persona_ctx *c = find_ctx(active);
                    if (!c)
                        return fail("还没有打开角色");
                    std::string text = args.value("text", "");
                    auto rows = memory_load(c->p.id, 4000);
                    std::error_code ec;
                    std::ofstream f(memory_file(c->p.id), std::ios::binary | std::ios::trunc);
                    for (auto &e : rows)
                    {
                        if (e.text == text)
                            continue;
                        f << nlohmann::json{{"t", e.t}, {"text", e.text}}.dump() << '\n';
                    }
                    f.close();
                    apply_prompt(*c);
                    post_state_locked(true);
                    return ok();
                }

                // ---- 语音 --------------------------------------------------
                if (name == "voice.record.start")
                {
                    std::string err;
                    if (!mic.start(err))
                        return fail(err);
                    bus.post({{"t", "voice_state"}, {"recording", true}});
                    nlohmann::json r = ok();
                    r["recording"] = true;
                    return r;
                }
                if (name == "voice.record.stop")
                {
                    std::string err;
                    long long stamp = std::chrono::duration_cast<std::chrono::seconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count();
                    fs::path out = mic_dir() / std::format("rec-{}.wav", stamp);
                    int seconds = 0;
                    if (!mic.stop(out, &seconds, err))
                        return fail(err);
                    bus.post({{"t", "voice_state"}, {"recording", false}});
                    nlohmann::json r = ok();
                    r["path"] = out.string();
                    r["seconds"] = seconds;
                    return r;
                }
                if (name == "voice.transcribe")
                {
                    fs::path file = args.value("path", "");
                    std::string b64 = args.value("data_b64", "");
                    std::string ext = cell::text::trim(args.value("ext", "wav"));
                    std::error_code ec;
                    if (!b64.empty())
                    {
                        std::string raw = cell::encrypt::b64_decode(b64);
                        if (raw.size() < 256)
                            return fail("音频太短");
                        if (ext != "wav" && ext != "webm" && ext != "ogg" && ext != "mp3" && ext != "m4a")
                            ext = "webm";
                        file = mic_dir() / std::format("clip-{}.{}", (long long)std::time(nullptr), ext);
                        std::ofstream f(file, std::ios::binary | std::ios::trunc);
                        if (!f)
                            return fail("无法写入临时音频");
                        f.write(raw.data(), (std::streamsize)raw.size());
                    }
                    if (file.empty() || !fs::is_regular_file(file, ec))
                        return fail("找不到录音文件");
                    std::string text, err;
                    if (!voice::asr(ac.asr, file, text, err))
                        return fail(err);
                    cell::sys::logger::instance().info("voice", std::format("asr ok chars={} file={}", text.size(), file.filename().string()));
                    bus.post({{"t", "asr"}, {"text", text}, {"auto_send", ac.asr_auto_send}});
                    nlohmann::json r = ok();
                    r["text"] = text;
                    r["auto_send"] = ac.asr_auto_send;
                    return r;
                }
                if (name == "voice.speak")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    persona_ctx *c = find_ctx(active);
                    if (!c)
                        return fail("还没有打开角色");
                    std::string text = cell::text::trim(args.value("text", ""));
                    if (text.empty())
                        return fail("空文本");
                    if (!ac.tts.enabled())
                        return fail("TTS 服务未启用");
                    {
                        std::lock_guard<std::mutex> lk2(tts_mx);
                        tts_q.push_back({c->p.id, text, c->p.voice, c->emotion, c->voice_gen.load()});
                    }
                    tts_cv.notify_one();
                    return ok();
                }
                if (name == "voice.stop")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    if (auto *c = find_ctx(active))
                        stop_voice(*c);
                    return ok();
                }
                if (name == "service.set")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    std::string which = args.value("name", "");
                    nlohmann::json patch = args.contains("patch") && args["patch"].is_object() ? args["patch"] : nlohmann::json::object();
                    if (which != "tts" && which != "asr")
                        return fail("未知服务：" + which);
                    auto cur = which == "tts" ? ac.tts : ac.asr;
                    nlohmann::json merged = cur.to_json();
                    // key 不落在 to_json 里（只在内存中），单独处理
                    for (auto &[k, v] : patch.items())
                        merged[k] = v;
                    auto next = voice::service::from_json(merged);
                    auto key_it = patch.find("key");
                    if (key_it != patch.end() && key_it->is_string())
                    {
                        next.key = key_it->get<std::string>();
                        vault.set(which == "tts" ? "service:tts" : "service:asr", next.key);
                    }
                    else
                        next.key = cur.key;
                    if (which == "tts")
                        ac.tts = next;
                    else
                        ac.asr = next;
                    ac.save();
                    post_state_locked(true);
                    return ok();
                }
                if (name == "service.test")
                {
                    std::string which = args.value("name", "");
                    if (which == "tts")
                    {
                        if (!ac.tts.enabled())
                            return fail("TTS 服务未启用");
                        long long n = ++voice_seq;
                        std::string ext = voice::ext_for(ac.tts.format);
                        std::string name_ = std::format("test-{}-{}.{}", (long long)std::time(nullptr), n, ext);
                        fs::path out = audio_dir() / name_;
                        std::string err;
                        if (!voice::tts(ac.tts, "你好，我在。", "", out, err))
                            return fail(err);
                        nlohmann::json r = ok();
                        r["url"] = "/api/audio/" + name_;
                        return r;
                    }
                    if (which == "asr")
                    {
                        auto clips = fs::directory_iterator(mic_dir());
                        fs::path latest;
                        std::error_code ec;
                        for (auto &e : clips)
                            if (e.is_regular_file(ec))
                                latest = e.path();
                        if (latest.empty())
                            return fail("还没有录音可以测试");
                        std::string text, err;
                        if (!voice::asr(ac.asr, latest, text, err))
                            return fail(err);
                        nlohmann::json r = ok();
                        r["text"] = text;
                        return r;
                    }
                    return fail("未知服务：" + which);
                }
                // ---- IM 接入 ----------------------------------------------
                if (name == "im.set")
                {
                    std::unique_lock<std::mutex> lk(cmd_mx);
                    std::string id = args.value("id", "");
                    if (!im::channel::id_ok(id))
                        return fail("渠道 id 只能用字母、数字、- 和 _（最长 32）");
                    nlohmann::json patch = args.contains("patch") && args["patch"].is_object()
                                               ? args["patch"]
                                               : nlohmann::json::object();
                    auto *cur = im_channel(id);
                    im::channel next = cur ? *cur : im::channel{};
                    next.id = id;
                    next.patch(patch);
                    // 凭据留空 = 保持原样（与 service.set 的 key 约定一致）
                    if (cur)
                    {
                        if (next.token.empty())
                            next.token = cur->token;
                        if (next.aes_key.empty())
                            next.aes_key = cur->aes_key;
                        if (next.corp_id.empty())
                            next.corp_id = cur->corp_id;
                        if (next.agent_id.empty())
                            next.agent_id = cur->agent_id;
                        if (next.corp_secret.empty())
                            next.corp_secret = cur->corp_secret;
                    }
                    // 凭据只在这一个作用域里拼成一次明文，写完立刻擦掉
                    std::string blob = next.creds_json().dump();
                    bool stored = vault.set(app_settings::im_key(id), blob);
                    cell::encrypt::wipe(blob);
                    if (!stored)
                        return fail("凭据写不进加密库");
                    if (cur)
                        *cur = next;
                    else
                        ac.imch.push_back(next);
                    ac.save();
                    post_state_locked(true);
                    nlohmann::json r = ok();
                    r["id"] = id;
                    r["webhook"] = next.webhook() ? "/api/im/" + id : "";
                    lk.unlock();
                    im_schedule_reload(); // 换线程是后台的事，命令立刻返回
                    return r;
                }
                if (name == "im.del")
                {
                    std::unique_lock<std::mutex> lk(cmd_mx);
                    std::string id = args.value("id", "");
                    std::erase_if(ac.imch, [&](const im::channel &c)
                                  { return c.id == id; });
                    vault.remove(app_settings::im_key(id));
                    {
                        std::lock_guard<std::mutex> lk2(im_st_mx);
                        im_st.erase(id);
                        im_qr_cache.erase(id);
                    }
                    im_offset.erase(id);
                    ac.save();
                    post_state_locked(true);
                    lk.unlock();
                    im_schedule_reload();
                    return ok();
                }
                if (name == "im.test")
                {
                    return im_test(args.value("id", ""));
                }
                if (name == "im.qr")
                {
                    return im_qr(args.value("id", ""), args.value("refresh", false));
                }
                if (name == "ui.pref")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    if (args.contains("voice_reply") && args["voice_reply"].is_boolean())
                        ac.voice_reply = args["voice_reply"].get<bool>();
                    if (args.contains("asr_auto_send") && args["asr_auto_send"].is_boolean())
                        ac.asr_auto_send = args["asr_auto_send"].get<bool>();
                    if (args.contains("idle_minutes") && args["idle_minutes"].is_number_integer())
                        ac.idle_minutes = std::clamp<int>((int)args["idle_minutes"].get<long long>(), 1, 1440);
                    // autoallow：工具不再逐条问，直接执行（同步到 cell 全局与每个角色）
                    if (args.contains("autoallow") && args["autoallow"].is_boolean())
                    {
                        ac.autoallow = args["autoallow"].get<bool>();
                        cell::box::autoallow_enabled() = ac.autoallow;
                        cfg.autoallow = ac.autoallow;
                        for (auto &[id, c] : ctxs)
                            c->pcfg.autoallow = ac.autoallow;
                    }
                    // compact_auto：长回合结束后自动压缩上下文
                    if (args.contains("compact_auto") && args["compact_auto"].is_boolean())
                    {
                        ac.compact_auto = args["compact_auto"].get<bool>();
                        cfg.compact_auto = ac.compact_auto;
                        for (auto &[id, c] : ctxs)
                            c->pcfg.compact_auto = ac.compact_auto;
                    }
                    ac.save();
                    nlohmann::json r = ok();
                    r["voice_reply"] = ac.voice_reply;
                    r["asr_auto_send"] = ac.asr_auto_send;
                    r["autoallow"] = ac.autoallow;
                    r["compact_auto"] = ac.compact_auto;
                    return r;
                }

                // ---- 模型供应商 --------------------------------------------
                if (name == "providers.add")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    std::string why;
                    if (config_blocked(why))
                        return fail(why);
                    return providers_add(args);
                }
                if (name == "providers.rm")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    std::string why;
                    if (config_blocked(why))
                        return fail(why);
                    std::string nm = args.value("name", "");
                    int idx = cell::config::find(cfg, nm);
                    if (idx < 0)
                        return fail("供应商不存在：" + nm);
                    std::string kid = cfg.providers[(size_t)idx].key_id;
                    if (!kid.empty())
                        vault.remove(kid);
                    cfg.providers.erase(cfg.providers.begin() + idx);
                    if (cfg.current_provider == nm)
                    {
                        cfg.current_provider = cfg.providers.empty() ? "" : cfg.providers[0].name;
                        cfg.current_model.clear();
                    }
                    cell::config::save(cfg, false);
                    sync_settings_to_ctxs();
                    post_state_locked(true);
                    return ok();
                }
                if (name == "providers.select")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    std::string why;
                    if (config_blocked(why))
                        return fail(why);
                    std::string nm = args.value("name", "");
                    if (cell::config::find(cfg, nm) < 0)
                        return fail("供应商不存在：" + nm);
                    cell::config::select_provider(cfg, nm);
                    cell::config::save(cfg, false);
                    sync_settings_to_ctxs();
                    post_state_locked(true);
                    return ok();
                }
                if (name == "provider.set_key")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    std::string why;
                    if (config_blocked(why))
                        return fail(why);
                    std::string nm = args.value("name", "");
                    std::string key = args.value("key", "");
                    int idx = cell::config::find(cfg, nm);
                    if (idx < 0)
                        return fail("供应商不存在：" + nm);
                    if (key.empty())
                        return fail("空 key");
                    auto &p = cfg.providers[(size_t)idx];
                    std::string kid = p.key_id.empty() ? "provider:" + p.name : p.key_id;
                    if (!vault.set(kid, key))
                        return fail("密钥保存失败");
                    p.key_id = kid;
                    cell::config::save(cfg, false);
                    sync_settings_to_ctxs();
                    post_state_locked(true);
                    return ok();
                }
                if (name == "models.fetch")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    return models_fetch();
                }
                if (name == "model.set")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    std::string why;
                    if (config_blocked(why))
                        return fail(why);
                    std::string m = args.value("model", "");
                    if (m.empty())
                        return fail("空模型名");
                    cfg.current_model = m;
                    cell::config::save(cfg, false);
                    sync_settings_to_ctxs();
                    post_state_locked(true);
                    return ok();
                }
                if (name == "think.set")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    std::string why;
                    if (config_blocked(why))
                        return fail(why);
                    int lv = (int)args.value("level", 0);
                    if (lv < 0 || lv > 4)
                        return fail("思考级别 0..4");
                    cfg.think_level = lv;
                    cell::config::save(cfg, false);
                    sync_settings_to_ctxs();
                    post_state_locked(true);
                    return ok();
                }

                // ---- 表情包 ------------------------------------------------
                if (name == "sticker.list")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    persona_ctx *c = find_ctx(active);
                    if (!c)
                        return fail("还没有打开角色");
                    nlohmann::json arr = nlohmann::json::array();
                    for (auto &n : media::sticker_names(c->p.id))
                        arr.push_back({{"name", n}, {"url", media::sticker_url(c->p.id, n)}});
                    nlohmann::json r = ok();
                    r["stickers"] = std::move(arr);
                    return r;
                }
                if (name == "sticker.add")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    persona_ctx *c = find_ctx(active);
                    if (!c)
                        return fail("还没有打开角色");
                    std::string b64 = args.value("data_b64", "");
                    if (b64.empty())
                        return fail("空图片");
                    std::string ext = media::lower(cell::text::trim(args.value("ext", "png")));
                    if (!media::is_sticker_ext(ext))
                        ext = "png";
                    std::string nm = cell::text::trim(args.value("name", ""));
                    if (nm.empty())
                        nm = std::format("sticker-{}", (long long)std::time(nullptr));
                    constexpr size_t kMaxSticker = 8ull * 1024 * 1024;
                    if (b64.size() > (kMaxSticker * 4 / 3) + 16)
                        return fail(std::format("表情包太大（上限 {}）", media::human_size((long long)kMaxSticker)));
                    std::string raw = cell::encrypt::b64_decode(b64);
                    cell::encrypt::wipe(b64);
                    if (raw.empty())
                        return fail("图片解码失败（data_b64 不是有效的 base64）");
                    if (raw.size() > kMaxSticker)
                        return fail(std::format("表情包太大（{}，上限 {}）",
                                                media::human_size((long long)raw.size()),
                                                media::human_size((long long)kMaxSticker)));
                    media::item it;
                    std::string err;
                    if (!media::sticker_add(c->p.id, nm, ext, raw, it, err))
                        return fail(err);
                    post_state_locked(true);
                    nlohmann::json r = ok();
                    r["name"] = it.name;
                    r["url"] = media::sticker_url(c->p.id, it.name);
                    return r;
                }
                if (name == "sticker.del")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    persona_ctx *c = find_ctx(active);
                    if (!c)
                        return fail("还没有打开角色");
                    std::string nm = cell::text::trim(args.value("name", ""));
                    std::string err;
                    if (!media::sticker_remove(c->p.id, nm, err))
                        return fail(err);
                    post_state_locked(true);
                    return ok();
                }
                if (name == "open.stickers_dir")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    persona_ctx *c = find_ctx(active);
                    if (!c)
                        return fail("还没有打开角色");
                    std::error_code ec;
                    fs::path dir = media::persona_stickers_dir(c->p.id);
                    fs::create_directories(dir, ec);
                    if (ec)
                        return fail("无法创建表情包目录：" + ec.message());
                    std::string cmd = "explorer.exe \"" + dir.string() + "\"";
#ifdef __APPLE__
                    cmd = "open \"" + dir.string() + "\"";
#elif !defined(_WIN32)
                    cmd = "xdg-open \"" + dir.string() + "\"";
#endif
                    std::string out, err;
                    int code = -1;
                    cell::plat::spawn_cmd(cmd, 10, out, code, err);
                    nlohmann::json r = ok();
                    r["path"] = dir.string();
                    return r;
                }

                // ---- 杂项 --------------------------------------------------
                if (name == "open.data_dir")
                {
                    std::string cmd = "explorer.exe \"" + cell::root.string() + "\"";
#ifdef __APPLE__
                    cmd = "open \"" + cell::root.string() + "\"";
#elif !defined(_WIN32)
                    cmd = "xdg-open \"" + cell::root.string() + "\"";
#endif
                    std::string out, err;
                    int code = -1;
                    cell::plat::spawn_cmd(cmd, 10, out, code, err);
                    return ok();
                }
                if (name == "app.quit")
                {
                    bus.post({{"t", "quit"}});
                    return ok();
                }
                return fail("未知命令：" + name);
            }
            catch (const std::exception &e)
            {
                return fail(std::string("命令失败：") + e.what());
            }
        }

        static nlohmann::json ok_with(nlohmann::json payload)
        {
            payload["ok"] = true;
            return payload;
        }

        // ---- 供应商实现（从 cell-app 移植，去掉 UI 专属字段）---------------
        cell::encrypt::secure_string resolve_key(const cell::config::provider_entry &p)
        {
            if (!p.key_id.empty())
            {
                auto k = vault.get(p.key_id);
                if (!k.empty())
                    return k;
            }
            const char *env = std::getenv(p.api_style == "anthropic" ? "ANTHROPIC_API_KEY" : "OPENAI_API_KEY");
            if (env && *env)
                return cell::encrypt::secure_string(env);
            return vault.get("api_key");
        }
        nlohmann::json providers_add(const nlohmann::json &a)
        {
            std::string style = cell::text::trim(a.value("api_style", "openai"));
            std::string canonical = cell::config::provider_entry::parse_api_style(style);
            if (canonical.empty())
                return fail("未知的协议：" + style);
            std::string base = cell::text::trim(a.value("base", ""));
            if (base.empty())
                return fail("需要 API base url");
            std::string name = cell::text::trim(a.value("name", ""));
            name = cell::config::unique_name(cfg, name.empty() ? canonical : name);
            cell::config::provider_entry ne;
            ne.name = name;
            ne.set_api_style(canonical);
            ne.base = base;
            ne.proxy = cell::text::trim(a.value("proxy", ""));
            std::string key = a.value("key", "");
            if (key.empty())
            {
                // 本机服务（llama.cpp / vLLM / LM Studio）通常不校验密钥，但引擎
                // 要求「有密钥」才发起请求 —— 给回环地址放一个占位密钥。
                std::string b = base;
                cell::lower_ascii(b);
                if (b.find("127.0.0.1") != std::string::npos || b.find("localhost") != std::string::npos ||
                    b.find("[::1]") != std::string::npos)
                    key = "local";
            }
            if (!key.empty())
            {
                std::string kid = "provider:" + name;
                if (!vault.set(kid, key))
                    return fail("密钥保存失败");
                ne.key_id = kid;
            }
            cfg.providers.push_back(std::move(ne));
            cfg.current_model.clear();
            cell::config::select_provider(cfg, name);
            cell::config::save(cfg, false);
            sync_settings_to_ctxs();
            post_state_locked(true);
            return ok();
        }
        nlohmann::json models_fetch()
        {
            const cell::config::provider_entry *p = cfg.current_provider_entry();
            if (!p)
                return fail("还没有配置供应商");
            cell::encrypt::secure_string key = resolve_key(*p);
            std::string buf, err;
            long code = 0;
            bool got = false;
            CURL *c = curl_easy_init();
            for (const auto &[url, anthropic_auth] : p->model_list_candidates())
            {
                std::vector<std::string> hdrs;
                if (anthropic_auth)
                    hdrs = {"x-api-key: " + std::string(key.data(), key.size()), "anthropic-version: 2023-06-01"};
                else
                    hdrs = {"Authorization: Bearer " + std::string(key.data(), key.size())};
                buf.clear();
                code = 0;
                err.clear();
                bool okc = c && cell::net::CURL_get(c, url.c_str(), hdrs, buf, &code, &err,
                                                    p->proxy.empty() ? nullptr : p->proxy.c_str(), 5);
                for (auto &h : hdrs)
                    cell::encrypt::wipe(h);
                if (okc && code == 200)
                {
                    got = true;
                    break;
                }
                if (err.empty())
                    err = std::format("HTTP {}", code);
                if (code != 404)
                    break;
            }
            if (c)
                curl_easy_cleanup(c);
            if (!got)
                return fail(err.empty() ? "拉取失败" : err);
            nlohmann::json models = nlohmann::json::array();
            auto j = nlohmann::json::parse(buf, nullptr, false);
            if (j.is_object() && j.contains("data") && j["data"].is_array())
                for (auto &m : j["data"])
                    if (m.contains("id") && m["id"].is_string())
                        models.push_back(m["id"].get<std::string>());
            nlohmann::json r = ok();
            r["models"] = std::move(models);
            return r;
        }
    };
} // namespace app
