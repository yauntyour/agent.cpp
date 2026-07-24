#pragma once
#include <string>
#include <string_view>
#include <map>
#include <vector>
#include <optional>
#include <functional>
#include <curl/curl.h>

namespace agent::http {

struct HttpResponse {
    long status_code = 0;
    std::string body;
    std::map<std::string, std::string> headers;
    std::string error;
    bool ok() const { return status_code >= 200 && status_code < 300 && error.empty(); }
};

struct HttpRequest {
    std::string url;
    std::string method = "GET";
    std::map<std::string, std::string> headers;
    std::string body;
    std::string proxy;
    int timeout_sec = 60;
    bool follow_redirects = true;
    bool verify_ssl = true;
    long connect_timeout_sec = 30;

    int64_t resume_from = -1;
    std::function<bool(int64_t downloaded, int64_t total)> progress_callback;
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    static HttpClient& instance();

    HttpResponse request(const HttpRequest& req);
    HttpResponse get(std::string_view url, const std::map<std::string, std::string>& headers = {});
    HttpResponse post(std::string_view url, std::string_view body, const std::map<std::string, std::string>& headers = {});
    HttpResponse download(std::string_view url, const std::string& filepath, int64_t resume_from = -1);

    void set_default_proxy(std::string_view proxy);
    void set_default_user_agent(std::string_view ua);
    void set_default_headers(const std::map<std::string, std::string>& headers);

private:
    static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp);
    static size_t header_callback(void* contents, size_t size, size_t nmemb, void* userp);
    static int progress_callback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);

    std::string m_proxy;
    std::string m_user_agent;
    std::map<std::string, std::string> m_default_headers;
};

class SSEParser {
public:
    using EventCallback = std::function<void(std::string_view event, std::string_view data)>;

    void feed(std::string_view chunk, EventCallback callback);
    void reset();

private:
    std::string m_buffer;
    std::string m_event_type;
    std::string m_data;
};

class URLBuilder {
public:
    explicit URLBuilder(std::string_view base_url);

    URLBuilder& add_query(std::string_view key, std::string_view value);
    URLBuilder& add_path(std::string_view segment);
    std::string build() const;

private:
    std::string m_url;
};

} // namespace agent::http
