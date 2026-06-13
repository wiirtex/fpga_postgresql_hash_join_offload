#pragma once
#include <cstddef>

// Abstract transport layer — decouples protocol logic from physical medium.
//
// Contract for recv():
//   - Returns up to max_len bytes; may return fewer (partial reads are normal).
//   - Returns 0 if no data is available within the implementation's internal timeout.
//   - Throws on unrecoverable error.
//
// Implementations: MockTransport (tests), UartTransport (physical board).
class ITransport {
public:
    virtual void   send(const void* data, size_t len) = 0;
    virtual size_t recv(void* buf,        size_t max_len) = 0;
    virtual void   reset() = 0;
    virtual ~ITransport() = default;
};
