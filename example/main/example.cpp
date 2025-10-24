#include <cstdio>
#include <esp_http_server.hpp>
#include <esp_wifi_types_generic.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <lwip/err.h>
#include <lwip/sys.h>
#include <freertos/FreeRTOS.h>
#include <nvs.h>
#include <nvs_flash.h>

static auto wifi_event_group = EventGroupHandle_t{};
static auto retry_count      = int{0};

constexpr auto WIFI_CONNECTED = BIT0;
constexpr auto WIFI_FAIL      = BIT1;

constexpr auto WIFI_TAG = "WiFi";
constexpr auto IP_TAG   = "IP";
#define SSID      CONFIG_WIFI_SSID
#define PASSWORD  CONFIG_WIFI_PASSWORD
#define MAX_RETRY CONFIG_WIFI_MAX_RETRY

void wifi_init_sta(void);

int hello_handler(struct netconn *conn, std::string_view /*request*/) {
        http_server::HttpResponse resp;
        resp.set_body("Hello, World!");
        resp.set_header("Content-Type", "text/plain");
        resp.send(conn);
        return ESP_OK;
}
int not_found_handler(struct netconn *conn, std::string_view /*request*/) {
        http_server::HttpResponse resp(http_server::HttpResponse::StatusCode::NotFound);
        resp.set_body("404 Not Found");
        resp.set_header("Content-Type", "text/plain");
        resp.send(conn);
        return ESP_OK;
}

extern "C" void app_main(void) {
        auto ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
                ESP_ERROR_CHECK(nvs_flash_erase());
                ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);
        wifi_init_sta();

        static http_server::HttpServer server;

        server.add_route("/hello", http_server::HttpMethod::GET, hello_handler);

        // Start the HTTP server task
        if (!server.start()) {
                ESP_LOGE("Server", "Failed to start HTTP server");
        } else {
                ESP_LOGI("Server", "HTTP server started");
        }

        while (true) { vTaskDelay(pdMS_TO_TICKS(10)); }
}

static auto wifi_event_handler = [](void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
        case WIFI_EVENT_STA_DISCONNECTED:
                if (retry_count < MAX_RETRY) {
                        esp_wifi_connect();
                        retry_count++;
                        ESP_LOGI(WIFI_TAG, "connection retrying.");
                } else {
                        xEventGroupSetBits(wifi_event_group, WIFI_FAIL);
                        ESP_LOGE(WIFI_TAG, "connection failed.");
                }
        }
};

static auto ip_event_handler = [](void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
        switch (event_id) {
        case IP_EVENT_STA_GOT_IP:
                auto event = (ip_event_got_ip_t *)event_data;
                ESP_LOGI(IP_TAG, "got ip:", IPSTR, IP2STR(&event->ip_info.ip));
                retry_count = 0;
                xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED);
        }
};

void wifi_init_sta(void) {
        wifi_event_group = xEventGroupCreate();
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();
        auto wifi_init_cfg = (wifi_init_config_t)WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
        auto instance_wifi_id = esp_event_handler_instance_t{};
        auto instance_ip_id   = esp_event_handler_instance_t{};
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, nullptr, &instance_wifi_id));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, ip_event_handler, nullptr, &instance_ip_id));

        auto wifi_cfg = wifi_config_t{};
        memset(&wifi_cfg, 0, sizeof(wifi_config_t));
        strcpy((char *)wifi_cfg.sta.ssid, SSID);
        strcpy((char *)wifi_cfg.sta.password, PASSWORD);
        wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        wifi_cfg.sta.sae_pwe_h2e        = WPA3_SAE_PWE_BOTH;
        strcpy((char *)wifi_cfg.sta.sae_h2e_identifier, "");

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
        ESP_ERROR_CHECK(esp_wifi_start());

        auto bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED | WIFI_FAIL, pdFALSE, pdFALSE, portMAX_DELAY);
        if (bits & WIFI_CONNECTED) {
                ESP_LOGI(WIFI_TAG, "connected to AP SSID: Airtel_RemoteStation");
        } else if (bits & WIFI_FAIL) {
                ESP_LOGE(WIFI_TAG, "failed to connect to AP SSID: Airtel_RemoteStation");
        } else {
                ESP_LOGW(WIFI_TAG, "unexpeected event");
        }
}
