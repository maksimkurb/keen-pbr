#include "resolver_fallback.hpp"

#include "../crypto/md5.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <ostream>

namespace keen_pbr3::ipc {
namespace {

bool valid_reason_code(const std::string& value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::islower(c) || std::isdigit(c) || c == '_';
    });
}

} // namespace

bool emit_resolver_fallback(std::ostream& output,
                            const std::string& fallback_path,
                            const std::string& reason_code,
                            std::int64_t timestamp) {
    if (!valid_reason_code(reason_code)) return false;
    std::ifstream input(fallback_path, std::ios::binary);
    if (!input) return false;

    crypto::detail::MD5State md5;
    char hash_buffer[16 * 1024];
    while (input.read(hash_buffer, sizeof(hash_buffer)) || input.gcount() != 0) {
        md5.update(reinterpret_cast<const std::uint8_t*>(hash_buffer),
                   static_cast<std::size_t>(input.gcount()));
    }
    if (!input.eof()) return false;
    input.clear();
    input.seekg(0);
    if (!input) return false;

    output << "# keen-pbr resolver state: fallback reason=" << reason_code << '\n';
    output << "txt-record=config-hash.keen.pbr," << timestamp << "|"
           << crypto::digest_to_hex(md5.digest()) << '\n';
    output << "txt-record=resolver-state.keen.pbr," << timestamp << "|fallback|" << reason_code << '\n';
    output << input.rdbuf();
    return static_cast<bool>(output);
}

} // namespace keen_pbr3::ipc
