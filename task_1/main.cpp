
#include <Arduino.h>

// =====================================================
// PINS
// =====================================================

constexpr int BUTTON_PIN = 26;
constexpr int LED_PIN = 25;

// =====================================================
// BUTTON SETTINGS
// =====================================================

constexpr uint32_t DEBOUNCE_MS = 30;
constexpr uint32_t DOUBLE_CLICK_MS = 400;

// LED blink intervals, in milliseconds
constexpr uint32_t BLINK_INTERVALS[] = {
    250, 500, 1000, 2000
};

constexpr int INTERVAL_COUNT = 4;

// =====================================================
// FREERTOS
// =====================================================

// Queue carries one uint32_t interval value per message.
QueueHandle_t blinkQueue = nullptr;

// =====================================================
// BUTTON TASK
// Runs on Core 0.
// Detects single and double clicks.
// Sends the selected interval to the queue.
// =====================================================

void buttonTask(void *parameter) {
    Serial.println("[ButtonTask] Running on Core 0");

    int rawState = digitalRead(BUTTON_PIN);
    int stableState = rawState;

    uint32_t lastDebounceTime = millis();
    uint32_t firstReleaseTime = 0;

    bool clickPending = false;
    int intervalIndex = 0;  // Start at 250 ms

    while (true) {
        int reading = digitalRead(BUTTON_PIN);
        uint32_t now = millis();

        // Debounce: detect a possible state change
        if (reading != rawState) {
            rawState = reading;
            lastDebounceTime = now;
        }

        // Accept the change after the debounce interval
        if ((now - lastDebounceTime >= DEBOUNCE_MS) &&
            (rawState != stableState)) {

            stableState = rawState;

            // A click is registered when the button is released
            if (stableState == HIGH) {
                now = millis();

                if (clickPending &&
                    (now - firstReleaseTime <= DOUBLE_CLICK_MS)) {

                    // Double click: move one step backward
                    intervalIndex--;

                    if (intervalIndex < 0) {
                        intervalIndex = INTERVAL_COUNT - 1;
                    }

                    uint32_t interval =
                        BLINK_INTERVALS[intervalIndex];

                    if (xQueueSend(blinkQueue, &interval, 0) == pdPASS) {
                        Serial.print("[ButtonTask] DOUBLE click, interval = ");
                        Serial.print(interval);
                        Serial.println(" ms");
                    } else {
                        Serial.println("[ButtonTask] ERROR: Queue full");
                    }

                    clickPending = false;
                } else {
                    // If a previous click expired, report it as single
                    if (clickPending) {
                        intervalIndex =
                            (intervalIndex + 1) % INTERVAL_COUNT;

                        uint32_t interval =
                            BLINK_INTERVALS[intervalIndex];

                        if (xQueueSend(blinkQueue, &interval, 0) == pdPASS) {
                            Serial.print("[ButtonTask] SINGLE click, interval = ");
                            Serial.print(interval);
                            Serial.println(" ms");
                        } else {
                            Serial.println("[ButtonTask] ERROR: Queue full");
                        }
                    }

                    // Wait for a possible second click
                    clickPending = true;
                    firstReleaseTime = now;
                }
            }
        }

        // If no second click arrives, register a single click
        now = millis();

        if (clickPending &&
            (now - firstReleaseTime > DOUBLE_CLICK_MS)) {

            intervalIndex = (intervalIndex + 1) % INTERVAL_COUNT;

            uint32_t interval = BLINK_INTERVALS[intervalIndex];

            if (xQueueSend(blinkQueue, &interval, 0) == pdPASS) {
                Serial.print("[ButtonTask] SINGLE click, interval = ");
                Serial.print(interval);
                Serial.println(" ms");
            } else {
                Serial.println("[ButtonTask] ERROR: Queue full");
            }

            clickPending = false;
        }

        // Yield processor time
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// =====================================================
// LED TASK
// Runs on Core 1.
// Receives blink intervals from the queue.
// =====================================================

void ledTask(void *parameter) {
    Serial.println("[LEDTask] Running on Core 1");

    uint32_t blinkInterval = BLINK_INTERVALS[0];
    uint32_t nextInterval;

    uint32_t lastToggleTime = millis();
    bool ledState = false;

    digitalWrite(LED_PIN, LOW);

    while (true) {
        // Read a new interval without blocking.
        // If several updates accumulated, use the newest one.
        while (xQueueReceive(blinkQueue, &nextInterval, 0) == pdPASS) {
            blinkInterval = nextInterval;

            Serial.print("[LEDTask] New interval = ");
            Serial.print(blinkInterval);
            Serial.println(" ms");
        }

        uint32_t now = millis();

        if (now - lastToggleTime >= blinkInterval) {
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState ? HIGH : LOW);
            lastToggleTime = now;
        }

        // Small delay prevents busy waiting
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// =====================================================
// SETUP
// =====================================================

void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    Serial.println();
    Serial.println("=== FreeRTOS Dual-Core LED Lab ===");

    // Create queue before creating the tasks
    blinkQueue = xQueueCreate(5, sizeof(uint32_t));

    if (blinkQueue == nullptr) {
        Serial.println("[ERROR] Failed to create Queue");

        while (true) {
            delay(1000);
        }
    }

    // Initial interval: 250 ms
    uint32_t initialInterval = BLINK_INTERVALS[0];
    xQueueSend(blinkQueue, &initialInterval, 0);

    // Button task pinned to Core 0
    BaseType_t buttonResult = xTaskCreatePinnedToCore(
        buttonTask,
        "ButtonTask",
        4096,
        nullptr,
        2,
        nullptr,
        0
    );

    // LED task pinned to Core 1
    BaseType_t ledResult = xTaskCreatePinnedToCore(
        ledTask,
        "LEDTask",
        4096,
        nullptr,
        1,
        nullptr,
        1
    );

    if (buttonResult != pdPASS || ledResult != pdPASS) {
        Serial.println("[ERROR] Failed to create tasks");

        while (true) {
            delay(1000);
        }
    }

    Serial.println("[Setup] Queue created");
    Serial.println("[Setup] ButtonTask pinned to Core 0");
    Serial.println("[Setup] LEDTask pinned to Core 1");
    Serial.println("[Setup] Initial interval = 250 ms");
    Serial.println("[Setup] Single click: forward");
    Serial.println("[Setup] Double click: backward");
}

void loop() {
    // The two FreeRTOS tasks perform the actual work.
    // Arduino's loop task yields processor time here.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
