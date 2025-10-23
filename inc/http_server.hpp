#pragma once

#include <array>
#include <cassert>
#include <lwip/api.h>
#include <string>
#include <string_view>

namespace http_server {
static constexpr size_t MAX_HEADERS = 10;
enum class HttpMethod {
  GET,
  POST,
};

using HttpHandler = int (*)(struct netconn *conn,
                            [[maybe_unused]] std::string_view request);

struct HttpRoute {
  std::string_view uri;
  HttpMethod method;
  HttpHandler handler;
};

class HttpServer {
public:
  HttpServer();
  ~HttpServer();

  bool add_route(std::string_view uri, HttpMethod method, HttpHandler handler);
  bool start();

private:
  static constexpr uint32_t fnv1a_hash(std::string_view data);
  static constexpr size_t MAX_ROUTES = 10;
  std::array<HttpRoute, MAX_ROUTES> routes;
  std::array<uint32_t, MAX_ROUTES> route_hashes;
  size_t route_count;
  struct netconn *listen_conn;
  TaskHandle_t server_task_handle;

  static constexpr std::string_view method_to_str(HttpMethod method);
  static bool parse_request_line(const char *req, size_t len,
                                 HttpMethod &method, std::string_view &uri);
  HttpRoute *find_route(std::string_view uri, HttpMethod method);
  esp_err_t handle_client(struct netconn *conn);
  void send_404(struct netconn *conn);
  void run();
  static void server_task(void *arg);
};

class HttpResponse {
public:
  enum class StatusCode {
    OK = 200,
    NotFound = 404,
    BadRequest = 400,
    InternalServerError = 500,
  };

  HttpResponse(StatusCode code = StatusCode::OK);

  void set_status(StatusCode code);
  bool set_header(const char *key, const std::string &value);
  void set_body(const std::string &body);
  std::string build_response() const;
  void send(struct netconn *conn) const;

private:
  struct Header {
    const char *key;
    std::string value;
  };

  static constexpr size_t MAX_HEADERS = 10;
  StatusCode status_code;
  Header headers[MAX_HEADERS];
  size_t header_count = 0;
  std::string body_;

  static constexpr const char *status_text(StatusCode code);
  constexpr const char *status_text() const;
};
} // namespace http_server
