#pragma once

#ifndef HOUNDTTS_PCM_CACHE_H
#define HOUNDTTS_PCM_CACHE_H

#include "../backend.h"
#include "pcm_queue.h"

#include <vector>
#include <list>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <string>
#include <chrono>
#include <cstdint>
#include <atomic>

namespace HoundTTS {

// Compute a deterministic 64-bit FNV-1a hash over the TTSRequest fields that
// affect audio output (provider, message, voice, speaker, gender, culture,
// speed, volume, awsPollyEngine, isSSML, translateProvider/Language/SourceLanguage).
// Transmission-only fields (freqs, host, coalition, lat/lon, encrypt, etc.) are excluded.
uint64_t ComputeTTSRequestKey(const TTSRequest& req);

// ---------------------------------------------------------------------------
// PCMCache — in-memory LRU cache of synthesised 16kHz mono PCM buffers.
//
// Keyed by uint64_t (from ComputeTTSRequestKey).
// Byte-capped with LRU eviction + lazy TTL sweep (last-access based).
// Thread-safe (single mutex, short critical sections).
// Lifecycle: lives for the DLL load; call Clear() on mission end.
// ---------------------------------------------------------------------------
class PCMCache {
public:
    static PCMCache& Instance();

    // Look up cached PCM. Returns nullptr on miss.
    // On hit: bumps last-access, promotes to LRU front, increments hit counter.
    std::shared_ptr<const std::vector<int16_t>> Get(uint64_t key);

    // Insert or replace an entry. Runs TTL sweep + LRU eviction if over budget.
    void Put(uint64_t key, std::shared_ptr<std::vector<int16_t>> pcm);

    // Drop all entries and reset stats.
    void Clear();

    // Apply config (called lazily on first access or when config reloads).
    void SetMaxBytes(size_t bytes);
    void SetTTL(std::chrono::seconds ttl);
    void SetEnabled(bool on);

    struct Stats {
        size_t entries    = 0;
        size_t bytes      = 0;
        uint64_t hits     = 0;
        uint64_t misses   = 0;
        uint64_t insertions = 0;
        uint64_t evictions  = 0;
    };
    Stats GetStats() const;

private:
    PCMCache() = default;
    ~PCMCache() = default;
    PCMCache(const PCMCache&) = delete;
    PCMCache& operator=(const PCMCache&) = delete;

    struct Entry {
        uint64_t key = 0;
        std::shared_ptr<std::vector<int16_t>> pcm;
        size_t bytes = 0;
        std::chrono::steady_clock::time_point last_access;
    };

    using LRUList     = std::list<Entry>;
    using LRUIterator = LRUList::iterator;

    // Evict TTL-expired entries from LRU tail (must hold mutex_).
    void SweepExpired_NoLock();

    // Evict LRU entries until totalBytes_ <= maxBytes_ (must hold mutex_).
    void EnforceByteCap_NoLock();

    // Lazy-init config from ConfigReader on first access (must hold mutex_).
    void EnsureConfig_NoLock();

    mutable std::mutex mutex_;
    LRUList lru_;
    std::unordered_map<uint64_t, LRUIterator> map_;

    size_t totalBytes_ = 0;
    size_t maxBytes_   = 0;   // 0 = not yet initialized from config
    std::chrono::seconds ttl_{0};     // 0 = not yet initialized
    bool   enabled_    = true;
    bool   configLoaded_ = false;

    // Stats
    std::atomic<uint64_t> hits_{0};
    std::atomic<uint64_t> misses_{0};
    std::atomic<uint64_t> insertions_{0};
    std::atomic<uint64_t> evictions_{0};
};

// ---------------------------------------------------------------------------
// CachingPCMQueue — tee wrapper that forwards chunks to a real PCMQueue AND
// accumulates them for cache insertion on completion.
//
// Subclass of PCMQueue so providers (which take PCMQueue&) can use it directly.
// Thread-safety: Push/MarkDone/MarkFailed/Finalize may be invoked from the
// provider synthesis thread while the caller thread observes completion via
// the downstream queue. accumulator_ is guarded by accMutex_; failed_ and
// doneForwarded_ are std::atomic.
//
// Cache-commit lifecycle:
//   - MarkDone()      → forwards done to downstream only (does NOT commit).
//                       Providers internally call MarkDone() on the queue
//                       reference, so committing here would cache truncated
//                       audio whenever the provider fails mid-stream.
//   - MarkFailed()    → latches the failed flag so a subsequent Finalize()
//                       will not commit to cache.
//   - Finalize(ok)    → called by the pipeline after the provider returns.
//                       Commits the accumulated buffer to PCMCache only if
//                       ok && !failed && !empty, then forwards MarkDone()
//                       to the downstream queue (idempotent).
//
// Requires: downstream must be non-null (throws std::invalid_argument if null).
// ---------------------------------------------------------------------------
class CachingPCMQueue : public PCMQueue {
public:
    CachingPCMQueue(std::shared_ptr<PCMQueue> downstream, uint64_t cacheKey)
        : downstream_(std::move(downstream))
        , cacheKey_(cacheKey)
    {
        if (!downstream_) {
            throw std::invalid_argument("CachingPCMQueue: downstream cannot be null");
        }
    }

    void Push(std::vector<int16_t> chunk) override {
        {
            std::lock_guard<std::mutex> lock(accMutex_);
            accumulator_.insert(accumulator_.end(), chunk.begin(), chunk.end());
        }
        // Forward to downstream consumer (downstream handles its own locking)
        downstream_->Push(std::move(chunk));
    }

    // Forwards completion to the downstream queue so blocked consumers in
    // PCMQueue::Pop() wake up. Idempotent. Does NOT commit to cache — that
    // decision is deferred to Finalize() so the pipeline can suppress
    // caching when the provider reports failure.
    void MarkDone() override {
        if (doneForwarded_.exchange(true, std::memory_order_acq_rel)) return;
        downstream_->MarkDone();
    }

    // Latch the "failed" flag; any subsequent Finalize() will skip cache
    // insertion. Safe to call from any thread; no-op if already failed.
    void MarkFailed() {
        failed_.store(true, std::memory_order_release);
    }

    // Commit the accumulated buffer to PCMCache iff success && !failed &&
    // non-empty, then forward MarkDone() to the downstream queue. Call from
    // the pipeline once the provider returns, with ok = provider return value.
    void Finalize(bool success) {
        if (!success) failed_.store(true, std::memory_order_release);
        if (!failed_.load(std::memory_order_acquire)) {
            std::vector<int16_t> pcm;
            {
                std::lock_guard<std::mutex> lock(accMutex_);
                pcm.swap(accumulator_);
            }
            if (!pcm.empty()) {
                auto sp = std::make_shared<std::vector<int16_t>>(std::move(pcm));
                PCMCache::Instance().Put(cacheKey_, std::move(sp));
            }
        }
        MarkDone();
    }

private:
    std::shared_ptr<PCMQueue> downstream_;
    uint64_t cacheKey_;
    std::mutex accMutex_;
    std::vector<int16_t> accumulator_;
    std::atomic<bool> failed_{false};
    std::atomic<bool> doneForwarded_{false};
};

} // namespace HoundTTS

#endif // HOUNDTTS_PCM_CACHE_H
