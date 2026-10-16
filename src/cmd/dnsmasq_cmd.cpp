#include "dnsmasq_cmd.hpp"

#include "../config/config.hpp"
#include "../daemon/dnsmasq_detector.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace keen_pbr3 {

namespace {

std::string or_dash(const std::string& s) { return s.empty() ? "-" : s; }

std::string listener_text(const SocketListener& l) {
    const char* proto = l.udp && l.tcp ? "udp+tcp" : (l.udp ? "udp" : "tcp");
    return std::string(proto) + " " + l.local.str();
}

void print_usage_to_stderr() {
    std::cerr << "Usage: keen-pbr resolver dnsmasq detect [--json]\n";
}

std::optional<Config> load_config_or_warn(const std::string& config_path, bool warn) {
    try {
        std::ifstream in(config_path);
        if (!in.is_open()) throw std::runtime_error("cannot open file");
        return parse_config(in);
    } catch (const std::exception& e) {
        if (warn) std::cerr << "warning: cannot read " << config_path << ": " << e.what() << '\n';
        return std::nullopt;
    }
}

int run_detect(const std::vector<std::string>& args, const std::string& config_path,
               const std::string& hook_path) {
    bool json = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--json") {
            json = true;
        } else {
            print_usage_to_stderr();
            return 64;
        }
    }
    const auto config = load_config_or_warn(config_path, true);
    DnsmasqDetectOptions opts;
    if (config) opts.configured_selector = configured_dnsmasq_selector(*config);
    opts.probe = true;
    opts.probe_all = true;
    const auto detection = DnsmasqDetector(hook_path).detect(opts);
    if (json) {
        std::cout << format_dnsmasq_detection_json(detection).dump(2) << '\n';
    } else {
        std::cout << format_dnsmasq_detection_text(detection, hook_path, config_path);
    }
    std::cout.flush();
    return dnsmasq_detection_exit_code(detection);
}

} // namespace

std::string format_dnsmasq_detection_text(const DnsmasqDetection& d,
                                          const std::string& hook_path,
                                          const std::string& config_path) {
    std::ostringstream o;
    o << "dnsmasq hook: " << or_dash(hook_path) << " (platform "
      << or_dash(d.inspect.platform) << ", binary " << or_dash(d.inspect.binary) << ")\n";

    for (std::size_t idx = 0; idx < d.instances.size(); ++idx) {
        const auto& in = d.instances[idx];
        o << '\n' << "  " << or_dash(in.hook.selector);
        if (d.selected && *d.selected == idx) {
            o << "  [selected: " << selection_kind_name(d.selection) << "]";
        }
        o << '\n';
        o << "    running:     ";
        if (in.process) o << "yes (pid " << in.process->pid << ")";
        else o << "no";
        o << '\n';
        o << "    enabled:     " << (in.hook.enabled ? "yes" : "no") << '\n';
        o << "    endpoint:    ";
        if (in.endpoint) o << in.endpoint->str() << " (" << endpoint_evidence_name(in.evidence) << ")";
        else o << "-";
        o << '\n';
        o << "    listeners:   ";
        if (in.listeners.empty()) o << "-";
        for (std::size_t i = 0; i < in.listeners.size(); ++i) {
            if (i) o << ", ";
            o << listener_text(in.listeners[i]);
        }
        o << '\n';
        o << "    keen-pbr:    ";
        switch (in.hook.dropin_state) {
            case DropinState::Current:
                o << "installed";
                if (in.stamp) o << ", loaded hash " << in.stamp->hash;
                break;
            case DropinState::Absent: o << "not installed"; break;
            case DropinState::Foreign:
                o << "foreign file at " << or_dash(in.hook.dropin_file);
                break;
        }
        o << '\n';
        o << "    conf-dir:    " << or_dash(in.hook.dropin_dir) << " ("
          << (in.dropin_dir_included ? "included" : "not included") << ")\n";
        o << "    problem:     ";
        if (in.problem == DnsmasqErrorCode::None) {
            o << "none";
        } else {
            o << dnsmasq_error_code_name(in.problem);
            if (!in.problem_detail.empty()) o << ": " << in.problem_detail;
        }
        o << '\n';
        for (const auto& w : in.warnings) o << "    warning: " << w << '\n';
    }

    o << '\n';
    for (const auto& w : d.warnings) o << "warning: " << w << '\n';
    if (d.error != DnsmasqErrorCode::None) {
        o << "Selection: none \xE2\x80\x94 " << dnsmasq_error_code_name(d.error) << ": "
          << d.message << '\n';
        // Only a selection problem is fixed by choosing an instance.
        if (d.error == DnsmasqErrorCode::InstanceAmbiguous ||
            d.error == DnsmasqErrorCode::InstanceNotFound) {
            o << "Set \"dns\": {\"dnsmasq_instance\": \"<selector>\"} in " << config_path
              << " (or in the web UI settings), then apply the configuration.\n";
        }
    } else if (const auto* sel = d.selected_instance()) {
        o << "Selection: " << sel->hook.selector << " (" << selection_kind_name(d.selection)
          << ")\n";
    } else {
        o << "Selection: none\n";
    }
    return o.str();
}

nlohmann::json format_dnsmasq_detection_json(const DnsmasqDetection& d) {
    using nlohmann::json;
    json j = json::object();
    j["hook_ok"] = d.hook_ok;
    j["platform"] = d.inspect.platform;
    j["binary"] = d.inspect.binary;
    const auto* sel = d.selected_instance();
    j["selected"] = sel ? json(sel->hook.selector) : json(nullptr);
    j["selection"] = selection_kind_name(d.selection);
    j["error_code"] = dnsmasq_error_code_name(d.error);
    j["message"] = d.message;
    j["warnings"] = d.warnings;
    json arr = json::array();
    for (const auto& in : d.instances) {
        json e = json::object();
        e["selector"] = in.hook.selector;
        e["enabled"] = in.hook.enabled;
        e["pid"] = in.process ? json(static_cast<long>(in.process->pid)) : json(nullptr);
        e["exe"] = in.process ? json(in.process->exe) : json(nullptr);
        e["endpoint"] = in.endpoint ? json(in.endpoint->str()) : json(nullptr);
        e["endpoint_evidence"] = endpoint_evidence_name(in.evidence);
        json ls = json::array();
        for (const auto& l : in.listeners) ls.push_back(listener_text(l));
        e["listeners"] = ls;
        e["dropin_state"] = dropin_state_name(in.hook.dropin_state);
        e["dropin_dir_included"] = in.dropin_dir_included;
        e["stamp_hash"] = in.stamp ? json(in.stamp->hash) : json(nullptr);
        e["problem"] = dnsmasq_error_code_name(in.problem);
        e["problem_detail"] = in.problem_detail;
        e["warnings"] = in.warnings;
        arr.push_back(std::move(e));
    }
    j["instances"] = std::move(arr);
    return j;
}

int dnsmasq_detection_exit_code(const DnsmasqDetection& d) {
    const auto* sel = d.selected_instance();
    return sel && sel->problem == DnsmasqErrorCode::None ? 0 : 1;
}

int run_dnsmasq_command(const std::vector<std::string>& args, const std::string& config_path,
                        const std::string& hook_path) {
    try {
        if (!args.empty() && args[0] == "detect") return run_detect(args, config_path, hook_path);
        print_usage_to_stderr();
        return 64;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}

} // namespace keen_pbr3
