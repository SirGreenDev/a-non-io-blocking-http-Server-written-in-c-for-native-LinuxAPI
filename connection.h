#pragma once

#include <ctime>
#include <cstddef>
#include <string>

struct Connection
{
    enum State
    {
        Reading, 
        Writing  
    };

    State state = Reading;

    
    std::string read_buf;

    std::string write_buf;
    std::size_t write_offset = 0;

    std::time_t last_active = 0;
};
