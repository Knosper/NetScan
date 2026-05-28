#ifndef DB_NOTE_REPOSITORY_HPP
#define DB_NOTE_REPOSITORY_HPP

#include "db/database.hpp"
#include "db/note_types.hpp"
#include <memory>
#include <vector>

// REQUIRES: caller passes the sqlite3* handle from the enclosing
// Database::read()/write() access scope.
class NoteRepository
{
public:
    explicit NoteRepository(Database& db);

    std::vector<ScanNote> list_notes_for_scan(sqlite3* h, int scan_id);
    std::unique_ptr<ScanNote> insert_note(sqlite3* h, int scan_id, const std::string& body);
    bool delete_note(sqlite3* h, int note_id, int scan_id);
    bool scan_exists(sqlite3* h, int scan_id);
};

#endif
