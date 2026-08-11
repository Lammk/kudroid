#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace kudroid {

/// CacheManager for translated DEX files.
///
/// Translates a DEX file into an iOS-understandable form (currently a JAR for
/// the Avian JVM) and caches the result keyed by:
///   - the SHA-256 hash of the ORIGINAL DEX file (detects APK updates)
///   - a tool version integer (detects translator upgrades)
///
/// If either the hash or the version changes, the cache is considered stale
/// and the DEX is re-translated. This mirrors ART's .oat/.odex cache pattern.
///
/// Cache layout:
///   <cacheDir>/<dex_sha256>_v<version>.bin        — translated data
///   <cacheDir>/<dex_sha256>_v<version>.meta.json  — metadata (hash, version)
class DexCacheManager {
public:
    static DexCacheManager& getInstance();

    /// Set the cache directory (e.g. Documents/android_cache).
    void setCacheDirectory(const std::string& dir);

    /// Return the current cache directory.
    const std::string& cacheDirectory() const { return cacheDir_; }

    /// Check whether a valid cache entry exists for the given DEX + tool version.
    /// Returns true only if BOTH the DEX hash matches AND the version matches.
    bool hasValidCache(const std::string& dexPath, int toolVersion);

    /// Load the cached translated data for the given DEX + version.
    /// Returns true and fills `out` on success.
    bool loadCache(const std::string& dexPath, int toolVersion,
                   std::vector<uint8_t>& out);

    /// Save translated data to the cache for the given DEX + version.
    /// Writes atomically (tmp file + rename) to avoid corruption on crash.
    bool saveCache(const std::string& dexPath, int toolVersion,
                   const std::vector<uint8_t>& data);

    /// Translate a DEX file to a JAR (via DexToJar), using the cache.
    ///
    /// If a valid cache entry exists (hash + version match), the cached JAR is
    /// loaded. Otherwise the DEX is translated, cached, and returned.
    /// Returns true and fills `outJar` on success.
    bool translateAndCache(const std::string& dexPath, int toolVersion,
                           std::vector<uint8_t>& outJar, std::string* error = nullptr);

    /// Remove all cache entries for the given DEX (all versions).
    void clearCacheForDex(const std::string& dexPath);

    /// Remove the entire cache directory contents.
    void clearCache();

    /// Compute the SHA-256 hex digest of a file. Returns empty string on error.
    static std::string sha256File(const std::string& path);

private:
    DexCacheManager() = default;

    /// Compute the cache file path (without extension) for a DEX + version.
    std::string cacheBasePath(const std::string& dexPath, int version) const;

    /// Compute the SHA-256 hex digest of a byte buffer.
    static std::string sha256(const uint8_t* data, size_t len);

    std::string cacheDir_;
};

} // namespace kudroid
