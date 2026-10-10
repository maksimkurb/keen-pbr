#include "dnsmasq_detector.hpp"

#include "dnsmasq_conf.hpp"
#include "dnsmasq_hook_protocol.hpp"
#include "dnsmasq_proc.hpp"
#include "../dns/dns_client.hpp"
#include "../dns/dnsmasq_gen.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <exception>

namespace keen_pbr3 {

std::vector<InterfaceAddress> system_interface_addresses() {
    std::vector<InterfaceAddress> out;
    struct ifaddrs* list = nullptr;
    if (::getifaddrs(&list) != 0) return out;
    for (struct ifaddrs* it = list; it; it = it->ifa_next) {
        if (!it->ifa_addr || !it->ifa_name) continue;
        char buf[INET6_ADDRSTRLEN] = {0};
        if (it->ifa_addr->sa_family == AF_INET) {
            const auto* sin = reinterpret_cast<const sockaddr_in*>(it->ifa_addr);
            if (!::inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf))) continue;
            out.push_back({it->ifa_name, 4, buf});
        } else if (it->ifa_addr->sa_family == AF_INET6) {
            const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(it->ifa_addr);
            const auto* b = sin6->sin6_addr.s6_addr;
            if (b[0] == 0xfe && (b[1] & 0xc0) == 0x80) continue;  // link-local
            if (!::inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof(buf))) continue;
            out.push_back({it->ifa_name, 6, buf});
        }
    }
    ::freeifaddrs(list);
    return out;
}

namespace {

constexpr std::size_t kMaxCandidates = 16;
constexpr auto kProbeBudget = std::chrono::seconds(4);
constexpr auto kProbeTimeout = std::chrono::milliseconds(500);

std::string read_link(const std::string& path) {
    char buf[PATH_MAX];
    const ssize_t n = ::readlink(path.c_str(), buf, sizeof(buf) - 1);
    if (n < 0) return {};
    return std::string(buf, static_cast<std::size_t>(n));
}

std::string strip_trailing_slash(std::string s) {
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

std::string base_name(const std::string& p) {
    const auto pos = p.rfind('/');
    return pos == std::string::npos ? p : p.substr(pos + 1);
}

bool iface_matches(const std::string& pattern, const std::string& ifname) {
    if (pattern == ifname) return true;
    if (!pattern.empty() && pattern.back() == '*') {
        return ifname.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
    }
    return false;
}

bool interface_allowed(const std::string& ifname, const DnsmasqEffectiveConfig& cfg) {
    if (ifname == "lo") return true;
    for (const auto& p : cfg.except_interfaces) if (iface_matches(p, ifname)) return false;
    if (!cfg.interfaces.empty()) {
        for (const auto& p : cfg.interfaces) if (iface_matches(p, ifname)) return true;
        return false;
    }
    return true;
}

std::string join(const std::vector<std::string>& v, const char* sep) {
    std::string r;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) r += sep;
        r += v[i];
    }
    return r;
}

// §8.4
std::optional<ProcessInfo> match_process(const HookInstanceRecord& rec,
                                         const std::vector<ProcessInfo>& dnsmasq_procs,
                                         const std::string& proc_root,
                                         std::vector<std::string>& warnings) {
    if (rec.main_pid) return read_process_info(proc_root, *rec.main_pid);

    std::string want;
    if (!rec.exe_required.empty()) {
        char buf[PATH_MAX];
        want = ::realpath(rec.exe_required.c_str(), buf) ? std::string(buf) : rec.exe_required;
    }

    std::vector<const ProcessInfo*> matches;
    for (const auto& p : dnsmasq_procs) {
        if (!want.empty() && p.exe != want) continue;
        if (!rec.match_conf_file.empty()) {
            const auto confs = conf_files_from_argv(p.argv);
            bool ok = std::find(confs.begin(), confs.end(), rec.match_conf_file) != confs.end();
            if (!ok && confs.empty() && rec.default_conf_file == rec.match_conf_file) ok = true;
            if (!ok) continue;
        }
        matches.push_back(&p);
    }
    if (matches.empty()) return std::nullopt;

    std::vector<const ProcessInfo*> mains;
    for (const auto* p : matches) {
        const bool child = std::any_of(matches.begin(), matches.end(), [&](const ProcessInfo* o) {
            return o != p && o->pid == p->ppid;
        });
        if (!child) mains.push_back(p);
    }
    if (mains.empty()) mains = matches;
    const ProcessInfo* best = mains.front();
    for (const auto* p : mains) if (p->pid < best->pid) best = p;
    if (mains.size() > 1) {
        std::vector<std::string> pids;
        for (const auto* p : mains) pids.push_back(std::to_string(p->pid));
        warnings.push_back("several dnsmasq processes match " + rec.selector + ": " +
                           join(pids, ",") + "; using " + std::to_string(best->pid));
    }
    return *best;
}

