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
#define RADAR_PC_IP "192.168.1.50"
#define RADAR_UDP_PORT 9999

#define ETH_MOSI_GPIO 23
#define ETH_MISO_GPIO 19
#define ETH_SCLK_GPIO 18
#define ETH_CS_GPIO 5
#define ETH_INT_GPIO 4
#define ETH_RST_GPIO 33

static const char *TAG = "csi_radar";
static int udp_sock = -1;
static struct sockaddr_in pc_addr;
static volatile uint32_t csi_count = 0;

static void csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    csi_count++;
    if (udp_sock < 0 || !info->buf) return;
    char msg[256];
    int n = snprintf(msg, sizeof(msg),
        "{\"seq\":%u,\"rssi\":%d,\"len\":%u,\"first_word\":%u}\n",
        csi_count, info->rx_ctrl.rssi, info->buf_len, info->buf ? info->buf[0] : 0);
    sendto(udp_sock, msg, n, 0, (struct sockaddr *)&pc_addr, sizeof(pc_addr));
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
}

static void udp_init(void)
{
    udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    memset(&pc_addr, 0, sizeof(pc_addr));
    pc_addr.sin_family = AF_INET;
    pc_addr.sin_port = htons(RADAR_UDP_PORT);
    inet_aton(RADAR_PC_IP, &pc_addr.sin_addr);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    udp_init();
    eth_init();
    wifi_init();
    while (1) {
        ESP_LOGI(TAG, "CSI packets: %u", csi_count);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
