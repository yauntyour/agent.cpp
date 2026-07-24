#include "utils/http.hpp"
#include <iostream>
#include <cstring>

namespace agent::http {

// ── HttpClient ────────────────────────────────────────────────────
HttpClient::HttpClient() {
    curl_global_init(CURL_GLOBAL_ALL);
    m_handle = curl_easy_init();
}

HttpClient::~HttpClient() {
    if (m_handle) curl_easy_cleanup(m_handle);
    curl_global_cleanup();
}

HttpClient& HttpClient::instance() {
    static HttpClient client;
    return client;
}

size_t HttpClient::write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* body = static_cast<std::string*>(userp);
    size_t total = size * nmemb;
    body->append(static_cast<char*>(contents), total);
    return total;
}

size_t HttpClient::header_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* headers = static_cast<std::map<std::string, std::string>*>(userp);
    size_t total = size * nmemb;
    std::string line(static_cast<char*>(contents), total);
    auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();
        while (!value.empty() && (value.front() == ' ')) value.erase(0, 1);
        headers->emplace(std::move(key), std::move(value));
    }
    return total;
}

int HttpClient::progress_callback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                                   curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal; (void)ulnow;
    auto* callback = static_cast<std::function<bool(int64_t, int64_t)>*>(clientp);
    if (callback && *callback) {
        return (*callback)(dlnow, dltotal) ? 0 : 1;
    }
    return 0;
}

