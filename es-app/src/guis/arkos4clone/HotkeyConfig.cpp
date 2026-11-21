#include "guis/arkos4clone/HotkeyConfig.h"

#include "Log.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <set>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace HotkeyConfig
{
    static const char* CONF = "/home/ark/ogage.conf";

    static std::string trim(const std::string& s)
    {
        const size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos)
            return std::string();
        const size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    std::map<std::string, std::string> read()
    {
        std::map<std::string, std::string> out;
        std::ifstream in(CONF);
        std::string line;
        while (std::getline(in, line))
        {
            const std::string t = trim(line);
            if (t.empty() || t[0] == '#')
                continue;

            const size_t eq = t.find('=');
            if (eq == std::string::npos)
                continue;

            out[trim(t.substr(0, eq))] = trim(t.substr(eq + 1));
        }
        return out;
    }

    bool writeValues(const std::map<std::string, std::string>& values)
    {
        std::ifstream in(CONF);
        std::vector<std::string> lines;
        std::string line;
        std::set<std::string> seen;

        while (std::getline(in, line))
        {
            const std::string t = trim(line);
            if (!t.empty() && t[0] != '#')
            {
                const size_t eq = t.find('=');
                if (eq != std::string::npos)
                {
                    const std::string key = trim(t.substr(0, eq));
                    const auto it = values.find(key);
                    if (it != values.end())
                    {
                        lines.push_back(key + "=" + it->second);
                        seen.insert(key);
                        continue;
                    }
                }
            }
            lines.push_back(line);
        }

        for (const auto& kv : values)
        {
            if (!seen.count(kv.first))
                lines.push_back(kv.first + "=" + kv.second);
        }

        const std::string tmp = std::string(CONF) + ".tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            if (!out.is_open())
                return false;
            for (const auto& l : lines)
                out << l << '\n';
        }

        if (rename(tmp.c_str(), CONF) != 0)
        {
            unlink(tmp.c_str());
            return false;
        }

        chmod(CONF, 0644);
        LOG(LogInfo) << "HotkeyConfig: wrote " << values.size() << " entries to " << CONF;
        return true;
    }

    void restartOgAge()
    {
        // --no-block: ogage.service has ExecStartPre=sleep 3, do not stall ES
        std::system("sudo systemctl restart --no-block ogage >/dev/null 2>&1");
    }
}
