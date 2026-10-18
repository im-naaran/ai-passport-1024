#include "badge_wifi_esp.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

typedef struct {
    esp_netif_t *netif;
    esp_event_handler_instance_t wifi_handler;
    esp_event_handler_instance_t ip_handler;
    badge_wifi_station_callback_t station_callback;
    void *station_context;
    bool owns_event_loop;
    portMUX_TYPE lock;
} badge_wifi_esp_context_t;

static badge_wifi_esp_context_t context = {.lock = portMUX_INITIALIZER_UNLOCKED};
static const char *TAG = "badge_wifi";

static bool check_result(const char *stage, esp_err_t result) {
    if (result == ESP_OK) return true;
    ESP_LOGE(TAG, "%s failed: %s", stage, esp_err_to_name(result));
    return false;
}

static void lock_service(void *opaque) {
    badge_wifi_esp_context_t *adapter = opaque;
    portENTER_CRITICAL(&adapter->lock);
}

static void unlock_service(void *opaque) {
    badge_wifi_esp_context_t *adapter = opaque;
    portEXIT_CRITICAL(&adapter->lock);
}

static bool prepare_global(void *opaque) {
    badge_wifi_esp_context_t *adapter = opaque;
    esp_err_t result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
        return check_result("esp_netif_init", result);
    result = esp_event_loop_create_default();
    if (result == ESP_OK) adapter->owns_event_loop = true;
    else if (result != ESP_ERR_INVALID_STATE)
        return check_result("esp_event_loop_create_default", result);
    return true;
}

static void release_global(void *opaque) {
    badge_wifi_esp_context_t *adapter = opaque;
    if (adapter->owns_event_loop) esp_event_loop_delete_default();
    adapter->owns_event_loop = false;
}

static bool device_suffix(void *opaque, uint16_t *suffix) {
    (void)opaque;
    uint8_t mac[6];
    if (!suffix || esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK) return false;
    *suffix = (uint16_t)((uint16_t)mac[4] << 8) | mac[5];
    return true;
}

static bool create_ap_netif(void *opaque) {
    badge_wifi_esp_context_t *adapter = opaque;
    adapter->netif = esp_netif_create_default_wifi_ap();
    if (!adapter->netif) {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_ap failed");
        return false;
    }
    /* The ESP-IDF default AP netif already owns 192.168.4.1 and starts its DHCP server from
     * WIFI_EVENT_AP_START. Do not stop/restart it ahead of the radio: that can report STARTED
     * without establishing a reliable DHCP data path on every phone. */
    esp_netif_ip_info_t info = {0};
    esp_err_t result = esp_netif_get_ip_info(adapter->netif, &info);
    if (!check_result("SoftAP default IP query", result)) {
        esp_netif_destroy_default_wifi(adapter->netif);
        adapter->netif = NULL;
        return false;
    }
    if (info.ip.addr != ESP_IP4TOADDR(192, 168, 4, 1) ||
        info.gw.addr != ESP_IP4TOADDR(192, 168, 4, 1) ||
        info.netmask.addr != ESP_IP4TOADDR(255, 255, 255, 0)) {
        ESP_LOGE(TAG, "SoftAP default IP configuration is unexpected");
        esp_netif_destroy_default_wifi(adapter->netif);
        adapter->netif = NULL;
        return false;
    }
    return true;
}

static void destroy_ap_netif(void *opaque) {
    badge_wifi_esp_context_t *adapter = opaque;
    if (adapter->netif) esp_netif_destroy_default_wifi(adapter->netif);
    adapter->netif = NULL;
}

static bool init_wifi(void *opaque, const char *ssid) {
    (void)opaque;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    // The temporary open AP has no station credentials or radio state to persist.
    // Avoid a needless NVS allocation at the tightest startup point.
    init.nvs_enable = 0;
    if (!check_result("esp_wifi_init", esp_wifi_init(&init))) return false;
    wifi_config_t config = {0};
    size_t length = strlen(ssid);
    if (length > sizeof(config.ap.ssid)) length = sizeof(config.ap.ssid);
    memcpy(config.ap.ssid, ssid, length);
    config.ap.ssid_len = length;
    config.ap.channel = 1;
    /* Keep the SDK's usual multi-station headroom even though the UI has one logical client.
     * iOS can reassociate before the driver's previous station slot is released; a single slot
     * then produces a permanent "max connection, deauth" loop and blocks all IP traffic. */
    config.ap.max_connection = 4;
    config.ap.authmode = WIFI_AUTH_OPEN;
    esp_err_t result = esp_wifi_set_mode(WIFI_MODE_AP);
    if (result == ESP_OK) result = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (!check_result("SoftAP mode configuration", result)) {
        esp_wifi_deinit();
        return false;
    }
    return true;
}

static void deinit_wifi(void *opaque) { (void)opaque; esp_wifi_deinit(); }

