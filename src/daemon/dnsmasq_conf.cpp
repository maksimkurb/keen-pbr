#include "dnsmasq_conf.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>

namespace keen_pbr3 {

namespace {

std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return std::string(s.substr(b, e - b));
}

std::vector<std::string> split_commas(std::string_view v) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= v.size()) {
        std::size_t end = v.find(',', start);
        if (end == std::string_view::npos) end = v.size();
        out.push_back(trim(v.substr(start, end - start)));
        start = end + 1;
    }
    return out;
}

bool ends_with(std::string_view s, std::string_view suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

bool parse_port(const std::string& v, std::uint16_t& out) {
    if (v.empty() || v.size() > 5) return false;
    unsigned long n = 0;
    for (char c : v) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + static_cast<unsigned>(c - '0');
    }
    if (n > 65535) return false;
    out = static_cast<std::uint16_t>(n);
    return true;
}

struct OptionDef {
    char short_name;          // 0 if none
    const char* long_name;    // without leading "--"
    bool takes_value;
};

constexpr OptionDef kOptions[] = {
    {'C', "conf-file", true},
    {'7', "conf-dir", true},
    {'p', "port", true},
    {'a', "listen-address", true},
    {'i', "interface", true},
    {'I', "except-interface", true},
    {'z', "bind-interfaces", false},
    {0, "bind-dynamic", false},
    {0, "servers-file", true},
};

struct ArgvOption {
    const OptionDef* def;
    std::string value;
};

std::vector<ArgvOption> walk_argv(const std::vector<std::string>& argv) {
    std::vector<ArgvOption> out;
    for (std::size_t i = 1; i < argv.size(); ++i) {
        const std::string& a = argv[i];
        if (a.size() >= 3 && a[0] == '-' && a[1] == '-') {
            const auto eq = a.find('=');
            const std::string name = a.substr(2, eq == std::string::npos ? std::string::npos : eq - 2);
            for (const auto& d : kOptions) {
                if (name != d.long_name) continue;
                if (!d.takes_value) {
                    out.push_back({&d, ""});
                } else if (eq != std::string::npos) {
                    out.push_back({&d, a.substr(eq + 1)});
                } else if (i + 1 < argv.size()) {
                    out.push_back({&d, argv[++i]});
                }
                break;
            }
        } else if (a.size() >= 2 && a[0] == '-' && a[1] != '-') {
            for (const auto& d : kOptions) {
                if (d.short_name == 0 || a[1] != d.short_name) continue;
                if (!d.takes_value) {
                    out.push_back({&d, ""});
                } else if (a.size() > 2) {
                    out.push_back({&d, a.substr(2)});
                } else if (i + 1 < argv.size()) {
                    out.push_back({&d, argv[++i]});
                }
                break;
            }
        }
    }
    return out;
}

void append_list(std::vector<std::string>& dst, const std::string& v) {
    for (auto& item : split_commas(v)) {
        if (!item.empty()) dst.push_back(std::move(item));
    }
}

// Removes a trailing comment: first '#' preceded by space/tab, outside quotes.
std::string strip_comment(const std::string& line) {
    bool in_quotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') in_quotes = !in_quotes;
        else if (c == '#' && !in_quotes && i > 0 && (line[i - 1] == ' ' || line[i - 1] == '\t')) {
            return trim(line.substr(0, i));
        }
    }
    return line;
}

class Reader {
public:
    Reader(DnsmasqEffectiveConfig& cfg, const std::string& skip, const DnsmasqConfReadLimits& lim)
        : cfg_(cfg), skip_(skip), lim_(lim) {}

    void read_file(const std::string& path, int depth) {
        if (depth > lim_.max_depth) {
            cfg_.warnings.push_back("conf-file nesting too deep, skipped: " + path);
            return;
        }
        if (cfg_.conf_files_read.size() >= lim_.max_files) {
            cfg_.warnings.push_back("too many config files, skipped: " + path);
            return;
        }
        if (!skip_.empty() && path == skip_) return;

        struct stat st;
        if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            cfg_.warnings.push_back("cannot read config file: " + path);
            return;
        }
        if (static_cast<std::size_t>(st.st_size) > lim_.max_file_bytes) {
            cfg_.warnings.push_back("config file too large, skipped: " + path);
            return;
        }
        std::ifstream f(path);
        if (!f) {
            cfg_.warnings.push_back("cannot read config file: " + path);
            return;
        }
        cfg_.conf_files_read.push_back(path);

