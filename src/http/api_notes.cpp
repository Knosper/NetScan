#include "http/api_notes.hpp"
#include "http/api_handler.hpp"
#include "http/http_status.hpp"
#include "http/responses.hpp"
#include "http/route_utils.hpp"

#include <nlohmann/json.hpp>

struct NoteCreateRequest
{
    int         scan_id = 0;
    std::string body;
};

static nlohmann::json note_to_json(const ScanNote& note)
{
    nlohmann::json payload;
    payload["id"]        = note.id;
    payload["scanId"]    = note.scan_id;
    payload["body"]      = note.body;
    payload["createdAt"] = note.created_at;
    return payload;
}

static void handle_notes_list_request(const httplib::Request& req, httplib::Response& res,
                                      NoteService& service, Logger& logger)
{
    handle_api_action(res, ApiRequestContext{"GET /api/scans/:id/notes", logger},
                      [&req, &res, &service]()
                      {
                          int scan_id = 0;
                          if (!try_parse_path_id(req, 1U, scan_id))
                          {
                              set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request", "invalid scan id"});
                              return;
                          }

                          if (!service.scan_exists(scan_id))
                          {
                              set_json_error(res, JsonError{http_status::NOT_FOUND, "not_found", "scan not found"});
                              return;
                          }

                          std::vector<ScanNote> notes = service.list_notes(scan_id);
                          nlohmann::json body;
                          body["notes"] = nlohmann::json::array();
                          for (const ScanNote& note : notes)
                              body["notes"].push_back(note_to_json(note));
                          set_json_response(res, http_status::OK, body.dump());
                      });
}

static bool parse_note_create_request(const httplib::Request& req, httplib::Response& res,
                                      NoteCreateRequest& request)
{
    if (!try_parse_path_id(req, 1U, request.scan_id))
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request", "invalid scan id"});
        return false;
    }
    if (reject_if_body_too_large(req, res))
        return false;

    nlohmann::json body = nlohmann::json::parse(req.body, nullptr, false);
    std::string parse_error;
    if (!ensure_json_object(body, parse_error))
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request", parse_error});
        return false;
    }

    nlohmann::json::const_iterator body_it = body.find("body");
    if (body_it == body.end() || !body_it->is_string())
    {
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      "field 'body' must be a string"});
        return false;
    }

    request.body = body_it->get<std::string>();
    return true;
}

static void handle_note_create_result(httplib::Response& res,
                                      const NoteService::CreateResult& result)
{
    switch (result.status)
    {
    case NoteService::CreateStatus::Created:
    {
        nlohmann::json resp;
        resp["note"] = note_to_json(*result.note);
        set_json_response(res, http_status::CREATED, resp.dump());
        return;
    }
    case NoteService::CreateStatus::ScanNotFound:
        set_json_error(res, JsonError{http_status::NOT_FOUND, "not_found", "scan not found"});
        return;
    case NoteService::CreateStatus::BadRequest:
        set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request",
                                      result.error_message});
        return;
    case NoteService::CreateStatus::Error:
    default:
        set_json_error(res, JsonError{http_status::INTERNAL_SERVER_ERROR, "internal_error",
                                      "failed to create note"});
        return;
    }
}

static void handle_note_create_request(const httplib::Request& req, httplib::Response& res,
                                       NoteService& service, Logger& logger)
{
    handle_api_action(res, ApiRequestContext{"POST /api/scans/:id/notes", logger},
                      [&req, &res, &service]()
                      {
                          NoteCreateRequest request;
                          if (!parse_note_create_request(req, res, request))
                              return;

                          handle_note_create_result(res, service.create_note(request.scan_id, request.body));
                      });
}

static void handle_note_delete_request(const httplib::Request& req, httplib::Response& res,
                                       NoteService& service, Logger& logger)
{
    handle_api_action(res, ApiRequestContext{"DELETE /api/scans/:id/notes/:noteId", logger},
                      [&req, &res, &service]()
                      {
                          int scan_id = 0;
                          if (!try_parse_path_id(req, 1U, scan_id))
                          {
                              set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request", "invalid scan id"});
                              return;
                          }

                          int note_id = 0;
                          if (!try_parse_path_id(req, 2U, note_id))
                          {
                              set_json_error(res, JsonError{http_status::BAD_REQUEST, "bad_request", "invalid note id"});
                              return;
                          }

                          NoteService::DeleteStatus result = service.delete_note(scan_id, note_id);
                          switch (result)
                          {
                          case NoteService::DeleteStatus::Deleted:
                          {
                              nlohmann::json body;
                              body["status"]        = "ok";
                              body["deletedNoteId"] = note_id;
                              set_json_response(res, http_status::OK, body.dump());
                              return;
                          }
                          case NoteService::DeleteStatus::NotFound:
                              set_json_error(res, JsonError{http_status::NOT_FOUND, "not_found", "note not found"});
                              return;
                          case NoteService::DeleteStatus::Error:
                          default:
                              set_json_error(res, JsonError{http_status::INTERNAL_SERVER_ERROR, "internal_error",
                                                            "failed to delete note"});
                              return;
                          }
                      });
}

void register_notes_routes(httplib::Server& svr, NoteService& service, Logger& logger)
{
    NoteService* svc = &service;
    Logger*      log = &logger;
    svr.Get(R"(/api/scans/(\d+)/notes)",
            [svc, log](const httplib::Request& req, httplib::Response& res)
            { handle_notes_list_request(req, res, *svc, *log); });
    svr.Post(R"(/api/scans/(\d+)/notes)",
             [svc, log](const httplib::Request& req, httplib::Response& res)
             { handle_note_create_request(req, res, *svc, *log); });
    svr.Delete(R"(/api/scans/(\d+)/notes/(\d+))",
               [svc, log](const httplib::Request& req, httplib::Response& res)
               { handle_note_delete_request(req, res, *svc, *log); });
}
