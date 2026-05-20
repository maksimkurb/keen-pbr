#include "conntrack_rerouter.hpp"

#include "../log/logger.hpp"
#include "../util/safe_exec.hpp"

#include <atomic>
#include <mutex>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace keen_pbr3 {

namespace {

// nft table that owns keen-pbr's sets. Mirrors NftablesFirewall::TABLE_NAME;
// duplicated as a literal to avoid pulling the whole nftables backend header.
constexpr const char* kNftTableName = "KeenPbrTable";

// Extracts a plain-IP element value from one entry of an nft set "elem" array.
// Entries are either bare strings ("1.2.3.4") or, for timeout sets, objects of
// the shape {"elem": {"val": "1.2.3.4", ...}}. Range/prefix elements (objects
// instead of a string val) are skipped -- dynamic sets only hold single IPs.
std::string extract_nft_elem_ip(const nlohmann::json& entry) {
    if (entry.is_string()) {
        return entry.get<std::string>();
    }
    if (entry.is_object()) {
        const auto elem_it = entry.find("elem");
        if (elem_it != entry.end()) {
            return extract_nft_elem_ip(*elem_it);
        }
        const auto val_it = entry.find("val");
        if (val_it != entry.end() && val_it->is_string()) {
            return val_it->get<std::string>();
        }
    }
    return {};
}

} // namespace

ConntrackRerouter::ConntrackRerouter(DynamicSetReader reader, ConntrackFlusher flusher)
    : reader_(std::move(reader)), flusher_(std::move(flusher)) {}

void ConntrackRerouter::reset() {
    known_members_.clear();
}

std::size_t ConntrackRerouter::poll(const std::vector<std::string>& set_names) {
    if (set_names.empty() || !reader_ || !flusher_) {
        return 0;
    }

    const auto current = reader_(set_names);

    std::size_t flushed = 0;
    std::set<std::string> observed;

    for (const auto& [set_name, members] : current) {
        observed.insert(set_name);
        std::set<std::string> now(members.begin(), members.end());

        const auto known_it = known_members_.find(set_name);
        if (known_it == known_members_.end()) {
            // First observation of this set: record a silent baseline so we do
            // not flush conntrack for every pre-existing member at startup.
            known_members_.emplace(set_name, std::move(now));
            continue;
        }

        for (const auto& ip : now) {
            if (known_it->second.count(ip) == 0) {
                flusher_(ip);
                ++flushed;
            }
        }
        known_it->second = std::move(now);
    }

    // Drop baselines for sets that are no longer present, so that if such a
    // set reappears later it is re-baselined rather than diffed against stale
    // membership.
    for (auto it = known_members_.begin(); it != known_members_.end();) {
        if (observed.count(it->first) == 0) {
            it = known_members_.erase(it);
        } else {
            ++it;
        }
    }

    return flushed;
}

std::map<std::string, std::vector<std::string>>
read_ipset_dynamic_sets(const std::vector<std::string>& set_names) {
    std::map<std::string, std::vector<std::string>> result;

    for (const auto& set_name : set_names) {
        const auto capture =
            safe_exec_capture({"ipset", "save", set_name}, /*suppress_stderr=*/true);
        if (capture.exit_code != 0) {
            // Set does not exist yet (no domains resolved into it). Skip it;
            // poll() will baseline it once it appears.
            continue;
        }

        std::vector<std::string> members;
        std::istringstream stream(capture.stdout_output);
        std::string line;
        while (std::getline(stream, line)) {
            // Lines look like: "add <set> <ip> [timeout N ...]"
            std::istringstream line_stream(line);
            std::string verb;
            std::string parsed_set;
            std::string member;
            if (line_stream >> verb >> parsed_set >> member && verb == "add"
                && parsed_set == set_name && !member.empty()) {
                members.push_back(member);
            }
        }
        result.emplace(set_name, std::move(members));
    }

    return result;
}

std::map<std::string, std::vector<std::string>>
read_nft_dynamic_sets(const std::vector<std::string>& set_names) {
    std::map<std::string, std::vector<std::string>> result;

    for (const auto& set_name : set_names) {
        const auto capture = safe_exec_capture(
            {"nft", "-j", "list", "set", "inet", kNftTableName, set_name},
            /*suppress_stderr=*/true);
        if (capture.exit_code != 0 || capture.stdout_output.empty()) {
            continue; // Set does not exist yet.
        }

        nlohmann::json doc = nlohmann::json::parse(capture.stdout_output, nullptr, false);
        if (doc.is_discarded() || !doc.is_object()) {
            continue;
        }
        const auto nft_it = doc.find("nftables");
        if (nft_it == doc.end() || !nft_it->is_array()) {
            continue;
        }

        std::vector<std::string> members;
        for (const auto& node : *nft_it) {
            const auto set_it = node.find("set");
            if (set_it == node.end() || !set_it->is_object()) {
                continue;
            }
            const auto elem_it = set_it->find("elem");
            if (elem_it == set_it->end() || !elem_it->is_array()) {
                continue;
            }
            for (const auto& entry : *elem_it) {
                std::string ip = extract_nft_elem_ip(entry);
                if (!ip.empty()) {
                    members.push_back(std::move(ip));
                }
            }
        }
        result.emplace(set_name, std::move(members));
    }

    return result;
}

void flush_conntrack_for_ip(const std::string& ip) {
    static std::atomic<bool> conntrack_unavailable{false};
    static std::once_flag probe_flag;

    std::call_once(probe_flag, []() {
        const int rc = safe_exec({"conntrack", "--version"}, /*suppress_output=*/true);
        // 127 == exec failed (command not found). Anything else means the tool
        // ran, so it is usable.
        if (rc == 127 || rc < 0) {
            conntrack_unavailable.store(true, std::memory_order_relaxed);
            Logger::instance().warn(
                "conntrack tool not found; stale-connection rerouting is "
                "disabled. Install conntrack-tools to enable it.");
        }
    });

    if (conntrack_unavailable.load(std::memory_order_relaxed) || ip.empty()) {
        return;
    }

    std::vector<std::string> args = {"conntrack", "-D"};
    if (ip.find(':') != std::string::npos) {
        args.push_back("-f");
        args.push_back("ipv6");
    }
    args.push_back("-d");
    args.push_back(ip);

    // conntrack -D exits non-zero when nothing matched, which is normal and
    // expected; suppress its output and ignore the status.
    (void)safe_exec(args, /*suppress_output=*/true);
}

} // namespace keen_pbr3
