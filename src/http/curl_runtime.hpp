#pragma once

namespace keen_pbr3 {

// Own this in each process entry point.  It must outlive all worker threads
// that can issue HTTP requests.
class CurlRuntime {
public:
    CurlRuntime();
    ~CurlRuntime();
    CurlRuntime(const CurlRuntime&) = delete;
    CurlRuntime& operator=(const CurlRuntime&) = delete;
};

} // namespace keen_pbr3
