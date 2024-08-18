/* LwIP SNTP example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_netif_sntp.h"
#include "lwip/ip_addr.h"
#include "esp_sntp.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "rtc_wdt.h"

static const char *TAG = "example";

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 48
#endif

#define BLINK_GPIO 2
#define SENSOR_GPIO 17

/* Variable holding number of times ESP32 restarted since first boot.
 * It is placed into RTC memory using RTC_DATA_ATTR and
 * maintains its value when ESP32 wakes from deep sleep.
 */
// RTC_DATA_ATTR static int boot_count = 0;

static void obtain_time(void);
static int8_t setup_procedure(void);
void blink_LED(int8_t numCycles);
void configure_GPIOS(void);
void time_sync_notification_cb(struct timeval *tv);

// void watchdogHandler(void* pData)
// {
//     rtc_wdt_feed();
//     vTaskDelay(100 / portTICK_PERIOD_MS);
//     vTaskDelete(NULL);
// }

void app_main(void)
{
    int8_t currentTime = setup_procedure();
    bool motionDetected = false;

    while(1)
    {
        if (gpio_get_level(SENSOR_GPIO) == 1)
        {
            gpio_set_level(BLINK_GPIO, 1);
            if (motionDetected == false)
            {
                ESP_LOGI(TAG, "MOTION DETECTED!");
                motionDetected = true;
                
            }
            vTaskDelay( 5000 / portTICK_PERIOD_MS);
        }
        else
        {
            gpio_set_level(BLINK_GPIO, 0);
            if (motionDetected == true)
            {
                ESP_LOGI(TAG, "MOTION NO LONGER DETECTED!");
                motionDetected = false;
            }
            vTaskDelay( 100 / portTICK_PERIOD_MS);
        }
    }
}

static int8_t setup_procedure(void)
{
    configure_GPIOS();
    gpio_set_level(BLINK_GPIO, 1);

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    // Is time set? If not, tm_year will be (1970 - 1900).
    if (timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGI(TAG, "Time is not set yet. Connecting to WiFi and getting time over NTP.");
        obtain_time();
        // update 'now' variable with current time
        time(&now);
    }

    char strftime_buf[64];
    setenv("TZ", "CST6EDT,M3.2.0/2,M11.1.0", 1); // Dallas
    tzset();
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in Dallas is: %s", strftime_buf);
    ESP_LOGI(TAG, "The hours is: %d", timeinfo.tm_hour);
    gpio_set_level(BLINK_GPIO, 0);
    blink_LED(timeinfo.tm_hour);
    return timeinfo.tm_hour;
}

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

void configure_GPIOS(void)
{
    gpio_reset_pin(BLINK_GPIO);
    gpio_reset_pin(SENSOR_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(SENSOR_GPIO, GPIO_MODE_INPUT);
}

void blink_LED(int8_t numCycles)
{
    uint8_t ledState = 1;
    for( ; numCycles > 0; numCycles--)
    {   gpio_set_level(BLINK_GPIO, ledState);
        vTaskDelay(500  / portTICK_PERIOD_MS);
        ledState = !ledState;
        gpio_set_level(BLINK_GPIO, ledState);
        vTaskDelay(500  / portTICK_PERIOD_MS);
        ledState = !ledState;
    }
    gpio_set_level(BLINK_GPIO, 0);

}

static void print_servers(void)
{
    ESP_LOGI(TAG, "List of configured NTP servers:");

    for (uint8_t i = 0; i < SNTP_MAX_SERVERS; ++i){
        if (esp_sntp_getservername(i)){
            ESP_LOGI(TAG, "server %d: %s", i, esp_sntp_getservername(i));
        } else {
            // we have either IPv4 or IPv6 address, let's print it
            char buff[INET6_ADDRSTRLEN];
            ip_addr_t const *ip = esp_sntp_getserver(i);
            if (ipaddr_ntoa_r(ip, buff, INET6_ADDRSTRLEN) != NULL)
                ESP_LOGI(TAG, "server %d: %s", i, buff);
        }
    }
}

static void obtain_time(void)
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK( esp_event_loop_create_default() );

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
    ESP_LOGI(TAG, "Initializing and starting SNTP");

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);

    config.sync_cb = time_sync_notification_cb;     // Note: This is only needed if we want

    esp_netif_sntp_init(&config);

    print_servers();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 15;
    while (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    }
    time(&now);
    localtime_r(&now, &timeinfo);

    ESP_ERROR_CHECK( example_disconnect() );
    esp_netif_sntp_deinit();
}
