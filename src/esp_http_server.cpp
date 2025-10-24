#include <esp_http_server.hpp>
#include <cassert>
#include <cstring>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/api.h>
#include <sstream>

static constexpr const char *TAG = "HttpServer";

namespace http_server {

        HttpServer::HttpServer() : route_count(0), listen_conn(nullptr), server_task_handle(nullptr) {
        }

        HttpServer::~HttpServer() {
                if (listen_conn) {
                        netconn_close(listen_conn);
                        netconn_delete(listen_conn);
                }
                if (server_task_handle)
                        vTaskDelete(server_task_handle);
        }

        bool HttpServer::add_route(std::string_view uri, HttpMethod method, HttpHandler handler) {
                if (route_count >= MAX_ROUTES) {
                        ESP_LOGW(TAG, "Route limit reached (%d), cannot add: %.*s", MAX_ROUTES, int(uri.size()), uri.data());
                        assert(false and "Too many HTTP routes");
                        return false;
                }
                routes[route_count]       = HttpRoute{uri, method, handler};
                route_hashes[route_count] = fnv1a_hash(uri);
                ++route_count;
                return true;
        }

        bool HttpServer::start() {
                if (server_task_handle != nullptr)
                        return false;
                BaseType_t result = xTaskCreate(server_task, "http_server", 4096, this, 5, &server_task_handle);
                return result == pdPASS;
        }

        constexpr std::string_view HttpServer::method_to_str(HttpMethod method) {
                switch (method) {
                case HttpMethod::GET:
                        return "GET";
                case HttpMethod::POST:
                        return "POST";
                default:
                        return "";
                }
        }

        bool HttpServer::parse_request_line(const char *req, size_t len, HttpMethod &method, std::string_view &uri) {
                if (len < 5)
                        return false;

                if (std::strncmp(req, "GET ", 4) == 0) {
                        method            = HttpMethod::GET;
                        const char *start = req + 4;
                        const char *end   = static_cast<const char *>(std::memchr(start, ' ', len - 4));
                        if (not end)
                                return false;
                        uri = std::string_view(start, end - start);
                        return true;
                }
                if (std::strncmp(req, "POST ", 5) == 0) {
                        method            = HttpMethod::POST;
                        const char *start = req + 5;
                        const char *end   = static_cast<const char *>(std::memchr(start, ' ', len - 5));
                        if (not end)
                                return false;
                        uri = std::string_view(start, end - start);
                        return true;
                }
                return false;
        }

        HttpRoute *HttpServer::find_route(std::string_view uri, HttpMethod method) {
                if (route_count == 0)
                        return nullptr;
                uint32_t search_hash = fnv1a_hash(uri);
                for (size_t i = 0; i < route_count; ++i) {
                        if (route_hashes[i] == search_hash and routes[i].method == method and routes[i].uri == uri)
                                return &routes[i];
                }
                return nullptr;
        }

        esp_err_t HttpServer::handle_client(struct netconn *conn) {
                struct netbuf *inbuf = nullptr;
                char *buf            = nullptr;
                u16_t buflen         = 0;

                if (netconn_recv(conn, &inbuf) != ERR_OK)
                        return ESP_FAIL;
                netbuf_data(inbuf, (void **)&buf, &buflen);
                HttpMethod method;
                std::string_view uri;
                if (not parse_request_line(buf, buflen, method, uri)) {
                        netbuf_delete(inbuf);
                        send_404(conn);
                        return ESP_FAIL;
                }
                HttpRoute *route = find_route(uri, method);
                esp_err_t err    = ESP_FAIL;
                if (route and route->handler) {
                        err = route->handler(conn, std::string_view(buf, buflen));
                } else {
                        send_404(conn);
                        err = ESP_OK;
                }

                netbuf_delete(inbuf);
                return err;
        }

        void HttpServer::send_404(struct netconn *conn) {
                const char *resp_404 = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
                netconn_write(conn, resp_404, strlen(resp_404), NETCONN_COPY);
        }

