#include "files.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace
{

std::string real_path(const std::string& path)
{
    char* rp = realpath(path.c_str(), nullptr);
    if (rp == nullptr)
    {
        return std::string();
    }

    std::string result(rp);
    free(rp);
    return result;
}

std::string to_lower(std::string s)
{
    for (char& c : s)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return s;
}

} 

std::string canonical_dir(const std::string& path)
{
    std::string resolved = real_path(path);
    if (resolved.empty())
    {
        return std::string();
    }

    struct stat st = {};
    if (stat(resolved.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
    {
        return std::string();
    }

    return resolved;
}

std::string resolve_path(const std::string& docroot,
                         const std::string& target_in)
{
    std::string target = target_in;

    
    std::size_t cut = target.find_first_of("?#");
    if (cut != std::string::npos)
    {
        target.erase(cut);
    }

    if (target.empty() || target[0] != '/')
    {
        return std::string();
    }

    
    if (target.find("..") != std::string::npos)
    {
        return std::string();
    }

    
    if (target == "/")
    {
        target = "/index.html";
    }

   
    std::string path = docroot + target;

    
    std::string resolved = real_path(path);
    if (resolved.empty())
    {
        return std::string();
    }

    
    std::string root = docroot;
    if (!root.empty() && root[root.size() - 1] != '/')
    {
        root.push_back('/');
    }

    if (resolved.size() <= root.size() ||
        resolved.compare(0, root.size(), root) != 0)
    {
        return std::string();
    }

    
    struct stat st = {};
    if (stat(resolved.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
    {
        return std::string();
    }

    return resolved;
}

bool read_file(const std::string& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        return false;
    }

    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();

    return true;
}

std::string mime_type(const std::string& path)
{
    std::size_t dot = path.find_last_of('.');
    std::size_t slash = path.find_last_of('/');

    
    if (dot == std::string::npos ||
        (slash != std::string::npos && dot < slash))
    {
        return "application/octet-stream";
    }

    std::string ext = to_lower(path.substr(dot + 1));

    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css")                  return "text/css; charset=utf-8";
    if (ext == "js")                   return "application/javascript; charset=utf-8";
    if (ext == "json")                 return "application/json; charset=utf-8";
    if (ext == "txt")                  return "text/plain; charset=utf-8";
    if (ext == "png")                  return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif")                  return "image/gif";
    if (ext == "svg")                  return "image/svg+xml";
    if (ext == "ico")                  return "image/x-icon";
    if (ext == "pdf")                  return "application/pdf";

    return "application/octet-stream";
}
