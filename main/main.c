#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "hermes-nabu";
static const TickType_t HEARTBEAT_PERIOD = pdMS_TO_TICKS(1000);

void app_main(void)
{
    ESP_LOGI(TAG, "booted");

    for (unsigned heartbeat = 0;; heartbeat++) {
        ESP_LOGI(TAG, "heartbeat=%u", heartbeat);
        vTaskDelay(HEARTBEAT_PERIOD);
    }
}
