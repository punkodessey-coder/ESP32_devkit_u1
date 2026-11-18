#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_eth.h"
#include "lwip/sockets.h"

#define WIFI_SSID CONFIG_ESP_WIFI_SSID
#define WIFI_PASS CONFIG_ESP_WIFI_PASSWORD

/* Static W5500 address: any PC on the same LAN can connect to it. */
#define W5500_IP "192.168.1.23"
#define W5500_GW "192.168.1.1"
#define W5500_MASK "255.255.255.0"
#define RADAR_TCP_PORT 9999

#define ETH_MOSI_GPIO 23
#define ETH_MISO_GPIO 19
#define ETH_SCLK_GPIO 18
#define ETH_CS_GPIO 5
#define ETH_INT_GPIO 4
#define ETH_RST_GPIO 33

static const char *TAG = "csi_radar";
static volatile uint32_t csi_count = 0;
static int listen_sock = -1;
static int client_sock = -1;

static void csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    csi_count++;
    if (client_sock < 0 || !info->buf) return;
    char msg[256];
    int n = snprintf(msg, sizeof(msg),
        "{\"seq\":%u,\"rssi\":%d,\"len\":%u,\"first_word\":%u}\n",
        csi_count, info->rx_ctrl.rssi, info->buf_len, info->buf[0]);
    send(client_sock, msg, n, 0);
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wc = { 0 };
    strcpy((char *)wc.sta.ssid, WIFI_SSID);
    strcpy((char *)wc.sta.password, WIFI_PASS);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    wifi_csi_config_t csi_cfg = {
        .lltf_en = true, .ht_ltf_en = true, .stbc_ht_ltf_en = true,
        .ltf_merge_en = true, .channel_filter_en = true, .manu_scale = false,
        .shift = 0
    };
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_config(&csi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(csi_rx_cb, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
}

static void eth_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = ETH_MOSI_GPIO, .miso_io_num = ETH_MISO_GPIO,
        .sclk_io_num = ETH_SCLK_GPIO, .quadwp_io_num = -1, .quadhd_io_num = -1
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    eth_w5500_config_t w5500 = ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &(spi_device_interface_config_t){
        .mode = 0, .clock_speed_hz = 20 * 1000 * 1000,
        .spics_io_num = ETH_CS_GPIO, .queue_size = 20
    });
    w5500.int_gpio_num = ETH_INT_GPIO;
    eth_mac_config_t mac = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy = ETH_PHY_DEFAULT_CONFIG();
    phy.reset_gpio_num = ETH_RST_GPIO;
    esp_eth_mac_t *mac_handle;
    esp_eth_phy_t *phy_handle;
    ESP_ERROR_CHECK(esp_eth_mac_new_w5500(&w5500, &mac, &mac_handle));
    ESP_ERROR_CHECK(esp_eth_phy_new_dp83848(&(eth_phy_config_t){.reset_gpio_num=ETH_RST_GPIO}, &phy_handle));
    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac_handle, phy_handle);
    esp_eth_handle_t eth_handle;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_cfg, &eth_handle));

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));

    /* Wait briefly for the interface, then apply the static address. */
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(eth_netif));
    esp_netif_ip_info_t ip = { 0 };
    ip.ip.addr = ipaddr_addr(W5500_IP);
    ip.gw.addr = ipaddr_addr(W5500_GW);
    ip.netmask.addr = ipaddr_addr(W5500_MASK);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(eth_netif, &ip));
}

static void tcp_server_task(void *arg)
{
    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(RADAR_TCP_PORT);

    listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ESP_ERROR_CHECK(bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0 ? ESP_FAIL : ESP_OK);
    ESP_ERROR_CHECK(listen(listen_sock, 1) < 0 ? ESP_FAIL : ESP_OK);
    ESP_LOGI(TAG, "Radar TCP server ready at %s:%d", W5500_IP, RADAR_TCP_PORT);

    while (1) {
        client_sock = accept(listen_sock, NULL, NULL);
        if (client_sock < 0) continue;
        ESP_LOGI(TAG, "PC connected");
        char buf[64];
        while (recv(client_sock, buf, sizeof(buf), 0) > 0) { }
        close(client_sock);
        client_sock = -1;
        ESP_LOGI(TAG, "PC disconnected");
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    eth_init();
    wifi_init();
    xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, NULL);
    while (1) {
        ESP_LOGI(TAG, "CSI packets: %u, client: %s", csi_count, client_sock >= 0 ? "connected" : "none");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
