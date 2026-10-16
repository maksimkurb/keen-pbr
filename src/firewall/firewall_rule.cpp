#include "firewall_rule.hpp"

#include "../crypto/md5.hpp"

#include <stdexcept>

namespace keen_pbr3 {
namespace {

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