        std::string raw;
        while (std::getline(f, raw)) {
            std::string line = trim(raw);
            if (line.empty() || line[0] == '#') continue;
            line = strip_comment(line);
            if (line.empty()) continue;
            const auto eq = line.find('=');
            const std::string key = trim(line.substr(0, eq));
            const std::string value = eq == std::string::npos ? std::string() : trim(line.substr(eq + 1));

            if (key == "conf-file") {
                read_file(value, depth + 1);
            } else if (key == "conf-dir") {
                read_conf_dir(parse_conf_dir_value(value), depth);
            } else if (key == "port") {
                std::uint16_t p = 0;
                if (parse_port(value, p)) cfg_.port = p;
            } else if (key == "listen-address") {
                append_list(cfg_.listen_addresses, value);
            } else if (key == "interface") {
                append_list(cfg_.interfaces, value);
            } else if (key == "except-interface") {
                append_list(cfg_.except_interfaces, value);
            } else if (key == "bind-interfaces") {
                cfg_.bind_interfaces = true;
            } else if (key == "bind-dynamic") {
                cfg_.bind_dynamic = true;
            } else if (key == "servers-file") {
                if (!value.empty()) cfg_.servers_files.push_back(value);
            }
            // conf-script and everything else: ignored
        }
    }

    // Records the spec and reads the files it accepts.
    void read_conf_dir(const ConfDirSpec& spec, int depth) {
        cfg_.conf_dirs.push_back(spec);
        std::unique_ptr<DIR, int (*)(DIR*)> dir(::opendir(spec.dir.c_str()), [](DIR* d) { return d ? ::closedir(d) : 0; });
        if (!dir) {
            cfg_.warnings.push_back("cannot read conf-dir: " + spec.dir);
            return;
        }
        std::vector<std::string> names;
        while (const dirent* e = ::readdir(dir.get())) {
            const std::string name = e->d_name;
            if (!conf_dir_accepts(spec, name)) continue;
            struct stat st;
            const std::string full = spec.dir + "/" + name;
            if (::stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
            names.push_back(name);
        }
        std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
            return std::strcmp(a.c_str(), b.c_str()) < 0;
        });
        for (const auto& n : names) read_file(spec.dir + "/" + n, depth + 1);
    }

private:
    DnsmasqEffectiveConfig& cfg_;
    const std::string& skip_;
    const DnsmasqConfReadLimits& lim_;
};

} // namespace

std::vector<std::string> conf_files_from_argv(const std::vector<std::string>& argv) {
    std::vector<std::string> out;
    for (const auto& o : walk_argv(argv)) {
        if (o.def->takes_value && std::strcmp(o.def->long_name, "conf-file") == 0) out.push_back(o.value);
    }
    return out;
}

ConfDirSpec parse_conf_dir_value(std::string_view value) {
    ConfDirSpec spec;
    const auto parts = split_commas(value);
    if (parts.empty()) return spec;
    spec.dir = parts[0];
    while (spec.dir.size() > 1 && spec.dir.back() == '/') spec.dir.pop_back();
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const std::string& p = parts[i];
        if (p.empty()) continue;
        if (p[0] == '*') spec.include_suffixes.push_back(p.substr(1));
        else spec.exclude_suffixes.push_back(p);
    }
    return spec;
}

bool conf_dir_accepts(const ConfDirSpec& spec, std::string_view name) {
    if (name.empty()) return false;
    if (name[0] == '.') return false;
    if (name.back() == '~') return false;
    if (name.size() >= 2 && name.front() == '#' && name.back() == '#') return false;
    for (const auto& s : spec.exclude_suffixes) {
        if (ends_with(name, s)) return false;
    }
    if (!spec.include_suffixes.empty()) {
        for (const auto& s : spec.include_suffixes) {
            if (ends_with(name, s)) return true;
        }
        return false;
    }
    return true;
}

DnsmasqEffectiveConfig read_effective_config(const std::vector<std::string>& argv,
                                             const std::string& default_conf_file,
                                             const std::string& skip_file,
                                             const DnsmasqConfReadLimits& limits) {
    DnsmasqEffectiveConfig cfg;
    Reader reader(cfg, skip_file, limits);

    std::vector<std::string> queue = conf_files_from_argv(argv);
    if (queue.empty() && !default_conf_file.empty()) queue.push_back(default_conf_file);
    for (const auto& f : queue) reader.read_file(f, 0);

    // Command line is applied after the files: it wins for `port`, lists concatenate.
    for (const auto& o : walk_argv(argv)) {
        const std::string name = o.def->long_name;
        if (name == "conf-dir") {
            // Deviation from the literal spec text: also read the files of an
            // argv conf-dir (Debian passes `-7 /etc/dnsmasq.d`).
            reader.read_conf_dir(parse_conf_dir_value(o.value), 0);
        } else if (name == "port") {
            std::uint16_t p = 0;
            if (parse_port(o.value, p)) cfg.port = p;
        } else if (name == "listen-address") {
            append_list(cfg.listen_addresses, o.value);
        } else if (name == "interface") {
            append_list(cfg.interfaces, o.value);
        } else if (name == "except-interface") {
            append_list(cfg.except_interfaces, o.value);
        } else if (name == "bind-interfaces") {
            cfg.bind_interfaces = true;
        } else if (name == "bind-dynamic") {
            cfg.bind_dynamic = true;
        } else if (name == "servers-file") {
            if (!o.value.empty()) cfg.servers_files.push_back(o.value);
        }
    }
    return cfg;
}

} // namespace keen_pbr3
