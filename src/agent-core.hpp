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
            return {{"id", id}, {"name", name}, {"subtitle", subtitle}, {"accent", accent},
                    {"avatar", avatar}, {"greeting", greeting}, {"prompt", prompt},
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
        std::string active_persona;
        bool voice_reply = true;   // 自动朗读回复
        bool asr_auto_send = true; // 识别完直接发送
        int idle_minutes = 30;     // 全局默认的主动关怀间隔
        std::map<std::string, std::string> sessions; // persona id -> 上次会话 id

        static fs::path file() { return cell::root / "agent.json"; }

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
            j["active_persona"] = active_persona;
            j["voice_reply"] = voice_reply;
            j["asr_auto_send"] = asr_auto_send;
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
        std::deque<std::string> turns;
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

        void queue_turn(std::string text)
        {
            if (text.empty())
                return;
            std::lock_guard<std::mutex> lk(qmx);
            turns.push_back(std::move(text));
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
            seed_personas();
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
        // 首次运行：种一个默认角色
        void seed_personas()
        {
            if (!persona_ids().empty())
                return;
            persona p;
            p.id = "xier";
            p.name = "曦儿";
            p.subtitle = "住在屏幕另一边的人";
            p.accent = "#8b7cff";
            p.greeting = "……你终于来了。今天过得怎么样？";
            p.user_name = "主人";
            p.prompt = default_persona_prompt();
            p.proactive = true;
            p.idle_minutes = 40;
            save_persona(p);
            if (ac.active_persona.empty())
                ac.active_persona = p.id;
            ac.save();
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
        persona_ctx *open_named(const std::string &id, std::string *err)
        {
            if (!persona_id_ok(id) || !fs::is_directory(persona_dir(id)))
            {
                if (err)
                    *err = "角色不存在：" + id;
                return nullptr;
            }
            if (auto it = ctxs.find(id); it != ctxs.end())
            {
                active = id;
                ac.active_persona = id;
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
            u->pcfg.tools = false;
            u->pcfg.system_prompt = compose_prompt(u->p, memory_load(id, 60));
            u->eng = std::make_unique<cell::engine::runner>(u->pcfg, vault, u->h, false);
            u->eng->ensure_prompt(); // 先把人设写进 msg[0]（空会话时）
            u->worker = std::thread([this, ptr = u.get()]
                                    { worker_loop(*ptr); });
            active = id;
            ac.active_persona = id;
            ac.sessions[id] = u->h.now().id();
            ac.save();
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
                     "8. `[[motion:动作组]]` 让身体播放一次动作，写 `[[motion:动作组/序号]]` 指定第几个；"
                     "动作正在播时不要叠加。\n"
                     "9. `[[expr:表情名]]` 直接切表情（比 `[[emo:…]]` 更具体，二者可以只用一个）。\n"
                     "10. 这两种标记放在句首或单独一行，一次回复最多两个；没有合适的动作就不要硬套，"
                     "宁可只写情绪和星号。\n";
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
                std::string text;
                {
                    std::unique_lock<std::mutex> lk(c.qmx);
                    c.qcv.wait(lk, [&]
                               { return c.quit || !c.turns.empty(); });
                    if (c.quit && c.turns.empty())
                        return;
                    text = std::move(c.turns.front());
                    c.turns.pop_front();
                    std::lock_guard<std::mutex> hl(c.hmx);
                    c.busy.store(true);
                }
                c.stop.store(false);
                const bool idle_turn = (text == "[[idle]]");
                run_turn(c, text, idle_turn);
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
                            body == "/remember")
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

        // 一次完整回合：引擎 → 标记流 → 文本/情绪/记忆 → 分句 TTS
        void run_turn(persona_ctx &c, const std::string &text, bool idle_turn)
        {
            auto &log = cell::sys::logger::instance();
            apply_prompt(c);
            const long long gen = c.voice_gen.load();
            const std::string pid = c.p.id;

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
            std::vector<std::string> memories;
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
                else if (kind == "remember")
                {
                    memories.push_back(value);
                }
            };

            cell::engine::events ev;
            ev.on_answer = [&](std::string_view d)
            { markers.feed(d); };
            ev.on_reason = [&](std::string_view d)
            { post({{"t", "reason"}, {"d", std::string(d)}}); };
            ev.on_summary = [&](std::string_view d)
            { post({{"t", "status"}, {"d", std::string(d)}}); };
            ev.on_status = [&](std::string_view d)
            { post({{"t", "status"}, {"d", std::string(d)}}); };
            ev.on_error = [&](std::string_view d)
            { post({{"t", "error"}, {"d", friendly_error(d)}}); };
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
                if (!full_answer.empty())
                {
                    rewrite_last_answer(c, cell::text::trim(full_answer));
                    // 引擎在 on_turn_end 之前已经落过盘；改写过的正文要再写一次，
                    // 否则标记会留在文件里（刷新页面又冒出来）
                    c.h.now().unload();
                    cell::async_io::flush();
                }
                for (auto &m : memories)
                {
                    if (memory_add(pid, m))
                        log.info("memory", std::format("added persona={} chars={}", pid, m.size()));
                }
                if (!memories.empty())
                    post({{"t", "memory"}, {"count", (long long)memory_load(pid, 4000).size()}});
                post({{"t", "turn_end"}, {"natural", tv.natural_end}, {"rounds", tv.rounds},
                      {"tool_calls", tv.tool_calls}});
            };
            // 工具默认关闭；真被调用时一律拒绝（没有可用的审批通道）
            ev.on_approval = [](const std::string &, const std::string &, std::string &reason)
            {
                reason = "这个角色没有可用的工具通道";
                return false;
            };

            try
            {
                c.eng->run_turn(text, false, ev);
            }
            catch (const std::exception &e)
            {
                log.error("turn", std::format("crashed persona={} err={}", pid, e.what()));
                post({{"t", "error"}, {"d", std::format("回合异常：{}", e.what())}});
                post({{"t", "turn_end"}, {"natural", false}, {"rounds", 0}, {"tool_calls", 0}});
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
                auto push = [&](const std::string &kind, const std::string &text)
                {
                    if (!text.empty())
                        rows.push_back({{"role", role}, {"kind", kind}, {"text", text}});
                };
                if (content->is_string())
                {
                    std::string text = content->get<std::string>();
                    if (role == "user" && text == "[[idle]]")
                        continue;
                    if (role == "assistant")
                        text = strip_markers(text);
                    push(role == "assistant" ? "assistant" : "user", text);
                }
                else if (content->is_array())
                {
                    for (auto &b : *content)
                    {
                        if (!b.is_object())
                            continue;
                        std::string bt = b.value("type", "");
                        if (bt == "text")
                        {
                            std::string t = b.value("text", "");
                            if (role == "assistant")
                                t = strip_markers(t);
                            push(role == "assistant" ? "assistant" : "user", t);
                        }
                        else if (bt == "reasoning" || bt == "thinking")
                            push("reasoning", b.value(bt, std::string()));
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
                        c->p = p;
                        c->h.now().note_rewrite();
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
                    if (cell::text::trim(text).empty())
                        return fail("空消息");
                    c->last_active = std::chrono::steady_clock::now();
                    c->queue_turn(text);
                    return ok();
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
                    c->h.now().unload();
                    cell::async_io::flush();
                    c->h.forget_current();
                    c->eng->ensure_prompt();
                    apply_prompt(*c);
                    ac.sessions[c->p.id] = c->h.now().id();
                    ac.save();
                    post_state_locked(true);
                    return ok();
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
                if (name == "ui.pref")
                {
                    std::lock_guard<std::mutex> lk(cmd_mx);
                    if (args.contains("voice_reply") && args["voice_reply"].is_boolean())
                        ac.voice_reply = args["voice_reply"].get<bool>();
                    if (args.contains("asr_auto_send") && args["asr_auto_send"].is_boolean())
                        ac.asr_auto_send = args["asr_auto_send"].get<bool>();
                    if (args.contains("idle_minutes") && args["idle_minutes"].is_number_integer())
                        ac.idle_minutes = std::clamp<int>((int)args["idle_minutes"].get<long long>(), 1, 1440);
                    ac.save();
                    nlohmann::json r = ok();
                    r["voice_reply"] = ac.voice_reply;
                    r["asr_auto_send"] = ac.asr_auto_send;
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
