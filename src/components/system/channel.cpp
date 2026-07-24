#include "components/system/system.hpp"
#include "components/agent/agent.hpp"
#include "utils/http.hpp"
#include <nlohmann/json.hpp>
#include <thread>
#include <atomic>
#include <iostream>

namespace agent {

using json = nlohmann::json;

namespace {

class TelegramBot {
public:
    TelegramBot(std::string_view token, std::string_view proxy = "")
        : m_token(token), m_proxy(proxy), m_running(false) {}

    void start(System::ChannelDriver& driver) {
        if (m_running.load()) return;
        m_running.store(true);

        m_thread = std::thread([this, &driver]() {
            int last_update_id = 0;

            while (m_running.load()) {
                try {
                    auto updates = get_updates(last_update_id);
                    for (auto& update : updates) {
                        int update_id = update.value("update_id", 0);
                        if (update_id > last_update_id) last_update_id = update_id;

                        if (update.contains("message")) {
                            auto& msg = update["message"];
                            std::string text = msg.value("text", "");
                            int64_t chat_id = msg["chat"].value("id", 0LL);

                            if (!text.empty() && chat_id != 0) {
                                if (driver.on_message) {
                                    driver.on_message(text, msg);
                                }

                                auto& agent = ModuleRegistry::instance().require<Agent>();
                                auto result = agent.execute(text, nullptr, false);

                                std::string reply = result.success ? result.output : "Error: " + result.error;
                                if (reply.size() > 4000) reply = reply.substr(0, 4000) + "...";

                                send_message(chat_id, reply);
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Telegram bot error: " << e.what() << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                }
            }
        });
    }

    void stop() {
        m_running.store(false);
        if (m_thread.joinable()) m_thread.join();
    }

    bool send_message(int64_t chat_id, std::string_view text) {
        auto& client = http::HttpClient::instance();
        if (!m_proxy.empty()) client.set_default_proxy(m_proxy);

        std::string url = "https://api.telegram.org/bot" + m_token + "/sendMessage";

        json body = {
            {"chat_id", chat_id},
            {"text", text},
            {"parse_mode", "Markdown"}
        };

        auto resp = client.post(url, body.dump(), {{"Content-Type", "application/json"}});
        return resp.ok();
    }

private:
    json get_updates(int last_update_id) {
        auto& client = http::HttpClient::instance();
        if (!m_proxy.empty()) client.set_default_proxy(m_proxy);

        std::string url = "https://api.telegram.org/bot" + m_token +
                          "/getUpdates?offset=" + std::to_string(last_update_id + 1) +
                          "&timeout=30&allowed_updates=[\"message\"]";

        http::HttpRequest req;
        req.url = url;
        req.method = "GET";
        req.timeout_sec = 35;

        auto resp = client.request(req);
        if (!resp.ok()) return json::array();

        auto j = json::parse(resp.body, nullptr, false);
        if (j.is_discarded() || !j.contains("result")) return json::array();
        return j["result"];
    }

    std::string m_token;
    std::string m_proxy;
    std::atomic<bool> m_running;
    std::thread m_thread;
};

std::vector<std::unique_ptr<TelegramBot>> g_bots;

} // anonymous namespace

void System::start_channels() {
    auto& cfg = Config::instance();

    for (auto& ch_cfg : cfg.channels) {
        if (!ch_cfg.enabled) continue;

        if (ch_cfg.type == "telegram") {
            auto bot = std::make_unique<TelegramBot>(ch_cfg.token, ch_cfg.proxy);

            ChannelDriver driver;
            driver.type = "telegram";
            driver.name = "Telegram Bot";
            driver.send_message = [bot_ptr = bot.get()](std::string_view text) {
                // Default chat_id - in practice you'd track per-chat
            };

            bot->start(driver);
            g_bots.push_back(std::move(bot));
            m_channels.push_back(driver);
        }
    }
}

void System::stop_channels() {
    for (auto& bot : g_bots) {
        bot->stop();
    }
    g_bots.clear();

    for (auto& ch : m_channels) {
        ch.send_message = nullptr;
        ch.on_message = nullptr;
    }
    m_channels.clear();
}

} // namespace agent