static void ensure_dhcp_started(badge_wifi_esp_context_t *adapter) {
    esp_netif_dhcp_status_t status = ESP_NETIF_DHCP_INIT;
    esp_err_t result = esp_netif_dhcps_get_status(adapter->netif, &status);
    if (result == ESP_OK && status != ESP_NETIF_DHCP_STARTED)
        result = esp_netif_dhcps_start(adapter->netif);
    if (result == ESP_OK) result = esp_netif_dhcps_get_status(adapter->netif, &status);
    if (!check_result("SoftAP DHCP start", result)) return;
    if (status != ESP_NETIF_DHCP_STARTED) {
        ESP_LOGE(TAG, "SoftAP DHCP did not reach STARTED state: %d", (int)status);
        return;
    }
    ESP_LOGI(TAG, "SoftAP ready at 192.168.4.1");
}

static void wifi_event(void *opaque, esp_event_base_t base, int32_t id, void *data) {
    (void)base;
    badge_wifi_esp_context_t *adapter = opaque;
    if (id == WIFI_EVENT_AP_START) {
        ensure_dhcp_started(adapter);
    } else if (id == WIFI_EVENT_AP_STOP) {
        ESP_LOGI(TAG, "event AP_STOP");
    } else if (id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *event = data;
        ESP_LOGI(TAG, "event STA_ASSOCIATED aid=%u; waiting for client traffic",
                 event ? event->aid : 0);
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *event = data;
        ESP_LOGW(TAG, "event STA_DISCONNECTED aid=%u reason=%u",
                 event ? event->aid : 0, event ? event->reason : 0);
        wifi_sta_list_t stations = {0};
        esp_err_t result = esp_wifi_ap_get_sta_list(&stations);
        if (result == ESP_OK)
            ESP_LOGI(TAG, "associated stations remaining=%u", stations.num);
        else
            ESP_LOGW(TAG, "station list query failed: %s", esp_err_to_name(result));
        if (adapter->station_callback && (result != ESP_OK || stations.num == 0))
            adapter->station_callback(adapter->station_context, false);
    }
}

static void ip_event(void *opaque, esp_event_base_t base, int32_t id, void *data) {
    (void)opaque;
    (void)base;
    if (id == IP_EVENT_AP_STAIPASSIGNED) {
        const ip_event_ap_staipassigned_t *event = data;
        if (event)
            ESP_LOGI(TAG, "event DHCP_ACK_QUEUED ip=" IPSTR "; awaiting client traffic",
                     IP2STR(&event->ip));
        else
            ESP_LOGI(TAG, "event DHCP_ACK_QUEUED; awaiting client traffic");
    }
}

static bool register_events(void *opaque, badge_wifi_station_callback_t callback,
                            void *callback_context) {
    badge_wifi_esp_context_t *adapter = opaque;
    adapter->station_callback = callback;
    adapter->station_context = callback_context;
    esp_err_t result = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, adapter, &adapter->wifi_handler);
    if (!check_result("Wi-Fi event registration", result)) {
        adapter->station_callback = NULL;
        adapter->station_context = NULL;
        return false;
    }
    result = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, ip_event, adapter, &adapter->ip_handler);
    if (!check_result("IP event registration", result)) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              adapter->wifi_handler);
        adapter->wifi_handler = NULL;
        adapter->station_callback = NULL;
        adapter->station_context = NULL;
        return false;
    }
    return true;
}

static void unregister_events(void *opaque) {
    badge_wifi_esp_context_t *adapter = opaque;
    if (adapter->ip_handler)
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED,
                                              adapter->ip_handler);
    if (adapter->wifi_handler)
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              adapter->wifi_handler);
    adapter->ip_handler = NULL;
    adapter->wifi_handler = NULL;
    adapter->station_callback = NULL;
    adapter->station_context = NULL;
}

static bool start_wifi(void *opaque) {
    (void)opaque;
    return check_result("esp_wifi_start", esp_wifi_start());
}
static void stop_wifi(void *opaque) { (void)opaque; esp_wifi_stop(); }

badge_wifi_adapter_t badge_wifi_esp_adapter(void) {
    context.netif = NULL;
    context.wifi_handler = NULL;
    context.ip_handler = NULL;
    context.station_callback = NULL;
    context.station_context = NULL;
    context.owns_event_loop = false;
    return (badge_wifi_adapter_t){
        .context = &context,
        .lock = lock_service,
        .unlock = unlock_service,
        .prepare_global = prepare_global,
        .release_global = release_global,
        .device_suffix = device_suffix,
        .create_ap_netif = create_ap_netif,
        .destroy_ap_netif = destroy_ap_netif,
        .init_wifi = init_wifi,
        .deinit_wifi = deinit_wifi,
        .register_events = register_events,
        .unregister_events = unregister_events,
        .start_wifi = start_wifi,
        .stop_wifi = stop_wifi,
    };
}
