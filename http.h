#pragma once

#include <string>


struct Request
{
    std::string method;
    std::string target;
    std::string version;
    bool valid = false;
};


bool is_request_complete(const std::string& buf);

Request parse_request(const std::string& buf);

std::string status_text(int code);

std::string build_response(int code,
                           const std::string& content_type,
                           const std::string& body,
                           bool include_body = true);

std::string make_error_response(int code);
