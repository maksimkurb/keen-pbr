#include "curl_runtime.hpp"

#include "../log/logger.hpp"

#include <curl/curl.h>
#include <mutex>
#include <stdexcept>

namespace keen_pbr3 {
namespace {
std::mutex runtime_mutex;
unsigned runtime_users = 0;
bool capabilities_logged = false;

bool feature_enabled(long features, long feature) {
    return (features & feature) != 0;
}
} // namespace

CurlRuntime::CurlRuntime() {
    std::lock_guard<std::mutex> lock(runtime_mutex);
    if (runtime_users++ != 0) return;
    const CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (result != CURLE_OK) {
        --runtime_users;
        throw std::runtime_error(std::string("curl_global_init failed: ") + curl_easy_strerror(result));
    }
    const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
    if (info && !capabilities_logged) {
        capabilities_logged = true;
        Logger::instance().verbose("curl runtime initialized: version={} asynch_dns={} thread_safe={}",
                                info->version,
                                feature_enabled(info->features, CURL_VERSION_ASYNCHDNS),
#ifdef CURL_VERSION_THREADSAFE
                                feature_enabled(info->features, CURL_VERSION_THREADSAFE)
#else
                                false
#endif
        );
    }
}

CurlRuntime::~CurlRuntime() {
    std::lock_guard<std::mutex> lock(runtime_mutex);
    if (--runtime_users == 0) curl_global_cleanup();
}

} // namespace keen_pbr3
