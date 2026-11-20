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
//   4. the NDM SCGI socket /var/run/ndm.scgi.socket.  KeeneticOS 5.1.9 refuses
//      every HTTP request ("403, X-Detail: 0x1867") and has no tokens, but
//      the socket still answers there.
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

// Let this process create RCI access tokens and keep them in `path`.  Only
// the service calls it; one-shot CLI commands never create tokens.  Reads the
// token saved by an earlier run, if any.
void configure_rci_token_file(const std::string& path);

namespace rci_detail {

// SCGI request: netstring of NUL-separated CGI variables, then the body.
std::string build_scgi_request(const std::string& method, const std::string& uri,
                               const std::string& body);

struct ScgiResponse {
    int status{0};
    std::string detail; // X-Detail header, when NDM sends one
    std::string body;
};

// Parse "Status: 200 OK\r\n<headers>\r\n\r\n<body>".  Returns no value while
// the headers, or the Content-Length bytes of the body, are not complete.  A
// response without Content-Length is complete only at end of stream.
std::optional<ScgiResponse> parse_scgi_response(const std::string& raw, bool at_eof);

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

void set_rci_scgi_socket_path_for_tests(const std::string& path);
void set_rci_http_base_for_tests(const std::string& base);
void set_rci_http_transport_for_tests(std::shared_ptr<HttpTransport> transport);
void set_rci_ndmc_for_tests(RciNdmcFn ndmc);
void reset_rci_test_state();
#endif

} // namespace keen_pbr3
