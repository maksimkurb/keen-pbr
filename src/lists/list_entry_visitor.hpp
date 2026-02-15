#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace keen_pbr3 {

enum class EntryType {
    Ip,
    Cidr,
    Domain,
};

// Abstract visitor interface for processing list entries one-by-one without storing them.
class ListEntryVisitor {
public:
    virtual ~ListEntryVisitor() = default;

    // Called for each parsed entry.
    virtual void on_entry(EntryType type, std::string_view entry) = 0;

    // Called when all sources for a named list have been streamed.
    virtual void on_list_complete(const std::string& list_name) { (void)list_name; }

    // Called when all processing is done (e.g., close pipes, flush buffers).
    virtual void finish() {}
};

// Convenience visitor wrapping a std::function callback.
class FunctionalVisitor : public ListEntryVisitor {
public:
    using Callback = std::function<void(EntryType, std::string_view)>;

    explicit FunctionalVisitor(Callback cb) : cb_(std::move(cb)) {}

    void on_entry(EntryType type, std::string_view entry) override {
        if (cb_) cb_(type, entry);
    }

private:
    Callback cb_;
};

// Visitor that counts entries by type without storing them.
class EntryCounter : public ListEntryVisitor {
public:
    void on_entry(EntryType type, std::string_view /*entry*/) override {
        switch (type) {
            case EntryType::Ip:     ++ips_; break;
            case EntryType::Cidr:   ++cidrs_; break;
            case EntryType::Domain: ++domains_; break;
        }
    }

    [[nodiscard]] size_t ips() const { return ips_; }
    [[nodiscard]] size_t cidrs() const { return cidrs_; }
    [[nodiscard]] size_t domains() const { return domains_; }
    [[nodiscard]] size_t total() const { return ips_ + cidrs_ + domains_; }

    void reset() { ips_ = 0; cidrs_ = 0; domains_ = 0; }

private:
    size_t ips_ = 0;
    size_t cidrs_ = 0;
    size_t domains_ = 0;
};

} // namespace keen_pbr3
