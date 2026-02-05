#pragma once

#include <keen-pbr3/version.hpp>

#include <chrono>
#include <stdexcept>
#include <string>

namespace keen_pbr3 {

class HttpError : public std::runtime_error {
public:
    HttpError(const std::string& message, long status_code = 0);
    long status_code() const noexcept;

private:
    long status_code_;
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    void set_timeout(std::chrono::seconds timeout);
    void set_user_agent(const std::string& user_agent);

    std::string download(const std::string& url);

private:
    std::chrono::seconds timeout_{30};
    std::string user_agent_{"keen-pbr3/" KEEN_PBR3_VERSION_STRING};
};

} // namespace keen_pbr3