bool dropin_dir_included(const DnsmasqInstance& inst) {
    const std::string dir = strip_trailing_slash(inst.hook.dropin_dir);
    const std::string name = base_name(inst.hook.dropin_file);
    for (const auto& d : inst.config.conf_dirs) {
        if (strip_trailing_slash(d.dir) == dir && conf_dir_accepts(d, name)) return true;
    }
    return false;
}

bool listener_matches(const SocketListener& l, const DnsmasqEndpoint& ep) {
    if (l.local.port != ep.port) return false;
    if (l.local.address == ep.address) return true;
    const int fam = ep.address.find(':') != std::string::npos ? 6 : 4;
    return l.wildcard && l.family == fam;
}

void add_unique(std::vector<DnsmasqEndpoint>& v, const DnsmasqEndpoint& ep) {
    if (v.size() >= kMaxCandidates) return;
    if (std::find(v.begin(), v.end(), ep) == v.end()) v.push_back(ep);
}

// §8.6
void build_candidates(DnsmasqInstance& inst, const std::optional<DnsmasqEndpoint>& previous,
                      const std::vector<InterfaceAddress>& ifaces) {
    auto& cands = inst.candidates;
    cands.clear();
    if (previous) {
        const bool owned = inst.listeners.empty() ||
            std::any_of(inst.listeners.begin(), inst.listeners.end(),
                        [&](const SocketListener& l) { return listener_matches(l, *previous); });
        if (owned) add_unique(cands, *previous);
    }
    if (inst.hook.static_endpoint) add_unique(cands, *inst.hook.static_endpoint);
    for (const auto& l : inst.listeners) {
        if (!l.udp) continue;
        if (!l.wildcard) {
            add_unique(cands, l.local);
        } else if (l.family == 4) {
            add_unique(cands, {"127.0.0.1", l.local.port});
            for (const auto& a : ifaces) {
                if (a.family == 4 && interface_allowed(a.ifname, inst.config) && a.address != "127.0.0.1")
                    add_unique(cands, {a.address, l.local.port});
            }
        } else {
            add_unique(cands, {"::1", l.local.port});
            for (const auto& a : ifaces) {
                if (a.family == 6 && interface_allowed(a.ifname, inst.config) && a.address != "::1")
                    add_unique(cands, {a.address, l.local.port});
            }
        }
    }
    if (inst.listeners.empty()) {
        for (const auto& a : inst.config.listen_addresses) {
            add_unique(cands, {a, inst.config.port.value_or(53)});
        }
    }
}

void probe_candidates(DnsmasqInstance& inst, const std::string& expected_hash,
                      const DnsmasqDetectorDeps& deps) {
    inst.probed = true;
    const auto start = std::chrono::steady_clock::now();
    std::optional<std::pair<DnsmasqEndpoint, DnsmasqConfigStamp>> other;
    std::optional<DnsmasqEndpoint> responding;
    for (const auto& c : inst.candidates) {
        if (std::chrono::steady_clock::now() - start > kProbeBudget) break;
        const DnsTxtProbeResult r = deps.probe_txt(c, kProbeTimeout);
        if (r.status == DnsTxtProbeStatus::Ok) {
            const auto s = parse_dnsmasq_config_stamp(r.txt);
            if (s && (expected_hash.empty() || s->hash == expected_hash)) {
                inst.endpoint = c;
                inst.evidence = EndpointEvidence::StampVerified;
                inst.stamp = s;
                return;
            }
            if (s && !other) other = std::make_pair(c, *s);
            if (!responding) responding = c;
            continue;
        }
        if (r.answered) {
            if (!responding) responding = c;
            continue;
        }
        if (!responding && deps.probe_responding(c, kProbeTimeout)) responding = c;
    }
    if (other) {
        inst.endpoint = other->first;
        inst.stamp = other->second;
        inst.evidence = EndpointEvidence::Responding;
    } else if (responding) {
        inst.endpoint = responding;
        inst.evidence = EndpointEvidence::Responding;
    } else if (!inst.candidates.empty()) {
        inst.endpoint = inst.candidates.front();
        inst.evidence = EndpointEvidence::SocketOwned;
    } else {
        inst.evidence = EndpointEvidence::None;
    }
}

