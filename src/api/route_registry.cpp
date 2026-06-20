#include "route_registry.h"
#include "auth_handler.h"
#include "download_handler.h"
#include "multipart_upload_handler.h"
#include "share_handler.h"
#include "folder_handler.h"
#include "file_handler.h"
#include "file_upload_ops.h"
#include "../auth/request_auth.h"
#include "../monitor/metrics.h"

namespace solar_api {

void register_api_routes(const std::shared_ptr<solar_http::HttpRouter>& router,
                         const AppServices& svc) {
    router->get("/api/v1/health", [](const solar_http::HttpRequest&,
                                       solar_http::HttpResponse& resp) {
        resp.set_json(R"({"status":"ok","service":"SolarDrive"})");
    });

    router->get("/metrics", [](const solar_http::HttpRequest&,
                                 solar_http::HttpResponse& resp) {
        resp.set_body(solar_monitor::Metrics::dump());
        resp.set_header("Content-Type", "text/plain; version=0.0.4; charset=utf-8");
    });

    router->post("/api/v1/auth/register",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            svc.auth.handle_register(req, resp);
        });

    router->post("/api/v1/auth/login",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            svc.auth.handle_login(req, resp);
        });

    router->post("/api/v1/upload",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            try {
                if (!solar_auth::require_auth_user(req, resp)) {
                    return;
                }
                handle_single_upload(
                    req, resp, req.auth_user_id(),
                    svc.store, svc.file_dao, svc.folder_dao, svc.redis,
                    svc.cfg.limits.max_file_size_bytes());
            } catch (const std::exception& e) {
                resp.set_error(500, std::string("upload failed: ") + e.what());
            }
        });

    router->post("/api/v1/upload/init",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            svc.multipart.handle_init(req, resp);
        });

    router->put("/api/v1/upload/{upload_id}/part/{part_num}",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            svc.multipart.handle_upload_part(req, resp);
        });

    router->post("/api/v1/upload/{upload_id}/complete",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            svc.multipart.handle_complete(req, resp);
        });

    router->get("/api/v1/upload/{upload_id}",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            svc.multipart.handle_status(req, resp);
        });

    router->del("/api/v1/upload/{upload_id}",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            svc.multipart.handle_abort(req, resp);
        });

    router->get("/api/v1/files",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            svc.files.handle_list(req, resp);
        });

    router->del("/api/v1/files/{id}",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            svc.files.handle_delete(req, resp);
        });

    router->get("/api/v1/folders",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            svc.folder.handle_list(req, resp);
        });

    router->post("/api/v1/folders",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            svc.folder.handle_create(req, resp);
        });

    router->put("/api/v1/folders/{id}",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            svc.folder.handle_rename(req, resp);
        });

    router->del("/api/v1/folders/{id}",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            svc.folder.handle_delete(req, resp);
        });

    router->get("/api/v1/files/{id}",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            try {
                svc.download.handle(req, resp);
            } catch (const std::exception& e) {
                resp.set_error(500, std::string("download failed: ") + e.what());
            }
        });

    router->post("/api/v1/share",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            svc.share.handle_create(req, resp);
        });

    router->post("/api/v1/share/save",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            svc.share.handle_save(req, resp);
        });

    router->get("/api/v1/shares",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            svc.share.handle_list(req, resp);
        });

    router->del("/api/v1/share/{token}",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            svc.share.handle_revoke(req, resp);
        });

    router->get("/s/{token}",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            svc.share.handle_download(req, resp);
        });
}

} // namespace solar_api
