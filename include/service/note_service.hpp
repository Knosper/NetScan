#ifndef SERVICE_NOTE_SERVICE_HPP
#define SERVICE_NOTE_SERVICE_HPP

#include "db/database.hpp"
#include "db/note_repository.hpp"
#include "db/note_types.hpp"
#include "util/logger.hpp"
#include <memory>
#include <string>
#include <vector>

class NoteService
{
public:
    NoteService(Database& db, Logger& logger);

    enum class CreateStatus { Created, ScanNotFound, BadRequest, Error };
    enum class DeleteStatus { Deleted, NotFound, Error };

    struct CreateResult
    {
        CreateStatus              status = CreateStatus::Error;
        std::unique_ptr<ScanNote> note;
        std::string               error_message;
    };

    std::vector<ScanNote> list_notes(int scan_id);
    bool scan_exists(int scan_id);
    CreateResult create_note(int scan_id, const std::string& body);
    DeleteStatus delete_note(int scan_id, int note_id);

private:
    Database&      db_;
    Logger&        logger_;
    NoteRepository repo_;
};

#endif