void evaluate_problem(DnsmasqInstance& inst, const std::string& own_netns) {
    const std::string sel = inst.selector.str();
    auto set = [&](DnsmasqErrorCode c, const std::string& text) {
        inst.problem = c;
        inst.problem_detail = text;
    };
    const std::string pid = inst.process ? std::to_string(inst.process->pid) : "";
    if (!inst.process) {
        set(DnsmasqErrorCode::NotRunning, sel + ": dnsmasq process not found");
    } else if (inst.config.port && *inst.config.port == 0) {
        set(DnsmasqErrorCode::DnsDisabled, sel + " (pid " + pid + "): DNS is disabled (port=0)");
    } else if (!inst.process->netns.empty() && !own_netns.empty() && inst.process->netns != own_netns) {
        set(DnsmasqErrorCode::OtherNetns, sel + " (pid " + pid + "): runs in network namespace " +
                                              inst.process->netns + ", keen-pbr in " + own_netns);
    } else if (inst.hook.dropin_state == DropinState::Foreign) {
        set(DnsmasqErrorCode::ForeignDropin, sel + ": " + inst.hook.dropin_file +
                                                 " exists but was not written by keen-pbr");
    } else if (inst.hook.dropin_include == DropinInclude::Static && !inst.dropin_dir_included) {
        set(DnsmasqErrorCode::DropinNotLoaded, sel + " (pid " + pid + "): conf-dir " +
                                                   inst.hook.dropin_dir + " is not loaded by dnsmasq");
    } else if (inst.candidates.empty()) {
        set(DnsmasqErrorCode::NoDnsListener, sel + " (pid " + pid + "): no DNS listener found");
    } else if (inst.probed && inst.evidence == EndpointEvidence::SocketOwned) {
        set(DnsmasqErrorCode::EndpointUnresponsive,
            sel + " (pid " + pid + "): no DNS answer from " +
                (inst.endpoint ? inst.endpoint->str() : std::string("candidates")));
    }
}

} // namespace

DnsmasqDetector::DnsmasqDetector(std::string hook_path, DnsmasqDetectorDeps deps)
    : hook_path_(std::move(hook_path)), deps_(std::move(deps)) {
    if (!deps_.exec) {
        deps_.exec = [](const std::vector<std::string>& args) {
            return safe_exec_capture(args, true, 16384, true);
        };
    }
    if (!deps_.probe_txt) {
        deps_.probe_txt = [](const DnsmasqEndpoint& ep, std::chrono::milliseconds t) {
            return probe_dns_txt(ep.address, ep.port, kDnsmasqStampDomain, t);
        };
    }
    if (!deps_.probe_responding) {
        deps_.probe_responding = [](const DnsmasqEndpoint& ep, std::chrono::milliseconds t) {
            return dns_probe_responding(ep.address, ep.port, t);
        };
    }
    if (!deps_.interface_addresses) deps_.interface_addresses = system_interface_addresses;
}

