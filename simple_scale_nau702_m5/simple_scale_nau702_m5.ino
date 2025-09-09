#include <Wire.h>
#include <Adafruit_NAU7802.h>


Adafruit_NAU7802 nau = Adafruit_NAU7802();
int SCALE_SPS = 80;

// ---------- FreeRTOS ----------
TaskHandle_t taskNAU7802Handle = nullptr;

void taskNAU7802(void*) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(1 + (1000 / SCALE_SPS));

  for (;;) {
    if (nau.available()) {
      float load_cell_value = nau.read();
      printf("Load cell value is : %.1fg\n", load_cell_value);

    } else {
      Serial.println("NAU7802 not ready in taskNAU7802");
      vTaskDelay(5 / portTICK_PERIOD_MS);
    }
    vTaskDelayUntil(&xLastWakeTime, xFrequency);  // Exécution régulière
  }
}

void setup() {

  Serial.begin(115200);
  delay(200);

  const uint8_t pin_SDA = 16;
  const uint8_t pin_SCL = 17;

  Wire.begin(pin_SDA, pin_SCL, 400000);  // SDA, SCL
  delay(200);

  Serial.println("Scan I2C...");

  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.print("Found I2C component at 0x");
      Serial.println(address, HEX);
    }
  }


  if (!nau.begin()) {
    Serial.println("Erreur : NAU7802 non détecté !");
    while (1) vTaskDelay(1000);
  }

  // Config LDO, gain et rate
  nau.setLDO(NAU7802_3V3);
  nau.setGain(NAU7802_GAIN_128);

  Serial.print("Conversion rate set to ");
  switch (SCALE_SPS) {
    case 10:
      nau.setRate(NAU7802_RATE_10SPS);
      Serial.println("10 SPS");
      break;
    case 20:
      nau.setRate(NAU7802_RATE_20SPS);
      Serial.println("20 SPS");
      break;
    case 40:
      nau.setRate(NAU7802_RATE_40SPS);
      Serial.println("40 SPS");
      break;
    case 80:
      nau.setRate(NAU7802_RATE_80SPS);
      Serial.println("80 SPS");
      break;
    case 320:
      nau.setRate(NAU7802_RATE_320SPS);
      Serial.println("320 SPS");
      break;
  }

  // Take SCALE_SPS readings to flush out readings
  for (uint8_t i = 0; i < SCALE_SPS; i++) {
    while (!nau.available()) delay(1);
    nau.read();
  }

  while (!nau.calibrate(NAU7802_CALMOD_INTERNAL)) {
    Serial.println("Failed to calibrate internal offset, retrying!");
    delay(1000);
  }
  Serial.println("Calibrated internal offset");

  while (!nau.calibrate(NAU7802_CALMOD_OFFSET)) {
    Serial.println("Failed to calibrate system offset, retrying!");
    delay(1000);
  }
  Serial.println("Calibrated system offset");

  xTaskCreatePinnedToCore(taskNAU7802, "NAU7802_Task", 8192, nullptr, 9, &taskNAU7802Handle, 1);
}

void loop() {
}