        void HttpServer::run() {
                listen_conn = netconn_new(NETCONN_TCP);
                if (not listen_conn)
                        return; // handle error as needed
                netconn_bind(listen_conn, nullptr, 80);
                netconn_listen(listen_conn);
                while (true) {
                        struct netconn *newconn = nullptr;
                        err_t err               = netconn_accept(listen_conn, &newconn);
                        if (err == ERR_OK) {
                                handle_client(newconn);
                                netconn_close(newconn);
                                netconn_delete(newconn);
                        } else {
                                vTaskDelay(pdMS_TO_TICKS(10)); // avoid busy loop
                        }
                }
        }

        void HttpServer::server_task(void *arg) {
                HttpServer *server = static_cast<HttpServer *>(arg);
                server->run();
                vTaskDelete(nullptr);
        }

        constexpr uint32_t HttpServer::fnv1a_hash(std::string_view data) {
                uint32_t hash = 2166136261u;
                for (char c : data) {
                        hash ^= static_cast<uint8_t>(c);
                        hash *= 16777619u;
                }
                return hash;
        }

        HttpResponse::HttpResponse(StatusCode code) : status_code(code), header_count(0) {
                set_header("Content-Type", "text/plain");
        }

        void HttpResponse::set_status(StatusCode code) {
                status_code = code;
        }

        bool HttpResponse::set_header(const char *key, const std::string &value) {
                if (key == nullptr) {
                        ESP_LOGW(TAG, "Header key cannot be null");
                        return false;
                }
                for (size_t i = 0; i < header_count; ++i) {
                        if (std::strcmp(headers[i].key, key) == 0) {
                                headers[i].value = value;
                                return true;
                        }
                }
                if (header_count >= MAX_HEADERS) {
                        ESP_LOGW(TAG, "Header limit reached (%d), cannot add header: %s", MAX_HEADERS, key);
                        assert(false and "Too many HTTP headers");
                        return false;
                }
                headers[header_count].key   = key;
                headers[header_count].value = value;
                ++header_count;
                return true;
        }

        void HttpResponse::set_body(const std::string &body) {
                HttpResponse::body_ = body;
                set_header("Content-Length", std::to_string(body_.size()));
        }

        std::string HttpResponse::build_response() const {
                constexpr size_t BUFFER_SIZE = 1024;
                char buffer[BUFFER_SIZE];
                size_t offset = 0;
                int written   = snprintf(buffer + offset, BUFFER_SIZE - offset, "HTTP/1.1 %d %s\r\n", static_cast<int>(status_code), status_text());
                if (written < 0 or static_cast<size_t>(written) >= BUFFER_SIZE - offset)
                        return {};
                offset += written;
                for (size_t i = 0; i < header_count; ++i) {
                        written = snprintf(buffer + offset, BUFFER_SIZE - offset, "%s: %s\r\n", headers[i].key, headers[i].value.c_str());
                        if (written < 0 or static_cast<size_t>(written) >= BUFFER_SIZE - offset)
                                return {};
                        offset += written;
                }
                if (offset + 2 >= BUFFER_SIZE)
                        return {};
                strcpy(buffer + offset, "\r\n");
                offset += 2;
                size_t body_size = body_.size();
                if (offset + body_size >= BUFFER_SIZE)
                        return {};
                memcpy(buffer + offset, body_.data(), body_size);
                offset += body_size;
                return std::string(buffer, offset);
        }

        void HttpResponse::send(struct netconn *conn) const {
                std::string resp = build_response();
                netconn_write(conn, resp.data(), resp.size(), NETCONN_COPY);
        }

        constexpr const char *HttpResponse::status_text(StatusCode code) {
                switch (code) {
                case StatusCode::OK:
                        return "OK";
                case StatusCode::NotFound:
                        return "Not Found";
                case StatusCode::BadRequest:
                        return "Bad Request";
                case StatusCode::InternalServerError:
                        return "Internal Server Error";
                default:
                        return "";
                }
        }

        constexpr const char *HttpResponse::status_text() const {
                return status_text(status_code);
        }
} // namespace http_server
