#pragma once
#ifndef ES_CORE_UTILS_NAMERESOLVER_H
#define ES_CORE_UTILS_NAMERESOLVER_H

#include <string>
#include <unordered_map>
#include <ctime>

class NameResolver
{
public:
    explicit NameResolver(const std::string& rootPath);
    ~NameResolver() = default;

    // Resolve display name for a given full path
    // Priority: gamelist.xml > .name sidecar > script comment > filename
    std::string resolve(const std::string& fullPath);

    // Check if gamelist.xml was loaded
    bool isLoaded() const { return mLoaded; }

    // Force reload gamelist.xml
    void reload();

private:
    void loadGamelist();
    void checkAndReloadIfNeeded();
    std::string normalizeRelative(const std::string& fullPath) const;
    std::string readSidecar(const std::string& fullPath) const;
    std::string readScriptComment(const std::string& fullPath) const;
    std::string getFallbackName(const std::string& fullPath) const;

    std::string mRootPath;
    std::string mGamelistPath;
    std::unordered_map<std::string, std::string> mNameMap;
    std::unordered_map<std::string, std::string> mResolveCache;
    std::time_t mLastModified;
    std::time_t mLastCheckTime;
    bool mLoaded;
};

#endif // ES_CORE_UTILS_NAMERESOLVER_H
