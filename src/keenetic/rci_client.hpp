#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#ifdef KEEN_PBR3_TESTING
#include <memory>
#endif

namespace keen_pbr3 {

// Local KeeneticOS RCI access.
//
// The first request finds a way in and later requests reuse it:
//   1. HTTP on 127.0.0.1:79 with the saved access token (X-Ndma-Tkn), if any;
//   2. HTTP without a token.  On KeeneticOS 5.2+ the service then creates a
//      token right away: 5.2 logs a warning for every tokenless request and
//      is closing that access;
//   3. on 401/403: create a token with
//      `ndmc -c 'authentication token generate keen-pbr'`, save it and retry;
// When the way in that worked stops working (token revoked, firmware
// changed), the search runs again.
//
// Failures throw HttpError so existing callers keep their error handling.
struct RciRequestLimits {
    std::chrono::seconds timeout{3};
    std::size_t max_response_size{std::size_t{1024} * 1024U};
};

// GET /rci/<path>, for example rci_get("show/dns-proxy").
std::string rci_get(const std::string& path, const RciRequestLimits& limits = {});

// POST /rci/ with a JSON body.
std::string rci_post(const std::string& json_body, const RciRequestLimits& limits = {});

// Read the RCI access token saved in `path` by an earlier run, if any.  With
// `allow_create` this process may also create a token and save it there; only
// the service does that.  One-shot CLI commands pass false and only use the
// saved token: they must not create tokens or delete the service's.
void configure_rci_token_file(const std::string& path, bool allow_create = true);

namespace rci_detail {

struct GeneratedToken {
    std::string id;
    std::string value;
};

// Token from `authentication token generate` output: "id: N", then the value
// on the "value:" line or on the next non-empty line.
std::optional<GeneratedToken> parse_generated_token(const std::string& ndmc_output);

// Ids of tokens whose user-data equals `name`, from
// `show authentication token` output.
std::vector<std::string> parse_token_ids(const std::string& ndmc_output, const std::string& name);

} // namespace rci_detail

struct RciNdmcResult {
    int exit_code{-1};
    std::string output;
};
using RciNdmcFn = std::function<RciNdmcResult(const std::vector<std::string>& args)>;

#ifdef KEEN_PBR3_TESTING
class HttpTransport;

void set_rci_http_base_for_tests(const std::string& base);
void set_rci_http_transport_for_tests(std::shared_ptr<HttpTransport> transport);
void set_rci_ndmc_for_tests(RciNdmcFn ndmc);
void reset_rci_test_state();
#endif

} // namespace keen_pbr3
