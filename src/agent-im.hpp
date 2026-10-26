// agent-im.hpp — IM 接入层：Telegram / QQ(OneBot) / 企业微信 / 通用 HTTP 桥
// =============================================================================
// 和 TTS / ASR 一样，「接一个聊天软件」是一份可替换的配置，不是一段写死的代码：
//
//   kind = "telegram"  Bot API 长轮询（getUpdates / sendMessage），出网即可，
//                      可挂代理；不需要公网地址
//   kind = "onebot"    OneBot v11（NapCat / go-cqhttp / Lagrange）：对端把事件
//                      POST 到 /api/im/<id>，我们调它的 HTTP API 回话
//   kind = "wecom"     企业微信自建应用：加密回调（SHA1 验签 + AES-256-CBC 解密）
//                      收，cgi-bin/message/send 发 —— 微信体系里唯一有官方 API
//                      的个人私聊通道
//   kind = "bridge"    通用 HTTP 桥：任何本地桥（个人微信 hook / wechaty /
//                      WeChatFerry / 自己写的脚本）POST 进来，我们 POST 回去
//   kind = "off"       关闭
//
// 这一层只管协议：解析出来的消息交给 app_core 排队，回复怎么送回由 app_core
// 决定（见 agent-core.hpp 的 im_* 一段）。回合本身不另起一条路径 —— 消息进的是
// persona_ctx::queue_turn，跑的还是 cell::engine::runner::run_turn 那套 agent loop
// （多轮、工具调用、打断、上下文压缩都照旧）。
//
// 依赖复用而不是重写：传输走 cell::net::perform，base64 走 cell::encrypt，
// 切词与 CLA 走 libsodium，SHA1 / AES-256-CBC 走 OpenSSL（CMake 找它）。
//
// 个人微信没有官方 API，任何声称「直接连个人微信」的库都是 hook 客户端；
// 想接个人号请走 kind=bridge（见 README「IM 接入」）。
#pragma once

