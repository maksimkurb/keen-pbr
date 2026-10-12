#pragma once

#include "test_routing.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

namespace keen_pbr3 {

// An invalid routing-test request. what() is the user-facing message.
class TestRoutingRequestError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct TestRoutingRequest {
    std::string target;
    TestRoutingCriteria criteria;
};

// Validate a routing-test request body ({"target", "proto", "dest_port",
// "src_addr", "src_port", "dscp"}). Shared by the REST API and the control
// socket so both accept exactly the same requests.
TestRoutingRequest parse_test_routing_request(const nlohmann::json& body);

// The routing-test result in the REST API response shape.
nlohmann::json test_routing_result_to_json(const TestRoutingResult& result);

// Inverse of test_routing_result_to_json, for rendering a daemon response.
TestRoutingResult test_routing_result_from_json(const nlohmann::json& value);

} // namespace keen_pbr3
