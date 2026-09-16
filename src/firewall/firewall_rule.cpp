#include "firewall_rule.hpp"

#include "../crypto/md5.hpp"

#include <algorithm>
#include <stdexcept>

namespace keen_pbr3 {
namespace {

bool addr_equal(const std::string& left, const std::string& right) {
  const auto left_slash = left.rfind('/');
  const auto right_slash = right.rfind('/');
  if (left_slash == std::string::npos || right_slash == std::string::npos) {
    return left == right ||
           (left_slash != std::string::npos &&
            left.substr(left_slash + 1) ==
                (left.find(':') == std::string::npos ? "32" : "128") &&
            left.substr(0, left_slash) == right) ||
           (right_slash != std::string::npos &&
            right.substr(right_slash + 1) ==
                (right.find(':') == std::string::npos ? "32" : "128") &&
            right.substr(0, right_slash) == left);
  }
  return left == right;
}

bool addresses_equal(const std::vector<std::string>& left,
                     const std::vector<std::string>& right) {
  if (left.size() != right.size()) return false;
  return std::equal(left.begin(), left.end(), right.begin(), addr_equal);
}

bool valid_id_character(char value) {
  return (value >= 'a' && value <= 'z') ||
         (value >= 'A' && value <= 'Z') ||
         (value >= '0' && value <= '9') || value == '.' || value == '_' ||
         value == '-';
}

void validate_id(std::string_view value, const char *name) {
  if (value.empty()) {
    throw std::invalid_argument(std::string("firewall rule ") + name +
                                " must not be empty");
  }
  if (value.size() > kFirewallRuleIdMaxLength) {
    throw std::invalid_argument(std::string("firewall rule ") + name +
                                " is too long");
  }
  for (const char character : value) {
    if (!valid_id_character(character)) {
      throw std::invalid_argument(std::string("firewall rule ") + name +
                                  " contains an invalid character");
    }
  }
}

std::string readable_comment(const FirewallRuleKey &key) {
  std::string serialized;
  serialized.reserve(kFirewallRuleCommentPrefix.size() + key.module_id.size() +
                     1U + key.instance_id.size());
  serialized += kFirewallRuleCommentPrefix;
  serialized += key.module_id;
  serialized.push_back(':');
  serialized += key.instance_id;
  return serialized;
}

} // namespace

std::string normalize_firewall_set_name(const std::string& name) {
  if (name.rfind("kpbr4s_", 0) == 0 || name.rfind("kpbr4S_", 0) == 0) {
    return "kpbr4_" + name.substr(7);
  }
  if (name.rfind("kpbr6s_", 0) == 0 || name.rfind("kpbr6S_", 0) == 0) {
    return "kpbr6_" + name.substr(7);
  }
  return name;
}

bool firewall_rule_criteria_equal(const FirewallRuleCriteria& left,
                                  const FirewallRuleCriteria& right) {
  const bool sets_equal = (!left.dst_set_name.has_value() &&
                           !right.dst_set_name.has_value()) ||
      (left.dst_set_name.has_value() && right.dst_set_name.has_value() &&
       normalize_firewall_set_name(*left.dst_set_name) ==
           normalize_firewall_set_name(*right.dst_set_name));
  const bool gateway_equal =
      (left.default_gateway == right.default_gateway &&
       left.default_gateway_bypass == right.default_gateway_bypass) ||
      (left.default_gateway != DefaultGatewayFamily::None &&
       right.default_gateway == DefaultGatewayFamily::None &&
       right.negate_dst_addr &&
       addresses_equal(left.default_gateway_bypass, right.dst_addr)) ||
      (right.default_gateway != DefaultGatewayFamily::None &&
       left.default_gateway == DefaultGatewayFamily::None &&
       left.negate_dst_addr &&
       addresses_equal(right.default_gateway_bypass, left.dst_addr));
  const bool destination_equal =
      addresses_equal(left.dst_addr, right.dst_addr) ||
      (left.default_gateway != DefaultGatewayFamily::None &&
       right.default_gateway == DefaultGatewayFamily::None &&
       addresses_equal(left.default_gateway_bypass, right.dst_addr)) ||
      (right.default_gateway != DefaultGatewayFamily::None &&
       left.default_gateway == DefaultGatewayFamily::None &&
       addresses_equal(right.default_gateway_bypass, left.dst_addr));
  const bool destination_negation_equal =
      left.negate_dst_addr == right.negate_dst_addr ||
      (left.default_gateway != DefaultGatewayFamily::None &&
       right.default_gateway == DefaultGatewayFamily::None &&
       right.negate_dst_addr) ||
      (right.default_gateway != DefaultGatewayFamily::None &&
       left.default_gateway == DefaultGatewayFamily::None &&
       left.negate_dst_addr);
  return sets_equal && left.dscp == right.dscp && left.proto == right.proto &&
         left.src_port.to_config_string() == right.src_port.to_config_string() &&
         left.dst_port.to_config_string() == right.dst_port.to_config_string() &&
         addresses_equal(left.src_addr, right.src_addr) && destination_equal &&
         left.negate_src_port == right.negate_src_port &&
         left.negate_dst_port == right.negate_dst_port &&
         left.negate_src_addr == right.negate_src_addr &&
         destination_negation_equal && gateway_equal;
}

std::string FirewallRuleKey::comment() const {
  validate_id(module_id, "module_id");
  validate_id(instance_id, "instance_id");

  const auto readable = readable_comment(*this);
  if (readable.size() > kFirewallRuleCommentMaxLength) {
    throw std::length_error("firewall rule comment is too long");
  }
  return readable;
}

FirewallRuleKey FirewallRuleKey::compact(
    std::string_view module_id, std::string_view semantic_instance) {
  validate_id(module_id, "module_id");
  if (semantic_instance.empty()) {
    throw std::invalid_argument(
        "firewall rule semantic_instance must not be empty");
  }

  const FirewallRuleKey semantic{std::string(module_id),
                                 std::string(semantic_instance)};
  return {semantic.module_id,
          crypto::md5_hex(readable_comment(semantic))};
}

FirewallRuleKey FirewallRuleKey::from_comment(std::string_view serialized) {
  if (serialized.size() > kFirewallRuleCommentMaxLength ||
      serialized.rfind(kFirewallRuleCommentPrefix, 0) != 0) {
    throw std::invalid_argument("invalid firewall rule comment");
  }

  serialized.remove_prefix(kFirewallRuleCommentPrefix.size());
  const auto separator = serialized.find(':');
  if (separator == std::string_view::npos ||
      serialized.find(':', separator + 1U) != std::string_view::npos) {
    throw std::invalid_argument("invalid firewall rule comment");
  }

  FirewallRuleKey key{std::string(serialized.substr(0, separator)),
                      std::string(serialized.substr(separator + 1U))};
  validate_id(key.module_id, "module_id");
  validate_id(key.instance_id, "instance_id");
  return key;
}

} // namespace keen_pbr3
