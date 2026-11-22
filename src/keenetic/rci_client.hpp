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
// KeeneticOS 5.2+ always accepts an RCI access token (X-Ndma-Tkn), even before
// it enforces tokens, so the service uses one from the start.  Tokenless access
// is the legacy path for older firmware.  The first request finds a way in and
// later requests reuse it:
//   1. HTTP on 127.0.0.1:79 with the access token we hold (saved by an earlier
//      run or created in this one).  Only a refusal (401/403) moves on; a
//      timeout or another error is reported as it is;
//   2. the service creates a token with
//      `ndmc -c 'authentication token generate keen-pbr'`, saves it and sends
//      the request with it.  Whether ndmc can create one is the only check for
//      token support: a firmware without the command is remembered as having no
//      tokens;
//   3. HTTP without a token, for older firmware and while no token can be
//      created.  The service tries to create one again after ten minutes.
// A request is sent at most once after an answer other than 401/403, so a POST
// never runs twice.  When the way in that worked stops working (token revoked,
// firmware changed), the search runs again.
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

// True when RCI answers over HTTP: a 2xx, or a 401/403 (it is up and wants an
// access token).  False on a timeout, a connection failure or another error.
// Does no token work beyond what configure_rci_token_file() allowed.  `note`,
// if given, receives the reason for false or the hint for a 401/403.
bool rci_answers(const RciRequestLimits& limits = {}, std::string* note = nullptr);

// Read the RCI access token saved in `path` by an earlier run, if any.  With
// `allow_create` this process may also create a token and save it there; only
// the service does that.  One-shot CLI commands pass false and only use the
// saved token: they must not create tokens or delete the service's.
void configure_rci_token_file(const std::string& path, bool allow_create);

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

// Netfilter components (opkg-kmod-netfilter, opkg-kmod-netfilter-addons) that
// the `show version/ndw/components` answer does not list.  Throws on an answer
// that is not a string or array naming components (an error object, nothing).
std::vector<std::string> missing_netfilter_components(const std::string& components_json);

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
void set_rci_clock_for_tests(std::function<std::chrono::steady_clock::time_point()> now);
void reset_rci_test_state();
#endif

} // namespace keen_pbr3
