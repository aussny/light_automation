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
#include "driver/ledc.h"

static const char *TAG = "example";

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 48
#endif

#define LED_GPIO 2
#define MAX_DUTY_CYCLE 0x3FF
static ledc_channel_config_t ledc_channel;
#define SENSOR_GPIO 17

void monitor_motion(void);
void obtain_time(void);
int8_t check_hour(void);
int8_t setup_procedure(void);
void blink_LED(int8_t numCycles);
void configure_GPIOS(void);
void configure_LED(void);
void time_sync_notification_cb(struct timeval *tv);
void fadeUpLed(void);
void fadeDownLed(void);

bool ledON = false;

void app_main(void)
{
    configure_LED();
    blink_LED(5);
    vTaskDelay( 1000 / portTICK_PERIOD_MS);
    while(true)
    {
        fadeUpLed();
        blink_LED(2);
        vTaskDelay( 1000 / portTICK_PERIOD_MS);
        fadeDownLed();
        blink_LED(2);
        vTaskDelay( 1000 / portTICK_PERIOD_MS);
    }

    // int8_t currentHour = setup_procedure();
    
    // while(true)
    // {
    //     currentHour = check_hour();
    //     if ((currentHour >= 8) && (currentHour <= 19))
    //     {
    //         monitor_motion();
    //     }
    //     else
    //     {
    //         if (ledON)
    //         {
    //            fadeDownLed(); 
    //         }
    //     }
    //     vTaskDelay(60000 / portTICK_PERIOD_MS);
    // }
}

int8_t setup_procedure(void)
{
    int8_t currentHour;
    configure_GPIOS();
    configure_LED();
    gpio_set_level(LED_GPIO, 1);
    vTaskDelay( 5000 / portTICK_PERIOD_MS);

    currentHour = check_hour();
    gpio_set_level(LED_GPIO, 0);
    blink_LED(currentHour);
    return currentHour;
}

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

void configure_GPIOS(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_reset_pin(SENSOR_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(SENSOR_GPIO, GPIO_MODE_INPUT);
}

void configure_LED(void)
{
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 1000,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    ledc_timer_config(&ledc_timer);
    ledc_channel.channel = LEDC_CHANNEL_0;
    ledc_channel.duty = 0;
    ledc_channel.gpio_num = LED_GPIO;
    ledc_channel.speed_mode = LEDC_HIGH_SPEED_MODE;
    ledc_channel.hpoint = 0;
    ledc_channel.timer_sel = LEDC_TIMER_0;
    ledc_channel_config(&ledc_channel);
}

void blink_LED(int8_t numCycles)
{
    /* This function toggles the LED in the amount specified in the input
        INPUTS: int8_t numCycles - the number of LED cycles to toggle
        RETURNS: none    
    */


   /* OLD */
    uint8_t ledState = 1;
    for( ; numCycles > 0; numCycles--)
    {   
        // gpio_set_level(LED_GPIO, ledState);
        ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0);
        ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);

        vTaskDelay(500  / portTICK_PERIOD_MS);
        ledState = !ledState;

        // gpio_set_level(LED_GPIO, ledState);
        ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0xFF);
        ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);

        vTaskDelay(500  / portTICK_PERIOD_MS);
        ledState = !ledState;
    }
    // gpio_set_level(LED_GPIO, 0);
    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);

}

void print_servers(void)
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

int8_t check_hour(void)
{
    /* This function finds and returns the hour of the day as int8 */
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
    setenv("TZ", "CST6EDT,M3.2.0/2,M11.1.0", 1); // CST timezone config
    tzset();
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in Dallas is: %s", strftime_buf);
    ESP_LOGI(TAG, "The hours is: %d", timeinfo.tm_hour);
    return timeinfo.tm_hour;
}

void obtain_time(void)
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

void monitor_motion(void)
{
    /* This funciton monitors motion from an IR sensor and handles the LED control */
    if (gpio_get_level(SENSOR_GPIO) == 1)
    {
        if (ledON == false)
        {
            fadeUpLed();
        }
        vTaskDelay( 60000 / portTICK_PERIOD_MS);
    }
    else
    {
        gpio_set_level(LED_GPIO, 0);
        if (ledON == true)
        {
            ESP_LOGI(TAG, "MOTION NO LONGER DETECTED!");
            ledON = false;
        }
        vTaskDelay( 100 / portTICK_PERIOD_MS);
    }
}

void fadeUpLed(void)
{
    /* TODO: This function fades up the LED */
    // gpio_set_level(LED_GPIO, 1);
    // uint32_t maxDutyCycle = 0x3FF;
    uint32_t ledDutyCycle;
    for (ledDutyCycle = 0; ledDutyCycle <= MAX_DUTY_CYCLE; ledDutyCycle++)
    {
        ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, ledDutyCycle);
        ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
        vTaskDelay( 10 / portTICK_PERIOD_MS);
    }

    // for(int8_t cycles = 3; cycles >= 0; cycles--)
    // {
    //     ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0);
    //     ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
    //     vTaskDelay( 1000 / portTICK_PERIOD_MS);
    //     ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, maxDutyCycle);
    //     ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
    //     vTaskDelay( 1000 / portTICK_PERIOD_MS);
    // }

    ledON = true;
}

void fadeDownLed(void)
{
    /* TODO: This function fades down the LED */
    // gpio_set_level(LED_GPIO, 0);
    uint32_t ledDutyCycle;
    for (ledDutyCycle = MAX_DUTY_CYCLE; ledDutyCycle > 0; ledDutyCycle--)
    {
        ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, ledDutyCycle);
        ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
        vTaskDelay( 10 / portTICK_PERIOD_MS);
    }
    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
    ledON = false;
}
