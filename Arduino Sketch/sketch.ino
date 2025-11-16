#include <NimBLEDevice.h>
#include <FastLED.h>

// Device configuration
#define DEVICE_NAME "Soil Moisture Sensor"
#define BLINK_LED true
#define NUM_LEDS 1

#define LED_PIN 8        // GPIO8 - LED/Logging pin
#define MOIST_ADC_PIN 2  // GPIO2 - ADC1_CH1
#define BAT_ADC_PIN 1    // GPIO1 - ADC1_CH0

#define MOIST_PWR_PIN 22  //GPIO22

// Caliberation values
#define MOISTURE_SENSOR_READING_IN_AIR 3500
#define MOISTURE_SENSOR_READING_IN_WATER 1200
#define VOLTAGE_DIVIDER_RESISTOR_1_KO 100  // Connecte dto Vcc
#define VOLTAGE_DIVIDER_RESISTOR_2_KO 100  //Connected to GND

// BTHome V2 Object IDs.  https://bthome.io/format/
#define BTHOME_BATTERY 0x01
#define BTHOME_VOLTAGE 0x0C
#define BTHOME_MOISTURE 0x14


CRGB leds[NUM_LEDS];
BLEAdvertising* pAdvertising;

void setup() {
  pinMode(MOIST_PWR_PIN, OUTPUT);
  digitalWrite(MOIST_PWR_PIN, LOW);

  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
  analogSetPinAttenuation(MOIST_ADC_PIN, ADC_11db);

  if (BLINK_LED) {
    FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, NUM_LEDS);
    FastLED.setBrightness(8);
  }

  // Blink LED post restart
  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_EXT || reason == ESP_RST_POWERON || reason == ESP_RST_DEEPSLEEP) {
    if (BLINK_LED) {
      blinkLED();
    }
  }

  // Initialize BLE
  BLEDevice::init(DEVICE_NAME);

  // Read your sensor values here
  float moisture = readMoistureLevel();
  float voltage = readBatteryVoltage();
  uint8_t battery = getBatteryRemainingPercentage(voltage);

  // Advertise BTHome data
  advertiseBTHome(moisture, battery, voltage);

  // Enter deep sleep for 5 Minutes
  const uint64_t SLEEP_TIME = 5ULL * 60ULL * 1000000ULL;
  esp_sleep_enable_timer_wakeup(SLEEP_TIME);
  esp_deep_sleep_start();
}

void loop() {}

void blinkLED() {
  leds[0] = CRGB::White;
  FastLED.show();
  delay(1000);
  leds[0] = CRGB::Black;
  FastLED.show();
}

float readMoistureLevel() {
  digitalWrite(MOIST_PWR_PIN, HIGH);
  delay(500);
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(MOIST_ADC_PIN);
    delay(50);
  }
  digitalWrite(MOIST_PWR_PIN, LOW);

  int raw = sum / 10;

  if (raw < 500 || raw > 4000) {
    return 0;
  }
  int percent = map(raw, MOISTURE_SENSOR_READING_IN_AIR, MOISTURE_SENSOR_READING_IN_WATER, 0, 100);
  if (percent < 0) {
    percent = 0;
  }
  if (percent > 100) {
    percent = 100;
  }
  return percent;
}

void advertiseBTHome(float moisture, uint8_t battery, float voltage) {
  // Convert values to BTHome format
  uint16_t moist = (uint16_t)(moisture * 100);
  uint16_t volt = (uint16_t)(voltage * 1000);

  pAdvertising = BLEDevice::getAdvertising();

  BLEAdvertisementData advertisementData;

  advertisementData.setFlags(0x06);  // BLE General Discoverable + BR/EDR Not Supported
  advertisementData.setName(DEVICE_NAME);

  std::string serviceData;
  serviceData += (char)0x40;  // BTHome Device Info: V2, unencrypted, not triggered

  // Battery (Object ID 0x01) - uint8
  serviceData += (char)BTHOME_BATTERY;
  serviceData += (char)battery;

  // Voltage (Object ID 0x0C) - uint16 little-endian, factor 0.001
  serviceData += (char)BTHOME_VOLTAGE;
  serviceData += (char)(volt & 0xFF);
  serviceData += (char)((volt >> 8) & 0xFF);

  // Moisture (Object ID 0x14) - uint16 little-endian, factor 0.01
  serviceData += (char)BTHOME_MOISTURE;
  serviceData += (char)(moist & 0xFF);
  serviceData += (char)((moist >> 8) & 0xFF);

  // Set service data
  advertisementData.setCompleteServices(BLEUUID((uint16_t)0xFCD2));
  advertisementData.setServiceData(BLEUUID((uint16_t)0xFCD2), serviceData);

  pAdvertising->setAdvertisementData(advertisementData);

  // Send multiple packets before sleep for reliability
  for (int i = 0; i < 3; i++) {
    pAdvertising->start();
    delay(1000);
    pAdvertising->stop();
    delay(200);
  }
  BLEDevice::deinit(false);
}

float readBatteryVoltage() {
  long sum = 0;

  for (int i = 0; i < 10; i++) {
    sum += analogRead(BAT_ADC_PIN);
    delay(10);
  }

  int rawValue = sum / 10;
  float adcVoltage = (rawValue / 4095.0) * 3.3;

  // Calculate battery voltage using voltage divider formula: Vbat = Vadc * (R1+R2)/R2
  // For 100kΩ + 100kΩ: multiplier = 2.0
  float multiplier = (float)(VOLTAGE_DIVIDER_RESISTOR_1_KO + VOLTAGE_DIVIDER_RESISTOR_2_KO) / VOLTAGE_DIVIDER_RESISTOR_2_KO;
  float batteryVoltage = adcVoltage * multiplier;

  if (batteryVoltage > 4.3) {
    batteryVoltage = 4.2;
  }
  if (batteryVoltage < 2.8) {
    batteryVoltage = 3.0;
  }
  return batteryVoltage;
}



uint8_t getBatteryRemainingPercentage(float voltage) {
  const float MIN_VOLTAGE = 3.0;  // 0%
  const float MAX_VOLTAGE = 4.2;  // 100%

  int percent = (int)((voltage - MIN_VOLTAGE) / (MAX_VOLTAGE - MIN_VOLTAGE) * 100);

  return constrain(percent, 0, 100);
}
