#pragma once
// SoftwareKernel — C++ simulation of the FPGA hash_join_kernel.
//
// Implements ITransport: processes real wire-protocol frames in send(),
// queues ACK/RESULT responses for recv().
//
// The join logic uses std::unordered_map (not linear probing) —
// the FPGA kernel uses linear probing, but the results are identical.
//
// Usage: swap SoftwareKernel → UartTransport to run on the real board.
#include "transport.hpp"
#include "hash_join_types.hpp"
#include <cstdint>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <vector>

class SoftwareKernel final : public ITransport {
public:
    // ── ITransport ────────────────────────────────────────────────────────────

    // Each send() call is exactly one complete wire-protocol frame.
    void send(const void* data, size_t len) override {
        const auto* p = static_cast<const uint8_t*>(data);
        all_sent_.insert(all_sent_.end(), p, p + len);

        if (len < 3) return;
        auto msg   = static_cast<MsgType>(p[0]);
        uint16_t n = static_cast<uint16_t>(p[1]) | (static_cast<uint16_t>(p[2]) << 8);
        const uint8_t* payload = p + 3;

        switch (msg) {
        case MSG_CONFIGURE:  on_configure(payload);     break;
        case MSG_INNER_DATA: on_inner(n, payload);      break;
        case MSG_OUTER_DATA: on_outer(n, payload);      break;
        case MSG_RESET:      on_reset(true);            break;
        default: break;
        }
    }

    size_t recv(void* buf, size_t max_len) override {
        while (out_buf_.empty()) {
            if (resp_queue_.empty()) return 0;
            out_buf_ = std::move(resp_queue_.front());
            resp_queue_.pop_front();
        }
        size_t n = std::min(max_len, out_buf_.size());
        std::memcpy(buf, out_buf_.data(), n);
        out_buf_.erase(out_buf_.begin(), out_buf_.begin() + static_cast<std::ptrdiff_t>(n));
        return n;
    }

    void reset() override { on_reset(false); }

    // ── Inspection ────────────────────────────────────────────────────────────

    // All bytes ever passed to send() — useful for hex-dump in demos/tests.
    const std::vector<uint8_t>& all_sent() const { return all_sent_; }

private:
    KeyType  key_type_    = KEY_INT32;
    uint32_t n_matched_   = 0;
    uint32_t n_processed_ = 0;
    uint32_t outer_total_ = 0;
    uint8_t  session_id_  = 0;

    std::unordered_map<int64_t, Tid> ht_;
    std::deque<std::vector<uint8_t>> resp_queue_;
    std::vector<uint8_t>             out_buf_;
    std::vector<uint8_t>             all_sent_;

    // ── Protocol handlers ─────────────────────────────────────────────────────

    void on_configure(const uint8_t* p) {
        // p[0]=algorithm, p[1]=key_type, p[2..3]=rx_buf_slots,
        // p[4..7]=inner_count, p[8..11]=outer_count, p[12]=session_id
        key_type_    = static_cast<KeyType>(p[1]);
        n_matched_   = 0;
        n_processed_ = 0;
        outer_total_  = read_u32(p + 8);
        session_id_   = p[12];
        ht_.clear();
        push_ack(PHASE_BUILDING, RX_CREDIT_TUPLES, 0, 0);
    }

    void on_inner(uint16_t count, const uint8_t* p) {
        size_t stride = tuple_stride();
        for (uint16_t i = 0; i < count; ++i, p += stride) {
            int64_t key = read_key(p);
            Tid     tid = read_tid(p + key_bytes());
            ht_[key]    = tid;
        }
        push_ack(PHASE_BUILDING, RX_CREDIT_TUPLES, 0, 0);
    }

    void on_outer(uint16_t count, const uint8_t* p) {
        size_t stride = tuple_stride();
        std::vector<ResultPair> matches;
        for (uint16_t i = 0; i < count; ++i, p += stride) {
            int64_t key      = read_key(p);
            Tid     outer_tid = read_tid(p + key_bytes());
            auto    it        = ht_.find(key);
            if (it != ht_.end()) {
                matches.push_back({it->second, outer_tid});
                ++n_matched_;
            }
            ++n_processed_;
        }
        if (!matches.empty()) push_result(matches);
        const uint16_t credit = (n_processed_ >= outer_total_) ? 0 : RX_CREDIT_TUPLES;
        push_ack(PHASE_PROBING, credit, n_processed_, n_matched_);
    }

    void on_reset(bool emit_ack) {
        ht_.clear();
        resp_queue_.clear();
        out_buf_.clear();
        n_matched_ = n_processed_ = outer_total_ = 0;
        session_id_ = 0;
        if (emit_ack) push_ack(PHASE_IDLE, 0, 0, 0);
    }

    // ── Frame builders ────────────────────────────────────────────────────────

    void push_ack(Phase phase, uint16_t credit, uint32_t processed, uint32_t matched) {
        std::vector<uint8_t> f;
        f.push_back(MSG_ACK);
        push_u16(f, 1);
        f.push_back(static_cast<uint8_t>(phase));
        f.push_back(session_id_);
        push_u16(f, credit);
        push_u32(f, processed);
        push_u32(f, matched);
        resp_queue_.push_back(std::move(f));
    }

    void push_result(const std::vector<ResultPair>& pairs) {
        std::vector<uint8_t> f;
        f.push_back(MSG_RESULT);
        push_u16(f, static_cast<uint16_t>(pairs.size()));
        for (const auto& r : pairs) {
            push_u32(f, r.inner_tid.blkno);
            push_u16(f, r.inner_tid.offno);
            push_u32(f, r.outer_tid.blkno);
            push_u16(f, r.outer_tid.offno);
        }
        resp_queue_.push_back(std::move(f));
    }

    // ── Byte helpers ──────────────────────────────────────────────────────────

    size_t key_bytes()    const { return (key_type_ == KEY_INT32) ? 4u : 8u; }
    size_t tuple_stride() const { return key_bytes() + 6u; }  // key + blkno(4) + offno(2)

    int64_t read_key(const uint8_t* p) const {
        if (key_type_ == KEY_INT32) {
            uint32_t u = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
                       | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
            return static_cast<int64_t>(static_cast<int32_t>(u));
        }
        uint64_t u = 0;
        for (int i = 0; i < 8; ++i) u |= static_cast<uint64_t>(p[i]) << (8 * i);
        return static_cast<int64_t>(u);
    }

    static Tid read_tid(const uint8_t* p) {
        uint32_t blkno = read_u32(p);
        uint16_t offno = static_cast<uint16_t>(p[4]) | (static_cast<uint16_t>(p[5]) << 8);
        return {blkno, offno};
    }

    static uint32_t read_u32(const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
             | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }

    static void push_u16(std::vector<uint8_t>& v, uint16_t x) {
        v.push_back(static_cast<uint8_t>(x));
        v.push_back(static_cast<uint8_t>(x >> 8));
    }
    static void push_u32(std::vector<uint8_t>& v, uint32_t x) {
        v.push_back(static_cast<uint8_t>(x));
        v.push_back(static_cast<uint8_t>(x >> 8));
        v.push_back(static_cast<uint8_t>(x >> 16));
        v.push_back(static_cast<uint8_t>(x >> 24));
    }
};
