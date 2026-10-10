#include "dnsmasq_hook_protocol.hpp"

#include <cstdlib>

namespace keen_pbr3 {

namespace {

bool parse_uint(std::string_view s, unsigned long long max, unsigned long long& out) {
    if (s.empty() || s.size() > 18) return false;
    unsigned long long v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<unsigned>(c - '0');
    }
    if (v > max) return false;
    out = v;
    return true;
}

bool valid_key(std::string_view k) {
    if (k.empty()) return false;
    for (char c : k) {
        if (!((c >= 'a' && c <= 'z') || c == '_')) return false;
    }
    return true;
}

std::vector<std::string> split_spaces(std::string_view v) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < v.size()) {
        while (i < v.size() && v[i] == ' ') ++i;
        std::size_t j = i;
        while (j < v.size() && v[j] != ' ') ++j;
        if (j > i) out.emplace_back(v.substr(i, j - i));
        i = j;
    }
    return out;
}

HookInspectParse fail(std::string msg) {
    HookInspectParse p;
    p.ok = false;
    p.error = std::move(msg);
    return p;
}

} // namespace

HookInspectParse parse_hook_inspect_output(std::string_view text) {
    HookInspectResult res;
    bool have_record = false;
    std::size_t line_no = 0;
    std::size_t pos = 0;

    while (pos <= text.size()) {
        std::size_t nl = text.find('\n', pos);
        std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        const bool last = (nl == std::string_view::npos);
        pos = last ? text.size() + 1 : nl + 1;
        ++line_no;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;

        const std::string where = "line " + std::to_string(line_no) + ": ";
        if (line == "[instance]") {
            res.instances.emplace_back();
            have_record = true;
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) return fail(where + "missing '='");
        const std::string_view key = line.substr(0, eq);
        const std::string_view value = line.substr(eq + 1);
        if (!valid_key(key)) return fail(where + "invalid key");

        if (!have_record) {
            if (key == "hook_version") {
                unsigned long long v = 0;
                if (!parse_uint(value, 1000000, v)) return fail(where + "invalid hook_version");
                res.hook_version = static_cast<int>(v);
            } else if (key == "platform") {
                res.platform = std::string(value);
            } else if (key == "binary") {
                res.binary = std::string(value);
            }
            continue;
        }

        HookInstanceRecord& r = res.instances.back();
        if (key == "selector") {
            r.selector = std::string(value);
        } else if (key == "enabled") {
            if (value == "1") r.enabled = true;
            else if (value == "0") r.enabled = false;
            else return fail(where + "invalid enabled");
        } else if (key == "main_pid") {
            unsigned long long v = 0;
            if (!parse_uint(value, 0x7fffffff, v)) return fail(where + "invalid main_pid");
            if (v == 0) r.main_pid.reset();
            else r.main_pid = static_cast<pid_t>(v);
        } else if (key == "match_conf_file") {
            r.match_conf_file = std::string(value);
        } else if (key == "exe_required") {
            r.exe_required = std::string(value);
        } else if (key == "default_conf_file") {
            r.default_conf_file = std::string(value);
        } else if (key == "dropin_include") {
            if (value == "static") r.dropin_include = DropinInclude::Static;
            else if (value == "managed") r.dropin_include = DropinInclude::Managed;
            else return fail(where + "invalid dropin_include");
        } else if (key == "dropin_dir") {
            r.dropin_dir = std::string(value);
        } else if (key == "dropin_file") {
            r.dropin_file = std::string(value);
        } else if (key == "dropin_content") {
            r.dropin_content = std::string(value);
        } else if (key == "dropin_state") {
            const auto st = parse_dropin_state(value);
            if (!st) return fail(where + "invalid dropin_state");
            r.dropin_state = *st;
        } else if (key == "static_endpoint") {
            const auto ep = parse_dnsmasq_endpoint(value);
            if (!ep) return fail(where + "invalid static_endpoint");
            r.static_endpoint = *ep;
        } else if (key == "preflight") {
            if (value == "skip") r.preflight_skip = true;
        } else if (key == "owned_mounts") {
            r.owned_mounts = split_spaces(value);
        } else if (key == "missing_mounts") {
            r.missing_mounts = split_spaces(value);
        }
        // unknown keys: ignored
    }

    if (res.hook_version != 2) {
        return fail("unsupported hook_version " + std::to_string(res.hook_version));
    }
    for (const auto& r : res.instances) {
        if (r.selector.empty()) return fail("instance without selector");
    }

    HookInspectParse ok;
    ok.ok = true;
    ok.result = std::move(res);
    return ok;
}

} // namespace keen_pbr3