#include "qr.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sodium.h>
#ifdef AGENT_HAVE_OPENSSL
#include <openssl/evp.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace app::im
{
    // =====================================================================
    //  凭据的内存形态
    // =====================================================================
    // 和 cell.cpp 的 vault 同一套：解密出来的凭据只以 secure_string 存在
    // （sodium_malloc 出来的页，mlock 不进 swap，析构 sodium_memzero）。
    // 渠道结构体在进程里被到处拷贝（线程快照、qr 请求、发送线程），
    // 所以凭据不能是 std::string —— 那样会留下一串没人擦的明文副本。
    class secret : public cell::encrypt::secure_string
    {
    public:
        using cell::encrypt::secure_string::secure_string; // (const std::string&) / (const char*)
        secret() = default;
        secret(const secret &) = default;
        secret(secret &&) noexcept = default;
        secret(const cell::encrypt::secure_string &o) : cell::encrypt::secure_string(o) {}
        secret &operator=(const secret &) = default;
        secret &operator=(secret &&) noexcept = default;
        // 配置里写死的字符串、JSON 里的字面量：顺手拷进安全内存，不用每次都 assign
        secret &operator=(const char *s)
        {
            assign(s, s ? std::strlen(s) : 0);
            return *this;
        }
        secret &operator=(const std::string &s)
        {
            assign(s.data(), s.size());
            return *this;
        }
        secret &operator=(const cell::encrypt::secure_string &o)
        {
            cell::encrypt::secure_string::operator=(o);
            return *this;
        }
    };

    inline void wipe(std::string &s) { cell::encrypt::wipe(s); }

    namespace detail
    {
        // 带凭据的请求头：析构时把每条头里的明文擦掉
        struct headers : std::vector<std::string>
        {
            using std::vector<std::string>::vector;
            headers() = default;
            headers(std::initializer_list<std::string> il) : std::vector<std::string>(il) {}
            ~headers()
            {
                for (auto &h : *this)
                    cell::encrypt::wipe(h);
            }
        };
        // 把一次性明文搬进 secure 容器，然后立刻擦掉源串
        inline void adopt(secret &dst, std::string &src)
        {
            dst.assign(src.data(), src.size());
            cell::encrypt::wipe(src);
        }
        // 作用域结束时擦掉这块临时明文（拼 URL / 解密的中间产物）
        struct zero_guard
        {
            std::string &s;
            ~zero_guard() { cell::encrypt::wipe(s); }
        };
    } // namespace detail

    // =====================================================================
    //  渠道配置
    // =====================================================================
    struct channel
    {
        std::string id = "im";    // 唯一名；同时也是回调地址的最后一段 /api/im/<id>
        std::string kind = "off"; // telegram | onebot | wecom | bridge | off
        bool enabled = true;
        // telegram: 默认 https://api.telegram.org，可换自建反代
        // onebot:   默认 http://127.0.0.1:3000（go-cqhttp 的 HTTP 服务）
        // wecom:    默认 https://qyapi.weixin.qq.com
        // bridge:   必填，桥接收回复的地址
        std::string api_base;
        std::string proxy;              // telegram 国内要挂；其余按需
        std::string persona;            // 绑定到哪个角色；空 = 当前选中角色
        // ---- 谁可以说话：私聊 / 群聊分开配置 ----------------------------
        // 私聊：
        //   private_mode = "open"  谁都能私聊
        //                  "allow" 只有 private_allow 里的 user id
        // 群聊：
        //   group_mode = "off"           群里不响应（默认）
        //                "mention"       群里任何人 @ 她就回
        //                "allow_mention" group_allow 里的人 @ 她才回
        //                "allow"         group_allow 里的人任意消息都回（不用 @）
        //   group_ids 非空时，只处理这些群（任何 group_mode 都先过这一层）。
        std::string private_mode = "open";
        std::vector<std::string> private_allow;
        std::string group_mode = "off";
        std::vector<std::string> group_allow;
        std::vector<std::string> group_ids; // 群号 / 群 chat id；空 = 不限制
        // 旧版单一白名单：只用于迁移（配置里没有新字段时按旧语义展开），
        // 新配置不再读写它。保留字段是为了老 agent.json 和脚本能平滑升级。
        std::vector<std::string> allow;
        int limit = 0;                  // 每条消息最多多少字符，0 = 用平台默认上限

        // ---- 发送节奏 ---------------------------------------------------
        // 平台对同一会话的速率限制是硬的（Telegram 约 1 条/秒、NapCat 与企微
        // 各有自己的阈值），撞上去就是 429 / 风控 / 掉线。所以出站先排队，
        // 两条之间再隔 send_delay_ms —— 一次长回复会被切成几条，这几条也算。
        int send_delay_ms = 600; // 0 = 不限速（不建议）
        int queue_limit = 64;    // 出站队列上限，满了丢最旧的（新的更该送到）

        // 凭据 —— 只进加密库（.agent/.crypt），agent.json 里一个都不留。
        // 内存里是 secure_string：mlock + 析构清零，见上面的 secret。
        secret token;            // telegram bot token | onebot access token | wecom 回调 Token | bridge secret
        secret aes_key;          // wecom EncodingAESKey（43 位）
        std::string corp_id;     // wecom 企业 id
        std::string agent_id;    // wecom 应用 id
        secret corp_secret;      // wecom 应用 secret
        // 微信 iLink（ClawBot）：扫码登录后拿到的一组凭证
        secret bot_token; // Bearer token
        std::string bot_base; // baseurl（扫码后由服务端返回，可能不是默认域名）
        std::string bot_id;   // ilink_bot_id
        std::string bot_user; // ilink_user_id

        bool live() const { return enabled && !kind.empty() && kind != "off"; }
        bool webhook() const { return kind == "onebot" || kind == "wecom" || kind == "bridge"; }

        nlohmann::json to_json() const // 不含凭据：界面拿到的就是这一份
        {
            return {{"id", id}, {"kind", kind}, {"enabled", enabled}, {"api_base", api_base},
                    {"proxy", proxy}, {"persona", persona}, {"limit", limit},
                    {"send_delay_ms", send_delay_ms}, {"queue_limit", queue_limit},
                    {"private_mode", private_mode}, {"private_allow", private_allow},
                    {"group_mode", group_mode}, {"group_allow", group_allow},
                    {"group_ids", group_ids}, {"allow", allow},
                    {"has_token", !token.empty()}, {"has_aes_key", !aes_key.empty()},
                    {"has_corp", !corp_id.empty() && !corp_secret.empty() && !agent_id.empty()},
                    {"has_bot", !bot_token.empty()},
                    {"bot_id", bot_id}, {"bot_base", bot_base},
                    {"webhook", webhook() ? "/api/im/" + id : ""}};
        }
        // 有哪个字段就覆盖哪个：配置和凭据可以分开合并
        void patch(const nlohmann::json &j)
        {
            if (!j.is_object())
                return;
            auto s = [&](const char *k, std::string &dst)
            {
                auto it = j.find(k);
                if (it != j.end() && it->is_string())
                    dst = it->get<std::string>();
            };
            // 凭据直接抄进 secure 容器，中间不留 std::string 副本
            auto q = [&](const char *k, secret &dst)
            {
                auto it = j.find(k);
                if (it != j.end() && it->is_string())
                {
                    const std::string &v = it->get_ref<const std::string &>();
                    dst.assign(v.data(), v.size());
                }
            };
            s("id", id);
            s("kind", kind);
            s("api_base", api_base);
            s("proxy", proxy);
            s("persona", persona);
            q("token", token);
            q("aes_key", aes_key);
            s("corp_id", corp_id);
            s("agent_id", agent_id);
            q("corp_secret", corp_secret);
            q("bot_token", bot_token);
            s("bot_base", bot_base);
            s("bot_id", bot_id);
            s("bot_user", bot_user);
            auto b = j.find("enabled");
            if (b != j.end() && b->is_boolean())
                enabled = b->get<bool>();
            auto l = j.find("limit");
            if (l != j.end() && l->is_number_integer())
                limit = std::clamp<int>((int)l->get<long long>(), 0, 10000);
            auto d = j.find("send_delay_ms");
            if (d != j.end() && d->is_number_integer())
                send_delay_ms = std::clamp<int>((int)d->get<long long>(), 0, 60000);
            auto ql = j.find("queue_limit");
            if (ql != j.end() && ql->is_number_integer())
                queue_limit = std::clamp<int>((int)ql->get<long long>(), 1, 2000);
            // 白名单列表：字符串或数字 id，去空
            auto list = [&](const char *k, std::vector<std::string> &dst) -> bool
            {
                auto it = j.find(k);
                if (it == j.end() || !it->is_array())
                    return false;
                dst.clear();
                for (auto &v : *it)
                    if (v.is_string() && !v.get_ref<const std::string &>().empty())
                        dst.push_back(v.get<std::string>());
                    else if (v.is_number_integer())
                        dst.push_back(std::to_string(v.get<long long>()));
                return true;
            };
            const bool had_legacy = j.contains("allow");
            list("allow", allow);
            const bool had_new = j.contains("private_mode") || j.contains("private_allow") ||
                                 j.contains("group_mode") || j.contains("group_allow") ||
                                 j.contains("group_ids");
            auto mode = [&](const char *k, std::string &dst, std::initializer_list<const char *> legal,
                            const char *fallback)
            {
                auto it = j.find(k);
                if (it == j.end() || !it->is_string())
                    return;
                std::string v = it->get<std::string>();
                for (const char *ok : legal)
                    if (v == ok)
                    {
                        dst = std::move(v);
                        return;
                    }
                dst = fallback;
            };
            mode("private_mode", private_mode, {"open", "allow"}, "open");
            mode("group_mode", group_mode, {"off", "mention", "allow_mention", "allow"}, "off");
            list("private_allow", private_allow);
            list("group_allow", group_allow);
            list("group_ids", group_ids);
            // 旧配置迁移：没有新字段、只有 allow 时，按旧语义展开 ——
            // 空 = 私聊全放行 / 群聊不响应；非空 = 私聊白名单 + 群里白名单用户 @ 才回。
            if (!had_new && had_legacy)
            {
                private_allow = allow;
                group_allow = allow;
                if (allow.empty())
                {
                    private_mode = "open";
                    group_mode = "off";
                }
                else
                {
                    private_mode = "allow";
                    group_mode = "allow_mention";
                }
            }
            // 新字段一旦出现，旧 allow 就只是历史残留：清掉，避免它以后
            // 又被当作「老配置」触发迁移语义（UI 会把 to_json 原样 patch 回来）。
            if (had_new)
                allow.clear();
        }
        static channel from_json(const nlohmann::json &j)
        {
            channel c;
            c.patch(j);
            return c;
        }
        nlohmann::json creds_json() const
        {
            // .str() 会造一份临时明文：调用方拿到 dump 后必须立刻 wipe
            return {{"token", token.str()}, {"aes_key", aes_key.str()}, {"corp_id", corp_id},
                    {"agent_id", agent_id}, {"corp_secret", corp_secret.str()},
                    {"bot_token", bot_token.str()}, {"bot_base", bot_base},
                    {"bot_id", bot_id}, {"bot_user", bot_user}};
        }
        // 渠道 id 会进 URL 与 vault key，只允许安全字符
        static bool id_ok(const std::string &s)
        {
            if (s.empty() || s.size() > 32 || s.front() == '.' || s.front() == '-')
                return false;
            for (unsigned char c : s)
                if (!(std::isalnum(c) || c == '-' || c == '_'))
                    return false;
            return true;
        }
    };

    // 一段媒体（进来的或要发出去的都走这个形状）
    struct media_ref
    {
        std::string kind;    // image | sticker | audio | video | file | face
        std::string name;    // 文件名 / 表情名
        std::string mime;
        std::string url;     // 远端地址（能直接 GET 的那种）
        std::string file_id; // Telegram file_id / 企业微信 media_id / OneBot file
        std::string data;    // 内联 base64（QQ 的 base64:// 之类）
        std::string id;      // face 的表情编号 / OneBot mface emoji_id
        std::string local;   // app 落盘后的绝对路径（im 层不填）
        long long size = 0;
        // ---- 平台私有句柄（按需使用）----
        std::string media_id;            // 企业微信 MediaId
        std::string aeskey;              // iLink image_item.aeskey（32 位 hex）
        std::string encrypt_query_param; // iLink CDNMedia.encrypt_query_param
        int encrypt_type = 0;            // iLink CDNMedia.encrypt_type
        int encode_type = 0;             // iLink voice encode_type
        std::string file_name;           // iLink file_item.file_name / OneBot file
        nlohmann::json raw = nlohmann::json::object(); // 原始 segment / JSON，下载兜底用
    };

    // 一条进来的消息
    struct inbound
    {
        std::string chan; // 渠道 id（由调用方填）
        std::string chat; // 平台会话 id：私聊是用户 id，群聊是群 id
        std::string user; // 平台用户 id
        std::string name; // 昵称，只用于日志
        std::string text; // 文字部分（可能为空：只发了图）
        std::string msg_id;
        std::vector<media_ref> media; // 图片 / 语音 / 视频 / 文件 / 表情
        bool group = false;
        bool mention = false; // 群里被 @ 了（私聊恒为 true）
    };

    // 平台回调的原始请求（agent-http 拆好之后交过来）
    struct request
    {
        std::string method; // GET / POST
        std::string query;  // 不含 '?' 的原始 query
        std::string auth;   // Authorization 头
        std::string secret; // X-Im-Secret 头
        std::string body;
        // 反向代理会把 Host 换成公网域名，所以这句是「请求是不是本机直连」的唯一线索：
        // 没有 token 的渠道只接受本机直连。
        bool loopback = true;

        std::string param(const std::string &key) const; // 已 URL 解码
    };

    // =====================================================================
    //  细节：HTTP、URL、切段
    // =====================================================================
    namespace detail
    {
        struct resp
        {
            bool ok = false;
            long code = 0;
            std::string body, err;
            // 响应头（键一律小写）。企业微信要 Content-Type，iLink CDN 上传要
            // x-encrypted-param，所以每个请求都收一份。
            std::map<std::string, std::string> headers;
        };

        // 响应头里的一个字段（名字大小写不敏感）；没有就是空串
        inline std::string header(const resp &r, std::string_view name)
        {
            std::string key;
            key.reserve(name.size());
            for (char c : name)
                key.push_back((char)std::tolower((unsigned char)c));
            auto it = r.headers.find(key);
            return it == r.headers.end() ? std::string() : it->second;
        }

        // 中止探针：curl 传输过程中反复回调，返回非 0 立刻断开。
        // 轮询线程退出时用它，否则 join 要等满一个网络超时。
        inline int abort_probe(void *ud, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
        {
            auto *flag = static_cast<const std::atomic<bool> *>(ud);
            return (flag && flag->load()) ? 1 : 0;
        }

        inline size_t body_cb(char *data, size_t size, size_t nmemb, void *ud)
        {
            auto *out = static_cast<std::string *>(ud);
            const size_t n = size * nmemb;
            if (nmemb != 0 && n / nmemb != size)
                return 0; // 乘法溢出
            try
            {
                out->append(data, n);
            }
            catch (...)
            {
                return 0; // 异常不能穿过 libcurl 的 C 帧
            }
            return n;
        }
        inline size_t header_cb(char *data, size_t size, size_t nmemb, void *ud)
        {
            auto *out = static_cast<std::map<std::string, std::string> *>(ud);
            const size_t n = size * nmemb;
            std::string_view line(data, n);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.remove_suffix(1);
            if (line.empty() || line.starts_with("HTTP/"))
                return n; // 状态行 / 空行
            const size_t colon = line.find(':');
            if (colon == std::string_view::npos)
                return n;
            std::string key;
            key.reserve(colon);
            for (char c : line.substr(0, colon))
                key.push_back((char)std::tolower((unsigned char)c));
            size_t v = colon + 1;
            while (v < line.size() && (line[v] == ' ' || line[v] == '\t'))
                v++;
            std::string value(line.substr(v));
            auto it = out->find(key);
            if (it == out->end())
                (*out)[key] = std::move(value);
            else
                it->second += ", " + value; // 同名头（Set-Cookie 之类）用逗号并起来
            return n;
        }

        // 自己装 curl 句柄，而不是走 cell::net::perform：响应头也要（企业微信的
        // Content-Type、iLink CDN 的 x-encrypted-param 都在头里），而 perform 不
        // 暴露 header 回调。CA / 代理 / 回环放行 / 中止探针与 net::perform 保持一致。
        inline bool perform(const channel &ch, const std::string &url, const std::string &payload,
                            const std::vector<std::string> &headers, long timeout,
                            const std::atomic<bool> *abort, std::string &body,
                            std::map<std::string, std::string> &out_headers, long &code, std::string &err)
        {
            CURL *c = curl_easy_init();
            if (!c)
            {
                err = "curl 初始化失败";
                return false;
            }
            curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
            // TLS 信任锚：Windows 上编译进来的默认路径不存在，要换成系统根证书
            if (const char *ca = cell::net::ca_bundle(); ca && *ca)
                curl_easy_setopt(c, CURLOPT_CAINFO, ca);
            curl_easy_setopt(c, CURLOPT_URL, url.c_str());
            curl_easy_setopt(c, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1");
            if (!ch.proxy.empty())
            {
                curl_easy_setopt(c, CURLOPT_PROXY, ch.proxy.c_str());
                if (url.starts_with("https://"))
                    curl_easy_setopt(c, CURLOPT_HTTPPROXYTUNNEL, 1L);
            }
            if (timeout > 0)
            {
                curl_easy_setopt(c, CURLOPT_TIMEOUT, timeout);
                curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, std::min(timeout, 5L));
            }
            else
            {
                curl_easy_setopt(c, CURLOPT_TIMEOUT, 300L);
                curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
            }
            if (!payload.empty())
            {
                curl_easy_setopt(c, CURLOPT_POST, 1L);
                curl_easy_setopt(c, CURLOPT_POSTFIELDS, payload.data());
                curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)payload.size());
            }
            // 媒体直链经常 302 到 CDN
            curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, body_cb);
            curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
            curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, header_cb);
            curl_easy_setopt(c, CURLOPT_HEADERDATA, &out_headers);
            if (abort)
            {
                curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
                curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, abort_probe);
                curl_easy_setopt(c, CURLOPT_XFERINFODATA, (void *)abort);
            }
            curl_slist *list = nullptr;
            for (const auto &h : headers)
                list = curl_slist_append(list, h.c_str());
            if (list)
                curl_easy_setopt(c, CURLOPT_HTTPHEADER, list);
            const CURLcode res = curl_easy_perform(c);
            curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
            if (list)
                curl_slist_free_all(list);
            curl_easy_cleanup(c);
            if (res != CURLE_OK)
            {
                err = curl_easy_strerror(res);
                return false;
            }
            return true;
        }

        // 一次请求：payload 为空 = GET。
        // 走上面的 perform（CA / 代理 / 中止探针与模型请求同一套行为）。
        inline resp call(const channel &ch, const std::string &url, const std::string &payload,
                         const std::vector<std::string> &headers = {}, long timeout = 30,
                         const std::atomic<bool> *abort = nullptr)
        {
            resp r;
            std::vector<std::string> hdrs = headers;
            bool has_ct = false;
            for (const auto &h : hdrs)
            {
                std::string key = h.substr(0, std::min<size_t>(h.size(), 13));
                for (auto &c : key)
                    c = (char)std::tolower((unsigned char)c);
                if (key.starts_with("content-type:"))
                    has_ct = true;
            }
            if (!payload.empty() && !has_ct)
                hdrs.push_back("Content-Type: application/json");
            // 这份副本里有 Authorization / X-Im-Secret，出函数前擦掉
            struct wipe_guard
            {
                std::vector<std::string> &v;
                ~wipe_guard()
                {
                    for (auto &h : v)
                        cell::encrypt::wipe(h);
                }
            } guard{hdrs};
            std::string neterr;
            if (!perform(ch, url, payload, hdrs, timeout, abort, r.body, r.headers, r.code, neterr))
            {
                r.err = neterr.empty() ? "连接失败" : neterr;
                return r;
            }
            r.ok = r.code >= 200 && r.code < 300;
            if (!r.ok)
                r.err = std::format("HTTP {}", r.code);
            return r;
        }

        inline std::string trim_slash(std::string s)
        {
            while (!s.empty() && s.back() == '/')
                s.pop_back();
            return s;
        }
        inline std::string url_encode(std::string_view s)
        {
            static const char *hex = "0123456789ABCDEF";
            std::string out;
            out.reserve(s.size() + 8);
            for (unsigned char c : s)
            {
                if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                    out.push_back((char)c);
                else
                {
                    out.push_back('%');
                    out.push_back(hex[c >> 4]);
                    out.push_back(hex[c & 15]);
                }
            }
            return out;
        }
        inline std::string url_decode(std::string_view s)
        {
            auto hex = [](char c) -> int
            {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            std::string out;
            out.reserve(s.size());
            for (size_t i = 0; i < s.size(); i++)
            {
                if (s[i] == '%' && i + 2 < s.size())
                {
                    int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
                    if (hi >= 0 && lo >= 0)
                    {
                        out.push_back((char)((hi << 4) | lo));
                        i += 2;
                        continue;
                    }
                }
                if (s[i] == '+')
                {
                    out.push_back(' ');
                    continue;
                }
                out.push_back(s[i]);
            }
            return out;
        }
        // ---- base64 / hex / 文件 / mime 小工具（fetch / send_media 共用）----
        inline std::string b64_encode(const std::string &in) { return cell::encrypt::b64_encode(in); }
        inline std::string b64_decode(const std::string &in) { return cell::encrypt::b64_decode(in); }

        inline int hex_val(char c)
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        }
        inline std::string hex_encode(std::string_view in)
        {
            static const char *d = "0123456789abcdef";
            std::string out;
            out.reserve(in.size() * 2);
            for (unsigned char c : in)
            {
                out.push_back(d[c >> 4]);
                out.push_back(d[c & 15]);
            }
            return out;
        }
        // 严格 hex 解码：长度必须偶数、字符必须合法，否则返回 false
        inline bool hex_decode(std::string_view in, std::string &out)
        {
            if (in.size() % 2 != 0)
                return false;
            std::string tmp;
            tmp.reserve(in.size() / 2);
            for (size_t i = 0; i < in.size(); i += 2)
            {
                int hi = hex_val(in[i]), lo = hex_val(in[i + 1]);
                if (hi < 0 || lo < 0)
                    return false;
                tmp.push_back((char)((hi << 4) | lo));
            }
            out = std::move(tmp);
            return true;
        }
        inline std::string random_hex(size_t bytes)
        {
            std::string raw(bytes, '\0');
            randombytes_buf(raw.data(), raw.size());
            return hex_encode(raw);
        }
        inline bool read_file(const std::string &path, std::string &out)
        {
            if (path.empty())
                return false;
            std::ifstream f(path, std::ios::binary);
            if (!f)
                return false;
            std::string tmp((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (tmp.empty())
                return false;
            out = std::move(tmp);
            return true;
        }
        inline std::string lower_ascii(std::string s)
        {
            for (auto &c : s)
                c = (char)std::tolower((unsigned char)c);
            return s;
        }
        // 小写扩展名（不含点）
        inline std::string file_ext(const std::string &name)
        {
            size_t slash = name.find_last_of("/\\");
            size_t dot = name.find_last_of('.');
            if (dot == std::string::npos || (slash != std::string::npos && dot < slash) || dot + 1 >= name.size())
                return {};
            return lower_ascii(name.substr(dot + 1));
        }
        inline std::string mime_for_name(const std::string &name)
        {
            const std::string ext = file_ext(name);
            static const std::map<std::string, std::string> m = {
                {"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
                {"gif", "image/gif"}, {"webp", "image/webp"}, {"bmp", "image/bmp"},
                {"svg", "image/svg+xml"}, {"tgs", "application/x-tgs"},
                {"ogg", "audio/ogg"}, {"opus", "audio/ogg"}, {"mp3", "audio/mpeg"},
                {"amr", "audio/amr"}, {"silk", "audio/silk"}, {"wav", "audio/wav"},
                {"m4a", "audio/mp4"}, {"aac", "audio/aac"}, {"flac", "audio/flac"},
                {"mp4", "video/mp4"}, {"mov", "video/quicktime"}, {"webm", "video/webm"},
                {"mkv", "video/x-matroska"}, {"avi", "video/x-msvideo"},
                {"pdf", "application/pdf"}, {"zip", "application/zip"},
                {"txt", "text/plain"}, {"json", "application/json"},
                {"doc", "application/msword"},
                {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
                {"xls", "application/vnd.ms-excel"},
                {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
            };
            auto it = m.find(ext);
            return it == m.end() ? std::string() : it->second;
        }
        // mime → 常用扩展名（给没有文件名的媒体兜底）
        inline std::string ext_for_mime(std::string_view mime)
        {
            std::string m = lower_ascii(std::string(mime));
            if (m.starts_with("image/png")) return "png";
            if (m.starts_with("image/jpeg")) return "jpg";
            if (m.starts_with("image/gif")) return "gif";
            if (m.starts_with("image/webp")) return "webp";
            if (m.starts_with("audio/ogg")) return "ogg";
            if (m.starts_with("audio/mpeg")) return "mp3";
            if (m.starts_with("audio/amr")) return "amr";
            if (m.starts_with("audio/silk")) return "silk";
            if (m.starts_with("audio/")) return "ogg";
            if (m.starts_with("video/mp4")) return "mp4";
            if (m.starts_with("video/")) return "mp4";
            return "bin";
        }
        inline std::string default_name_for(const std::string &kind)
        {
            if (kind == "image") return "photo.jpg";
            if (kind == "sticker") return "sticker.webp";
            if (kind == "audio") return "voice.ogg";
            if (kind == "video") return "video.mp4";
            return "file.bin";
        }
        // 回环地址：测试用的假服务端会在这里
        inline bool is_loopback_url(std::string_view url)
        {
            const size_t p = url.find("://");
            if (p == std::string_view::npos)
                return false;
            std::string_view rest = url.substr(p + 3);
            const size_t slash = rest.find('/');
            std::string_view host = slash == std::string_view::npos ? rest : rest.substr(0, slash);
            // 去掉 userinfo 与端口
            if (const size_t at = host.rfind('@'); at != std::string_view::npos)
                host = host.substr(at + 1);
            if (!host.empty() && host.front() == '[')
            {
                const size_t close = host.find(']');
                if (close != std::string_view::npos)
                    host = host.substr(1, close - 1);
            }
            else if (const size_t colon = host.rfind(':'); colon != std::string_view::npos)
                host = host.substr(0, colon);
            return host == "127.0.0.1" || host == "localhost" || host == "::1" ||
                   host == "0:0:0:0:0:0:0:1";
        }
        inline bool looks_abs_path(const std::string &s)
        {
            if (s.size() >= 3 && std::isalpha((unsigned char)s[0]) && s[1] == ':' &&
                (s[2] == '\\' || s[2] == '/'))
                return true; // Windows 盘符
            if (!s.empty() && s[0] == '/')
                return !s.starts_with("//"); // POSIX 绝对路径（// 是协议相对 URL）
            return false;
        }
        // 把一段 http(s) 地址整段取回来（媒体直链 / 企业微信媒体接口共用）
        inline bool download(const channel &ch, const std::string &url, std::string &bytes,
                             std::string &err, const std::atomic<bool> *abort = nullptr, long timeout = 60)
        {
            auto r = call(ch, url, "", {}, timeout, abort);
            if (!r.ok)
            {
                err = r.err;
                return false;
            }
            if (r.body.empty())
            {
                err = "下载回来是空的";
                return false;
            }
            bytes = std::move(r.body);
            return true;
        }
        // 数字/字符串都认的 size 字段
        inline long long pick_size(const nlohmann::json &j, std::initializer_list<const char *> keys)
        {
            for (const char *k : keys)
            {
                auto it = j.find(k);
                if (it == j.end())
                    continue;
                if (it->is_number_integer() || it->is_number_unsigned())
                    return it->get<long long>();
                if (it->is_string())
                {
                    try
                    {
                        return std::stoll(it->get<std::string>());
                    }
                    catch (...)
                    {
                        continue;
                    }
                }
            }
            return 0;
        }

        // 平台 id 可能是数字也可能是字符串（QQ 号常常超出 int32）
        inline std::string id_str(const nlohmann::json &j)
        {
            if (j.is_string())
                return j.get<std::string>();
            if (j.is_number_unsigned())
                return std::to_string(j.get<unsigned long long>());
            if (j.is_number_integer())
                return std::to_string(j.get<long long>());
            if (j.is_number_float())
                return std::format("{:.0f}", j.get<double>());
            return {};
        }
        // 全数字就当数字发，否则原样发（不同实现接受的类型不一样）
        inline nlohmann::json id_json(const std::string &s)
        {
            if (s.empty())
                return s;
            bool digits = std::all_of(s.begin(), s.end(), [](unsigned char c)
                                      { return std::isdigit(c); });
            if (!digits)
                return s;
            try
            {
                return std::stoll(s);
            }
            catch (...)
            {
                return s;
            }
        }
        inline void replace_all(std::string &s, const std::string &from, const std::string &to)
        {
            if (from.empty())
                return;
            for (size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size())
                s.replace(p, from.size(), to);
        }
        inline size_t utf8_len(unsigned char c)
        {
            if (c < 0x80)
                return 1;
            if ((c >> 5) == 0x6)
                return 2;
            if ((c >> 4) == 0xE)
                return 3;
            if ((c >> 3) == 0x1E)
                return 4;
            return 1;
        }
        // 断点字符：切段时优先在这些字符之后断开（「。」是 3 字节，按串比）
        inline bool soft_break(const std::string &s, size_t i, size_t len)
        {
            std::string_view c(s.data() + i, len);
            static const char *const marks[] = {"。", "！", "？", "；", "，", "…", "\n",
                                                ".", "!", "?", ";", ","};
            for (const char *m : marks)
                if (c == m)
                    return true;
            return false;
        }
        // 按长度切段：limit 单位由 bytes 决定（企业微信按字节算，其余按字符算）
        inline std::vector<std::string> split(const std::string &text, size_t limit, bool bytes)
        {
            std::vector<std::string> out;
            if (limit < 8)
                limit = 8;
            size_t i = 0, n = text.size();
            while (i < n)
            {
                size_t start = i, cost = 0, soft = 0;
                while (i < n)
                {
                    size_t len = utf8_len((unsigned char)text[i]);
                    if (i + len > n)
                        len = 1;
                    size_t add = bytes ? len : 1;
                    if (cost + add > limit)
                        break;
                    bool mark = soft_break(text, i, len);
                    i += len;
                    cost += add;
                    if (mark)
                        soft = i;
                }
                if (i >= n)
                {
                    out.push_back(cell::text::trim(text.substr(start)));
                    break;
                }
                if (soft > start)
                    i = soft; // 回溯到最近的句末
                else if (i == start)
                    i += utf8_len((unsigned char)text[i]); // limit 比一个字还小：硬推
                out.push_back(cell::text::trim(text.substr(start, i - start)));
            }
            std::erase_if(out, [](const std::string &s)
                          { return s.empty(); });
            return out;
        }
    } // namespace detail

    inline std::string request::param(const std::string &key) const
    {
        size_t i = 0;
        while (i < query.size())
        {
            size_t amp = query.find('&', i);
            std::string_view kv(query.data() + i, (amp == std::string::npos ? query.size() : amp) - i);
            size_t eq = kv.find('=');
            if (eq != std::string_view::npos && kv.substr(0, eq) == key)
                return detail::url_decode(kv.substr(eq + 1));
            if (amp == std::string::npos)
                break;
            i = amp + 1;
        }
        return {};
    }

    // =====================================================================
    //  加密原语：企业微信要 SHA1 验签 + AES-256-CBC 解密
    // =====================================================================
    // 不自己写密码学：
    //   SHA1 / AES-256-CBC  → OpenSSL（Windows / Linux 上本来就随静态 libcurl
    //                         链进来了，CMake 里补一句 find_package 即可）
    //   base64 / 常数时间比较 → cell.cpp 已有的 cell::encrypt::b64_decode 与 libsodium
    namespace crypto
    {
#ifdef AGENT_HAVE_OPENSSL
        inline constexpr bool kAvailable = true;
#else
        inline constexpr bool kAvailable = false;
#endif

        inline std::string sha1_hex(std::string_view data)
        {
#ifdef AGENT_HAVE_OPENSSL
            unsigned char digest[EVP_MAX_MD_SIZE];
            unsigned int len = 0;
            std::string out;
            if (EVP_MD_CTX *ctx = EVP_MD_CTX_new())
            {
                if (EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr) == 1 &&
                    EVP_DigestUpdate(ctx, data.data(), data.size()) == 1 &&
                    EVP_DigestFinal_ex(ctx, digest, &len) == 1)
                {
                    out.reserve(len * 2);
                    for (unsigned int i = 0; i < len; i++)
                        out += std::format("{:02x}", digest[i]);
                }
                EVP_MD_CTX_free(ctx);
            }
            return out;
#else
            (void)data;
            return {};
#endif
        }

        // MD5：iLink 上报 media 时 rawfilemd5 必须是原文件的 md5 hex
        inline std::string md5_hex(std::string_view data)
        {
#ifdef AGENT_HAVE_OPENSSL
            unsigned char digest[EVP_MAX_MD_SIZE];
            unsigned int len = 0;
            std::string out;
            if (EVP_MD_CTX *ctx = EVP_MD_CTX_new())
            {
                if (EVP_DigestInit_ex(ctx, EVP_md5(), nullptr) == 1 &&
                    EVP_DigestUpdate(ctx, data.data(), data.size()) == 1 &&
                    EVP_DigestFinal_ex(ctx, digest, &len) == 1)
                {
                    out.reserve(len * 2);
                    for (unsigned int i = 0; i < len; i++)
                        out += std::format("{:02x}", digest[i]);
                }
                EVP_MD_CTX_free(ctx);
            }
            return out;
#else
            (void)data;
            return {};
#endif
        }

        // AES-256-CBC 解密。企业微信的填充是 PKCS#7(k=32)，OpenSSL 会按块长 16
        // 去填充，所以这里关掉它的去填充，由 wecom_decrypt 自己剥。
        inline bool aes256_cbc_decrypt(const std::string &key, const std::string &iv,
                                       const std::string &in, std::string &out)
        {
#ifdef AGENT_HAVE_OPENSSL
            if (key.size() != 32 || iv.size() != 16 || in.empty() || in.size() % 16 != 0)
                return false;
            EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
            if (!ctx)
                return false;
            bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                                         (const unsigned char *)key.data(),
                                         (const unsigned char *)iv.data()) == 1 &&
                      EVP_CIPHER_CTX_set_padding(ctx, 0) == 1;
            out.assign(in.size() + 16, '\0');
            int written = 0, tail = 0;
            if (ok)
                ok = EVP_DecryptUpdate(ctx, (unsigned char *)out.data(), &written,
                                       (const unsigned char *)in.data(), (int)in.size()) == 1;
            if (ok)
                ok = EVP_DecryptFinal_ex(ctx, (unsigned char *)out.data() + written, &tail) == 1;
            EVP_CIPHER_CTX_free(ctx);
            if (!ok)
            {
                out.clear();
                return false;
            }
            out.resize((size_t)written + (size_t)tail);
            return true;
#else
            (void)key; (void)iv; (void)in; (void)out;
            return false;
#endif
        }

        // AES-128-ECB + PKCS#7：iLink 的 CDN 媒体加解密（入站解密 / 出站加密）
        inline bool aes128_ecb_encrypt(const std::string &key16, const std::string &plain,
                                       std::string &out, std::string &err)
        {
#ifdef AGENT_HAVE_OPENSSL
            out.clear();
            if (key16.size() != 16)
            {
                err = "AES-128 密钥必须是 16 字节";
                return false;
            }
            EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
            if (!ctx)
            {
                err = "EVP 初始化失败";
                return false;
            }
            bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr,
                                         (const unsigned char *)key16.data(), nullptr) == 1;
            // 默认启用 PKCS#7 填充（OpenSSL 的 EVP_aes_128_ecb 默认就是它）
            out.assign(plain.size() + 16, '\0');
            int written = 0, tail = 0;
            if (ok)
                ok = EVP_EncryptUpdate(ctx, (unsigned char *)out.data(), &written,
                                       (const unsigned char *)plain.data(), (int)plain.size()) == 1;
            if (ok)
                ok = EVP_EncryptFinal_ex(ctx, (unsigned char *)out.data() + written, &tail) == 1;
            EVP_CIPHER_CTX_free(ctx);
            if (!ok)
            {
                out.clear();
                err = "AES-128-ECB 加密失败";
                return false;
            }
            out.resize((size_t)written + (size_t)tail);
            return true;
#else
            (void)key16; (void)plain; (void)out;
            err = "这个构建没有链 OpenSSL，iLink 媒体不可用";
            return false;
#endif
        }

        // 去 PKCS#7 填充；密文不是整块时报错（调用方会退回「当明文」）
        inline bool aes128_ecb_decrypt(const std::string &key16, const std::string &cipher,
                                       std::string &out, std::string &err)
        {
#ifdef AGENT_HAVE_OPENSSL
            out.clear();
            if (key16.size() != 16)
            {
                err = "AES-128 密钥必须是 16 字节";
                return false;
            }
            if (cipher.empty() || cipher.size() % 16 != 0)
            {
                err = "密文长度不是 16 的倍数";
                return false;
            }
            EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
            if (!ctx)
            {
                err = "EVP 初始化失败";
                return false;
            }
            bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr,
                                         (const unsigned char *)key16.data(), nullptr) == 1;
            ok = ok && EVP_CIPHER_CTX_set_padding(ctx, 1) == 1;
            out.assign(cipher.size(), '\0');
            int written = 0, tail = 0;
            if (ok)
                ok = EVP_DecryptUpdate(ctx, (unsigned char *)out.data(), &written,
                                       (const unsigned char *)cipher.data(), (int)cipher.size()) == 1;
            if (ok)
                ok = EVP_DecryptFinal_ex(ctx, (unsigned char *)out.data() + written, &tail) == 1;
            EVP_CIPHER_CTX_free(ctx);
            if (!ok)
            {
                out.clear();
                err = "AES-128-ECB 解密失败";
                return false;
            }
            out.resize((size_t)written + (size_t)tail);
            return true;
#else
            (void)key16; (void)cipher; (void)out;
            err = "这个构建没有链 OpenSSL，iLink 媒体不可用";
            return false;
#endif
        }
    } // namespace crypto

    // =====================================================================
    //  地址
    // =====================================================================
    inline std::string api_root(const channel &ch)
    {
        std::string def = ch.kind == "onebot" ? "http://127.0.0.1:3000"
                          : ch.kind == "wecom" ? "https://qyapi.weixin.qq.com"
                                               : "https://api.telegram.org";
        return detail::trim_slash(ch.api_base.empty() ? def : ch.api_base);
    }
    // 注意：返回的 URL 里带着 bot token，调用方用完要 wipe
    inline std::string tg_url(const channel &ch, const std::string &method)
    {
        return api_root(ch) + "/bot" + ch.token.str() + "/" + method;
    }

    // =====================================================================
    //  Telegram
    // =====================================================================
    struct identity
    {
        long long id = 0;
        std::string username;
    };

    inline bool parse_telegram(const nlohmann::json &msg, const identity &self, inbound &m)
    {
        try
        {
            if (!msg.is_object())
                return false;
        auto chat = msg.find("chat");
        if (chat == msg.end() || !chat->is_object())
            return false;
        auto from = msg.find("from");
        if (from != msg.end() && from->is_object() && from->value("is_bot", false))
            return false;

        // 文字：text 或 caption（图片 / 视频带说明时文字在 caption 里）
        std::string body;
        for (const char *k : {"text", "caption"})
            if (auto it = msg.find(k); it != msg.end() && it->is_string())
            {
                body = it->get<std::string>();
                break;
            }

        // 媒体：Telegram 把每一类放在各自的键里，图片是 sizes 数组（挑最大的）
        auto push = [&](media_ref r)
        {
            if (!r.name.empty() || !r.file_id.empty() || !r.url.empty())
                m.media.push_back(std::move(r));
        };
        if (auto it = msg.find("photo"); it != msg.end() && it->is_array() && !it->empty())
        {
            const auto &sizes = *it;
            const auto &big = sizes.back().is_object() ? sizes.back() : sizes.front();
            media_ref r;
            r.kind = "image";
            r.file_id = big.value("file_id", std::string());
            r.size = big.value("file_size", 0LL);
            r.name = "photo.jpg";
            r.mime = "image/jpeg";
            r.raw = *it; // 下载兜底 / 尺寸信息
            push(std::move(r));
        }
        for (const char *k : {"document", "video", "animation", "voice", "audio", "video_note"})
        {
            auto it = msg.find(k);
            if (it == msg.end() || !it->is_object())
                continue;
            const bool is_voice = std::string_view(k) == "voice";
            const bool is_video = std::string_view(k) == "video" || std::string_view(k) == "video_note";
            media_ref r;
            r.kind = is_voice ? "audio" : (is_video ? "video" : "file");
            r.file_id = it->value("file_id", std::string());
            r.size = it->value("file_size", 0LL);
            r.name = it->value("file_name", std::string());
            if (r.name.empty())
                r.name = is_voice ? "voice.ogg" : (is_video ? "video.mp4" : "file.bin");
            r.mime = it->value("mime_type", std::string());
            if (r.mime.empty())
                r.mime = is_voice ? "audio/ogg" : (is_video ? "video/mp4" : "");
            r.file_name = r.name;
            r.raw = *it;
            push(std::move(r));
        }
        if (auto it = msg.find("sticker"); it != msg.end() && it->is_object())
        {
            media_ref r;
            r.kind = "sticker";
            r.file_id = it->value("file_id", std::string());
            r.size = it->value("file_size", 0LL);
            r.mime = it->value("is_video", false) ? "video/webm" : "image/webp";
            r.name = it->value("emoji", std::string("sticker"));
            if (auto set = it->value("set_name", std::string()); !set.empty())
                r.name += "@" + set;
            else
                r.name = "sticker";
            r.raw = *it;
            push(std::move(r));
        }
        auto id_of = [](const nlohmann::json &o) -> std::string
        {
            if (!o.is_object())
                return {};
            auto it = o.find("id");
            return it == o.end() ? std::string() : detail::id_str(*it);
        };
        m.chat = id_of(*chat);
        m.user = (from != msg.end() && from->is_object()) ? id_of(*from) : m.chat;
        if (from != msg.end() && from->is_object())
            m.name = from->value("username", from->value("first_name", std::string()));
        if (auto id = msg.find("message_id"); id != msg.end())
            m.msg_id = detail::id_str(*id);

        m.group = chat->value("type", "private") != "private";
        if (!m.group)
            m.mention = true;
        else if (!self.username.empty())
        {
            std::string at = "@" + self.username;
            if (body.find(at) != std::string::npos)
            {
                m.mention = true;
                detail::replace_all(body, at, " ");
            }
            else if (auto rt = msg.find("reply_to_message"); rt != msg.end() && rt->is_object())
            {
                auto rf = rt->find("from");
                if (rf != rt->end() && rf->is_object() && self.id != 0 &&
                    id_of(*rf) == std::to_string(self.id))
                    m.mention = true;
            }
        }
            m.text = cell::text::trim(body);
            return !m.text.empty() || !m.media.empty();
        }
        catch (...)
        {
            return false; // 契约 3.3：解析函数不抛异常
        }
    }

    inline bool poll_telegram(const channel &ch, long long &offset, const identity &self,
                              std::vector<inbound> &out, std::string &err,
                              const std::atomic<bool> *abort = nullptr)
    {
        if (ch.token.empty())
        {
            err = "缺少 bot token";
            return false;
        }
        // 长轮询：服务端挂 25 秒才返回。中止探针在传输里，退出/换配置不会卡住。
        std::string url = tg_url(ch, "getUpdates") + "?timeout=25&allowed_updates=" +
                          detail::url_encode("[\"message\"]");
        if (offset > 0)
            url += "&offset=" + std::to_string(offset);
        auto r = detail::call(ch, url, "", {}, 30, abort);
        wipe(url); // URL 里有 token
        if (!r.ok)
        {
            err = r.err;
            return false;
        }
        auto j = nlohmann::json::parse(r.body, nullptr, false);
        if (!j.is_object())
        {
            err = "返回不是 JSON";
            return false;
        }
        if (!j.value("ok", false))
        {
            err = j.value("description", std::string("Telegram 拒绝了请求"));
            return false;
        }
        auto it = j.find("result");
        if (it == j.end() || !it->is_array())
            return true;
        for (auto &u : *it)
        {
            if (!u.is_object())
                continue;
            if (u.contains("update_id") && u["update_id"].is_number_integer())
                offset = std::max(offset, u["update_id"].get<long long>() + 1);
            auto mi = u.find("message");
            if (mi == u.end())
                continue;
            inbound m;
            if (parse_telegram(*mi, self, m))
            {
                m.chan = ch.id;
                out.push_back(std::move(m));
            }
        }
        return true;
    }

    inline bool probe_telegram(const channel &ch, identity &self, std::string &err,
                               const std::atomic<bool> *abort = nullptr)
    {
        if (ch.token.empty())
        {
            err = "缺少 bot token";
            return false;
        }
        std::string url = tg_url(ch, "getMe");
        auto r = detail::call(ch, url, "", {}, 20, abort);
        wipe(url);
        if (!r.ok)
        {
            err = r.err;
            return false;
        }
        auto j = nlohmann::json::parse(r.body, nullptr, false);
        if (!j.is_object() || !j.value("ok", false))
        {
            err = j.is_object() ? j.value("description", std::string("getMe 失败")) : "返回不是 JSON";
            return false;
        }
        auto it = j.find("result");
        if (it == j.end() || !it->is_object())
        {
            err = "getMe 没有 result";
            return false;
        }
        self.id = it->value("id", 0LL);
        self.username = it->value("username", std::string());
        return true;
    }

    // ---- 媒体：getFile 拿 file_path，再 GET /file/bot<token>/<file_path> ----
    inline bool tg_file_path(const channel &ch, const std::string &file_id, std::string &path,
                             std::string &err, const std::atomic<bool> *abort = nullptr)
    {
        if (file_id.empty())
        {
            err = "Telegram 这条消息没有 file_id";
            return false;
        }
        nlohmann::json body = {{"file_id", file_id}};
        std::string url = tg_url(ch, "getFile"); // URL 里带 token
        detail::zero_guard url_guard{url};
        auto r = detail::call(ch, url, body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                              {}, 20, abort);
        if (!r.ok)
        {
            err = r.err;
            return false;
        }
        auto j = nlohmann::json::parse(r.body, nullptr, false);
        if (!j.is_object() || !j.value("ok", false))
        {
            err = j.is_object() ? j.value("description", std::string("getFile 失败")) : "getFile 返回不是 JSON";
            return false;
        }
        auto it = j.find("result");
        if (it == j.end() || !it->is_object())
        {
            err = "getFile 没有 result";
            return false;
        }
        path = it->value("file_path", std::string());
        if (path.empty())
        {
            err = "getFile 没有返回 file_path";
            return false;
        }
        return true;
    }

    inline bool tg_fetch(const channel &ch, const media_ref &ref, std::string &bytes,
                         std::string &name, std::string &mime, std::string &err,
                         const std::atomic<bool> *abort)
    {
        std::string path;
        if (!tg_file_path(ch, ref.file_id, path, err, abort))
            return false;
        std::string url = api_root(ch) + "/file/bot" + ch.token.str() + "/" + path;
        detail::zero_guard url_guard{url}; // 里面有 token
        auto r = detail::call(ch, url, "", {}, 120, abort);
        if (!r.ok)
        {
            err = r.err;
            return false;
        }
        if (r.body.empty())
        {
            err = "Telegram 返回的文件是空的";
            return false;
        }
        bytes = std::move(r.body);
        const size_t slash = path.find_last_of('/');
        const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
        if (name.empty())
            name = base;
        if (detail::file_ext(name).empty() && !detail::file_ext(base).empty())
            name += "." + detail::file_ext(base);
        if (mime.empty())
            mime = detail::mime_for_name(name.empty() ? base : name);
        return true;
    }

    inline bool send_telegram_media(const channel &ch, const std::string &chat, const std::string &kind,
                                    const std::string &path, const std::string &name,
                                    const std::string &mime, std::string &err,
                                    const std::atomic<bool> *abort = nullptr)
    {
        if (ch.token.empty())
        {
            err = "缺少 bot token";
            return false;
        }
        const std::string ext = detail::file_ext(name.empty() ? path : name);
        const std::string low_mime = detail::lower_ascii(mime);
        std::string method, field;
        if (kind == "image")
        {
            method = "sendPhoto";
            field = "photo";
        }
        else if (kind == "sticker")
        {
            if (ext == "webp" || ext == "tgs" || low_mime.starts_with("image/webp") ||
                low_mime == "application/x-tgs")
            {
                method = "sendSticker";
                field = "sticker";
            }
            else
            {
                method = "sendPhoto";
                field = "photo";
            }
        }
        else if (kind == "audio")
        {
            if (ext == "ogg" || ext == "opus" || low_mime.starts_with("audio/ogg"))
            {
                method = "sendVoice";
                field = "voice";
            }
            else
            {
                method = "sendAudio";
                field = "audio";
            }
        }
        else if (kind == "video")
        {
            method = "sendVideo";
            field = "video";
        }
        else if (kind == "file")
        {
            method = "sendDocument";
            field = "document";
        }
        else
        {
            err = "Telegram 不支持发这种媒体：" + kind;
            return false;
        }
        std::string bytes;
        if (!detail::read_file(path, bytes))
        {
            err = "读不到本地文件：" + path;
            return false;
        }
        // 自己拼 multipart：boundary 随机，文件字段名按方法定
        const std::string boundary = "----agentim" + detail::random_hex(12);
        std::string body;
        body.reserve(bytes.size() + 1024);
        auto add_field = [&](const std::string &k, const std::string &v)
        {
            body += "--" + boundary + "\r\n";
            body += "Content-Disposition: form-data; name=\"" + k + "\"\r\n\r\n";
            body += v + "\r\n";
        };
        add_field("chat_id", chat);
        add_field("disable_notification", "false");
        std::string filename = name.empty() ? detail::default_name_for(kind) : name;
        for (auto &c : filename)
            if (c == '"' || c == '\r' || c == '\n')
                c = '_';
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"" + field + "\"; filename=\"" + filename + "\"\r\n";
        body += "Content-Type: " + (mime.empty() ? std::string("application/octet-stream") : mime) + "\r\n\r\n";
        body += bytes;
        body += "\r\n--" + boundary + "--\r\n";

        detail::headers hdrs;
        hdrs.push_back("Content-Type: multipart/form-data; boundary=" + boundary);
        std::string url = tg_url(ch, method);
        detail::zero_guard url_guard{url};
        auto r = detail::call(ch, url, body, hdrs, 120, abort);
        auto j = nlohmann::json::parse(r.body, nullptr, false);
        if (!r.ok)
        {
            err = j.is_object() ? j.value("description", r.err) : r.err;
            return false;
        }
        if (!j.is_object() || !j.value("ok", false))
        {
            err = j.is_object() ? j.value("description", std::string("Telegram 拒绝了媒体")) : "返回不是 JSON";
            return false;
        }
        return true;
    }

    // =====================================================================
    //  QQ —— OneBot v11（NapCat / go-cqhttp / Lagrange）
    // =====================================================================
    // ---- CQ 码字符串 → segment 数组 ----
    // message 既可能是 array 也可能是 "[CQ:at,qq=1]你好" 这种字符串，统一成 array 再处理。
    inline std::string cq_unescape(std::string s)
    {
        // 顺序要紧：先解 &#44; 之类，最后才解 &amp;（否则 &amp;#44; 会被二次解码）
        detail::replace_all(s, "&#91;", "[");
        detail::replace_all(s, "&#93;", "]");
        detail::replace_all(s, "&#44;", ",");
        detail::replace_all(s, "&amp;", "&");
        return s;
    }
    inline nlohmann::json cq_to_message(const std::string &s)
    {
        nlohmann::json arr = nlohmann::json::array();
        std::string text;
        auto flush = [&]()
        {
            if (!text.empty())
            {
                arr.push_back({{"type", "text"}, {"data", {{"text", text}}}});
                text.clear();
            }
        };
        size_t i = 0;
        while (i < s.size())
        {
            const size_t open = s.find("[CQ:", i);
            if (open == std::string::npos)
            {
                text += s.substr(i);
                break;
            }
            text += s.substr(i, open - i);
            const size_t close = s.find(']', open + 4);
            if (close == std::string::npos)
            {
                text += s.substr(open);
                break;
            }
            flush();
            const std::string inner = s.substr(open + 4, close - open - 4);
            const size_t comma = inner.find(',');
            const std::string type = cq_unescape(inner.substr(0, comma));
            nlohmann::json data = nlohmann::json::object();
            if (comma != std::string::npos)
            {
                size_t p = comma + 1;
                while (p <= inner.size())
                {
                    const size_t c = inner.find(',', p);
                    const std::string kv = inner.substr(p, c == std::string::npos ? std::string::npos : c - p);
                    const size_t eq = kv.find('=');
                    if (eq != std::string::npos)
                        data[cq_unescape(kv.substr(0, eq))] = cq_unescape(kv.substr(eq + 1));
                    if (c == std::string::npos)
                        break;
                    p = c + 1;
                }
            }
            arr.push_back({{"type", type}, {"data", data}});
            i = close + 1;
        }
        flush();
        return arr;
    }

    // QQ 系统表情 → emoji：不落盘，但正文不能因为一个表情就丢
    inline std::string qq_face_text(const std::string &id)
    {
        static const std::map<std::string, std::string> m = {
            {"0", "😮"}, {"1", "😐"}, {"2", "😃"}, {"3", "😄"}, {"4", "😁"}, {"5", "😭"},
            {"6", "😆"}, {"7", "😅"}, {"8", "😧"}, {"9", "😮"}, {"10", "😲"}, {"11", "😡"},
            {"12", "😱"}, {"13", "😳"}, {"14", "🙂"}, {"15", "😉"}, {"16", "😊"},
            {"17", "😍"}, {"18", "😘"}, {"19", "😜"}, {"20", "😝"}, {"21", "😔"},
            {"22", "😟"}, {"23", "😣"}, {"24", "😖"}, {"25", "😫"}, {"26", "😤"},
            {"27", "😠"}, {"28", "😩"}, {"29", "😨"}, {"30", "😰"}, {"31", "😥"},
            {"32", "😢"}, {"33", "😪"}, {"34", "😓"}, {"35", "😏"}, {"36", "😒"},
            {"37", "😌"}, {"38", "😞"}, {"178", "🤔"}, {"179", "😇"}, {"180", "🤩"},
            {"181", "🤨"}, {"182", "😀"}, {"183", "😎"}, {"184", "🥳"}, {"185", "😴"},
        };
        auto it = m.find(id);
        return it == m.end() ? "[face:" + id + "]" : it->second;
    }

    namespace detail
    {
        inline std::string qq_raw(const nlohmann::json &data, const char *k)
        {
            auto it = data.find(k);
            if (it == data.end())
                return {};
            if (it->is_string())
                return it->get<std::string>();
            return id_str(*it);
        }
        struct qq_ctx
        {
            std::string &text;
            std::vector<media_ref> &media;
            bool &mention;
            std::string &reply;
            long long self = 0;
        };
    } // namespace detail

    inline void onebot_take_segment(const nlohmann::json &seg, detail::qq_ctx &c)
    {
        using detail::qq_raw;
        if (!seg.is_object())
            return;
        const std::string type = seg.value("type", "");
        const nlohmann::json data = (seg.contains("data") && seg["data"].is_object())
                                        ? seg["data"]
                                        : nlohmann::json::object();
        if (type == "text")
        {
            c.text += qq_raw(data, "text");
        }
        else if (type == "at")
        {
            const std::string qq = qq_raw(data, "qq");
            if (qq == "all")
                c.text += "@全体成员";
            else if (!qq.empty() && c.self != 0 && qq == std::to_string(c.self))
                c.mention = true; // 群里 @ 了机器人：去掉、只记 mention
            else if (!qq.empty())
            {
                std::string nick = qq_raw(data, "name");
                c.text += "@" + (nick.empty() ? qq : nick);
            }
        }
        else if (type == "image")
        {
            media_ref r;
            r.kind = "image";
            r.url = qq_raw(data, "url");
            r.file_id = qq_raw(data, "file");
            r.file_name = r.file_id;
            const std::string b64 = qq_raw(data, "base64");
            if (!b64.empty())
                r.data = "base64://" + b64;
            const std::string sub = qq_raw(data, "sub_type");
            const std::string summary = qq_raw(data, "summary");
            if (sub == "1" || summary.find("表情") != std::string::npos)
                r.kind = "sticker"; // 收藏表情 / 表情包
            r.name = r.file_id.empty() ? (r.kind == "sticker" ? "sticker.webp" : "photo.jpg") : r.file_id;
            r.mime = detail::mime_for_name(r.name);
            r.raw = seg;
            if (!r.url.empty() || !r.data.empty() || !r.file_id.empty())
                c.media.push_back(std::move(r));
        }
        else if (type == "mface")
        {
            media_ref r;
            r.kind = "sticker";
            r.url = qq_raw(data, "url");
            r.id = qq_raw(data, "emoji_id");
            r.name = qq_raw(data, "summary");
            if (r.name.empty())
                r.name = "sticker";
            r.mime = detail::mime_for_name(r.url.empty() ? r.name : r.url);
            const std::string b64 = qq_raw(data, "base64");
            if (!b64.empty())
                r.data = "base64://" + b64;
            r.raw = seg;
            if (!r.url.empty() || !r.data.empty())
                c.media.push_back(std::move(r));
            else if (!r.name.empty())
                c.text += "[" + r.name + "]";
        }
        else if (type == "face")
        {
            c.text += qq_face_text(qq_raw(data, "id"));
        }
        else if (type == "record")
        {
            media_ref r;
            r.kind = "audio";
            r.url = qq_raw(data, "url");
            r.file_id = qq_raw(data, "file");
            r.file_name = r.file_id;
            const std::string b64 = qq_raw(data, "base64");
            if (!b64.empty())
                r.data = "base64://" + b64;
            r.name = r.file_id.empty() ? "voice.ogg" : r.file_id;
            r.mime = detail::mime_for_name(r.name);
            r.raw = seg;
            if (!r.url.empty() || !r.data.empty() || !r.file_id.empty())
                c.media.push_back(std::move(r));
        }
        else if (type == "video")
        {
            media_ref r;
            r.kind = "video";
            r.url = qq_raw(data, "url");
            r.file_id = qq_raw(data, "file");
            r.file_name = r.file_id;
            const std::string b64 = qq_raw(data, "base64");
            if (!b64.empty())
                r.data = "base64://" + b64;
            r.name = r.file_id.empty() ? "video.mp4" : r.file_id;
            r.mime = detail::mime_for_name(r.name);
            r.raw = seg;
            if (!r.url.empty() || !r.data.empty() || !r.file_id.empty())
                c.media.push_back(std::move(r));
        }
        else if (type == "file")
        {
            media_ref r;
            r.kind = "file";
            r.url = qq_raw(data, "url");
            r.file_id = qq_raw(data, "file");
            r.file_name = qq_raw(data, "file_name");
            r.name = r.file_name.empty() ? (qq_raw(data, "name").empty() ? "file.bin" : qq_raw(data, "name"))
                                         : r.file_name;
            if (r.file_name.empty())
                r.file_name = r.name;
            r.size = (long long)data.value("file_size", 0LL);
            r.mime = detail::mime_for_name(r.name);
            r.raw = seg;
            c.media.push_back(std::move(r));
        }
        else if (type == "reply")
        {
            c.reply = qq_raw(data, "id");
        }
        else if (type == "forward")
        {
            auto it = data.find("content");
            if (it == data.end())
                return;
            if (it->is_array())
            {
                for (const auto &sub : *it)
                {
                    if (sub.is_object() && sub.contains("message") && sub["message"].is_array())
                        for (const auto &m2 : sub["message"])
                            onebot_take_segment(m2, c);
                    else
                        onebot_take_segment(sub, c);
                }
            }
            else if (it->is_string())
            {
                c.text += it->get<std::string>();
            }
        }
        else if (type == "json" || type == "xml")
        {
            c.text += "[卡片]";
        }
        else if (type == "dice")
        {
            const std::string v = qq_raw(data, "result");
            c.text += "[骰子:" + (v.empty() ? std::string("?") : v) + "]";
        }
        else if (type == "rps")
        {
            const std::string v = qq_raw(data, "result");
            c.text += "[猜拳:" + (v.empty() ? std::string("?") : v) + "]";
        }
        else if (type == "poke")
        {
            c.text += "（戳了戳你）";
            c.mention = true;
        }
        // 其它未知段忽略（不因为一个不认识的段把整条消息丢掉）
    }

    inline bool parse_onebot(const nlohmann::json &ev, inbound &m)
    {
        try
        {
            if (!ev.is_object())
                return false;
        const long long self = ev.value("self_id", 0LL);
        const std::string post = ev.value("post_type", "");
        // 戳一戳是 notice 不是 message：给 AI 一个反应的机会
        if (post == "notice")
        {
            if (ev.value("notice_type", "") != "notify" || ev.value("sub_type", "") != "poke")
                return false;
            const std::string target = ev.contains("target_id") ? detail::id_str(ev["target_id"]) : std::string();
            if (self != 0 && !target.empty() && target != std::to_string(self))
                return false; // 戳的是别人
            m.group = ev.contains("group_id") && !ev["group_id"].is_null();
            m.chat = m.group ? detail::id_str(ev["group_id"])
                             : detail::id_str(ev.contains("user_id") ? ev["user_id"] : nlohmann::json());
            m.user = ev.contains("user_id") ? detail::id_str(ev["user_id"]) : m.chat;
            if (auto sid = ev.find("sender"); sid != ev.end() && sid->is_object())
                m.name = sid->value("card", sid->value("nickname", std::string()));
            m.text = "（戳了戳你）";
            m.mention = true;
            return !m.chat.empty();
        }
        if (post != "message")
            return false;
        const std::string type = ev.value("message_type", "");
        if (type != "private" && type != "group")
            return false;
        const long long uid = ev.value("user_id", 0LL);
        if (self != 0 && uid == self)
            return false; // 自己发的（比如别的端）
        if (auto sid = ev.find("sender"); sid != ev.end() && sid->is_object() && sid->value("bot", false))
            return false;

        m.group = type == "group";
        m.chat = m.group ? detail::id_str(ev.contains("group_id") ? ev["group_id"] : nlohmann::json())
                         : detail::id_str(ev.contains("user_id") ? ev["user_id"] : nlohmann::json());
        m.user = detail::id_str(ev.contains("user_id") ? ev["user_id"] : nlohmann::json());
        if (auto sid = ev.find("sender"); sid != ev.end() && sid->is_object())
            m.name = sid->value("card", sid->value("nickname", std::string()));
        if (auto id = ev.find("message_id"); id != ev.end())
            m.msg_id = detail::id_str(*id);
        m.mention = !m.group;

        nlohmann::json segs = nlohmann::json::array();
        auto msg = ev.find("message");
        if (msg != ev.end() && msg->is_array())
            segs = *msg;
        else if (msg != ev.end() && msg->is_string())
            segs = cq_to_message(msg->get<std::string>());
        else if (auto raw = ev.find("raw_message"); raw != ev.end() && raw->is_string())
            segs = cq_to_message(raw->get<std::string>());

        std::string text, reply;
        std::vector<media_ref> media;
        detail::qq_ctx ctx{text, media, m.mention, reply, self};
        for (const auto &seg : segs)
            onebot_take_segment(seg, ctx);
        text = cell::text::trim(text);
        if (!reply.empty())
            text = "（回复 " + reply + "）" + text;
            m.text = std::move(text);
            m.media = std::move(media);
            return !m.text.empty() || !m.media.empty();
        }
        catch (...)
        {
            return false; // 契约 3.3：解析函数不抛异常
        }
    }

    // ---- 调 OneBot 的 HTTP API ----
    inline bool onebot_api(const channel &ch, const std::string &action, const nlohmann::json &body,
                           nlohmann::json *data_out, std::string &err,
                           const std::atomic<bool> *abort = nullptr)
    {
        detail::headers hdrs;
        if (!ch.token.empty())
            hdrs.push_back("Authorization: Bearer " + ch.token.str());
        auto r = detail::call(ch, api_root(ch) + "/" + action,
                              body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                              hdrs, 60, abort);
        if (!r.ok)
        {
            err = r.err;
            return false;
        }
        auto j = nlohmann::json::parse(r.body, nullptr, false);
        if (!j.is_object())
        {
            err = "OneBot 返回不是 JSON";
            return false;
        }
        if (j.contains("retcode") && j["retcode"].is_number_integer() &&
            j["retcode"].get<long long>() != 0)
        {
            err = j.value("wording", j.value("message", std::string("OneBot 返回失败")));
            return false;
        }
        if (data_out)
            *data_out = j.contains("data") ? j["data"] : nlohmann::json::object();
        return true;
    }

    // 报文形状只有一份：send_* 与 send_media 都走这里
    inline bool send_onebot_segments(const channel &ch, const std::string &chat, bool group,
                                     const nlohmann::json &segments, std::string &err,
                                     const std::atomic<bool> *abort = nullptr)
    {
        nlohmann::json body;
        std::string action;
        if (group)
        {
            action = "send_group_msg";
            body["group_id"] = detail::id_json(chat);
        }
        else
        {
            action = "send_private_msg";
            body["user_id"] = detail::id_json(chat);
        }
        body["message"] = segments;
        return onebot_api(ch, action, body, nullptr, err, abort);
    }

    // 兼容旧调用：纯文本 = 一个 text 段
    inline bool send_onebot(const channel &ch, const std::string &chat, bool group,
                            const std::string &text, std::string &err, const std::atomic<bool> *abort = nullptr)
    {
        return send_onebot_segments(ch, chat, group,
                                    nlohmann::json::array({{{"type", "text"}, {"data", {{"text", text}}}}}),
                                    err, abort);
    }

    // ---- 入站媒体下载：base64:// / http url / 本地绝对路径 / 对端 API 兜底 ----
    inline bool onebot_fetch(const channel &ch, const media_ref &ref, std::string &bytes,
                             std::string &name, std::string &mime, std::string &err,
                             const std::atomic<bool> *abort)
    {
        auto finish = [&]() -> bool
        {
            if (bytes.empty())
                return false;
            if (name.empty())
                name = ref.file_name.empty() ? detail::default_name_for(ref.kind) : ref.file_name;
            if (mime.empty())
                mime = detail::mime_for_name(name);
            if (detail::file_ext(name).empty())
                name += "." + detail::ext_for_mime(mime);
            return true;
        };
        // 1) 内联 base64
        if (!ref.data.empty())
        {
            std::string b64 = ref.data;
            if (b64.starts_with("base64://"))
                b64.erase(0, 9);
            bytes = detail::b64_decode(b64);
            if (finish())
                return true;
        }
        // 2) http(s) 直链
        if (ref.url.starts_with("http://") || ref.url.starts_with("https://"))
        {
            std::string derr;
            std::string tmp;
            if (detail::download(ch, ref.url, tmp, derr, abort))
            {
                bytes = std::move(tmp);
                if (finish())
                    return true;
            }
        }
        const std::string key = !ref.file_id.empty() ? ref.file_id
                                : !ref.file_name.empty() ? ref.file_name
                                                         : std::string();
        // 3) file 看着像本地绝对路径（NapCat 与 bot 同机时就是这样）
        if (detail::looks_abs_path(key))
        {
            std::string tmp;
            if (detail::read_file(key, tmp))
            {
                bytes = std::move(tmp);
                if (finish())
                    return true;
            }
        }
        // 4) 调对端 API 兜底
        auto take = [&](const nlohmann::json &d) -> bool
        {
            if (!d.is_object())
                return false;
            std::string tmp;
            std::string f = d.contains("file") ? detail::id_str(d["file"]) : std::string();
            if (detail::looks_abs_path(f) && detail::read_file(f, tmp))
            {
                bytes = std::move(tmp);
                return true;
            }
            std::string b64 = d.contains("base64") ? detail::id_str(d["base64"]) : std::string();
            if (!b64.empty())
            {
                tmp = detail::b64_decode(b64);
                if (!tmp.empty())
                {
                    bytes = std::move(tmp);
                    return true;
                }
            }
            if (!f.empty() && !detail::looks_abs_path(f))
            {
                tmp = detail::b64_decode(f); // 少数实现对 file 直接塞 base64
                if (!tmp.empty())
                {
                    bytes = std::move(tmp);
                    return true;
                }
            }
            std::string u = d.contains("url") ? detail::id_str(d["url"]) : std::string();
            if (!u.starts_with("http://") && !u.starts_with("https://"))
                return false;
            std::string derr;
            return detail::download(ch, u, bytes, derr, abort);
        };
        if (!key.empty())
        {
            std::string last;
            for (const char *field : {"file", "file_id"})
            {
                nlohmann::json params = {{field, detail::id_json(key)}};
                nlohmann::json d;
                std::string e2;
                const char *action = ref.kind == "image" ? "get_image"
                                     : ref.kind == "audio" ? "get_record"
                                                           : "get_file";
                if (onebot_api(ch, action, params, &d, e2, abort) && take(d))
                {
                    if (finish())
                        return true;
                }
                last = e2;
            }
            if (!last.empty())
                err = last;
        }
        err = err.empty() ? "OneBot 拿不到这段媒体（没有可用的 base64 / url / 本地路径 / 对端接口）" : err;
        return false;
    }

    // ---- 出站媒体 ----
    inline bool onebot_send_media(const channel &ch, const std::string &chat, bool group,
                                  const std::string &kind, const std::string &path,
                                  const std::string &name, const std::string &mime,
                                  std::string &err, const std::atomic<bool> *abort)
    {
        (void)mime;
        if (kind == "image" || kind == "sticker")
        {
            // 先发本地绝对路径（NapCat 同机可读），失败再退回 base64
            nlohmann::json seg = {{"type", "image"},
                                  {"data", {{"file", path}, {"sub_type", kind == "sticker" ? 1 : 0}}}};
            if (!name.empty())
                seg["data"]["name"] = name;
            if (send_onebot_segments(ch, chat, group, nlohmann::json::array({seg}), err, abort))
                return true;
            std::string bytes;
            if (!detail::read_file(path, bytes))
                return false; // err 已经是第一次发送的失败原因
            nlohmann::json retry = {{"type", "image"},
                                    {"data", {{"file", "base64://" + detail::b64_encode(bytes)},
                                              {"sub_type", kind == "sticker" ? 1 : 0}}}};
            if (!name.empty())
                retry["data"]["name"] = name;
            std::string err2;
            if (send_onebot_segments(ch, chat, group, nlohmann::json::array({retry}), err2, abort))
                return true;
            err = std::move(err2);
            return false;
        }
        nlohmann::json seg;
        if (kind == "audio")
            seg = {{"type", "record"}, {"data", {{"file", path}}}};
        else if (kind == "video")
            seg = {{"type", "video"}, {"data", {{"file", path}}}};
        else if (kind == "file")
            seg = {{"type", "file"}, {"data", {{"file", path}, {"name", name}}}};
        else
        {
            err = "QQ 不支持发这种媒体：" + kind;
            return false;
        }
        return send_onebot_segments(ch, chat, group, nlohmann::json::array({seg}), err, abort);
    }

    // =====================================================================
    //  企业微信自建应用
    // =====================================================================
    namespace detail
    {
        inline std::mutex &token_mx()
        {
            static std::mutex m;
            return m;
        }
        inline std::map<std::string, std::pair<std::string, long long>> &token_map()
        {
            static std::map<std::string, std::pair<std::string, long long>> m;
            return m;
        }
        inline std::string xml_text(const std::string &xml, const char *tag)
        {
            const std::string open = std::string("<") + tag + ">";
            const std::string close = std::string("</") + tag + ">";
            size_t a = xml.find(open);
            if (a == std::string::npos)
                return {};
            a += open.size();
            size_t b = xml.find(close, a);
            if (b == std::string::npos)
                return {};
            std::string v = xml.substr(a, b - a);
            if (v.rfind("<![CDATA[", 0) == 0 && v.size() >= 12 && v.ends_with("]]>"))
                v = v.substr(9, v.size() - 12);
            return cell::text::trim(v);
        }
    } // namespace detail

    inline std::string wecom_root(const channel &ch)
    {
        return ch.api_base.empty() ? "https://qyapi.weixin.qq.com" : detail::trim_slash(ch.api_base);
    }

    // 企业微信的签名：token / timestamp / nonce / 密文 四个字符串字典序排序后
    // 拼起来算 SHA1。这是它唯一的鉴权手段，所以每一步都不能省。
    inline bool wecom_sign_ok(const channel &ch, const std::string &sig,
                              const std::string &timestamp, const std::string &nonce,
                              const std::string &payload)
    {
        if (ch.token.empty() || sig.empty())
            return false;
        std::vector<std::string> parts = {ch.token.str(), timestamp, nonce, payload};
        std::sort(parts.begin(), parts.end());
        std::string joined;
        for (auto &p : parts)
            joined += p;
        std::string want = crypto::sha1_hex(joined);
        wipe(joined);
        for (auto &p : parts)
            cell::encrypt::wipe(p); // 里面有 token 的副本
        if (want.empty() || want.size() != sig.size())
            return false;
        return sodium_memcmp(want.data(), sig.data(), want.size()) == 0; // 常数时间
    }

    // 解开一层：Base64 → AES-256-CBC（key = Base64Decode(EncodingAESKey + "=")，iv = key 前 16 字节）
    // → 去 PKCS7 → [16 随机][4 字节长度][正文][receiveid]
    inline bool wecom_decrypt(const channel &ch, const std::string &b64, std::string &plain,
                              std::string &receive_id, std::string &err)
    {
        if constexpr (!crypto::kAvailable)
        {
            err = "这个构建没有链 OpenSSL，企业微信不可用（见 README「IM 接入」）";
            return false;
        }
        if (ch.aes_key.size() != 43)
        {
            err = "EncodingAESKey 应该是 43 位";
            return false;
        }
        std::string key = cell::encrypt::b64_decode(ch.aes_key.str() + "=");
        detail::zero_guard key_guard{key}; // 32 字节的 AES 密钥，出函数前擦掉
        if (key.size() != 32)
        {
            err = "EncodingAESKey 解出来不是 32 字节";
            return false;
        }
        std::string ct = cell::encrypt::b64_decode(b64);
        if (ct.empty() || ct.size() % 16 != 0)
        {
            err = "密文长度不对";
            return false;
        }
        std::string raw;
        if (!crypto::aes256_cbc_decrypt(key, std::string(key.begin(), key.begin() + 16), ct, raw))
        {
            err = "AES 解密失败";
            return false;
        }
        if (raw.empty())
        {
            err = "解密结果为空";
            return false;
        }
        size_t pad = (unsigned char)raw.back();
        if (pad < 1 || pad > 32 || pad > raw.size())
        {
            err = "填充不合法";
            return false;
        }
        raw.resize(raw.size() - pad);
        if (raw.size() < 20)
        {
            err = "解密结果太短";
            return false;
        }
        uint32_t len = ((uint32_t)(unsigned char)raw[16] << 24) | ((uint32_t)(unsigned char)raw[17] << 16) |
                       ((uint32_t)(unsigned char)raw[18] << 8) | (uint32_t)(unsigned char)raw[19];
        if (len > raw.size() - 20)
        {
            err = "长度字段越界";
            return false;
        }
        plain = raw.substr(20, len);
        receive_id = raw.substr(20 + len);
        return true;
    }

    inline std::string wecom_access_token(const channel &ch, std::string &err, const std::atomic<bool> *abort = nullptr)
    {
        if (ch.corp_id.empty() || ch.corp_secret.empty())
        {
            err = "企业微信还缺企业 id / 应用 secret";
            return {};
        }
        // 缓存键只放非凭据字段：token_map 是进程级容器，别让 secret 常驻在里面
        const std::string key = ch.id + "|" + ch.corp_id;
        {
            std::lock_guard<std::mutex> lk(detail::token_mx());
            auto &m = detail::token_map();
            auto it = m.find(key);
            if (it != m.end() && it->second.second > std::time(nullptr) + 120)
                return it->second.first;
        }
        std::string url = wecom_root(ch) + "/cgi-bin/gettoken?corpid=" + detail::url_encode(ch.corp_id) +
                          "&corpsecret=" + detail::url_encode(ch.corp_secret.str());
        detail::zero_guard url_guard{url}; // URL 里带着 corpsecret
        auto r = detail::call(ch, url, "", {}, 20, abort);
        if (!r.ok)
        {
            err = r.err;
            return {};
        }
        auto j = nlohmann::json::parse(r.body, nullptr, false);
        if (!j.is_object() || j.value("errcode", 0) != 0)
        {
            err = j.is_object() ? j.value("errmsg", std::string("gettoken 失败")) : "gettoken 返回不是 JSON";
            return {};
        }
        std::string tok = j.value("access_token", "");
        if (tok.empty())
        {
            err = "gettoken 没有返回 access_token";
            return {};
        }
        long long exp = (long long)std::time(nullptr) + j.value("expires_in", 7200LL);
        {
            std::lock_guard<std::mutex> lk(detail::token_mx());
            detail::token_map()[key] = {tok, exp};
        }
        return tok;
    }

    namespace detail
    {
        // token 失效（42001 / 40014）时清缓存，下次自动重取
        inline void forget_wecom_token(const channel &ch)
        {
            std::lock_guard<std::mutex> lk(token_mx());
            token_map().erase(ch.id + "|" + ch.corp_id);
        }
    } // namespace detail

    inline bool send_wecom(const channel &ch, const std::string &to, const std::string &text,
                           std::string &err, const std::atomic<bool> *abort = nullptr)
    {
        long long agent = 0;
        try
        {
            agent = std::stoll(ch.agent_id);
        }
        catch (...)
        {
            err = "AgentId 必须是数字";
            return false;
        }
        std::string token = wecom_access_token(ch, err, abort);
        if (token.empty())
            return false;
        detail::zero_guard tok_guard{token};
        nlohmann::json body = {
            {"touser", to},
            {"msgtype", "text"},
            {"agentid", agent},
            {"text", {{"content", text}}},
            {"safe", 0},
        };
        std::string url = wecom_root(ch) + "/cgi-bin/message/send?access_token=" + detail::url_encode(token);
        detail::zero_guard send_url_guard{url};
        auto r = detail::call(ch, url, body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                              {}, 30, abort);
        if (!r.ok)
        {
            err = r.err;
            return false;
        }
        auto j = nlohmann::json::parse(r.body, nullptr, false);
        if (!j.is_object())
        {
            err = "message/send 返回不是 JSON";
            return false;
        }
        long long code = j.value("errcode", -1LL);
        if (code != 0)
        {
            err = std::format("{} ({})", j.value("errmsg", std::string("发送失败")), code);
            if (code == 42001 || code == 40014) // token 过期：下次重取
            {
                std::lock_guard<std::mutex> lk(detail::token_mx());
                detail::token_map().erase(ch.id + "|" + ch.corp_id);
            }
            return false;
        }
        return true;
    }

    inline bool parse_wecom(const std::string &plain, const channel &ch, inbound &m)
    {
        (void)ch;
        try
        {
        // 明文既可能是 XML 也可能是 JSON（企业微信后台可选），两种都认；
        // 统一先收成一份 raw JSON，后面的字段读取只有一套代码。
        nlohmann::json raw = nlohmann::json::object();
        if (!plain.empty() && plain.front() == '{')
        {
            auto j = nlohmann::json::parse(plain, nullptr, false);
            if (!j.is_object())
                return false;
            raw = std::move(j);
        }
        else
        {
            for (const char *k : {"FromUserName", "MsgType", "Content", "MsgId", "PicUrl", "MediaId",
                                  "Format", "FileName", "FileExt", "ThumbMediaId"})
                if (std::string v = detail::xml_text(plain, k); !v.empty())
                    raw[k] = std::move(v);
        }
        auto gs = [&](const char *k) -> std::string
        {
            auto it = raw.find(k);
            if (it == raw.end())
                return {};
            if (it->is_string())
                return it->get<std::string>();
            return detail::id_str(*it);
        };
        const std::string from = gs("FromUserName");
        const std::string type = gs("MsgType");
        const std::string content = gs("Content");
        const std::string media_id = gs("MediaId");
        if (from.empty() || type.empty())
            return false;
        m.chat = from;
        m.user = from;
        m.name = from;
        m.msg_id = gs("MsgId");
        m.group = false; // 自建应用只做 1:1；群聊要走「群机器人」，那是发不进来的
        m.mention = true;
        if (type == "text")
        {
            m.text = cell::text::trim(content);
        }
        else if (type == "image")
        {
            media_ref r;
            r.kind = "image";
            r.url = gs("PicUrl");
            r.file_id = media_id;
            r.media_id = media_id;
            r.name = "image.jpg";
            r.mime = "image/jpeg";
            r.raw = raw;
            if (!r.url.empty() || !media_id.empty())
                m.media.push_back(std::move(r));
        }
        else if (type == "voice")
        {
            media_ref r;
            r.kind = "audio";
            r.file_id = media_id;
            r.media_id = media_id;
            std::string ext = detail::lower_ascii(gs("Format"));
            if (ext.empty())
                ext = "amr";
            r.name = "voice." + ext;
            r.mime = detail::mime_for_name(r.name);
            r.raw = raw;
            if (!media_id.empty())
                m.media.push_back(std::move(r));
        }
        else if (type == "video")
        {
            media_ref r;
            r.kind = "video";
            r.file_id = media_id;
            r.media_id = media_id;
            r.name = "video.mp4";
            r.mime = "video/mp4";
            r.raw = raw; // ThumbMediaId 留在 raw 里
            if (!media_id.empty())
                m.media.push_back(std::move(r));
        }
        else if (type == "file")
        {
            media_ref r;
            r.kind = "file";
            r.file_id = media_id;
            r.media_id = media_id;
            r.name = gs("FileName");
            std::string ext = detail::lower_ascii(gs("FileExt"));
            if (!ext.empty() && !ext.starts_with("."))
                ext = "." + ext;
            if (r.name.empty())
                r.name = "file" + ext;
            else if (detail::file_ext(r.name).empty() && !ext.empty())
                r.name += ext;
            r.mime = detail::mime_for_name(r.name);
            r.raw = raw;
            if (!media_id.empty())
                m.media.push_back(std::move(r));
        }
        else
        {
            return false; // event / 其它类型不处理
        }
            return !m.text.empty() || !m.media.empty();
        }
        catch (...)
        {
            return false; // 契约 3.3：解析函数不抛异常
        }
    }

    // ---- 媒体下载：PicUrl 直链优先，否则 cgi-bin/media/get ----
    inline bool wecom_fetch(const channel &ch, const media_ref &ref, std::string &bytes,
                            std::string &name, std::string &mime, std::string &err,
                            const std::atomic<bool> *abort)
    {
        auto finish = [&]() -> bool
        {
            if (bytes.empty())
                return false;
            if (name.empty())
                name = detail::default_name_for(ref.kind);
            if (mime.empty())
                mime = detail::mime_for_name(name);
            if (detail::file_ext(name).empty())
                name += "." + detail::ext_for_mime(mime);
            return true;
        };
        auto content_type = [](const detail::resp &r) -> std::string
        {
            std::string ct = detail::header(r, "content-type");
            if (const size_t semi = ct.find(';'); semi != std::string::npos)
                ct.resize(semi);
            return detail::lower_ascii(cell::text::trim(ct));
        };
        if (ref.url.starts_with("http://") || ref.url.starts_with("https://"))
        {
            auto r = detail::call(ch, ref.url, "", {}, 60, abort);
            if (r.ok && !r.body.empty())
            {
                bytes = std::move(r.body);
                const std::string ct = content_type(r);
                if (mime.empty() && !ct.empty())
                    mime = ct;
                return finish();
            }
        }
        const std::string mid = !ref.media_id.empty() ? ref.media_id : ref.file_id;
        if (mid.empty())
        {
            err = "企业微信这条消息没有 MediaId";
            return false;
        }
        std::string token = wecom_access_token(ch, err, abort);
        if (token.empty())
            return false;
        detail::zero_guard tok_guard{token};
        std::string url = wecom_root(ch) + "/cgi-bin/media/get?access_token=" + detail::url_encode(token) +
                          "&media_id=" + detail::url_encode(mid);
        detail::zero_guard url_guard{url};
        auto r = detail::call(ch, url, "", {}, 60, abort);
        if (!r.ok)
        {
            err = r.err;
            return false;
        }
        const std::string ct = content_type(r);
        // 失败时企业微信回的是 JSON 错误，不是媒体字节
        if (ct == "application/json" || ct == "text/plain")
        {
            auto j = nlohmann::json::parse(r.body, nullptr, false);
            if (j.is_object() && j.contains("errcode"))
            {
                const long long code = j.value("errcode", -1LL);
                err = std::format("{} ({})", j.value("errmsg", std::string("media/get 失败")), code);
                if (code == 42001 || code == 40014)
                    detail::forget_wecom_token(ch);
                return false;
            }
        }
        if (r.body.empty())
        {
            err = "media/get 返回空";
            return false;
        }
        bytes = std::move(r.body);
        if (mime.empty() && !ct.empty())
            mime = ct;
        return finish();
    }

    // ---- 媒体上传：multipart 字段名 media，拿 media_id ----
    inline bool wecom_upload_media(const channel &ch, const std::string &type, const std::string &path,
                                   const std::string &name, const std::string &mime,
                                   std::string &media_id, std::string &err,
                                   const std::atomic<bool> *abort)
    {
        std::string bytes;
        if (!detail::read_file(path, bytes))
        {
            err = "读不到本地文件：" + path;
            return false;
        }
        std::string token = wecom_access_token(ch, err, abort);
        if (token.empty())
            return false;
        detail::zero_guard tok_guard{token};
        const std::string boundary = "----agentim" + detail::random_hex(12);
        std::string filename = name.empty() ? detail::default_name_for(type == "image" ? "image" : "file") : name;
        for (auto &c : filename)
            if (c == '"' || c == '\r' || c == '\n')
                c = '_';
        std::string body;
        body.reserve(bytes.size() + 512);
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"media\"; filename=\"" + filename + "\"\r\n";
        body += "Content-Type: " + (mime.empty() ? std::string("application/octet-stream") : mime) + "\r\n\r\n";
        body += bytes;
        body += "\r\n--" + boundary + "--\r\n";
        detail::headers hdrs;
        hdrs.push_back("Content-Type: multipart/form-data; boundary=" + boundary);
        std::string url = wecom_root(ch) + "/cgi-bin/media/upload?access_token=" + detail::url_encode(token) +
                          "&type=" + detail::url_encode(type);
        detail::zero_guard url_guard{url};
        auto r = detail::call(ch, url, body, hdrs, 120, abort);
        if (!r.ok)
        {
            err = r.err;
            return false;
        }
        auto j = nlohmann::json::parse(r.body, nullptr, false);
        if (!j.is_object())
        {
            err = "media/upload 返回不是 JSON";
            return false;
        }
        const long long code = j.value("errcode", -1LL);
        if (code != 0)
        {
            err = std::format("上传失败：{} ({})", j.value("errmsg", std::string("media/upload")), code);
            if (code == 42001 || code == 40014)
                detail::forget_wecom_token(ch);
            return false;
        }
        media_id = j.value("media_id", "");
        if (media_id.empty())
        {
            err = "media/upload 没有返回 media_id";
            return false;
        }
        return true;
    }

    inline bool wecom_send_media(const channel &ch, const std::string &to, const std::string &kind,
                                 const std::string &path, const std::string &name,
                                 const std::string &mime, std::string &err,
                                 const std::atomic<bool> *abort)
    {
        long long agent = 0;
        try
        {
            agent = std::stoll(ch.agent_id);
        }
        catch (...)
        {
            err = "AgentId 必须是数字";
            return false;
        }
        std::string up_type, msg_type;
        if (kind == "image" || kind == "sticker")
        {
            up_type = "image";
            msg_type = "image";
        }
        else if (kind == "audio")
        {
            up_type = "voice";
            msg_type = "voice";
        }
        else if (kind == "video")
        {
            up_type = "video";
            msg_type = "video";
        }
        else if (kind == "file")
        {
            up_type = "file";
            msg_type = "file";
        }
        else
        {
            err = "企业微信不支持发这种媒体：" + kind;
            return false;
        }
        std::string media_id;
        if (!wecom_upload_media(ch, up_type, path, name, mime, media_id, err, abort))
            return false;
        std::string token = wecom_access_token(ch, err, abort);
        if (token.empty())
            return false;
        detail::zero_guard tok_guard{token};
        nlohmann::json body = {{"touser", to}, {"msgtype", msg_type}, {"agentid", agent}, {"safe", 0}};
        if (msg_type == "video")
        {
            body["video"] = {{"media_id", media_id},
                             {"title", name.empty() ? std::string("video") : name},
                             {"description", ""}};
        }
        else
        {
            body[msg_type] = {{"media_id", media_id}};
        }
        std::string url = wecom_root(ch) + "/cgi-bin/message/send?access_token=" + detail::url_encode(token);
        detail::zero_guard url_guard{url};
        auto r = detail::call(ch, url, body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                              {}, 60, abort);
        if (!r.ok)
        {
            err = r.err;
            return false;
        }
        auto j = nlohmann::json::parse(r.body, nullptr, false);
        if (!j.is_object())
        {
            err = "message/send 返回不是 JSON";
            return false;
        }
        const long long code = j.value("errcode", -1LL);
        if (code != 0)
        {
            err = std::format("{} ({})", j.value("errmsg", std::string("发送失败")), code);
            if (code == 42001 || code == 40014)
                detail::forget_wecom_token(ch);
            return false;
        }
        return true;
    }

    // GET 验证：验签后把 echostr 解出来原样返回
    inline bool wecom_verify_url(const channel &ch, const request &rq, std::string &echo, std::string &err)
    {
        std::string ts = rq.param("timestamp"), nonce = rq.param("nonce");
        std::string sig = rq.param("msg_signature");
        if (sig.empty())
            sig = rq.param("signature");
        std::string es = rq.param("echostr");
        if (es.empty())
        {
            err = "缺少 echostr";
            return false;
        }
        if (!wecom_sign_ok(ch, sig, ts, nonce, es))
        {
            err = "签名不匹配";
            return false;
        }
        std::string receive;
        if (!wecom_decrypt(ch, es, echo, receive, err))
            return false;
        if (!ch.corp_id.empty() && receive != ch.corp_id)
        {
            err = "receiveid 与应用的企业 id 不一致";
            return false;
        }
        return true;
    }

    // POST：验签 → 解密 → 明文
    inline bool wecom_take_message(const channel &ch, const request &rq, std::string &plain, std::string &err)
    {
        std::string enc;
        if (!rq.body.empty() && rq.body.front() == '{')
        {
            auto j = nlohmann::json::parse(rq.body, nullptr, false);
            if (j.is_object())
                enc = j.value("Encrypt", "");
        }
        if (enc.empty())
            enc = detail::xml_text(rq.body, "Encrypt");
        if (enc.empty())
        {
            err = "回调里没有 Encrypt";
            return false;
        }
        if (!wecom_sign_ok(ch, rq.param("msg_signature"), rq.param("timestamp"), rq.param("nonce"), enc))
        {
            err = "签名不匹配";
            return false;
        }
        std::string receive;
        if (!wecom_decrypt(ch, enc, plain, receive, err))
            return false;
        if (!ch.corp_id.empty() && receive != ch.corp_id)
        {
            err = "receiveid 与应用的企业 id 不一致";
            return false;
        }
        return true;
    }

    // =====================================================================
    //  扫码登录
    // =====================================================================
    // 我们不解协议、不碰账号密码：二维码由**外部客户端**（NapCat / go-cqhttp /
    // 微信的 HTTP 桥）生成，这里只负责把它取过来、归一成 data URI，并回答
    // "登录了没有"。QQ / 微信的个人号登录协议不是本仓库能维护的东西。
    struct qr_code
    {
        std::string state = "unsupported"; // online | wait | scaned | expired | unsupported
        std::string token;                 // 轮询令牌（iLink 的 qrcode）
        std::string image;                 // data:image/…;base64,…
        std::string account;               // 已登录时的昵称 / 号码
        std::string hint;                  // 给人看的一句话
    };

    namespace detail
    {
        inline bool looks_like_image(const std::string &s)
        {
            return s.rfind("iVBOR", 0) == 0 || s.rfind("/9j/", 0) == 0 ||
                   s.rfind("data:image/", 0) == 0 || s.rfind("base64://iVBOR", 0) == 0 ||
                   s.rfind("base64:///9j/", 0) == 0;
        }
        // 各家把图片塞在不同字段里（qrcode / image / base64 / url），
        // 所以不猜字段名，直接找"看起来像图片"的那个字符串
        inline std::string pick_image(const nlohmann::json &j, int depth = 0)
        {
            if (depth > 6)
                return {};
            if (j.is_string())
            {
                std::string v = j.get<std::string>();
                return looks_like_image(v) ? v : std::string();
            }
            if (j.is_object())
                for (auto &[k, v] : j.items())
                    if (auto hit = pick_image(v, depth + 1); !hit.empty())
                        return hit;
            if (j.is_array())
                for (auto &v : j)
                    if (auto hit = pick_image(v, depth + 1); !hit.empty())
                        return hit;
            return {};
        }
        // 归一成 data URI：base64:// 前缀、裸 base64、裸 PNG 字节都认
        inline std::string as_data_uri(std::string s)
        {
            if (s.rfind("data:image/", 0) == 0)
                return s;
            if (s.rfind("base64://", 0) == 0)
                s.erase(0, 9);
            if (s.rfind("\x89PNG", 0) == 0)
                s = cell::encrypt::b64_encode(s);
            std::string mime = s.rfind("/9j/", 0) == 0 ? "image/jpeg" : "image/png";
            return "data:" + mime + ";base64," + s;
        }
        // 响应可能是 JSON，也可能直接是图片字节
        inline std::string image_from_body(const std::string &body)
        {
            if (!body.empty() && (unsigned char)body[0] == 0x89 && body.size() > 8)
                return as_data_uri(body);
            auto j = nlohmann::json::parse(body, nullptr, false);
            if (j.is_discarded())
                return looks_like_image(body) ? as_data_uri(body) : std::string();
            std::string hit = pick_image(j);
            return hit.empty() ? std::string() : as_data_uri(hit);
        }
    } // namespace detail

    // 把待编码的 URL 变成能直接 <img src> 的二维码（SVG，省掉 PNG 编码器）
    inline std::string qr_data_uri(const std::string &payload)
    {
        app::qr::matrix m;
        if (!app::qr::encode(payload, 1 /*M 级纠错*/, m))
            return {};
        return "data:image/svg+xml;base64," + cell::encrypt::b64_encode(app::qr::to_svg(m));
    }

    // OneBot：先问登录状态，没登录再要二维码。
    // 不同的实现暴露的取码接口不一样，按常见顺序挨个试。
    inline qr_code fetch_qr_onebot(const channel &ch)
    {
        qr_code q;
        detail::headers hdrs;
        if (!ch.token.empty())
            hdrs.push_back("Authorization: Bearer " + ch.token.str());
        auto info = detail::call(ch, api_root(ch) + "/get_login_info", "", hdrs, 8);
        if (info.ok)
        {
            auto j = nlohmann::json::parse(info.body, nullptr, false);
            if (j.is_object() && j.value("retcode", -1LL) == 0 && j.contains("data") && j["data"].is_object() &&
                j["data"].value("user_id", 0LL) != 0)
            {
                q.state = "online";
                q.account = j["data"].value("nickname", detail::id_str(j["data"].value("user_id", nlohmann::json())));
                q.hint = "已经登录了，不用扫码";
                return q;
            }
        }
        for (const char *path : {"/get_qrcode", "/get_qrcode_image", "/qrcode"})
        {
            auto r = detail::call(ch, api_root(ch) + std::string(path), "", hdrs, 10);
            if (!r.ok)
                continue;
            std::string img = detail::image_from_body(r.body);
            if (img.empty())
                continue;
            q.state = "wait"; // 和 QR_TEXT / iLink 用同一套状态名
            q.image = std::move(img);
            q.hint = "用手机 QQ 扫码登录";
            return q;
        }
        // 对端没有二维码接口（NapCat 的登录在自己的界面里做）：保持 wait 让界面
        // 继续轮询 get_login_info，在 NapCat 那边扫完这里会自动变成「已登录」。
        if (info.ok)
        {
            q.state = "wait";
            q.hint = "对端没有二维码接口：在 NapCat / go-cqhttp 自己的界面里扫码，登录后这里会自动变成已登录";
        }
        else
        {
            q.state = "error";
            q.hint = "连不上 OneBot 服务：" + info.err;
        }
        return q;
    }

    // =====================================================================
    //  微信 iLink（ClawBot）—— 腾讯官方个人号 Bot API
    // =====================================================================
    // 2026-03-22 起开放，纯 HTTP/JSON，没有 WebSocket、没有本地客户端：
    //   扫码   GET  /ilink/bot/get_bot_qrcode?bot_type=3   → qrcode + 待编码 URL
    //          GET  /ilink/bot/get_qrcode_status?qrcode=…  → wait/scaned/confirmed
    //   收消息 POST /ilink/bot/getupdates                  长轮询 35s + 游标
    //   发消息 POST /ilink/bot/sendmessage                 必须带 context_token
    // 鉴权头：AuthorizationType / Authorization: Bearer / X-WECHAT-UIN（每次随机）。
    namespace detail
    {
        inline std::mutex &ctx_mx()
        {
            static std::mutex m;
            return m;
        }
        // 每个 (渠道, 用户) 的最新 context_token —— 没有它回复送不出去
        inline std::map<std::string, std::string> &ctx_map()
        {
            static std::map<std::string, std::string> m;
            return m;
        }
        inline void remember_context(const std::string &chan, const std::string &chat, const std::string &ctx)
        {
            if (ctx.empty())
                return;
            std::lock_guard<std::mutex> lk(ctx_mx());
            ctx_map()[chan + "|" + chat] = ctx;
        }
        inline std::string context_of(const std::string &chan, const std::string &chat)
        {
            std::lock_guard<std::mutex> lk(ctx_mx());
            auto it = ctx_map().find(chan + "|" + chat);
            return it == ctx_map().end() ? std::string() : it->second;
        }
        // 机器人自己的 user id：扫码时会带回来，但从老版本升上来的渠道可能没存。
        // 每条来信的 to_user_id 就是它，所以收到第一条消息就补上了。
        inline std::mutex &self_mx()
        {
            static std::mutex m;
            return m;
        }
        inline std::map<std::string, std::string> &self_map()
        {
            static std::map<std::string, std::string> m;
            return m;
        }
        inline void remember_self(const std::string &chan, const std::string &id)
        {
            if (chan.empty() || id.empty())
                return;
            std::lock_guard<std::mutex> lk(self_mx());
            self_map()[chan] = id;
        }
        inline std::string self_of(const std::string &chan)
        {
            std::lock_guard<std::mutex> lk(self_mx());
            auto it = self_map().find(chan);
            return it == self_map().end() ? std::string() : it->second;
        }
    } // namespace detail

    namespace detail
    {
        // 各家 SDK 对同一份数据的字段名不完全一样，取第一个命中的
        inline std::string pick_str(const nlohmann::json &j, std::initializer_list<const char *> keys)
        {
            for (const char *k : keys)
            {
                auto it = j.find(k);
                if (it == j.end())
                    continue;
                if (it->is_string() && !it->get_ref<const std::string &>().empty())
                    return it->get<std::string>();
                if (it->is_number_integer() || it->is_number_unsigned())
                    return id_str(*it);
                if (it->is_object())
                {
                    std::string v = pick_str(*it, {"user_id", "id", "openid", "uin"});
                    if (!v.empty())
                        return v;
                }
            }
            return {};
        }
        // item_list 里的文本：type=1 的 text / text_item.text / data.text
        inline std::string pick_text(const nlohmann::json &j)
        {
            std::string out;
            if (j.contains("item_list") && j["item_list"].is_array())
            {
                for (auto &item : j["item_list"])
                {
                    if (!item.is_object())
                        continue;
                    if (item.contains("text") && item["text"].is_string())
                        out += item["text"].get<std::string>();
                    else if (item.contains("text_item") && item["text_item"].is_object())
                        out += item["text_item"].value("text", std::string());
                    else if (item.contains("data") && item["data"].is_object())
                        out += item["data"].value("text", std::string());
                }
            }
            if (out.empty())
                out = pick_str(j, {"text", "content"});
            return out;
        }
        // iLink 的返回码：正常是 ret，鉴权/参数类错误有时只给 errcode。
        // 缺字段一律当 0（成功），但只要有非零的错误码就要算失败 —— 否则
        // 「服务端说不行」会被读成「已发送」。
        inline long long ilink_code(const nlohmann::json &j, std::string &msg)
        {
            long long code = 0;
            if (auto it = j.find("ret"); it != j.end() && it->is_number_integer())
                code = it->get<long long>();
            if (auto it = j.find("errcode"); it != j.end() && it->is_number_integer() && code == 0)
                code = it->get<long long>();
            if (auto it = j.find("errmsg"); it != j.end() && it->is_string())
                msg = it->get<std::string>();
            return code;
        }
    } // namespace detail
    inline std::string ilink_root(const channel &ch)
    {
        return detail::trim_slash(ch.bot_base.empty() ? "https://ilinkai.weixin.qq.com" : ch.bot_base);
    }
    // X-WECHAT-UIN：随机 uint32 → 十进制字符串 → base64（每次请求都换，防重放）
    inline std::string ilink_uin()
    {
        uint32_t v = randombytes_random();
        return cell::encrypt::b64_encode(std::to_string(v));
    }
    // 扫码阶段（还没有 bot_token）用的头：不挂 Authorization，免得发一个空的
    // Bearer 被网关当成鉴权失败挡掉
    inline detail::headers ilink_qr_headers()
    {
        detail::headers h;
        h.push_back("iLink-App-Id: bot");
        h.push_back("X-WECHAT-UIN: " + ilink_uin());
        return h;
    }
    // 业务接口（收 / 发消息）用的头：带上扫码后拿到的 bot_token
    inline detail::headers ilink_headers(const channel &ch)
    {
        detail::headers h;
        h.push_back("iLink-App-Id: bot");
        h.push_back("AuthorizationType: ilink_bot_token");
        h.push_back("Authorization: Bearer " + ch.bot_token.str());
        h.push_back("X-WECHAT-UIN: " + ilink_uin());
        return h;
    }
    inline nlohmann::json ilink_base_info() { return {{"channel_version", "2.0.0"}}; }

    // ---- 媒体：CDN 根 / 密钥编码 / 入站解密 ----
    // bot_base 是回环地址时（自测的假服务端）CDN 也走 <bot_base>/c2c，别的走官方 CDN
    inline std::string ilink_cdn_root(const channel &ch)
    {
        if (!ch.bot_base.empty() && detail::is_loopback_url(ch.bot_base))
            return detail::trim_slash(ch.bot_base) + "/c2c";
        return "https://novac2c.cdn.weixin.qq.com/c2c";
    }

    namespace detail
    {
        // 在 raw JSON 里递归找第一个命中的字符串字段（各平台/各版本字段名不一致）
        inline std::string json_find_str(const nlohmann::json &j, std::initializer_list<const char *> keys,
                                         int depth = 0)
        {
            if (depth > 6 || !j.is_structured())
                return {};
            if (j.is_object())
            {
                for (const char *k : keys)
                {
                    auto it = j.find(k);
                    if (it != j.end() && it->is_string() && !it->get_ref<const std::string &>().empty())
                        return it->get<std::string>();
                }
                for (auto &[k, v] : j.items())
                    if (auto hit = json_find_str(v, keys, depth + 1); !hit.empty())
                        return hit;
            }
            else
            {
                for (const auto &v : j)
                    if (auto hit = json_find_str(v, keys, depth + 1); !hit.empty())
                        return hit;
            }
            return {};
        }
    } // namespace detail

    // 入站 AES key 两种编码：32 位 hex；或 base64（16 字节直接用 / 32 字节 hex ascii 再 hex 解码）
    inline bool ilink_key16(const std::string &raw, std::string &key)
    {
        key.clear();
        if (raw.empty())
            return false;
        if (raw.size() == 32)
        {
            std::string d;
            if (detail::hex_decode(raw, d) && d.size() == 16)
            {
                key = std::move(d);
                return true;
            }
        }
        std::string dec = cell::encrypt::b64_decode(raw);
        if (dec.size() == 16)
        {
            key = std::move(dec);
            return true;
        }
        if (dec.size() == 32)
        {
            std::string d;
            if (detail::hex_decode(dec, d) && d.size() == 16)
            {
                key = std::move(d);
                return true;
            }
        }
        return false;
    }

    // 单条 iLink 消息 → inbound（纯解析，不跑网络：自测直接调它）。
    // node 是 getupdates 里的一条消息（poll_wechat 已展开 msg/message 包装），
    // chat / user / msg_id / context_token 由 poll_wechat 填，这里只管 text / media。
    inline bool parse_ilink(const nlohmann::json &node, inbound &m)
    {
        try
        {
        if (!node.is_object())
            return false;
        if (auto mt = node.find("message_type"); mt != node.end() && mt->is_number_integer() &&
                                                   mt->get<long long>() == 2)
            return false; // 自己发的
        std::string text;
        std::vector<media_ref> media;
        auto media_fields = [](const nlohmann::json &media_obj, media_ref &r)
        {
            if (!media_obj.is_object())
                return;
            r.encrypt_query_param = detail::pick_str(media_obj, {"encrypt_query_param", "encrypted_query_param"});
            if (auto it = media_obj.find("encrypt_type"); it != media_obj.end() &&
                (it->is_number_integer() || it->is_number_unsigned()))
                r.encrypt_type = (int)it->get<long long>();
            if (r.aeskey.empty())
                r.aeskey = detail::pick_str(media_obj, {"aes_key", "aeskey"});
            if (r.size == 0)
                r.size = detail::pick_size(media_obj, {"file_size", "size"});
        };
        if (node.contains("item_list") && node["item_list"].is_array())
        {
            for (const auto &item : node["item_list"])
            {
                if (!item.is_object())
                    continue;
                int type = 0;
                if (auto it = item.find("type"); it != item.end() && it->is_number_integer())
                    type = (int)it->get<long long>();
                if (type == 1)
                {
                    if (item.contains("text_item") && item["text_item"].is_object())
                        text += item["text_item"].value("text", std::string());
                    else
                        text += detail::pick_str(item, {"text", "content"});
                }
                else if (type == 2)
                {
                    const auto &img = item.contains("image_item") && item["image_item"].is_object()
                                          ? item["image_item"]
                                          : item;
                    media_ref r;
                    r.kind = "image";
                    r.name = "image.jpg";
                    r.mime = "image/jpeg";
                    if (img.contains("media") && img["media"].is_object())
                        media_fields(img["media"], r);
                    // image_item.aeskey（32 hex）优先
                    if (std::string ak = detail::pick_str(img, {"aeskey"}); !ak.empty())
                        r.aeskey = std::move(ak);
                    r.url = detail::pick_str(img, {"url", "cdn_url"});
                    if (r.size == 0)
                        r.size = detail::pick_size(img, {"mid_size", "file_size", "size"});
                    r.raw = item;
                    media.push_back(std::move(r));
                }
                else if (type == 3)
                {
                    const auto &voice = item.contains("voice_item") && item["voice_item"].is_object()
                                            ? item["voice_item"]
                                            : item;
                    if (std::string vt = detail::pick_str(voice, {"text"}); !vt.empty())
                    {
                        text += vt; // 服务端转写：并进正文，不落盘
                    }
                    else
                    {
                        media_ref r;
                        r.kind = "audio";
                        if (voice.contains("media") && voice["media"].is_object())
                            media_fields(voice["media"], r);
                        if (auto it = voice.find("encode_type"); it != voice.end() && it->is_number_integer())
                            r.encode_type = (int)it->get<long long>();
                        static const std::map<int, std::string> exts = {
                            {1, "pcm"}, {2, "adpcm"}, {3, "feature"}, {4, "speex"},
                            {5, "amr"}, {6, "silk"}, {7, "mp3"}, {8, "ogg"}};
                        auto e = exts.find(r.encode_type);
                        r.name = std::string("voice.") + (e == exts.end() ? "silk" : e->second);
                        r.mime = detail::mime_for_name(r.name);
                        r.raw = item;
                        media.push_back(std::move(r));
                    }
                }
                else if (type == 4)
                {
                    const auto &fi = item.contains("file_item") && item["file_item"].is_object()
                                         ? item["file_item"]
                                         : item;
                    media_ref r;
                    r.kind = "file";
                    if (fi.contains("media") && fi["media"].is_object())
                        media_fields(fi["media"], r);
                    r.file_name = detail::pick_str(fi, {"file_name", "name"});
                    r.name = r.file_name.empty() ? "file.bin" : r.file_name;
                    if (r.size == 0)
                        r.size = detail::pick_size(fi, {"len", "file_size", "size"});
                    r.mime = detail::mime_for_name(r.name);
                    r.raw = item;
                    media.push_back(std::move(r));
                }
                else if (type == 5)
                {
                    const auto &vi = item.contains("video_item") && item["video_item"].is_object()
                                         ? item["video_item"]
                                         : item;
                    media_ref r;
                    r.kind = "video";
                    if (vi.contains("media") && vi["media"].is_object())
                        media_fields(vi["media"], r);
                    r.name = "video.mp4";
                    r.mime = "video/mp4";
                    if (r.size == 0)
                        r.size = detail::pick_size(vi, {"video_size", "file_size", "size"});
                    r.raw = item;
                    media.push_back(std::move(r));
                }
            }
        }
        else if (text.empty())
        {
            text = detail::pick_text(node);
        }
        // 引用回复：正文前加「（回复：…）」
        if (node.contains("ref_msg") && node["ref_msg"].is_object())
        {
            const auto &ref = node["ref_msg"];
            std::string title = detail::pick_str(ref, {"title"});
            if (title.empty())
                title = detail::pick_text(ref);
            if (title.empty() && ref.contains("message") && ref["message"].is_object())
                title = detail::pick_text(ref["message"]);
            if (!title.empty())
                text = "（回复：" + title + "）" + text;
        }
        m.text = cell::text::trim(text);
        if (!media.empty())
            m.media = std::move(media);
        return !m.text.empty() || !m.media.empty();
        }
        catch (...)
        {
            return false; // 契约 3.3：解析函数不抛异常
        }
    }

    // ---- 入站下载：CDN + AES-128-ECB 解密 ----
    inline bool ilink_fetch(const channel &ch, const media_ref &ref, std::string &bytes,
                            std::string &name, std::string &mime, std::string &err,
                            const std::atomic<bool> *abort)
    {
        auto finish = [&]() -> bool
        {
            if (bytes.empty())
                return false;
            if (name.empty())
                name = detail::default_name_for(ref.kind);
            if (mime.empty())
                mime = detail::mime_for_name(name);
            if (detail::file_ext(name).empty())
                name += "." + detail::ext_for_mime(mime);
            return true;
        };
        std::string eqp = ref.encrypt_query_param;
        if (eqp.empty())
            eqp = detail::json_find_str(ref.raw, {"encrypt_query_param", "encrypted_query_param"});
        std::string ak = ref.aeskey;
        if (ak.empty())
            ak = detail::json_find_str(ref.raw, {"aes_key", "aeskey"});
        std::string body;
        if (!eqp.empty())
        {
            std::string url = ilink_cdn_root(ch) + "/download?encrypted_query_param=" + detail::url_encode(eqp);
            if (!detail::download(ch, url, body, err, abort))
                return false;
        }
        else if (ref.url.starts_with("http://") || ref.url.starts_with("https://"))
        {
            if (!detail::download(ch, ref.url, body, err, abort))
                return false;
        }
        else
        {
            err = "iLink 这条媒体没有 CDN 参数（encrypt_query_param / url）";
            return false;
        }
        // 有 key 就解；没 key / 解不开都当明文（有些实现与测试服务端不加密）
        std::string key;
        if (ilink_key16(ak, key))
        {
            std::string plain, derr;
            if (crypto::aes128_ecb_decrypt(key, body, plain, derr))
                bytes = std::move(plain);
            else
                bytes = std::move(body);
        }
        else
        {
            bytes = std::move(body);
        }
        return finish();
    }

    struct ilink_qr
    {
        bool ok = false;
        std::string qrcode; // 轮询令牌
        std::string url;    // 待编码成二维码的 URL
        std::string err;
    };
    inline ilink_qr ilink_start_qr(const channel &ch)
    {
        ilink_qr r;
        auto resp = detail::call(ch, ilink_root(ch) + "/ilink/bot/get_bot_qrcode?bot_type=3", "",
                                 ilink_qr_headers(), 8);
        if (!resp.ok)
        {
            r.err = resp.err;
            return r;
        }
        auto j = nlohmann::json::parse(resp.body, nullptr, false);
        if (!j.is_object())
        {
            r.err = "返回不是 JSON";
            return r;
        }
        std::string msg;
        if (long long code = detail::ilink_code(j, msg); code != 0)
        {
            r.err = std::format("ret={} {}", code, msg);
            return r;
        }
        r.qrcode = j.value("qrcode", "");
        r.url = j.value("qrcode_img_content", "");
        if (r.qrcode.empty() || r.url.empty())
        {
            r.err = "返回里没有 qrcode / qrcode_img_content";
            return r;
        }
        r.ok = true;
        return r;
    }

    struct ilink_status
    {
        std::string status; // wait | scaned | confirmed | expired
        std::string bot_token, base, bot_id, user_id, err;
    };
    inline ilink_status ilink_poll_status(const channel &ch, const std::string &qrcode)
    {
        ilink_status st;
        auto resp = detail::call(ch, ilink_root(ch) + "/ilink/bot/get_qrcode_status?qrcode=" +
                                          detail::url_encode(qrcode), "",
                                 ilink_qr_headers(), 8);
        if (!resp.ok)
        {
            st.err = resp.err;
            return st;
        }
        auto j = nlohmann::json::parse(resp.body, nullptr, false);
        if (!j.is_object())
        {
            st.err = "返回不是 JSON";
            return st;
        }
        st.status = j.value("status", "");
        st.bot_token = j.value("bot_token", "");
        st.base = j.value("baseurl", "");
        st.bot_id = j.value("ilink_bot_id", "");
        st.user_id = j.value("ilink_user_id", "");
        if (st.status.empty())
        {
            std::string msg;
            long long code = detail::ilink_code(j, msg);
            st.err = code != 0 ? std::format("ret={} {}", code, msg) : "返回里没有 status";
        }
        return st;
    }

    // 长轮询收消息：服务端挂起约 35 秒，返回新的游标
    inline bool poll_wechat(const channel &ch, std::string &buf, std::vector<inbound> &out,
                            std::string &err, const std::atomic<bool> *abort = nullptr)
    {
        if (ch.bot_token.empty())
        {
            err = "还没有扫码登录（缺 bot_token）";
            return false;
        }
        nlohmann::json body = {{"get_updates_buf", buf}, {"base_info", ilink_base_info()}};
        auto r = detail::call(ch, ilink_root(ch) + "/ilink/bot/getupdates",
                              body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                              ilink_headers(ch), 45, abort);
        if (!r.ok)
        {
            err = r.err;
            return false;
        }
        auto j = nlohmann::json::parse(r.body, nullptr, false);
        if (!j.is_object())
        {
            err = "返回不是 JSON";
            return false;
        }
        std::string rmsg;
        const long long ret = detail::ilink_code(j, rmsg);
        if (ret == -14)
        {
            err = "session-expired"; // 会话过期：需要重新扫码
            return false;
        }
        if (ret != 0)
        {
            err = std::format("ret={} {}", ret, rmsg);
            cell::sys::logger::instance().warn(
                "im", std::format("getupdates ret={} body={}", ret,
                                  r.body.substr(0, 240)));
            return false;
        }
        if (j.contains("get_updates_buf") && j["get_updates_buf"].is_string())
            buf = j["get_updates_buf"].get<std::string>();
        const nlohmann::json *list = nullptr;
        for (const char *k : {"msgs", "msg_list", "messages"})
            if (j.contains(k) && j[k].is_array())
            {
                list = &j[k];
                break;
            }
        if (!list)
            return true;
        for (auto &m : *list)
        {
            if (!m.is_object())
                continue;
            // 有的实现把字段套在 msg/message 里，先展开一层
            const nlohmann::json *node = &m;
            for (const char *wrap : {"msg", "message"})
                if (m.contains(wrap) && m[wrap].is_object())
                {
                    node = &m[wrap];
                    break;
                }
            inbound in;
            in.chan = ch.id;
            // message_type：1 = 用户发来的，2 = 自己（机器人）发出去的。
            // 不过滤的话，机器人自己的回复也会被当成人说的话再回一遍。
            if (auto mt = node->find("message_type"); mt != node->end() && mt->is_number_integer() &&
                                                       mt->get<long long>() == 2)
                continue;
            in.chat = detail::pick_str(*node, {"from_user_id", "from", "sender", "user_id", "chat_id"});
            if (in.chat.empty())
                in.chat = detail::pick_str(m, {"from_user_id", "from", "sender", "user_id", "chat_id"});
            if (in.chat.empty())
            {
                // 报文和文档不一致时，别静默丢弃：留一行原始样本，好查
                cell::sys::logger::instance().warn(
                    "im", std::format("wechat msg unparsed: {}",
                                      m.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).substr(0, 400)));
                continue;
            }
            in.user = in.chat;
            in.name = in.chat;
            in.msg_id = detail::pick_str(*node, {"msg_id", "message_id", "id"});
            // 自己的 id（iLink 的 ilink_user_id）：老渠道没存的话，这里捡回来
            detail::remember_self(ch.id, detail::pick_str(*node, {"to_user_id", "to_user", "bot_user_id"}));
            // 分组：有 group_id 时 chat 用群 id（context_token 也按最终 chat 记）
            if (const std::string gid = detail::pick_str(*node, {"group_id", "chatroom_id"}); !gid.empty())
            {
                in.group = true;
                in.chat = gid;
            }
            in.mention = !in.group;
            const std::string ctx = detail::pick_str(*node, {"context_token", "context", "token"});
            detail::remember_context(ch.id, in.chat, ctx);
            // 正文与媒体都交给 parse_ilink（自测直接测它，不跑网络）
            if (!parse_ilink(*node, in))
            {
                cell::sys::logger::instance().info(
                    "im", std::format("wechat msg without text/media: {}",
                                      node->dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).substr(0, 300)));
                continue;
            }
            out.push_back(std::move(in));
        }
        return true;
    }

    // 一条出站消息的 client_id：服务端要求每条唯一（自己去重），随便一个 uuid 形态
    inline std::string ilink_client_id()
    {
        unsigned char b[16];
        randombytes_buf(b, sizeof b);
        b[6] = (unsigned char)((b[6] & 0x0f) | 0x40); // v4
        b[8] = (unsigned char)((b[8] & 0x3f) | 0x80);
        std::string s;
        s.reserve(36);
        for (int i = 0; i < 16; i++)
        {
            if (i == 4 || i == 6 || i == 8 || i == 10)
                s.push_back('-');
            s += std::format("{:02x}", b[i]);
        }
        return s;
    }

    // 一条出站消息的信封：item_list 由调用方给（文本 / 图片 / 语音 / 视频 / 文件）
    // 单独拎出来是为了能被自测覆盖：少一个字段服务端只会回一个 ret，消息静默丢掉。
    inline nlohmann::json ilink_msg_envelope(const channel &ch, const std::string &to,
                                             const std::string &ctx, const nlohmann::json &items)
    {
        nlohmann::json msg = {
            {"to_user_id", to},
            {"client_id", ilink_client_id()},
            {"message_type", 2},
            {"message_state", 2},
            {"context_token", ctx},
            {"item_list", items}};
        if (!ch.bot_user.empty())
            msg["from_user_id"] = ch.bot_user;
        else if (std::string self = detail::self_of(ch.id); !self.empty())
            msg["from_user_id"] = self;
        return {{"msg", std::move(msg)}, {"base_info", ilink_base_info()}};
    }

    // 纯文本：item_list[0] = {type:1, text_item:{text}}（不是 {"text": ...}）
    inline nlohmann::json ilink_msg_body(const channel &ch, const std::string &to,
                                         const std::string &ctx, const std::string &text)
    {
        return ilink_msg_envelope(ch, to, ctx,
                                  nlohmann::json::array({{{"type", 1}, {"text_item", {{"text", text}}}}}));
    }

    // 发消息。报文形状是 iLink 定的：
    //   msg.from_user_id   机器人自己的 ilink_user_id
    //   msg.to_user_id     收件人 openid
    //   msg.client_id      本条唯一
    //   msg.message_type   2 = 机器人发的
    //   msg.message_state  2 = 正常
    //   msg.context_token  回复必须带，来自收到的那条消息
    inline bool send_ilink_items(const channel &ch, const std::string &to, const std::string &ctx,
                                 const nlohmann::json &items, std::string &err,
                                 const std::atomic<bool> *abort = nullptr)
    {
        nlohmann::json body = ilink_msg_envelope(ch, to, ctx, items);
        auto r = detail::call(ch, ilink_root(ch) + "/ilink/bot/sendmessage",
                              body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                              ilink_headers(ch), 20, abort);
        if (!r.ok)
        {
            err = r.err;
            return false;
        }
        // 空响应 / 非 JSON 不能当成功：那就等于把消息丢进黑洞还回一句「已发送」
        if (r.body.empty())
        {
            err = "sendmessage 返回空响应";
            return false;
        }
        auto j = nlohmann::json::parse(r.body, nullptr, false);
        if (!j.is_object())
        {
            err = "sendmessage 返回不是 JSON：" + r.body.substr(0, 200);
            return false;
        }
        std::string rmsg;
        const long long ret = detail::ilink_code(j, rmsg);
        if (ret == -14)
        {
            err = "session-expired";
            return false;
        }
        if (ret != 0)
        {
            err = std::format("ret={} {}", ret, rmsg);
            cell::sys::logger::instance().warn(
                "im", std::format("sendmessage failed chan={} body={}", ch.id, r.body.substr(0, 240)));
            return false;
        }
        return true;
    }

    inline bool send_ilink(const channel &ch, const std::string &to, const std::string &text,
                           std::string &err, const std::atomic<bool> *abort = nullptr)
    {
        const std::string ctx = detail::context_of(ch.id, to);
        if (ctx.empty())
        {
            err = "还没有收到过这个人的消息：iLink 的回复必须带 context_token";
            return false;
        }
        return send_ilink_items(ch, to, ctx,
                               nlohmann::json::array({{{"type", 1}, {"text_item", {{"text", text}}}}}),
                               err, abort);
    }

    // ---- 出站媒体（附录 A）：getuploadurl → CDN upload → sendmessage item ----
    // 语音 encode_type：mp3→7 ogg→8 amr→5 silk→6，其它按 mp3
    inline int ilink_voice_encode(const std::string &name, const std::string &mime)
    {
        const std::string ext = detail::file_ext(name);
        const std::string m = detail::lower_ascii(mime);
        if (ext == "ogg" || m.starts_with("audio/ogg"))
            return 8;
        if (ext == "amr" || m.starts_with("audio/amr"))
            return 5;
        if (ext == "silk" || m.starts_with("audio/silk"))
            return 6;
        return 7;
    }
    // CDNMedia：出站 aes_key 用 base64(hex ascii 32 字节)，encrypt_type=1
    inline nlohmann::json ilink_cdn_media(const std::string &eqp, const std::string &aeskey_hex)
    {
        return {{"encrypt_query_param", eqp},
                {"aes_key", cell::encrypt::b64_encode(aeskey_hex)},
                {"encrypt_type", 1}};
    }

    inline bool ilink_get_upload_param(const channel &ch, const std::string &to,
                                       const std::string &filekey, const std::string &aeskey_hex,
                                       int media_type, long long rawsize, long long filesize,
                                       const std::string &rawfilemd5, std::string &upload_param,
                                       std::string &err, const std::atomic<bool> *abort)
    {
        nlohmann::json body = {{"filekey", filekey},
                               {"media_type", media_type},
                               {"to_user_id", to},
                               {"rawsize", rawsize},
                               {"rawfilemd5", rawfilemd5},
                               {"filesize", filesize},
                               {"no_need_thumb", true},
                               {"aeskey", aeskey_hex},
                               {"base_info", ilink_base_info()}};
        auto r = detail::call(ch, ilink_root(ch) + "/ilink/bot/getuploadurl",
                              body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                              ilink_headers(ch), 30, abort);
        if (!r.ok)
        {
            err = r.err;
            return false;
        }
        auto j = nlohmann::json::parse(r.body, nullptr, false);
        if (!j.is_object())
        {
            err = "getuploadurl 返回不是 JSON";
            return false;
        }
        std::string rmsg;
        const long long ret = detail::ilink_code(j, rmsg);
        if (ret != 0)
        {
            err = std::format("getuploadurl ret={} {}", ret, rmsg);
            return false;
        }
        upload_param = detail::json_find_str(j, {"upload_param", "encrypted_query_param"});
        if (upload_param.empty())
        {
            err = "getuploadurl 没有返回 upload_param";
            return false;
        }
        return true;
    }

    inline bool ilink_cdn_upload(const channel &ch, const std::string &upload_param,
                                 const std::string &filekey, const std::string &cipher,
                                 std::string &encrypted_param, std::string &err,
                                 const std::atomic<bool> *abort)
    {
        std::string url = ilink_cdn_root(ch) + "/upload?encrypted_query_param=" +
                          detail::url_encode(upload_param) + "&filekey=" + detail::url_encode(filekey);
        detail::headers hdrs;
        hdrs.push_back("Content-Type: application/octet-stream");
        auto r = detail::call(ch, url, cipher, hdrs, 120, abort);
        if (!r.ok)
        {
            err = r.err;
            return false;
        }
        encrypted_param = detail::header(r, "x-encrypted-param");
        if (encrypted_param.empty())
            encrypted_param = detail::json_find_str(
                nlohmann::json::parse(r.body, nullptr, false),
                {"x-encrypted-param", "encrypted_param", "encrypt_query_param"});
        if (encrypted_param.empty())
        {
            err = "CDN 上传没有返回 x-encrypted-param";
            return false;
        }
        return true;
    }

    inline bool ilink_send_media(const channel &ch, const std::string &to, const std::string &kind,
                                 const std::string &path, const std::string &name,
                                 const std::string &mime, std::string &err,
                                 const std::atomic<bool> *abort)
    {
        const std::string ctx = detail::context_of(ch.id, to);
        if (ctx.empty())
        {
            err = "还没有收到过这个人的消息：iLink 的回复必须带 context_token";
            return false;
        }
        std::string raw;
        if (!detail::read_file(path, raw))
        {
            err = "读不到本地文件：" + path;
            return false;
        }
        const long long rawsize = (long long)raw.size();
        const std::string rawfilemd5 = crypto::md5_hex(raw);
        if (rawfilemd5.empty())
        {
            err = "这个构建没有链 OpenSSL，iLink 媒体发不了";
            return false;
        }
        const std::string aeskey_hex = detail::random_hex(16); // 16 字节 → 32 hex
        std::string key;
        if (!detail::hex_decode(aeskey_hex, key) || key.size() != 16)
        {
            err = "AES 密钥生成失败";
            return false;
        }
        std::string cipher, cerr;
        if (!crypto::aes128_ecb_encrypt(key, raw, cipher, cerr))
        {
            err = cerr;
            return false;
        }
        const long long filesize = ((rawsize / 16) + 1) * 16; // PKCS#7 后的密文长度
        int media_type = 3;                                   // 1 图 2 视频 3 文件 4 语音
        if (kind == "image" || kind == "sticker")
            media_type = 1;
        else if (kind == "video")
            media_type = 2;
        else if (kind == "audio")
            media_type = 4;
        else if (kind != "file")
        {
            err = "iLink 不支持发这种媒体：" + kind;
            return false;
        }
        const std::string filekey = detail::random_hex(8); // 契约附录 A：filekey 是 16 位 hex
        std::string upload_param;
        if (!ilink_get_upload_param(ch, to, filekey, aeskey_hex, media_type, rawsize,
                                    (long long)cipher.size(), rawfilemd5, upload_param, err, abort))
            return false;
        std::string eqp;
        if (!ilink_cdn_upload(ch, upload_param, filekey, cipher, eqp, err, abort))
            return false;
        const nlohmann::json media = ilink_cdn_media(eqp, aeskey_hex);
        nlohmann::json item;
        if (media_type == 1)
            item = {{"type", 2}, {"image_item", {{"media", media}, {"mid_size", filesize}}}};
        else if (media_type == 2)
            item = {{"type", 5}, {"video_item", {{"media", media}, {"video_size", filesize}}}};
        else if (media_type == 3)
            item = {{"type", 4},
                    {"file_item", {{"media", media},
                                   {"file_name", name.empty() ? std::string("file.bin") : name},
                                   {"len", std::to_string(rawsize)}}}};
        else
            item = {{"type", 3},
                    {"voice_item", {{"media", media}, {"encode_type", ilink_voice_encode(name, mime)}}}};
        return send_ilink_items(ch, to, ctx, nlohmann::json::array({item}), err, abort);
    }

    // =====================================================================
    //  通用 HTTP 桥
    // =====================================================================
    inline bool parse_bridge(const std::string &body, inbound &m)
    {
        try
        {
        auto j = nlohmann::json::parse(body, nullptr, false);
        if (!j.is_object())
            return false;
        std::string text;
        if (auto it = j.find("text"); it != j.end())
            text = it->is_string() ? it->get<std::string>() : detail::id_str(*it);
        m.chat = detail::id_str(j.contains("chat_id") ? j["chat_id"] : nlohmann::json());
        m.user = j.contains("user_id") ? detail::id_str(j["user_id"]) : m.chat;
        if (m.chat.empty())
            m.chat = m.user;
        m.name = j.value("user_name", m.user);
        m.text = cell::text::trim(text);
        m.msg_id = j.contains("msg_id") ? detail::id_str(j["msg_id"]) : std::string();
        m.group = j.value("group", false);
        m.mention = j.value("mention", true);

        // 每种媒介统一成 media_ref；path 留在 raw 里，fetch 时读本地文件
        auto push_media = [&](const nlohmann::json &o, const std::string &forced_kind = {})
        {
            media_ref r;
            r.kind = forced_kind;
            if (o.is_string())
            {
                const std::string v = o.get<std::string>();
                if (v.starts_with("http://") || v.starts_with("https://"))
                    r.url = v;
                else if (v.starts_with("data:"))
                {
                    const size_t comma = v.find(',');
                    if (comma != std::string::npos)
                    {
                        std::string head = v.substr(5, comma - 5);
                        if (head.ends_with(";base64"))
                            head.resize(head.size() - 7);
                        r.mime = head;
                        r.data = v.substr(comma + 1);
                    }
                }
                else
                {
                    r.data = v; // 裸 base64
                }
            }
            else if (o.is_object())
            {
                auto sv = [&](const char *k) -> std::string
                {
                    auto it = o.find(k);
                    return (it != o.end() && it->is_string()) ? it->get<std::string>() : std::string();
                };
                if (r.kind.empty())
                    r.kind = sv("kind");
                r.name = sv("name");
                r.mime = sv("mime");
                r.url = sv("url");
                const std::string b64 = !sv("data_b64").empty() ? sv("data_b64") : sv("data");
                if (!b64.empty())
                {
                    r.data = b64;
                    if (r.data.starts_with("base64://"))
                        r.data.erase(0, 9);
                }
                r.raw = o;
                const std::string path = sv("path");
                if (r.local.empty() && !path.empty())
                    r.raw["path"] = path;
                if (r.name.empty() && !path.empty())
                {
                    const size_t slash = path.find_last_of("/\\");
                    r.name = slash == std::string::npos ? path : path.substr(slash + 1);
                }
            }
            else
            {
                return;
            }
            if (r.kind.empty())
                r.kind = "file";
            if (r.name.empty())
                r.name = detail::default_name_for(r.kind);
            if (r.mime.empty())
                r.mime = detail::mime_for_name(r.name);
            const bool has_path = r.raw.is_object() && r.raw.contains("path") && r.raw["path"].is_string();
            if (r.url.empty() && r.data.empty() && !has_path)
                return; // 没有任何可取的内容
            m.media.push_back(std::move(r));
        };
        if (j.contains("media") && j["media"].is_array())
            for (const auto &e : j["media"])
                push_media(e);
        for (const char *k : {"image", "sticker"})
        {
            auto it = j.find(k);
            if (it == j.end())
                continue;
            if (it->is_array())
                for (const auto &e : *it)
                    push_media(e, k);
            else
                push_media(*it, k);
        }
        if (auto it = j.find("file"); it != j.end())
        {
            if (it->is_array())
                for (const auto &e : *it)
                    push_media(e, "file");
            else
                push_media(*it, "file");
        }
        return !m.chat.empty() && (!m.text.empty() || !m.media.empty());
        }
        catch (...)
        {
            return false; // 契约 3.3：解析函数不抛异常
        }
    }

    inline bool send_bridge(const channel &ch, const std::string &chat, const std::string &text,
                            std::string &err, const std::atomic<bool> *abort = nullptr)
    {
        if (ch.api_base.empty())
        {
            err = "桥没有填接收地址";
            return false;
        }
        detail::headers hdrs;
        if (!ch.token.empty())
            hdrs.push_back("X-Im-Secret: " + ch.token.str());
        nlohmann::json body = {{"chat_id", chat}, {"text", text}};
        auto r = detail::call(ch, ch.api_base,
                              body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                              hdrs, 30, abort);
        if (!r.ok)
        {
            err = r.err;
            return false;
        }
        return true;
    }

    // ---- 桥过来的媒体：path 本地读 / data_b64 解码 / url GET ----
    inline bool bridge_fetch(const channel &ch, const media_ref &ref, std::string &bytes,
                             std::string &name, std::string &mime, std::string &err,
                             const std::atomic<bool> *abort)
    {
        auto finish = [&]() -> bool
        {
            if (bytes.empty())
                return false;
            if (name.empty())
                name = detail::default_name_for(ref.kind);
            if (mime.empty())
                mime = detail::mime_for_name(name);
            if (detail::file_ext(name).empty())
                name += "." + detail::ext_for_mime(mime);
            return true;
        };
        std::string path;
        if (ref.raw.is_object() && ref.raw.contains("path") && ref.raw["path"].is_string())
            path = ref.raw["path"].get<std::string>();
        if (!path.empty() && detail::read_file(path, bytes))
            return finish();
        if (!ref.data.empty())
        {
            std::string b64 = ref.data;
            if (b64.starts_with("base64://"))
                b64.erase(0, 9);
            bytes = detail::b64_decode(b64);
            if (finish())
                return true;
        }
        if (ref.url.starts_with("http://") || ref.url.starts_with("https://"))
            return detail::download(ch, ref.url, bytes, err, abort) && finish();
        err = "桥过来的媒体没有可用的 path / data_b64 / url";
        return false;
    }

    // ---- 桥出站媒体：单条 ≤ 24 MiB ----
    inline bool send_bridge_media(const channel &ch, const std::string &chat, const std::string &kind,
                                  const std::string &path, const std::string &name,
                                  const std::string &mime, std::string &err,
                                  const std::atomic<bool> *abort)
    {
        if (ch.api_base.empty())
        {
            err = "桥没有填接收地址";
            return false;
        }
        std::string bytes;
        if (!detail::read_file(path, bytes))
        {
            err = "读不到本地文件：" + path;
            return false;
        }
        if (bytes.size() > 24ull * 1024 * 1024)
        {
            err = "桥单条媒体不能超过 24 MiB";
            return false;
        }
        detail::headers hdrs;
        if (!ch.token.empty())
            hdrs.push_back("X-Im-Secret: " + ch.token.str());
        nlohmann::json item = {{"kind", kind},
                               {"name", name},
                               {"mime", mime},
                               {"data_b64", detail::b64_encode(bytes)}};
        nlohmann::json body = {{"chat_id", chat}, {"media", nlohmann::json::array({item})}};
        auto r = detail::call(ch, ch.api_base,
                              body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                              hdrs, 120, abort);
        if (!r.ok)
        {
            err = r.err;
            return false;
        }
        return true;
    }

    // =====================================================================
    //  发送 / 探测
    // =====================================================================
    inline bool send_chunk(const channel &ch, const std::string &chat, bool group,
                           const std::string &text, std::string &err, const std::atomic<bool> *abort = nullptr)
    {
        if (ch.kind == "telegram")
        {
            if (ch.token.empty())
            {
                err = "缺少 bot token";
                return false;
            }
            nlohmann::json body = {{"chat_id", chat}, {"text", text}, {"disable_web_page_preview", true}};
            auto r = detail::call(ch, tg_url(ch, "sendMessage"),
                                  body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace),
                                  {}, 30, abort);
            if (!r.ok)
            {
                auto j = nlohmann::json::parse(r.body, nullptr, false);
                err = j.is_object() ? j.value("description", r.err) : r.err;
                return false;
            }
            return true;
        }
        if (ch.kind == "onebot")
            return send_onebot(ch, chat, group, text, err, abort);
        if (ch.kind == "wecom")
            return send_wecom(ch, chat, text, err, abort);
        if (ch.kind == "bridge")
            return send_bridge(ch, chat, text, err, abort);
        if (ch.kind == "wechat")
            return send_ilink(ch, chat, text, err, abort);
        err = "渠道没有启用";
        return false;
    }

    // 节流回调：返回 false = 中止（正在退出 / 渠道被停用）
    using pace_fn = std::function<bool()>;

    // 一条回复：按平台上限自动分条（最多 4 条，多的截断）
    // pace 在每次真正调用平台 API 之前被问一次（含分条），返回 false 表示中止。
    // 节流放在这里而不是调用方，是因为「一次 send 会变成几条 API 调用」只有
    // 这一层知道。
    inline bool send(const channel &ch, const std::string &chat, bool group,
                     const std::string &text, std::string &err,
                     const std::atomic<bool> *abort = nullptr, const pace_fn &pace = {})
    {
        if (!ch.live())
        {
            err = "渠道没有启用";
            return false;
        }
        size_t limit = (size_t)(ch.limit > 0 ? ch.limit : 0);
        bool bytes = ch.kind == "wecom";
        if (limit == 0)
            limit = ch.kind == "telegram" ? 4096
                    : ch.kind == "onebot" ? 4000
                    : ch.kind == "wecom"  ? 1800
                                          : 3500;
        auto parts = detail::split(text, limit, bytes);
        if (parts.size() > 4)
        {
            parts.resize(4);
            parts.back() += "…";
        }
        if (parts.empty())
        {
            err = "空消息";
            return false;
        }
        for (auto &p : parts)
        {
            if (pace && !pace())
            {
                err = "已取消";
                return false;
            }
            if (!send_chunk(ch, chat, group, p, err, abort))
                return false;
        }
        return true;
    }

    // =====================================================================
    //  媒体总入口（app-core 只调这两个；平台细节都在上面各节里）
    // =====================================================================
    // 下载一段进来的媒体：协议细节全在这里。成功时 bytes/name/mime 有效，失败 err 给人看。
    inline bool fetch(const channel &ch, const media_ref &ref, std::string &bytes,
                      std::string &name, std::string &mime, std::string &err,
                      const std::atomic<bool> *abort = nullptr)
    {
        bytes.clear();
        name = ref.name;
        mime = ref.mime;
        err.clear();
        if (!ch.live())
        {
            err = "渠道没有启用";
            return false;
        }
        bool ok = false;
        if (ch.kind == "telegram")
            ok = tg_fetch(ch, ref, bytes, name, mime, err, abort);
        else if (ch.kind == "onebot")
            ok = onebot_fetch(ch, ref, bytes, name, mime, err, abort);
        else if (ch.kind == "wecom")
            ok = wecom_fetch(ch, ref, bytes, name, mime, err, abort);
        else if (ch.kind == "wechat")
            ok = ilink_fetch(ch, ref, bytes, name, mime, err, abort);
        else if (ch.kind == "bridge")
            ok = bridge_fetch(ch, ref, bytes, name, mime, err, abort);
        else
        {
            err = "渠道没有启用";
            return false;
        }
        if (!ok)
        {
            bytes.clear();
            return false;
        }
        if (name.empty())
            name = detail::default_name_for(ref.kind);
        if (mime.empty())
            mime = detail::mime_for_name(name);
        return true;
    }

    // 发一段本地媒体（绝对路径）。kind: image|sticker|audio|video|file
    inline bool send_media(const channel &ch, const std::string &chat, bool group,
                           const std::string &kind, const std::string &path,
                           const std::string &name, const std::string &mime,
                           std::string &err, const std::atomic<bool> *abort = nullptr,
                           const pace_fn &pace = {})
    {
        err.clear();
        if (!ch.live())
        {
            err = "渠道没有启用";
            return false;
        }
        if (path.empty())
        {
            err = "没有本地文件路径";
            return false;
        }
        if (pace && !pace())
        {
            err = "已取消";
            return false;
        }
        const std::string nm = name.empty() ? detail::default_name_for(kind) : name;
        const std::string mm = mime.empty() ? detail::mime_for_name(nm) : mime;
        if (ch.kind == "telegram")
            return send_telegram_media(ch, chat, kind, path, nm, mm, err, abort);
        if (ch.kind == "onebot")
            return onebot_send_media(ch, chat, group, kind, path, nm, mm, err, abort);
        if (ch.kind == "wecom")
            return wecom_send_media(ch, chat, kind, path, nm, mm, err, abort);
        if (ch.kind == "wechat")
            return ilink_send_media(ch, chat, kind, path, nm, mm, err, abort);
        if (ch.kind == "bridge")
            return send_bridge_media(ch, chat, kind, path, nm, mm, err, abort);
        err = "渠道没有启用";
        return false;
    }

    // 连接测试：能拿到自己的身份才算真的通了
    inline bool probe(const channel &ch, nlohmann::json &info, std::string &err)
    {
        info = nlohmann::json::object();
        if (ch.kind == "telegram")
        {
            identity self;
            if (!probe_telegram(ch, self, err))
                return false;
            info = {{"username", self.username}, {"id", self.id}};
            return true;
        }
        if (ch.kind == "onebot")
        {
            detail::headers hdrs;
            if (!ch.token.empty())
                hdrs.push_back("Authorization: Bearer " + ch.token.str());
            auto r = detail::call(ch, api_root(ch) + "/get_login_info", "", hdrs, 15);
            if (!r.ok)
            {
                err = r.err;
                return false;
            }
            auto j = nlohmann::json::parse(r.body, nullptr, false);
            if (!j.is_object() || j.value("retcode", 0LL) != 0)
            {
                err = j.is_object() ? j.value("wording", std::string("get_login_info 失败")) : "返回不是 JSON";
                return false;
            }
            auto d = j.find("data");
            info = (d != j.end() && d->is_object()) ? *d : nlohmann::json::object();
            return true;
        }
        if (ch.kind == "wecom")
        {
            std::string tok = wecom_access_token(ch, err);
            if (tok.empty())
                return false;
            detail::zero_guard tok_guard{tok};
            info = {{"agent_id", ch.agent_id}, {"token", "ok"}};
            return true;
        }
        if (ch.kind == "wechat")
        {
            if (ch.bot_token.empty())
            {
                err = "还没有扫码登录";
                return false;
            }
            info = {{"bot_id", ch.bot_id}, {"base", ilink_root(ch)}};
            return true;
        }
        if (ch.kind == "bridge")
        {
            if (ch.api_base.empty())
            {
                err = "桥没有填接收地址";
                return false;
            }
            if (ch.token.empty())
            {
                err = "建议填一个 secret：不填的话任何本地进程都能往这个渠道灌消息";
                return true;
            }
            info = {{"send_url", ch.api_base}, {"secret", "ok"}};
            return true;
        }
        err = "渠道没有启用";
        return false;
    }
} // namespace app::im
