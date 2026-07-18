#include "http_transport.hpp"

#include <cerrno>
#include <climits>
#include <cctype>
#include <cstring>
#include <curl/curl.h>
#include <memory>
#include <sys/socket.h>

namespace keen_pbr3 {
namespace {
struct EasyDeleter { void operator()(CURL* curl) const { if (curl) curl_easy_cleanup(curl); } };
struct SlistDeleter { void operator()(curl_slist* list) const { if (list) curl_slist_free_all(list); } };
using EasyHandle = std::unique_ptr<CURL, EasyDeleter>;
using HeaderList = std::unique_ptr<curl_slist, SlistDeleter>;

[[noreturn]] void fail(CURLcode code, const char* operation) {
    throw HttpTransportError(std::string(operation) + ": " + curl_easy_strerror(code));
}
template <typename T>
void setopt(CURL* curl, CURLoption option, T value) {
    const CURLcode rc = curl_easy_setopt(curl, option, value);
    if (rc != CURLE_OK) fail(rc, "curl_easy_setopt");
}

struct TransferContext { const HttpTransportRequest* request; HttpTransportResponse* response; int mark_errno{0}; };
size_t write_callback(char* data, size_t size, size_t count, void* opaque) {
    if (count && size > SIZE_MAX / count) return 0;
    auto* context = static_cast<TransferContext*>(opaque);
    const size_t total = size * count;
    if (context->request->discard_body) return total;
    if (total > context->request->max_response_size - context->response->body.size()) return 0;
    context->response->body.append(data, total);
    return total;
}
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}
size_t header_callback(char* data, size_t size, size_t count, void* opaque) {
    if (count && size > SIZE_MAX / count) return 0;
    auto* response = static_cast<HttpTransportResponse*>(opaque);
    std::string line(data, size * count);
    if (line.rfind("HTTP/", 0) == 0) { response->headers.clear(); return line.size(); }
    const auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string name = line.substr(0, colon);
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        response->headers[name] = trim(line.substr(colon + 1));
    }
    return line.size();
}
int sockopt_callback(void* opaque, curl_socket_t fd, curlsocktype) {
    auto* context = static_cast<TransferContext*>(opaque);
    if (context->request->fwmark == 0) return CURL_SOCKOPT_OK;
    const uint32_t mark = context->request->fwmark;
    if (setsockopt(fd, SOL_SOCKET, SO_MARK, &mark, sizeof(mark)) == 0) return CURL_SOCKOPT_OK;
    context->mark_errno = errno;
    return CURL_SOCKOPT_ERROR;
}
void restrict_protocols(CURL* curl) {
#if LIBCURL_VERSION_NUM >= 0x075500
    setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    setopt(curl, CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    setopt(curl, CURLOPT_REDIR_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
}
} // namespace

HttpTransportResponse LibcurlHttpTransport::perform(const HttpTransportRequest& request) {
    EasyHandle curl(curl_easy_init());
    if (!curl) throw HttpTransportError("Failed to initialize curl handle");
    HttpTransportResponse response;
    TransferContext context{&request, &response};
    char error_buffer[CURL_ERROR_SIZE] = {};
    setopt(curl.get(), CURLOPT_URL, request.url.c_str());
    setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    setopt(curl.get(), CURLOPT_TIMEOUT_MS, request.timeout_ms);
    setopt(curl.get(), CURLOPT_USERAGENT, request.user_agent.c_str());
    setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    setopt(curl.get(), CURLOPT_MAXREDIRS, request.max_redirects);
    setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_callback);
    setopt(curl.get(), CURLOPT_WRITEDATA, &context);
    setopt(curl.get(), CURLOPT_HEADERFUNCTION, header_callback);
    setopt(curl.get(), CURLOPT_HEADERDATA, &response);
    setopt(curl.get(), CURLOPT_SOCKOPTFUNCTION, sockopt_callback);
    setopt(curl.get(), CURLOPT_SOCKOPTDATA, &context);
    setopt(curl.get(), CURLOPT_ERRORBUFFER, error_buffer);
    if (!request.discard_body) setopt(curl.get(), CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(request.max_response_size));
    restrict_protocols(curl.get());
    HeaderList headers;
    for (const auto& header : request.headers) {
        curl_slist* appended = curl_slist_append(headers.get(), header.c_str());
        if (!appended) throw HttpTransportError("Failed to allocate HTTP request header");
        headers.release(); headers.reset(appended);
    }
    if (headers) setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    const auto started = std::chrono::steady_clock::now();
    const CURLcode result = curl_easy_perform(curl.get());
    response.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    if (result != CURLE_OK) {
        std::string message = error_buffer[0] ? error_buffer : curl_easy_strerror(result);
        if (context.mark_errno) message += "; SO_MARK failed: " + std::string(std::strerror(context.mark_errno));
        throw HttpTransportError("HTTP request failed: " + message);
    }
    const CURLcode info = curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response.status_code);
    if (info != CURLE_OK) fail(info, "curl_easy_getinfo(CURLINFO_RESPONSE_CODE)");
    return response;
}

std::shared_ptr<HttpTransport> default_http_transport() {
    static const std::shared_ptr<HttpTransport> transport = std::make_shared<LibcurlHttpTransport>();
    return transport;
}
} // namespace keen_pbr3
