#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <unordered_map>
#include <mutex>

namespace HoundTTS {

// ---------------------------------------------------------------------------
// Session — backend-agnostic shared state for a live transmission.
// Owned jointly by the Lua thread and the streaming thread.
// ---------------------------------------------------------------------------
struct Session {
    // Unique opaque ID returned to Lua
    std::string id;

    // Kill flag — set alive=false to stop the transmission.
    // All backends must honour this; noise generator checks it in its loop.
    std::atomic<bool> alive{true};

    // Backend-specific extension data (e.g. SRSPositionData for SRS).
    // Set by the backend before Transmit() returns; read by l_updateSession.
    // Use std::static_pointer_cast<YourType>(session->backendData) to access.
    // Protected by backendMutex — lock before reading or writing.
    std::mutex backendMutex;
    std::shared_ptr<void> backendData;

    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
};

// ---------------------------------------------------------------------------
// SessionManager — global registry of active sessions.
// ---------------------------------------------------------------------------
class SessionManager {
public:
    static SessionManager& Instance() {
        static SessionManager inst;
        return inst;
    }

    // Register a new session, or return the existing one if id is already present
    // (idempotent). Callers that need failure-on-duplicate should call Get() first.
    // Accesses m_sessions under m_mutex.
    std::shared_ptr<Session> Register(const std::string& id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_sessions.find(id);
        if (it != m_sessions.end()) {
            return it->second;
        }
        auto s = std::make_shared<Session>();
        s->id = id;
        m_sessions[id] = s;
        return s;
    }

    // Look up a session by ID. Returns nullptr if not found.
    std::shared_ptr<Session> Get(const std::string& id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_sessions.find(id);
        return (it != m_sessions.end()) ? it->second : nullptr;
    }

    // Remove a session (called when streaming finishes).
    void Remove(const std::string& id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sessions.erase(id);
    }

    // Kill every active session.  Returns the number of sessions that were alive.
    // Does NOT remove sessions from the map — backends still need them for cleanup.
    // Reap() will collect them once their backend threads finish.
    int KillAll() {
        std::lock_guard<std::mutex> lock(m_mutex);
        int count = 0;
        for (auto& [id, s] : m_sessions) {
            if (s->alive.load()) {
                s->alive.store(false);
                ++count;
            }
        }
        return count;
    }

    // Remove all finished sessions (alive==false and no backend still holding data).
    //
    // Lock ordering: m_mutex is always acquired before any session->backendMutex.
    // All code that touches backendData (srs_backend.cpp, lua_tts.cpp) must
    // release backendMutex before calling SessionManager methods that acquire m_mutex.
    void Reap() {
        // Snapshot keys under m_mutex, then probe each session without nesting locks.
        std::vector<std::string> candidates;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& [id, s] : m_sessions) {
                if (!s->alive.load())
                    candidates.push_back(id);
            }
        }
        for (auto& id : candidates) {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_sessions.find(id);
            if (it == m_sessions.end()) continue;
            auto& s = it->second;
            std::lock_guard<std::mutex> blk(s->backendMutex);
            if (!s->alive.load() && !s->backendData)
                m_sessions.erase(it);
        }
    }

private:
    std::unordered_map<std::string, std::shared_ptr<Session>> m_sessions;
    std::mutex m_mutex;

    SessionManager() = default;
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;
};

} // namespace HoundTTS
