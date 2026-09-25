#include "http.h"

#include <sstream>

bool is_request_complete(const std::string& buf)
{
    return buf.find("\r\n\r\n") != std::string::npos;
}

Request parse_request(const std::string& buf)
{
    Request req;

    
    std::size_t line_end = buf.find("\r\n");
    std::string line = (line_end == std::string::npos)
                           ? buf
                           : buf.substr(0, line_end);

    std::size_t first_space = line.find(' ');
    if (first_space == std::string::npos)
    {
        return req; 
    }

    std::size_t second_space = line.find(' ', first_space + 1);
    if (second_space == std::string::npos)
    {
        return req;
    }

    req.method = line.substr(0, first_space);
    req.target = line.substr(first_space + 1, second_space - first_space - 1);
    req.version = line.substr(second_space + 1);

    if (req.method.empty() || req.target.empty())
    {
        return req;
    }

    if (req.target[0] != '/')
    {
        return req;
    }

    req.valid = true;
    return req;
}

std::string status_text(int code)
{
    switch (code)
    {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    default:  return "Unknown";
    }
}

std::string build_response(int code,
                           const std::string& content_type,
                           const std::string& body,
                           bool include_body)
{
    std::ostringstream os;

    os << "HTTP/1.1 " << code << ' ' << status_text(code) << "\r\n";
    os << "Content-Type: " << content_type << "\r\n";

    os << "Content-Length: " << body.size() << "\r\n";

    if (code == 405)
    {
        os << "Allow: GET, HEAD\r\n";
    }

    os << "Connection: close\r\n";
    os << "\r\n";

    if (include_body)
    {
        os << body;
    }

    return os.str();
}

std::string make_error_response(int code)
{
    std::ostringstream body;

    body << "<!DOCTYPE html>\n"
         << "<html>\n"
         << "<head><meta charset=\"utf-8\">"
         << "<title>" << code << ' ' << status_text(code) << "</title></head>\n"
         << "<body>\n"
         << "<h1>" << code << ' ' << status_text(code) << "</h1>\n"
         << "</body>\n"
         << "</html>\n";

    return build_response(code, "text/html; charset=utf-8", body.str());
}