HttpResponse HttpClient::request(const HttpRequest& req) {
    HttpResponse resp;
    std::string body;
    std::map<std::string, std::string> headers;

    curl_easy_setopt(m_handle, CURLOPT_URL, req.url.c_str());
    curl_easy_setopt(m_handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(m_handle, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(m_handle, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(m_handle, CURLOPT_HEADERDATA, &headers);
    curl_easy_setopt(m_handle, CURLOPT_TIMEOUT, req.timeout_sec > 0 ? (long)req.timeout_sec : 60L);
    curl_easy_setopt(m_handle, CURLOPT_CONNECTTIMEOUT, req.connect_timeout_sec > 0 ? req.connect_timeout_sec : 30L);
    curl_easy_setopt(m_handle, CURLOPT_FOLLOWLOCATION, req.follow_redirects ? 1L : 0L);
    curl_easy_setopt(m_handle, CURLOPT_SSL_VERIFYPEER, req.verify_ssl ? 1L : 0L);
    curl_easy_setopt(m_handle, CURLOPT_SSL_VERIFYHOST, req.verify_ssl ? 2L : 0L);

    if (!req.proxy.empty()) {
        curl_easy_setopt(m_handle, CURLOPT_PROXY, req.proxy.c_str());
    } else if (!m_proxy.empty()) {
        curl_easy_setopt(m_handle, CURLOPT_PROXY, m_proxy.c_str());
    }

    if (!m_user_agent.empty()) {
        curl_easy_setopt(m_handle, CURLOPT_USERAGENT, m_user_agent.c_str());
    }

    struct curl_slist* header_list = nullptr;
    auto cleanup_headers = [&]() { if (header_list) curl_slist_free_all(header_list); };

    for (auto& [k, v] : m_default_headers) {
        std::string h = k + ": " + v;
        header_list = curl_slist_append(header_list, h.c_str());
    }
    for (auto& [k, v] : req.headers) {
        std::string h = k + ": " + v;
        header_list = curl_slist_append(header_list, h.c_str());
    }

    if (req.method == "POST") {
        curl_easy_setopt(m_handle, CURLOPT_POST, 1L);
        curl_easy_setopt(m_handle, CURLOPT_POSTFIELDS, req.body.c_str());
        curl_easy_setopt(m_handle, CURLOPT_POSTFIELDSIZE, (long)req.body.size());
    } else if (req.method == "PUT") {
        curl_easy_setopt(m_handle, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(m_handle, CURLOPT_POSTFIELDS, req.body.c_str());
        curl_easy_setopt(m_handle, CURLOPT_POSTFIELDSIZE, (long)req.body.size());
    } else if (req.method == "DELETE") {
        curl_easy_setopt(m_handle, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    curl_easy_setopt(m_handle, CURLOPT_HTTPHEADER, header_list);

    if (req.resume_from >= 0) {
        curl_easy_setopt(m_handle, CURLOPT_RESUME_FROM_LARGE, req.resume_from);
    }

    if (req.progress_callback) {
        curl_easy_setopt(m_handle, CURLOPT_XFERINFOFUNCTION, progress_callback);
        curl_easy_setopt(m_handle, CURLOPT_XFERINFODATA, &req.progress_callback);
        curl_easy_setopt(m_handle, CURLOPT_NOPROGRESS, 0L);
    }

    CURLcode res = curl_easy_perform(m_handle);

    cleanup_headers();

    if (res != CURLE_OK) {
        resp.error = curl_easy_strerror(res);
    } else {
        long status = 0;
        curl_easy_getinfo(m_handle, CURLINFO_RESPONSE_CODE, &status);
        resp.status_code = status;
        resp.body = std::move(body);
        resp.headers = std::move(headers);
    }

    return resp;
}

HttpResponse HttpClient::get(std::string_view url, const std::map<std::string, std::string>& headers) {
    HttpRequest req;
    req.url = url;
    req.method = "GET";
    req.headers = headers;
    return request(req);
}

HttpResponse HttpClient::post(std::string_view url, std::string_view body,
                               const std::map<std::string, std::string>& headers) {
    HttpRequest req;
    req.url = url;
    req.method = "POST";
    req.body = body;
    req.headers = headers;
    return request(req);
}

HttpResponse HttpClient::download(std::string_view url, const std::string& filepath, int64_t resume_from) {
    HttpRequest req;
    req.url = url;
    req.method = "GET";
    if (resume_from >= 0) req.resume_from = resume_from;

    // Instead of using internal file writing, we use write callback redirect
    // For simplicity, use request() and then write to file
    auto resp = request(req);
    if (!resp.ok()) return resp;

    // Write body to file
    if (resume_from >= 0) {
        std::ofstream f(filepath, std::ios::app | std::ios::binary);
        f.write(resp.body.data(), resp.body.size());
    } else {
        std::ofstream f(filepath, std::ios::binary);
        f.write(resp.body.data(), resp.body.size());
    }
    return resp;
}

void HttpClient::set_default_proxy(std::string_view proxy) {
    m_proxy = proxy;
}

void HttpClient::set_default_user_agent(std::string_view ua) {
    m_user_agent = ua;
}

void HttpClient::set_default_headers(const std::map<std::string, std::string>& headers) {
    m_default_headers = headers;
}

// ── SSEParser ─────────────────────────────────────────────────────
void SSEParser::feed(std::string_view chunk, EventCallback callback) {
    m_buffer.append(chunk);

    size_t pos = 0;
    while (pos < m_buffer.size()) {
        size_t line_end = m_buffer.find('\n', pos);
        if (line_end == std::string::npos) line_end = m_buffer.size();

        std::string_view line(m_buffer.data() + pos, line_end - pos);

        // Remove \r if present
        if (!line.empty() && line.back() == '\r') {
            line = line.substr(0, line.size() - 1);
        }

        if (line.empty()) {
            // Empty line = event complete
            if (!m_data.empty() || !m_event_type.empty()) {
                callback(m_event_type, m_data);
                m_event_type.clear();
                m_data.clear();
            }
        } else if (line.starts_with("event:")) {
            m_event_type = std::string(line.substr(6));
            // Trim leading space
            if (!m_event_type.empty() && m_event_type.front() == ' ') {
                m_event_type.erase(0, 1);
            }
        } else if (line.starts_with("data:")) {
            std::string_view data_part = line.substr(5);
            if (!data_part.empty() && data_part.front() == ' ') {
                if (!m_data.empty()) m_data += '\n';
                m_data.append(data_part.substr(1));
            } else {
                if (!m_data.empty()) m_data += '\n';
                m_data.append(data_part);
            }
        }

        pos = (line_end == m_buffer.size()) ? line_end : line_end + 1;
    }

    // Remove processed data
    if (pos > 0 && pos < m_buffer.size()) {
        m_buffer.erase(0, pos);
    } else if (pos == m_buffer.size()) {
        m_buffer.clear();
    }
}

void SSEParser::reset() {
    m_buffer.clear();
    m_event_type.clear();
    m_data.clear();
}

// ── URLBuilder ────────────────────────────────────────────────────
URLBuilder::URLBuilder(std::string_view base_url) : m_url(base_url) {}

URLBuilder& URLBuilder::add_query(std::string_view key, std::string_view value) {
    char sep = m_url.find('?') == std::string::npos ? '?' : '&';
    m_url += sep;
    m_url += key;
    m_url += '=';
    // Basic URL encoding for value
    for (char c : value) {
        switch (c) {
        case ' ': m_url += "%20"; break;
        case '&': m_url += "%26"; break;
        case '=': m_url += "%3D"; break;
        case '+': m_url += "%2B"; break;
        default: m_url += c;
        }
    }
    return *this;
}

URLBuilder& URLBuilder::add_path(std::string_view segment) {
    if (!m_url.empty() && m_url.back() != '/') m_url += '/';
    m_url += segment;
    return *this;
}

std::string URLBuilder::build() const {
    return m_url;
}

} // namespace agent::http
