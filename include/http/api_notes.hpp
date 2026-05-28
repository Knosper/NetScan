#ifndef HTTP_API_NOTES_HPP
#define HTTP_API_NOTES_HPP

#include "platform.hpp"
#include "httplib/httplib.h"
#include "service/note_service.hpp"
#include "util/logger.hpp"

void register_notes_routes(httplib::Server& svr, NoteService& service, Logger& logger);

#endif
