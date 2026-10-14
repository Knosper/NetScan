#ifndef HTTP_API_NOTES_HPP
#define HTTP_API_NOTES_HPP

#include "platform.hpp"
#include "httplib/httplib.h"
#include "service/note_service.hpp"
#include "service/scan_service.hpp"
#include "service/settings_service.hpp"
#include "util/logger.hpp"

void register_notes_routes(httplib::Server& svr, NoteService& service, ScanService& scan_service,
                           SettingsService& settings_service, Logger& logger);

#endif
