#include "db/note_repository.hpp"
#include "db/sqlite_helpers.hpp"
#include <sqlite3.h>

namespace
{
ScanNote read_note_row(sqlite3_stmt* stmt)
{
    ScanNote note;
    note.id         = sqlite3_column_int(stmt, 0);
    note.scan_id    = sqlite3_column_int(stmt, 1);
    note.body       = col_text(stmt, 2);
    note.created_at = col_text(stmt, 3);
    return note;
}
} // namespace

NoteRepository::NoteRepository(Database& /*db*/) {}

std::vector<ScanNote> NoteRepository::list_notes_for_scan(sqlite3* h, int scan_id)
{
    const char* sql =
        "SELECT id, scan_id, body, created_at FROM scan_notes "
        "WHERE scan_id = ? ORDER BY id ASC;";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return {};

    sqlite3_bind_int(stmt.ptr, 1, scan_id);

    std::vector<ScanNote> notes;
    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
        notes.push_back(read_note_row(stmt.ptr));
    return notes;
}

std::unique_ptr<ScanNote> NoteRepository::insert_note(sqlite3* h, int scan_id,
                                                        const std::string& body)
{
    const char* insert_sql =
        "INSERT INTO scan_notes(scan_id, body, created_at) "
        "VALUES(?, ?, datetime('now'));";

    Stmt stmt;
    stmt.ptr = db_prepare(h, insert_sql);
    if (!stmt.ptr)
        return nullptr;

    sqlite3_bind_int(stmt.ptr, 1, scan_id);
    sqlite3_bind_text(stmt.ptr, 2, body.c_str(), -1, SQLITE_TRANSIENT);

    if (!exec_write_step(stmt.ptr))
        return nullptr;

    const int new_id = db_last_insert_rowid(h);

    const char* select_sql =
        "SELECT id, scan_id, body, created_at FROM scan_notes WHERE id = ?;";

    Stmt sel;
    sel.ptr = db_prepare(h, select_sql);
    if (!sel.ptr)
        return nullptr;

    sqlite3_bind_int(sel.ptr, 1, new_id);

    std::unique_ptr<ScanNote> note;
    if (sqlite3_step(sel.ptr) == SQLITE_ROW)
        note.reset(new ScanNote(read_note_row(sel.ptr)));
    return note;
}

bool NoteRepository::delete_note(sqlite3* h, int note_id, int scan_id)
{
    const char* sql = "DELETE FROM scan_notes WHERE id = ? AND scan_id = ?;";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return false;

    sqlite3_bind_int(stmt.ptr, 1, note_id);
    sqlite3_bind_int(stmt.ptr, 2, scan_id);

    return exec_write_step_changes(stmt.ptr, h) > 0;
}

bool NoteRepository::scan_exists(sqlite3* h, int scan_id)
{
    const char* sql =
        "SELECT 1 FROM scans WHERE id = ? AND deleted_at IS NULL LIMIT 1;";

    Stmt stmt;
    stmt.ptr = db_prepare(h, sql);
    if (!stmt.ptr)
        return false;

    sqlite3_bind_int(stmt.ptr, 1, scan_id);

    return sqlite3_step(stmt.ptr) == SQLITE_ROW;
}
