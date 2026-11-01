#pragma once

#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace kudroid::kuart {

// Canonical library paths share one initialization result.
class JniLibraryRegistry {
public:
    template <typename Initializer>
    bool Initialize(const std::string& path, Initializer initialize) {
        std::unique_lock<std::mutex> lock(mutex_);
        auto [it, inserted] = entries_.try_emplace(path);
        Entry& entry = it->second;
        if (!inserted) {
            if (!entry.finished && entry.owner == std::this_thread::get_id()) return true;
            ready_.wait(lock, [&] { return entry.finished; });
            return entry.succeeded;
        }
        entry.owner = std::this_thread::get_id();
        lock.unlock();
        bool succeeded = false;
        try {
            succeeded = initialize();
        } catch (...) {
            Finish(entry, false);
            throw;
        }
        Finish(entry, succeeded);
        return succeeded;
    }

    static bool IsSupportedVersion(int version) {
        return version == 0x00010002 || version == 0x00010004 || version == 0x00010006;
    }

private:
    struct Entry {
        std::thread::id owner;
        bool finished = false;
        bool succeeded = false;
    };
    void Finish(Entry& entry, bool succeeded) {
        std::lock_guard<std::mutex> lock(mutex_);
        entry.succeeded = succeeded;
        entry.finished = true;
        ready_.notify_all();
    }
    std::mutex mutex_;
    std::condition_variable ready_;
    std::map<std::string, Entry> entries_;
};

}  // namespace kudroid::kuart
