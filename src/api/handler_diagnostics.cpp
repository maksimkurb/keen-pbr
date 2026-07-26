#include "handler_diagnostics.hpp"

#ifdef WITH_API

#include "../util/safe_exec.hpp"

#include <algorithm>
#include <fstream>
#include <httplib.h>
#include <memory>
#include <mutex>

namespace keen_pbr3 {

void register_diagnostics_handler(ApiServer& server) {
    server.get_stream("/api/diagnostics/command-failure",
                      [](const httplib::Request&, httplib::Response& response) {
        auto input = std::make_shared<std::ifstream>(
            command_failure_log_path(), std::ios::binary | std::ios::ate);
        if (!*input) {
            response.status = 204;
            return;
        }

        const auto end = input->tellg();
        if (end < 0) {
            response.status = 204;
            return;
        }
        const auto length = static_cast<std::size_t>(end);
        auto mutex = std::make_shared<std::mutex>();
        // Failed-command records are published with an atomic rename. This open
        // stream remains attached to the old inode if a newer failure replaces
        // the path, so the size and bytes stay consistent for this response.
        response.set_content_provider(
            length,
            "text/plain; charset=utf-8",
            [input, mutex](std::size_t offset,
                           std::size_t requested,
                           httplib::DataSink& sink) {
                const std::lock_guard<std::mutex> lock(*mutex);
                input->clear();
                input->seekg(static_cast<std::streamoff>(offset));
                if (!*input) return false;

                char buffer[16 * 1024];
                std::size_t remaining = requested;
                while (remaining > 0) {
                    const auto chunk = std::min(remaining, sizeof(buffer));
                    input->read(buffer, static_cast<std::streamsize>(chunk));
                    const auto count = static_cast<std::size_t>(input->gcount());
                    if (count == 0) return false;
                    if (!sink.write(buffer, count)) return false;
                    remaining -= count;
                }
                return true;
            });
    });
}

} // namespace keen_pbr3

#endif // WITH_API
