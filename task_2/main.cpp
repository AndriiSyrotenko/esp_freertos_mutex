#include <Arduino.h>
#include "esp_task_wdt.h"

#define WDT_TIMEOUT_S 5

struct SensorMsg {
  uint32_t id;
  float    value;
  uint32_t tick_ms;
};

static QueueHandle_t     queueSensor;
static SemaphoreHandle_t mutexA; 
static SemaphoreHandle_t mutexB;  
static SensorMsg         sharedBuf;


void producerTask(void *) {
  esp_task_wdt_add(NULL);
  uint32_t id = 0;
  for (;;) {
    SensorMsg m{++id, 20.0f + (id % 10) * 0.5f, millis()};
    if (xQueueSend(queueSensor, &m, pdMS_TO_TICKS(100)) == pdTRUE)
      Serial.printf("[Producer] sent #%u (%.1f)\n", m.id, m.value);
    else
      Serial.printf("[Producer] queue FULL, #%u lost\n", m.id);
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}


void consumerTask(void *) {
  esp_task_wdt_add(NULL);
  SensorMsg m;
  for (;;) {
    if (xQueueReceive(queueSensor, &m, pdMS_TO_TICKS(1000)) == pdTRUE) {
      Serial.println("[Consumer] taking A...");
      xSemaphoreTake(mutexA, portMAX_DELAY);
      Serial.println("[Consumer] have A, waiting for B...");
      vTaskDelay(pdMS_TO_TICKS(100));           // вікно, щоб Logger встиг взяти B
      xSemaphoreTake(mutexB, portMAX_DELAY);    // <-- тут зависає
      sharedBuf = m;
      Serial.printf("[Consumer] processed #%u\n", m.id);
      xSemaphoreGive(mutexB);
      xSemaphoreGive(mutexA);
    }
    esp_task_wdt_reset();
  }
}


void loggerTask(void *) {
  esp_task_wdt_add(NULL);
  for (;;) {
    Serial.println("[Logger]   taking B...");
    xSemaphoreTake(mutexB, portMAX_DELAY);
    Serial.println("[Logger]   have B, waiting for A...");
    vTaskDelay(pdMS_TO_TICKS(100));
    xSemaphoreTake(mutexA, portMAX_DELAY);      // <-- тут зависає
    Serial.printf("[Logger]   last=#%u\n", sharedBuf.id);
    xSemaphoreGive(mutexA);
    xSemaphoreGive(mutexB);
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(700));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== Deadlock demo ===");

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t cfg = {
    .timeout_ms     = WDT_TIMEOUT_S * 1000,
    .idle_core_mask = (1 << 0),
    .trigger_panic  = true
  };
  esp_task_wdt_reconfigure(&cfg);
#else
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
#endif

  queueSensor = xQueueCreate(5, sizeof(SensorMsg));
  mutexA = xSemaphoreCreateMutex();
  mutexB = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(consumerTask, "Consumer", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(loggerTask,   "Logger",   4096, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(producerTask, "Producer", 4096, NULL, 2, NULL, 0);
}

void loop() { vTaskDelay(portMAX_DELAY); }