DnsmasqDetection DnsmasqDetector::detect(const DnsmasqDetectOptions& options) const {
    DnsmasqDetection det;
    try {
        if (hook_path_.empty()) {
            det.error = DnsmasqErrorCode::NoHook;
            det.message = "no dnsmasq hook for this platform";
            return det;
        }
        const ExecCaptureResult r = deps_.exec({hook_path_, "inspect"});
        if (r.timed_out) {
            det.error = DnsmasqErrorCode::HookFailed;
            det.message = "hook inspect timed out";
            return det;
        }
        if (r.exit_code == 2) {
            det.error = DnsmasqErrorCode::NotInstalled;
            det.message = "dnsmasq is not installed";
            return det;
        }
        if (r.exit_code != 0) {
            det.error = DnsmasqErrorCode::HookFailed;
            det.message = "hook inspect exited " + std::to_string(r.exit_code) + ": " +
                          r.stdout_output.substr(0, 200);
            return det;
        }
        HookInspectParse p = parse_hook_inspect_output(r.stdout_output);
        if (!p.ok) {
            det.error = DnsmasqErrorCode::HookFailed;
            det.message = p.error;
            return det;
        }
        if (p.result.binary.empty()) {
            det.error = DnsmasqErrorCode::NotInstalled;
            det.message = "dnsmasq is not installed";
            return det;
        }
        det.hook_ok = true;
        det.inspect = std::move(p.result);

        const std::string& proc_root = deps_.proc_root;
        const auto procs = scan_dnsmasq_processes(proc_root);
        const std::string own_netns = read_link(proc_root + "/self/ns/net");

        for (const auto& rec : det.inspect.instances) {
            const auto sel = parse_dnsmasq_selector(rec.selector);
            if (!sel) {
                det.warnings.push_back("inspect: invalid selector '" + rec.selector + "' ignored");
                continue;
            }
            DnsmasqInstance inst;
            inst.hook = rec;
            inst.selector = *sel;
            inst.process = match_process(rec, procs, proc_root, inst.warnings);
            if (inst.process) {
                inst.config = read_effective_config(inst.process->argv, rec.default_conf_file, rec.dropin_file);
                inst.listeners = listeners_of(proc_root, inst.process->pid,
                                              socket_inodes_of(proc_root, inst.process->pid));
            }
            inst.dropin_dir_included = dropin_dir_included(inst);
            det.instances.push_back(std::move(inst));
        }

        // ---- §8.7 selection
        auto sel_list = [&](const std::vector<std::size_t>& idx) {
            std::vector<std::string> names;
            for (auto i : idx) names.push_back(det.instances[i].selector.str());
            return join(names, ", ");
        };
        std::vector<std::size_t> all;
        for (std::size_t i = 0; i < det.instances.size(); ++i) all.push_back(i);

        if (options.configured_selector) {
            std::optional<std::size_t> found;
            for (auto i : all) {
                if (det.instances[i].selector.str() == *options.configured_selector) { found = i; break; }
            }
            if (!found) {
                det.error = DnsmasqErrorCode::InstanceNotFound;
                det.message = "dns.dnsmasq_instance '" + *options.configured_selector +
                              "' does not exist; found: " + (all.empty() ? "none" : sel_list(all));
            } else {
                det.selected = found;
                det.selection = DnsmasqSelectionKind::Explicit;
            }
        } else {
            std::vector<std::size_t> installed, usable;
            for (auto i : all) {
                const auto& in = det.instances[i];
                if (in.hook.dropin_state == DropinState::Current) installed.push_back(i);
                if (in.hook.enabled && !(in.config.port && *in.config.port == 0)) usable.push_back(i);
            }
            if (installed.size() == 1) {
                det.selected = installed[0];
                det.selection = DnsmasqSelectionKind::Adopted;
            } else if (installed.size() > 1) {
                det.error = DnsmasqErrorCode::InstanceAmbiguous;
                det.message = "keen-pbr is installed in several dnsmasq instances (" +
                              sel_list(installed) + "); set dns.dnsmasq_instance";
            } else if (usable.size() == 1) {
                det.selected = usable[0];
                det.selection = DnsmasqSelectionKind::Automatic;
            } else if (usable.empty()) {
                det.error = DnsmasqErrorCode::NoDnsInstance;
                det.message = "no enabled dnsmasq instance with DNS was found";
            } else {
                det.error = DnsmasqErrorCode::InstanceAmbiguous;
                det.message = "several dnsmasq instances can serve DNS (" + sel_list(usable) +
                              "); set dns.dnsmasq_instance";
            }
        }
        for (auto i : all) {
            if (det.selected && *det.selected == i) continue;
            const auto& in = det.instances[i];
            if (in.hook.dropin_state == DropinState::Current) {
                det.warnings.push_back("keen-pbr drop-in is also installed in " + in.selector.str() +
                                       "; remove it with: keen-pbr dnsmasq remove " + in.selector.str());
            }
        }

        // ---- candidates, probing and problems
        std::vector<InterfaceAddress> ifaces;
        bool ifaces_loaded = false;
        for (std::size_t i = 0; i < det.instances.size(); ++i) {
            if (!options.probe_all && !(det.selected && *det.selected == i)) continue;
            auto& inst = det.instances[i];
            if (inst.process) {
                if (!ifaces_loaded) { ifaces = deps_.interface_addresses(); ifaces_loaded = true; }
                build_candidates(inst, options.previous_endpoint, ifaces);
                const bool other_netns = !inst.process->netns.empty() && !own_netns.empty() &&
                                         inst.process->netns != own_netns;
                const bool dns_off = inst.config.port && *inst.config.port == 0;
                if (!other_netns && !dns_off && !inst.candidates.empty()) {
                    if (options.probe) {
                        probe_candidates(inst, options.expected_hash, deps_);
                    } else {
                        // Not probed: endpoint only when the first candidate is a
                        // hint we were given (previous or static); evidence stays
                        // SocketOwned but `probed` is false, so no problem fires.
                        const auto& c0 = inst.candidates.front();
                        if ((options.previous_endpoint && c0 == *options.previous_endpoint) ||
                            (inst.hook.static_endpoint && c0 == *inst.hook.static_endpoint)) {
                            inst.endpoint = c0;
                            inst.evidence = EndpointEvidence::SocketOwned;
                        }
                    }
                }
            }
            evaluate_problem(inst, own_netns);
        }
    } catch (const std::exception& e) {
        det.error = DnsmasqErrorCode::HookFailed;
        det.message = std::string("detection failed: ") + e.what();
    } catch (...) {
        det.error = DnsmasqErrorCode::HookFailed;
        det.message = "detection failed";
    }
    return det;
}

} // namespace keen_pbr3
