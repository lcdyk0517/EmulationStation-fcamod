#include "utils/NameResolver.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include "pugixml/src/pugixml.hpp"

#include <fstream>
#include <sys/stat.h>
#include <ctime>

NameResolver::NameResolver(const std::string& rootPath)
    : mRootPath(rootPath)
    , mLastModified(0)
    , mLastCheckTime(0)
    , mLoaded(false)
{
    // Normalize root path: remove trailing slash
    while (!mRootPath.empty() && mRootPath.back() == '/')
        mRootPath.pop_back();

    mGamelistPath = mRootPath + "/gamelist.xml";
    loadGamelist();
}

void NameResolver::loadGamelist()
{
    mNameMap.clear();
    mLoaded = false;
    mLastModified = 0;

    if (!Utils::FileSystem::exists(mGamelistPath))
        return;

    // Record mtime
    struct stat st;
    if (stat(mGamelistPath.c_str(), &st) == 0)
        mLastModified = st.st_mtime;

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(mGamelistPath.c_str());
    if (!result)
        return;

    pugi::xml_node gameList = doc.child("gameList");
    if (!gameList)
        return;

    // Parse <folder> entries
    for (pugi::xml_node folder = gameList.child("folder"); folder; folder = folder.next_sibling("folder"))
    {
        std::string path = Utils::String::trim(folder.child("path").text().as_string());
        std::string name = Utils::String::trim(folder.child("name").text().as_string());

        if (!path.empty() && !name.empty())
        {
            // Normalize: remove leading "./"
            if (path.size() > 2 && path[0] == '.' && path[1] == '/')
                path = path.substr(2);
            mNameMap[path] = name;
        }
    }

    // Parse <game> entries
    for (pugi::xml_node game = gameList.child("game"); game; game = game.next_sibling("game"))
    {
        std::string path = Utils::String::trim(game.child("path").text().as_string());
        std::string name = Utils::String::trim(game.child("name").text().as_string());

        if (!path.empty() && !name.empty())
        {
            // Normalize: remove leading "./"
            if (path.size() > 2 && path[0] == '.' && path[1] == '/')
                path = path.substr(2);
            mNameMap[path] = name;
        }
    }

    mLoaded = true;
}

void NameResolver::reload()
{
    mResolveCache.clear();
    loadGamelist();
}

void NameResolver::checkAndReloadIfNeeded()
{
    // Rate limit: only check stat every 2 seconds
    std::time_t now = std::time(nullptr);
    if (now - mLastCheckTime < 2)
        return;
    mLastCheckTime = now;

    if (mGamelistPath.empty())
        return;

    struct stat st;
    if (stat(mGamelistPath.c_str(), &st) != 0)
    {
        // File no longer exists
        if (mLoaded)
        {
            loadGamelist();
            mResolveCache.clear();
        }
        return;
    }

    if (st.st_mtime != mLastModified)
    {
        loadGamelist();
        mResolveCache.clear();
    }
}

std::string NameResolver::normalizeRelative(const std::string& fullPath) const
{
    // Check if fullPath starts with rootPath + "/"
    std::string prefix = mRootPath + "/";
    if (fullPath.rfind(prefix, 0) == 0)
        return fullPath.substr(prefix.size());

    // Fallback: return filename only
    return Utils::FileSystem::getFileName(fullPath);
}

std::string NameResolver::readSidecar(const std::string& fullPath) const
{
    std::string nameFile = fullPath + ".name";
    if (!Utils::FileSystem::exists(nameFile))
        return "";

    std::ifstream f(nameFile.c_str());
    if (!f)
        return "";

    std::string name;
    std::getline(f, name);
    return Utils::String::trim(name);
}

std::string NameResolver::readScriptComment(const std::string& fullPath) const
{
    std::ifstream f(fullPath.c_str());
    if (!f)
        return "";

    std::string line;
    const std::string marker = "# NAME:";
    int linesChecked = 0;

    while (linesChecked < 10 && std::getline(f, line))
    {
        linesChecked++;
        std::string trimmed = Utils::String::trim(line);
        if (trimmed.rfind(marker, 0) == 0)
        {
            std::string candidate = Utils::String::trim(trimmed.substr(marker.size()));
            if (!candidate.empty())
                return candidate;
        }
    }

    return "";
}

std::string NameResolver::getFallbackName(const std::string& fullPath) const
{
    return Utils::FileSystem::getStem(fullPath);
}

std::string NameResolver::resolve(const std::string& fullPath)
{
    // Check cache first
    auto itCache = mResolveCache.find(fullPath);
    if (itCache != mResolveCache.end())
        return itCache->second;

    // Check if gamelist.xml changed and reload if needed
    checkAndReloadIfNeeded();

    std::string result;

    // Priority 1: gamelist.xml
    std::string relativePath = normalizeRelative(fullPath);
    auto it = mNameMap.find(relativePath);
    if (it != mNameMap.end())
    {
        result = it->second;
    }
    else
    {
        // Priority 2: .name sidecar file
        result = readSidecar(fullPath);
        if (result.empty())
        {
            // Priority 3: # NAME: in script comments
            result = readScriptComment(fullPath);
            if (result.empty())
            {
                // Priority 4: filename fallback
                result = getFallbackName(fullPath);
            }
        }
    }

    mResolveCache[fullPath] = result;
    return result;
}
