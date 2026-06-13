#pragma once
#include "transport.hpp"
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

// Test double for ITransport.
//
// Usage:
//   MockTransport t;
//   t.push_response(build_ack_frame(...));   // queue what FPGA "would send"
//   t.push_response(build_result_frame(...));
//   // ... call FpgaClient / ResultDecoder ...
//   assert(t.sent() == expected_sent_bytes);
//   assert(t.all_responses_consumed());
class MockTransport final : public ITransport {
public:
    // Appends bytes to send log.
    void send(const void* data, size_t len) override {
        const auto* p = static_cast<const uint8_t*>(data);
        sent_.insert(sent_.end(), p, p + len);
        send_lengths_.push_back(len);
    }

    // Returns bytes from the front of the response queue.
    // Returns 0 immediately if no queued responses remain (emulates transport timeout).
    size_t recv(void* buf, size_t max_len) override {
        while (recv_buf_.empty()) {
            if (response_queue_.empty()) return 0;
            recv_buf_ = std::move(response_queue_.front());
            response_queue_.pop_front();
        }
        size_t n = std::min(max_len, recv_buf_.size());
        std::memcpy(buf, recv_buf_.data(), n);
        recv_buf_.erase(recv_buf_.begin(), recv_buf_.begin() + static_cast<std::ptrdiff_t>(n));
        return n;
    }

    void reset() override {
        sent_.clear();
        send_lengths_.clear();
        response_queue_.clear();
        recv_buf_.clear();
    }

    // ── Test helpers ──────────────────────────────────────────────────────────

    // Enqueue a pre-built frame that subsequent recv() calls will return.
    void push_response(std::vector<uint8_t> frame) {
        response_queue_.push_back(std::move(frame));
    }

    // Bytes captured from all send() calls.
    const std::vector<uint8_t>& sent() const { return sent_; }
    const std::vector<size_t>& send_lengths() const { return send_lengths_; }
    void clear_sent() {
        sent_.clear();
        send_lengths_.clear();
    }

    // True when all queued responses have been consumed — use in test assertions.
    bool all_responses_consumed() const {
        return response_queue_.empty() && recv_buf_.empty();
    }

private:
    std::deque<std::vector<uint8_t>> response_queue_;
    std::vector<uint8_t>             recv_buf_;   // currently-being-consumed frame
    std::vector<uint8_t>             sent_;
    std::vector<size_t>              send_lengths_;
};
