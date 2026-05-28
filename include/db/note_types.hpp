#ifndef DB_NOTE_TYPES_HPP
#define DB_NOTE_TYPES_HPP

#include <string>

struct ScanNote
{
    int         id = 0;
    int         scan_id = 0;
    std::string body;
    std::string created_at;
};

#endif
