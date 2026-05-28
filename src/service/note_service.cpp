#include "service/note_service.hpp"

static const int kMaxNoteBodyLength = 4096;

NoteService::NoteService(Database& db, Logger& logger)
    : db_(db), logger_(logger), repo_(db)
{
}

std::vector<ScanNote> NoteService::list_notes(int scan_id)
{
    return db_.read([this, scan_id](sqlite3* h) { return repo_.list_notes_for_scan(h, scan_id); });
}

bool NoteService::scan_exists(int scan_id)
{
    return db_.read([this, scan_id](sqlite3* h) { return repo_.scan_exists(h, scan_id); });
}

NoteService::CreateResult NoteService::create_note(int scan_id, const std::string& body)
{
    if (body.empty())
        return {CreateStatus::BadRequest, nullptr, "field 'body' must not be empty"};

    if (static_cast<int>(body.size()) > kMaxNoteBodyLength)
        return {CreateStatus::BadRequest, nullptr, "field 'body' exceeds maximum length"};

    const bool exists = db_.read([this, scan_id](sqlite3* h) { return repo_.scan_exists(h, scan_id); });
    if (!exists)
        return {CreateStatus::ScanNotFound, nullptr, ""};

    return db_.write(
        [this, scan_id, &body](sqlite3* h) -> CreateResult
        {
            std::unique_ptr<ScanNote> note = repo_.insert_note(h, scan_id, body);
            if (!note)
            {
                logger_.error("note_service: failed to insert note for scan id " +
                              std::to_string(scan_id));
                return {CreateStatus::Error, nullptr, ""};
            }
            return {CreateStatus::Created, std::move(note), ""};
        });
}

NoteService::DeleteStatus NoteService::delete_note(int scan_id, int note_id)
{
    return db_.write(
        [this, scan_id, note_id](sqlite3* h)
        {
            if (!repo_.delete_note(h, note_id, scan_id))
                return DeleteStatus::NotFound;
            return DeleteStatus::Deleted;
        });
}
