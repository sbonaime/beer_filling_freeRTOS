#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NAU7802.h>
#include <Preferences.h>  // Pour sauvegarde NVS
#include <algorithm>      // pour std::sort

Adafruit_NAU7802 nau;
Preferences preferences;

// --- FreeRTOS ---
TaskHandle_t taskReadLoadCell;
TaskHandle_t taskPrintSerial;
TaskHandle_t taskSerialMenuHandle;
SemaphoreHandle_t xMutex;

// --- Variables partagées ---
volatile int32_t rawValue = 0;
volatile float offset = 0.0;
volatile float scaleFactor = 1.0;             // counts par gramme
volatile bool calibrationInProgress = false;  // stoppe la tâche de lecture
int SPS = 40;

// --- Fonction Tare robuste ---
void doRobustTare() {
  Serial.println("Tare in progress ...");

  calibrationInProgress = true;  // stoppe taskLoadCell

  const int N = 300;
  static int32_t buf[N];
  int count = 0;

  while (count < N) {
    if (nau.available()) {
      buf[count++] = nau.read();
    } else {
      Serial.printf("nau not available in tare");
    }
    vTaskDelay(pdMS_TO_TICKS(1000 / SPS));
  }

  std::sort(buf, buf + N);
  int start = N / 4, end = N * 3 / 4;
  double sum = 0.0;
  for (int i = start; i < end; i++) sum += buf[i];
  double mean_core = sum / double(end - start);

  if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
    offset = (float)mean_core;
    xSemaphoreGive(xMutex);
  }

  preferences.begin("scale", false);
  preferences.putFloat("offset", offset);
  preferences.end();

  Serial.printf("[OK] Nouvelle tare : offset = %.1f\n", offset);
  preferences.putFloat("offset", offset);

  calibrationInProgress = false;  // relance la tâche de lecture
}

// --- Fonction Calibration 2 points ---
void doTwoPointCalibration() {
  String input;
  calibrationInProgress = true;  // stoppe taskLoadCell
  Serial.println("Entrez le poids de la masse de calibration (g) :");
  input = "";
  while (input.length() == 0) {
    if (Serial.available()) {
      input = Serial.readStringUntil('\n');
      input.trim();
    }
    vTaskDelay(10);
  }

  float knownMass = input.toFloat();
  if (knownMass > 0) {
    Serial.printf("Poids de calibration: %.1f g\n", knownMass);
  } else {
    Serial.println("[Erreur] Masse invalide !");
  }

  const int N = 300;
  static int32_t buf[N];
  int count = 0;

  while (count < N) {
    if (nau.available()) {
      buf[count++] = nau.read();
    } else {
      Serial.printf("nau not available in tare");
    }
    vTaskDelay(pdMS_TO_TICKS(1000 / SPS));
  }

  std::sort(buf, buf + N);
  int start = N / 4, end = N * 3 / 4;
  double sum = 0.0;
  for (int i = start; i < end; i++) sum += buf[i];
  double rawWithMass = sum / double(end - start);

  float localOffset;
  if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
    localOffset = offset;
    xSemaphoreGive(xMutex);
  }

  double newScale = (rawWithMass - localOffset) / knownMass;

  Serial.printf("rawWithMass = %.3f localOffset = %.3f knownMass  = %.3f newScale = %.3f \n", rawWithMass, localOffset, knownMass, newScale);


  if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
    scaleFactor = (float)newScale;
    xSemaphoreGive(xMutex);
  }

  preferences.begin("scale", false);
  preferences.putFloat("scale", scaleFactor);
  preferences.end();

  Serial.printf("[OK] Calibration faite : facteur = %.3f counts/g\n", scaleFactor);
  preferences.putFloat("scale", scaleFactor);

  vTaskDelay(pdMS_TO_TICKS(4000));  // 5 Hz

  calibrationInProgress = false;  // relance la tâche de lecture
}

// --- Tâche lecture NAU7802 (40 SPS) ---
void taskLoadCell(void *pvParameters) {
  (void)pvParameters;

  for (;;) {
    if (!calibrationInProgress) {  // seulement si pas en calibration/tare
      if (nau.available()) {
        int32_t val = nau.read();
        if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
          rawValue = val;
          xSemaphoreGive(xMutex);
        }
      } else {
        Serial.printf("nau not available");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000 / SPS));  // ~40 SPS
  }
}

// --- Tâche affichage Serial Plotter (5 Hz) ---
void taskSerialPrint(void *pvParameters) {
  (void)pvParameters;

  for (;;) {
    int32_t localRaw;
    float localOffset, localScale;
    if (!calibrationInProgress) {  // seulement si pas en calibration/tare

      if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        localRaw = rawValue;
        localOffset = offset;
        localScale = scaleFactor;
        xSemaphoreGive(xMutex);
      }

      float mass = (localRaw - localOffset) / localScale;

      // Affiche poids, offset, scaleFactor séparés par tabulation (Serial Plotter multi-traces)
      Serial.print(mass);
      Serial.print("\t");
      Serial.print(localOffset);
      Serial.print("\t");
      Serial.println(localScale);
    }
    vTaskDelay(pdMS_TO_TICKS(200));  // 5 Hz
  }
}

// --- Tâche menu série ---
void taskSerialMenu(void *pvParameters) {
  (void)pvParameters;

  Serial.println("\n=== Menu Balance ===");
  Serial.println("t : Tare (remise à zéro)");
  Serial.println("c : Calibration (nécessite masse connue)");
  Serial.println("h : Aide (menu)");

  for (;;) {
    if (Serial.available()) {
      char cmd = Serial.read();

      if (cmd == 't') {
        doRobustTare();
      } else if (cmd == 'c') {
        doTwoPointCalibration();
      } else if (cmd == 'h') {
        Serial.println("\n=== Menu Balance ===");
        Serial.println("t : Tare (remise à zéro)");
        Serial.println("c : Calibration (nécessite masse connue)");
        Serial.println("h : Aide (menu)");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  if (!nau.begin()) {
    Serial.println("Erreur : NAU7802 non détecté !");
    while (1) vTaskDelay(1000);
  }

  nau.setLDO(NAU7802_3V0);
  nau.setGain(NAU7802_GAIN_128);
  nau.setRate(NAU7802_RATE_40SPS);

  nau.calibrate(NAU7802_CALMOD_OFFSET);
  nau.calibrate(NAU7802_CALMOD_GAIN);

  // Charger les valeurs sauvegardées
  preferences.begin("scale", true);
  offset = preferences.getFloat("offset", -507.313);
  scaleFactor = preferences.getFloat("scale", -8.655);
  preferences.end();

  Serial.printf("Offset chargé: %.1f, Scale: %.3f\n", offset, scaleFactor);

  xMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(taskLoadCell, "TaskLoadCell", 4096, NULL, 3, &taskReadLoadCell, 1);
  xTaskCreatePinnedToCore(taskSerialPrint, "TaskSerialPrint", 4096, NULL, 1, &taskPrintSerial, 1);
  xTaskCreatePinnedToCore(taskSerialMenu, "TaskSerialMenu", 4096, NULL, 2, &taskSerialMenuHandle, 1);
}

void loop() {
}
