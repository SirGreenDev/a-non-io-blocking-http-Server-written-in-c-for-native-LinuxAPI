#pragma once

#include <string>

std::string resolve_path(const std::string& docroot, const std::string& target);
bool read_file(const std::string& path, std::string& out);

std::string mime_type(const std::string& path);

std::string canonical_dir(const std::string& path);
