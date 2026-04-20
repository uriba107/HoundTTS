#include "pcm_cache.h"
#include "../config_reader.h"
#include "../utils.h"

#include <sstream>
#include <iomanip>

namespace HoundTTS {

static const char* kTag = "HoundTTS/PCMCache";
static void LogI(const std::string& msg) { Logger::Instance().Info(kTag, msg); }

// ---------------------------------------------------------------------------
// FNV-1a 64-bit hash
// ---------------------------------------------------------------------------
static uint64_t fnv1a_64(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint64_t>(p[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

// ---------------------------------------------------------------------------
// ComputeTTSRequestKey
//
// NOTE: Only per-request TTSRequest fields are folded into the key. Resolved
// provider-level runtime config (openaiEndpoint / openaiModel, piperVoicePath,
// Azure/AWS region, etc.) is intentionally NOT included. Rationale:
//   * ConfigReader is loaded exactly once during HoundTTS.init() (see
//     dllmain.cpp::l_init) and is not reloaded at runtime today, so the
//     resolved config is effectively constant for the lifetime of the DLL.
//   * The cache is flushed on mission stop via the onSimulationStop hook
//     (HoundTTS-hook.lua → HoundTTS.clearPCMCache), so any config change
//     made by editing HoundTTS-credentials.ini between missions cannot
//     produce stale hits.
// If ConfigReader ever grows a runtime reload path, this function must be
// extended to fold the relevant per-provider config into `canonical` before
// hashing (or PCMCache::Clear() must be invoked as part of the reload).
// ---------------------------------------------------------------------------
uint64_t ComputeTTSRequestKey(const TTSRequest& req) {
    // Build a canonical string of all fields that affect audio output.
    // Fields separated by unit separator (0x1f) to avoid ambiguity.
    const char sep = '\x1f';
    std::string canonical;
    canonical.reserve(512);

    canonical += TtsProviderName(req.provider);             canonical += sep;
    canonical += req.message;                                canonical += sep;
    canonical += req.voice;                                  canonical += sep;
    canonical += req.speaker;                                canonical += sep;
    canonical += req.gender;                                 canonical += sep;
    canonical += req.culture;                                canonical += sep;

    // Speed: use fixed-precision string to avoid floating-point repr differences
    {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(6) << req.speed;
        canonical += ss.str();
    }
    canonical += sep;

    // Volume
    {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(6) << req.volume;
        canonical += ss.str();
    }
    canonical += sep;

    canonical += req.awsPollyEngine;                         canonical += sep;
    canonical += req.isSSML ? "1" : "0";                     canonical += sep;
    canonical += TranslateProviderName(req.translateProvider); canonical += sep;
    canonical += req.translateLanguage;                       canonical += sep;
    canonical += req.translateSourceLanguage;

    return fnv1a_64(canonical.data(), canonical.size());
}

// ---------------------------------------------------------------------------
// PCMCache singleton
// ---------------------------------------------------------------------------
PCMCache& PCMCache::Instance() {
    static PCMCache instance;
    return instance;
}

void PCMCache::EnsureConfig_NoLock() {
    if (configLoaded_) return;
    auto& cfg = ConfigReader::Instance();
    int mb = cfg.GetCacheMaxMb();
    maxBytes_ = (mb > 0) ? static_cast<size_t>(mb) * 1024 * 1024 : size_t{100} * 1024 * 1024;
    int ttlMin = cfg.GetCacheTtlMinutes();
    ttl_ = (ttlMin > 0) ? std::chrono::duration_cast<std::chrono::seconds>(std::chrono::minutes(ttlMin))
                        : std::chrono::seconds(0);
    enabled_ = cfg.GetCacheEnabled();
    configLoaded_ = true;
    LogI("Cache config: enabled=" + std::string(enabled_ ? "true" : "false")
         + " max_mb=" + std::to_string(maxBytes_ / (1024 * 1024))
         + " ttl_minutes=" + std::to_string(ttlMin));
}

// ---------------------------------------------------------------------------
// Get — cache lookup
// ---------------------------------------------------------------------------
std::shared_ptr<const std::vector<int16_t>> PCMCache::Get(uint64_t key) {
    std::lock_guard<std::mutex> lock(mutex_);
    EnsureConfig_NoLock();
    if (!enabled_) { misses_.fetch_add(1, std::memory_order_relaxed); return nullptr; }

    SweepExpired_NoLock();

    auto it = map_.find(key);
    if (it == map_.end()) {
        misses_.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    // Promote to front of LRU and update last_access
    auto& entry = *it->second;
    entry.last_access = std::chrono::steady_clock::now();
    lru_.splice(lru_.begin(), lru_, it->second);

    hits_.fetch_add(1, std::memory_order_relaxed);
    return entry.pcm;
}

// ---------------------------------------------------------------------------
// Put — cache insertion
// ---------------------------------------------------------------------------
void PCMCache::Put(uint64_t key, std::shared_ptr<std::vector<int16_t>> pcm) {
    if (!pcm || pcm->empty()) return;

    std::lock_guard<std::mutex> lock(mutex_);
    EnsureConfig_NoLock();
    if (!enabled_) return;

    size_t entryBytes = pcm->size() * sizeof(int16_t);

    // Reject entries that exceed the cache size limit
    if (entryBytes > maxBytes_) return;

    // If already present, update in-place
    auto it = map_.find(key);
    if (it != map_.end()) {
        auto& entry = *it->second;
        totalBytes_ -= entry.bytes;
        entry.pcm = std::move(pcm);
        entry.bytes = entryBytes;
        entry.last_access = std::chrono::steady_clock::now();
        totalBytes_ += entryBytes;
        lru_.splice(lru_.begin(), lru_, it->second);
        return;
    }

    // New entry — sweep + enforce before inserting
    SweepExpired_NoLock();

    Entry e;
    e.key = key;
    e.pcm = std::move(pcm);
    e.bytes = entryBytes;
    e.last_access = std::chrono::steady_clock::now();

    lru_.push_front(std::move(e));
    map_[key] = lru_.begin();
    totalBytes_ += entryBytes;
    insertions_.fetch_add(1, std::memory_order_relaxed);

    EnforceByteCap_NoLock();
}

// ---------------------------------------------------------------------------
// Clear — full reset
// ---------------------------------------------------------------------------
void PCMCache::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    lru_.clear();
    map_.clear();
    totalBytes_ = 0;
    hits_.store(0, std::memory_order_relaxed);
    misses_.store(0, std::memory_order_relaxed);
    insertions_.store(0, std::memory_order_relaxed);
    evictions_.store(0, std::memory_order_relaxed);
    LogI("Cache cleared");
}

// ---------------------------------------------------------------------------
// Config setters
// ---------------------------------------------------------------------------
void PCMCache::SetMaxBytes(size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    maxBytes_ = bytes;
    EnforceByteCap_NoLock();
}

void PCMCache::SetTTL(std::chrono::seconds ttl) {
    std::lock_guard<std::mutex> lock(mutex_);
    ttl_ = ttl;
}

void PCMCache::SetEnabled(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = on;
    if (!on) {
        lru_.clear();
        map_.clear();
        totalBytes_ = 0;
    }
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------
PCMCache::Stats PCMCache::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats s;
    s.entries     = map_.size();
    s.bytes       = totalBytes_;
    s.hits        = hits_.load(std::memory_order_relaxed);
    s.misses      = misses_.load(std::memory_order_relaxed);
    s.insertions  = insertions_.load(std::memory_order_relaxed);
    s.evictions   = evictions_.load(std::memory_order_relaxed);
    return s;
}

// ---------------------------------------------------------------------------
// SweepExpired_NoLock — evict entries from LRU tail that exceed TTL
// ---------------------------------------------------------------------------
void PCMCache::SweepExpired_NoLock() {
    if (ttl_.count() == 0) return;  // TTL disabled
    auto cutoff = std::chrono::steady_clock::now() - ttl_;
    while (!lru_.empty() && lru_.back().last_access < cutoff) {
        auto& entry = lru_.back();
        totalBytes_ -= entry.bytes;
        map_.erase(entry.key);
        lru_.pop_back();
        evictions_.fetch_add(1, std::memory_order_relaxed);
    }
}

// ---------------------------------------------------------------------------
// EnforceByteCap_NoLock — evict LRU entries until under budget
// ---------------------------------------------------------------------------
void PCMCache::EnforceByteCap_NoLock() {
    while (totalBytes_ > maxBytes_ && !lru_.empty()) {
        auto& entry = lru_.back();
        totalBytes_ -= entry.bytes;
        map_.erase(entry.key);
        lru_.pop_back();
        evictions_.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace HoundTTS
