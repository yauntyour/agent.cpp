#pragma once
#include "core/common.hpp"
#include "core/text.hpp"

namespace net_unit
{
    inline void CURL_proxy(CURL *__handle__, const char *proxy, bool ssl = false)
    {
        curl_easy_setopt(__handle__, CURLOPT_PROXY, proxy);
        curl_easy_setopt(__handle__, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
        if (ssl)
            curl_easy_setopt(__handle__, CURLOPT_HTTPPROXYTUNNEL, 1L);
        curl_easy_setopt(__handle__, CURLOPT_PROXYUSERPWD, "user:password");
        curl_easy_setopt(__handle__, CURLOPT_PROXYAUTH, CURLAUTH_BASIC);
    }

    inline size_t CURL_WriteCallback(void *contents, size_t size, size_t nmemb, std::string &userp)
    {
        userp.append((char *)contents, size * nmemb);
        return size * nmemb;
    }

    inline bool CURL_get(CURL *__handle__, const char *URL, std::string &buf, std::string header = "")
    {
        if (!__handle__)
            return false;
        curl_easy_reset(__handle__);
        curl_easy_setopt(__handle__, CURLOPT_URL, URL);
        curl_easy_setopt(__handle__, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1"); // 本地服务不走系统代理
        curl_easy_setopt(__handle__, CURLOPT_WRITEFUNCTION, CURL_WriteCallback);
        curl_easy_setopt(__handle__, CURLOPT_WRITEDATA, &buf);

        struct curl_slist *headers = nullptr;
        if (!header.empty())
        {
            headers = curl_slist_append(headers, header.c_str());
            curl_easy_setopt(__handle__, CURLOPT_HTTPHEADER, headers);
        }

        CURLcode res = curl_easy_perform(__handle__);
        if (headers)
            curl_slist_free_all(headers);
        if (res != CURLE_OK)
        {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            return false;
        }
        return true;
    }

    inline bool CURL_get(CURL *__handle__, const char *URL, std::string &buf, const std::vector<std::string> &header_list)
    {
        if (!__handle__)
            return false;
        curl_easy_reset(__handle__);
        curl_easy_setopt(__handle__, CURLOPT_URL, URL);
        curl_easy_setopt(__handle__, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1"); // 本地服务不走系统代理
        curl_easy_setopt(__handle__, CURLOPT_WRITEFUNCTION, CURL_WriteCallback);
        curl_easy_setopt(__handle__, CURLOPT_WRITEDATA, &buf);

        struct curl_slist *headers = nullptr;
        for (auto &header : header_list)
        {
            headers = curl_slist_append(headers, header.c_str());
            curl_easy_setopt(__handle__, CURLOPT_HTTPHEADER, headers);
        }

        CURLcode res = curl_easy_perform(__handle__);
        if (headers)
            curl_slist_free_all(headers);
        if (res != CURLE_OK)
        {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            return false;
        }
        return true;
    }

    inline bool CURL_post(CURL *__handle__, const char *URL, const std::string &data, std::string &buf, std::string header = "")
    {
        if (!__handle__)
            return false;
        curl_easy_reset(__handle__);
        curl_easy_setopt(__handle__, CURLOPT_URL, URL);
        curl_easy_setopt(__handle__, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1"); // 本地服务不走系统代理
        curl_easy_setopt(__handle__, CURLOPT_POST, 1L);
        curl_easy_setopt(__handle__, CURLOPT_POSTFIELDS, data.c_str());
        curl_easy_setopt(__handle__, CURLOPT_POSTFIELDSIZE, data.size());

        struct curl_slist *headers = nullptr;
        if (!header.empty())
        {
            headers = curl_slist_append(headers, header.c_str());
            curl_easy_setopt(__handle__, CURLOPT_HTTPHEADER, headers);
        }

        curl_easy_setopt(__handle__, CURLOPT_WRITEFUNCTION, CURL_WriteCallback);
        curl_easy_setopt(__handle__, CURLOPT_WRITEDATA, &buf);

        CURLcode res = curl_easy_perform(__handle__);
        if (headers)
            curl_slist_free_all(headers);
        if (res != CURLE_OK)
        {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            return false;
        }
        return true;
    }

    inline bool CURL_post(CURL *__handle__, const char *URL, const std::string &data, std::string &buf, const std::vector<std::string> &header_list)
    {
        if (!__handle__)
            return false;
        curl_easy_reset(__handle__);
        curl_easy_setopt(__handle__, CURLOPT_URL, URL);
        curl_easy_setopt(__handle__, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1"); // 本地服务不走系统代理
        curl_easy_setopt(__handle__, CURLOPT_POST, 1L);
        curl_easy_setopt(__handle__, CURLOPT_POSTFIELDS, data.c_str());
        curl_easy_setopt(__handle__, CURLOPT_POSTFIELDSIZE, data.size());

        struct curl_slist *headers = nullptr;
        for (auto &header : header_list)
        {
            headers = curl_slist_append(headers, header.c_str());
            curl_easy_setopt(__handle__, CURLOPT_HTTPHEADER, headers);
        }

        curl_easy_setopt(__handle__, CURLOPT_WRITEFUNCTION, CURL_WriteCallback);
        curl_easy_setopt(__handle__, CURLOPT_WRITEDATA, &buf);

        CURLcode res = curl_easy_perform(__handle__);
        if (headers)
            curl_slist_free_all(headers);
        if (res != CURLE_OK)
        {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            return false;
        }
        return true;
    }

    using StreamCallback = std::function<void(const char *, size_t)>;
    inline size_t WriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
        StreamCallback *callback = (StreamCallback *)userdata;
        try
        {
            (*callback)(ptr, size *nmemb);
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
        }
        return size * nmemb;
    }

    inline bool CURL_stream_post(CURL *curl, const char *url, const std::string &post_data, const std::string &header, StreamCallback on_token)
    {
        if (!curl || !url)
            return false;

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1"); // 本地服务不走系统代理
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &on_token);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        struct curl_slist *header_list = nullptr;
        header_list = curl_slist_append(header_list, header.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            std::cerr << "ERROR - stream_post(" << url << ") failed: " << curl_easy_strerror(res) << std::endl;
        curl_slist_free_all(header_list);
        return (res == CURLE_OK);
    }

    inline bool CURL_stream_post(CURL *curl, const char *url, const std::string &post_data, const std::vector<std::string> &header_list, StreamCallback on_token)
    {
        if (!curl || !url)
            return false;

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1"); // 本地服务不走系统代理
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &on_token);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        struct curl_slist *headers = nullptr;
        for (auto &header : header_list)
            headers = curl_slist_append(headers, header.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            std::cerr << "ERROR - stream_post(" << url << ") failed: " << curl_easy_strerror(res) << std::endl;
        curl_slist_free_all(headers);
        return (res == CURLE_OK);
    }
} // namespace net_unit

namespace net
{
    // ════════════════════════════════════════════════════════════════
    // HTTP 请求（每线程独立 CURL*，支持响应头捕获 / 代理 / 超时）
    // ════════════════════════════════════════════════════════════════
    struct HttpResp
    {
        long status = 0;
        std::string body;
        std::map<std::string, std::string> headers;
    };

    namespace http_impl
    {
        inline size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp)
        {
            ((std::string *)userp)->append((char *)contents, size * nmemb);
            return size * nmemb;
        }
        inline size_t header_cb(char *buffer, size_t size, size_t nitems, void *userdata)
        {
            auto *h = (std::map<std::string, std::string> *)userdata;
            size_t n = size * nitems;
            std::string line(buffer, n);
            size_t colon = line.find(':');
            if (colon != std::string::npos)
            {
                std::string key = to_lower_ascii(line.substr(0, colon));
                std::string val = line.substr(colon + 1);
                // 去掉行尾 \r\n 与首尾空白
                while (!val.empty() && (val.back() == '\r' || val.back() == '\n'))
                    val.pop_back();
                size_t lead = val.find_first_not_of(" \t");
                val = (lead == std::string::npos) ? "" : val.substr(lead);
                (*h)[key] = val;
            }
            return n;
        }
    } // namespace http_impl

    inline HttpResp http_request(const std::string &method, const std::string &url,
                                 const std::string &body, long timeout,
                                 const std::map<std::string, std::string> &hdrs,
                                 const std::string &proxy = "")
    {
        HttpResp resp;
        CURL *curl = curl_easy_init();
        if (!curl)
            return resp;
        std::string header_buf;
        std::map<std::string, std::string> captured;
        struct curl_slist *list = nullptr;
        for (auto &kv : hdrs)
            list = curl_slist_append(list, (kv.first + ": " + kv.second).c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_impl::write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, http_impl::header_cb);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &captured);
        if (!proxy.empty())
        {
            curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPPROXYTUNNEL, 1L);
        }
        std::string m = to_lower_ascii(method);
        if (m == "post" || m == "put")
        {
            // 用原始方法名（POST/PUT），不能小写：CURLOPT_CUSTOMREQUEST 会原样写入
            // 请求行，小写 "post" 会被服务器（如 ilink WAF）拒绝/断开
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
            if (!body.empty())
            {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.size());
            }
        }
        else
        {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        }
        if (list)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK)
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status);
        curl_slist_free_all(list);
        curl_easy_cleanup(curl);
        resp.headers = std::move(captured);
        return resp;
    }

    inline HttpResp http_get(const std::string &url, long timeout,
                             const std::map<std::string, std::string> &hdrs = {},
                             const std::string &proxy = "")
    {
        return http_request("GET", url, "", timeout, hdrs, proxy);
    }

    inline HttpResp http_post(const std::string &url, const std::string &body, long timeout,
                              const std::map<std::string, std::string> &hdrs = {},
                              const std::string &proxy = "")
    {
        return http_request("POST", url, body, timeout, hdrs, proxy);
    }
} // namespace net
