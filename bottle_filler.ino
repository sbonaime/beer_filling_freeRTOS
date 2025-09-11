#include "bottle_filler.h"
#include <M5Unified.h>
#include <M5GFX.h>
#include <Wire.h>
#include <Preferences.h>
#include <FIFObuf.h>
#include <SimpleKalmanFilter.h>
#include <Adafruit_NAU7802.h>



// Board
// https://docs.m5stack.com/en/arduino/arduino_board

// Librairies
// M5Unified 0.2.8
// M5GFX 0.2.11
// FIFObuf https://github.com/pervu/FIFObuf
// SimpleKalmanFilter https://github.com/denyssene/SimpleKalmanFilter

Adafruit_NAU7802 nau = Adafruit_NAU7802();

Preferences preferences;

// Kalman filter
/*
  SimpleKalmanFilter(e_mea, e_est, q);
  e_mea: Measurement Uncertainty
  e_est: Estimation Uncertainty
  q: Process Noise
*/
SimpleKalmanFilter kaman_filter = SimpleKalmanFilter(.1, .1, 0.1);


// Mutex for the weight
SemaphoreHandle_t xMutex = NULL;
bool debug_print = false;
// bool debug_print = true;


// FIFO object for double
FIFObuf<double> fifo_scale(MAX_FIFO_SIZE);

// ---------- Sprite (frame buffer en RAM, 8 bits pour CoreS3) ----------
M5Canvas image_in_memory(&M5.Display);
M5Canvas beer_in_memory(&M5.Display);
M5Canvas weight_in_memory(&M5.Display);
M5Canvas value_canvas(&M5.Display);

// ---------- Menu ----------
const char* menuItems[] = { "Filler", "Pump", "Gravity", "Tare", "Calibration" };
const int menuSize = sizeof(menuItems) / sizeof(menuItems[0]);
volatile int currentSelection = 0;

// ---------- Etat global ----------
enum AppState {
  STATE_MAIN_MENU,
  STATE_FILLER,
  STATE_PUMP,
  STATE_GRAVITY,
  STATE_TARE,
  STATE_CALIBRATION
};
volatile AppState appState = STATE_MAIN_MENU;

// ---------- Filler Status ----------
enum FillerState {
  STATE_START,
  STATE_WAITING_BOTTLE,
  STATE_FILLING_BOTTLE,
  STATE_FILLED_BOTTLE,
};
volatile FillerState fillerState = STATE_START;


// ---------- FreeRTOS ----------
TaskHandle_t taskButtonsHandle = nullptr;
TaskHandle_t taskMenuHandle = nullptr;
TaskHandle_t taskDisplayHandle = nullptr;
TaskHandle_t taskFillerHandle = nullptr;
TaskHandle_t taskNAU7802Handle = nullptr;

QueueHandle_t buttonQueue;  // events boutons -> menu

enum ButtonEvent { BTN_A,
                   BTN_B,
                   BTN_C,
                   BTN_A_LONG_1s,
                   BTN_B_LONG_1s,
                   BTN_C_LONG_1s };

// ---------- Rendu ----------
static inline void drawMenu() {
  if (debug_print) Serial.println("debut drawMenu ");

  M5.Display.setTextDatum(middle_left);
  M5.Display.setFont(&fonts::FreeMonoBold18pt7b);
  M5.Display.setTextSize(1);


  int menu_item_height = 37;
  for (int i = 0; i < menuSize; i++) {
    M5.Display.setTextColor((i == currentSelection) ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
    M5.Display.drawString(menuItems[i], 10, 20 + menu_item_height + i * menu_item_height);
  }

  bottle_scaling = 1;
  beer_in_memory.createSprite(1 + 24 * bottle_scaling, 1 + bottle_scaling * (72));
  // draw_beer_bottle(280, 20, 33, 33, TFT_BEER, 0x50, false);
  draw_beer_bottle(280, 50, 33, 33, TFT_BEER, 0xFFFFFF, false);
  drawWeight();
}


double sum_N_readings() {
  const int N = 300;
  static int32_t buf[N];
  int count = 0;

  while (count < N) {
    if (nau.available()) {
      buf[count++] = nau.read();
    } else {
      Serial.println("nau not available");
    }
    vTaskDelay(pdMS_TO_TICKS(2 + (1000 / SCALE_SPS)));
  }

  std::sort(buf, buf + N);
  int start = N / 4, end = N * 3 / 4;
  double sum = 0.0;
  for (int i = start; i < end; i++) sum += buf[i];
  return sum / double(end - start);
}
void do_tare_scale() {
  calibrationInProgress = true;
  Serial.println("Tare Start");
  double mean_core = sum_N_readings();

  offset = (float)mean_core;
  // Save values in EEPROM
  preferences.putFloat("offset", offset);

  Serial.println("Tare done");
  calibrationInProgress = false;
  M5.Display.fillScreen(TFT_BLACK);
  drawMenu();
}

void do_scale_factor() {
  Serial.println("Debut Calibration");

  calibrationInProgress = true;

  double rawWithMass = sum_N_readings();
  double newScale = (rawWithMass - offset) / calib_weight;

  Serial.println("calib_weight : ");
  Serial.println(calib_weight);

  Serial.println("New scale : ");
  Serial.println(newScale);

  scale = newScale;
  // Save values in EEPROM
  preferences.putFloat("scale", scale);
  preferences.putInt("calib_weight", calib_weight);

  Serial.println("Nouvelle calib_weight => sauvegarde");
  saved_calib_weight = calib_weight;
  Serial.println("Scale factor done");
  calibrationInProgress = false;
}

void taskNAU7802(void*) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(2 + (1000 / SCALE_SPS));  // 1 ms of delay

  for (;;) {
    if (!calibrationInProgress) {
      if (nau.available()) {
        int32_t local_raw_data = nau.read();
        float local_weight = (local_raw_data - offset) / scale;

        if (xSemaphoreTake(xMutex, (TickType_t)10) == pdTRUE) {
          // Kalman filter
          raw_data = local_raw_data;
          weight = local_weight;
          kf_weight = kaman_filter.updateEstimate(local_weight);


          // Fifo and moving average
          moving_average_sum -= fifo_scale.pop();
          fifo_scale.push(kf_weight);
          moving_average_sum += kf_weight;
          last_moving_average = moving_average;
          moving_average = moving_average_sum / MAX_FIFO_SIZE;
          xSemaphoreGive(xMutex);
        } else {
          Serial.println("Timeout mutex NAU7802");
        }

      } else {
        Serial.println("NAU7802 non prêt in taskNAU7802");
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }

    // 40 SPS
    // vTaskDelay(pdMS_TO_TICKS(25));
    // 10 SPS
    vTaskDelayUntil(&xLastWakeTime, xFrequency);  // Exécution régulière
  }
}

void taskButtons(void*) {
  // const int verylongPressDelay = 2000;  // ms avant auto-repeat
  const int longPressDelay = 1000;  // ms avant auto-repeat
  const int repeatInterval = 200;   // ms entre chaque incrément auto-repeat
  static uint32_t lastRepeatA = 0;
  static uint32_t lastRepeatC = 0;
  static bool aLongActive = false;
  static bool cLongActive = false;

  for (;;) {
    M5.update();
    uint32_t now = millis();

    // BTN_A
    if (M5.BtnA.wasPressed()) {
      ButtonEvent e = BTN_A;
      xQueueSend(buttonQueue, &e, 0);
      aLongActive = false;
      lastRepeatA = now;
    }
    if (M5.BtnA.pressedFor(longPressDelay)) {
      if (!aLongActive) {
        ButtonEvent e = BTN_A_LONG_1s;
        xQueueSend(buttonQueue, &e, 0);
        aLongActive = true;
        lastRepeatA = now;
      } else if (now - lastRepeatA > repeatInterval) {
        ButtonEvent e = BTN_A_LONG_1s;
        xQueueSend(buttonQueue, &e, 0);
        lastRepeatA = now;
      }
    } else {
      aLongActive = false;
    }

    // BTN_C
    if (M5.BtnC.wasPressed()) {
      ButtonEvent e = BTN_C;
      xQueueSend(buttonQueue, &e, 0);
      cLongActive = false;
      lastRepeatC = now;
    }
    if (M5.BtnC.pressedFor(longPressDelay)) {
      if (!cLongActive) {
        ButtonEvent e = BTN_C_LONG_1s;
        xQueueSend(buttonQueue, &e, 0);
        cLongActive = true;
        lastRepeatC = now;
      } else if (now - lastRepeatC > repeatInterval) {
        ButtonEvent e = BTN_C_LONG_1s;
        xQueueSend(buttonQueue, &e, 0);
        lastRepeatC = now;
      }
    } else {
      cLongActive = false;
    }

    // BTN_B (idem si besoin)
    if (M5.BtnB.wasPressed()) {
      ButtonEvent e = BTN_B;
      xQueueSend(buttonQueue, &e, 0);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}


void taskFiller(void*) {

  for (;;) {
    bool bottle_filled = false;
    float local_currentWeight = 0;
    float local_moving_average = 0;

    if (appState == STATE_FILLER) {
      if (xSemaphoreTake(xMutex, (TickType_t)25) == pdTRUE) {
        local_currentWeight = kf_weight;
        local_moving_average = moving_average;
        xSemaphoreGive(xMutex);

        if (fillerState == STATE_WAITING_BOTTLE) {

          // a new bottle is on the scale
          setPumpPWMpercent(0);
          ledcWrite(pwmPin, 0);
          // Lets find which kind
          if (fabs(local_moving_average - local_currentWeight) < 0.1f) {
            if ((local_moving_average > BOTTLE_33CL_MIN) && (local_moving_average < BOTTLE_33CL_MAX)) {
              if (debug_print) Serial.println("33cl detected");
              // display_detected_bottle("330");
              bottle_size = "33cl";
              mg_to_fill = 325.0 * beer_gravity;
              bottle_weight = local_moving_average;
              fillerState = STATE_FILLING_BOTTLE;
              fill_percentage = 0;
              last_fill_percentage = -99999;
              // 50
            } else if ((local_moving_average > BOTTLE_50CL_MIN) && (local_moving_average < BOTTLE_50CL_MAX)) {
              if (debug_print) Serial.println("50cl detected");
              // display_detected_bottle("500");
              bottle_size = "50cl";
              mg_to_fill = 495.0 * beer_gravity;
              bottle_weight = local_moving_average;
              fillerState = STATE_FILLING_BOTTLE;
              fill_percentage = 0;
              last_fill_percentage = -99999;
              // 75
            } else if ((local_moving_average > BOTTLE_75CL_MIN) && (local_moving_average < BOTTLE_75CL_MAX)) {
              if (debug_print) Serial.println("75cl detected");
              // display_detected_bottle("750");
              bottle_size = "75cl";
              mg_to_fill = 745.0 * beer_gravity;
              bottle_weight = local_moving_average;
              fillerState = STATE_FILLING_BOTTLE;
              fill_percentage = 0;
              last_fill_percentage = -99999;
            }
          }
        } else if ((fillerState == STATE_FILLING_BOTTLE) && (appState == STATE_FILLER)) {

          if ((fabs(fill_percentage - last_fill_percentage)) > 0.5) drawFilling(kf_weight - bottle_weight, mg_to_fill);

          last_fill_percentage = fill_percentage;

          fill_percentage = int(abs(local_currentWeight - bottle_weight) * 100 / mg_to_fill);

          // Smooth start
          if (fill_percentage < 5) setPumpPWMpercent(low_duty);

          // Full spedd
          if ((fill_percentage > 5) && (fill_percentage < 85)) setPumpPWMpercent(full_duty);

          // Smooth finish
          if (fill_percentage > 80) setPumpPWMpercent(low_duty);


          // Bottle is filled
          if (local_currentWeight >= (bottle_weight + mg_to_fill)) {
            setPumpPWMpercent(0);
            ledcWrite(pwmPin, 0);

            fillerState = STATE_FILLED_BOTTLE;
            drawFinish();
            bottle_filled = true;
          }
          // Waiting for the bottle to be removed
        } else if ((fillerState == STATE_FILLED_BOTTLE) && (appState == STATE_FILLER) && (local_moving_average < 10)) {
          setPumpPWMpercent(0);
          ledcWrite(pwmPin, 0);
          fillerState = STATE_START;
          bottle_filled = false;
          drawFilller();
        }
        if (bottle_filled) {
          vTaskDelay(pdMS_TO_TICKS(150));
        } else {
          vTaskDelay(pdMS_TO_TICKS(2 + (1000 / SCALE_SPS)));
        }
      } else {
        Serial.println("Timeout mutex dans taskFiller");
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(10));  // Ajoute un délai ici si pas en mode FILLER
    }
  }
}

void taskMenu(void*) {
  ButtonEvent evt;
  for (;;) {
    if (xQueueReceive(buttonQueue, &evt, portMAX_DELAY) == pdTRUE) {
      if (appState == STATE_MAIN_MENU) {
        switch (evt) {
          case BTN_A:
            currentSelection = (currentSelection - 1 + menuSize) % menuSize;
            if (debug_print) Serial.println("BTN_A");
            if (debug_print) Serial.print("currentSelection ");
            if (debug_print) Serial.println(currentSelection);
            break;
          case BTN_C:
            currentSelection = (currentSelection + 1) % menuSize;
            if (debug_print) Serial.println("BTN_C");
            if (debug_print) Serial.print("currentSelection ");
            if (debug_print) Serial.println(currentSelection);
            break;
          case BTN_B:
            if (debug_print) Serial.println("BTN_B");
            switch (currentSelection) {
              case 0:
                if (debug_print) Serial.println("going to STATE_FILLER");
                appState = STATE_FILLER;
                fillerState = STATE_START;
                break;
              case 1:
                if (debug_print) Serial.println("going to STATE_PUMP");
                appState = STATE_PUMP;
                break;
              case 2:
                if (debug_print) Serial.println("going to STATE_GRAVITY");
                appState = STATE_GRAVITY;
                break;
              case 3:
                if (debug_print) Serial.println("going to STATE_TARE");
                appState = STATE_TARE;
                break;
              case 4:
                if (debug_print) Serial.println("going to STATE_CALIBRATION");
                appState = STATE_CALIBRATION;
                break;
            }
            break;
        }
      } else if (appState == STATE_GRAVITY) {
        switch (evt) {
          case BTN_A:
            beer_gravity = beer_gravity - 0.001;
            updateValue(beer_gravity);
            break;
          case BTN_A_LONG_1s:
            beer_gravity = beer_gravity - 0.01;
            updateValue(beer_gravity);
            break;
          case BTN_C:
            beer_gravity = beer_gravity + 0.001;
            updateValue(beer_gravity);
            break;
          case BTN_C_LONG_1s:
            beer_gravity = beer_gravity + 0.01;
            updateValue(beer_gravity);
            break;
          case BTN_B:
            // Save beer_gravity in EEPROM
            if (beer_gravity != saved_beer_gravity) {
              preferences.putFloat("beer_gravity", beer_gravity);
              saved_beer_gravity = beer_gravity;
              // Serial.println("Nouvelle beer_gravity => sauvegarde");
            }
            appState = STATE_MAIN_MENU;
            break;
        }
      } else if (appState == STATE_CALIBRATION) {
        switch (evt) {
          case BTN_A:
            calib_weight = calib_weight - 1;
            updateValue(calib_weight);
            break;
          case BTN_A_LONG_1s:
            calib_weight = calib_weight - 10;
            updateValue(calib_weight);
            break;
          case BTN_C:
            calib_weight = calib_weight + 1;
            updateValue(calib_weight);
            break;
          case BTN_C_LONG_1s:
            calib_weight = calib_weight + 10;
            updateValue(calib_weight);
            break;
          case BTN_B:
            drawTwoLines("Calibration ", "in progress");
            do_scale_factor();
            appState = STATE_MAIN_MENU;
            break;
        }
      } else if (appState == STATE_PUMP) {
        if ((evt == BTN_A) || (evt == BTN_B) || (evt == BTN_C)) {
          appState = STATE_MAIN_MENU;
          // Stop the pump
          setPumpPWMpercent(0);
        }
      } else if (appState == STATE_FILLER) {
        if ((evt == BTN_A) || (evt == BTN_B) || (evt == BTN_C)) {
          appState = STATE_MAIN_MENU;
          fillerState = STATE_START;
          // Stop the pump
          setPumpPWMpercent(0);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void taskDisplay(void*) {
  AppState lastState = (AppState)-1;
  int lastSelection = -1;
  FillerState lastFillerState = (FillerState)-1;
  // appState = STATE_MAIN_MENU;
  for (;;) {

    bool needRedraw = false;
    float local_currentWeight = 0;
    float local_moving_average = 0;
    float seuil_affichage = 0.1;

    if (appState != lastState || currentSelection != lastSelection) {
      needRedraw = true;
      switch (appState) {
        case STATE_MAIN_MENU:
          if (lastState != STATE_MAIN_MENU) M5.Display.fillScreen(TFT_BLACK);
          drawMenu();
          break;
        case STATE_FILLER: drawFilller(); break;
        case STATE_PUMP: drawPump(TFT_BLACK); break;
        case STATE_GRAVITY: drawSettingMenu("Final Gravity", beer_gravity); break;
        case STATE_TARE:
          delay(100);
          drawTwoLines("Tare in", "progress");
          do_tare_scale();
          appState = STATE_MAIN_MENU;
          break;
        case STATE_CALIBRATION:
          drawSettingMenu("Calib Weight", calib_weight);
          break;
      }
      lastState = appState;
      lastSelection = currentSelection;
    }

    if (appState == STATE_MAIN_MENU) {
      // Prenez le mutex avec un timeout plus long
      if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        local_currentWeight = kf_weight;
        local_moving_average = moving_average;
        xSemaphoreGive(xMutex);

        if ((fabs(local_currentWeight - lastDrawnWeight) > seuil_affichage) || (fabs(local_moving_average - lastDrawnAverage) > seuil_affichage)) {
          lastDrawnWeight = local_currentWeight;
          lastDrawnAverage = local_moving_average;
          needRedraw = true;
          drawWeight();
        }
      } else {
        Serial.println("Timeout mutex dans taskDisplay");
      }
    }

    // Délai adaptatif
    vTaskDelay(pdMS_TO_TICKS(needRedraw ? 100 : 50));
  }
}


// Duty cycle to control the pump
// --- fonction pour régler PWM en % ---
void setPumpPWMpercent(float percent_duty) {
  // change PWM only if it's different
  if (last_percent_duty != percent_duty) {
    if (percent_duty < 0) percent_duty = 0;
    if (percent_duty > 100) percent_duty = 100;
    int maxDuty = (1 << pwmResolution) - 1;
    int duty = (int)(percent_duty / 100.0 * maxDuty);
    ledcWrite(pwmPin, duty);
    // update last_percent_duty
    last_percent_duty = percent_duty;
  }
}


// ---------- Setup / Loop ----------
void setup() {

  auto cfg = M5.config();

  const uint8_t pin_SDA = 16;
  const uint8_t pin_SCL = 17;



  // Mutex
  xMutex = xSemaphoreCreateMutex();
  kf_weight = 0.0;
  currentWeight = 0.0;
  weight = 0.0;
  calibrationInProgress = false;

  M5.begin(cfg);
  M5.Display.setRotation(1);

  Serial.begin(115200);
  delay(1000);

  // Wire1.begin(pin_SDA, pin_SCL, 100000);  // SDA, SCL
  Wire1.begin(pin_SDA, pin_SCL);  // SDA, SCL
  vTaskDelay(1000);

  Serial.println("Scan I2C...");
  for (byte address = 1; address < 127; address++) {
    Wire1.beginTransmission(address);
    if (Wire1.endTransmission() == 0) {
      Serial.print("Trouvé périphérique I2C à 0x");
      Serial.println(address, HEX);
    }
  }


  if (!nau.begin(&Wire1)) {
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

  // Take 2*SCALE_SPS readings to flush out readings
  for (uint8_t i = 0; i < 2 * SCALE_SPS; i++) {
    while (!nau.available()) delay(1);
    nau.read();
  }
  Serial.println("NAU7802 initialisé !");


  // Sprite 8 bits pour éviter 0x0
  image_in_memory.setColorDepth(8);
  image_in_memory.createSprite(M5.Display.width(), M5.Display.height());
  weight_in_memory.setColorDepth(8);
  weight_in_memory.createSprite(320, 24);
  value_canvas.setColorDepth(8);
  value_canvas.createSprite(100, 50);


  // INIT FIFO buffer
  for (int i = 0; i < MAX_FIFO_SIZE; ++i) {
    fifo_scale.push(0);
  }
  moving_average = 0.0;
  moving_average_sum = 0.0;
  lastDrawnWeight = -99999.0f;   // impossible value to force first draw
  lastDrawnAverage = -99999.0f;  // impossible value to force first draw




  preferences.begin("filler", false);
  // preferences.clear();
  saved_beer_gravity = preferences.getFloat("beer_gravity", 1.00);
  saved_calib_weight = preferences.getInt("calib_weight", 185);

  // scale_offset = preferences.getFloat("scale_offset", -954218);
  // scale_factor = preferences.getFloat("scale_factor", -1121.379150);

  offset = preferences.getFloat("offset", -81.58);
  scale = preferences.getFloat("scale", -1118.10);
  Serial.print("Loaded preferences -  offset : ");
  Serial.print(offset);
  Serial.print(" - scale :");
  Serial.println(scale);



  calib_weight = saved_calib_weight;
  beer_gravity = saved_beer_gravity;


  // Intro animée
  // intro();

  // PWM configuration
  // attache le canal PWM à la pin
  ledcAttach(pwmPin, pwmFreq, pwmResolution);

  // duty = 0 au démarrage
  ledcWrite(pwmPin, 0);
  last_percent_duty = -999;
  // delay(100);



  buttonQueue = xQueueCreate(10, sizeof(ButtonEvent));
  Serial.println("Starting Filler Machine");
  M5.Display.fillScreen(TFT_BLACK);

  appState = STATE_MAIN_MENU;


  // --- Tâches sur le CORE 0 (UI et moins critiques) ---
  xTaskCreatePinnedToCore(taskButtons, "TaskButtons", 4096, nullptr, 1, &taskButtonsHandle, 0);
  xTaskCreatePinnedToCore(taskMenu, "TaskMenu", 4096, nullptr, 1, &taskMenuHandle, 0);
  xTaskCreatePinnedToCore(taskDisplay, "TaskDisplay", 8192, nullptr, 1, &taskDisplayHandle, 0);

  // --- Tâches sur le CORE 1 (Temps réel et critiques) ---
  xTaskCreatePinnedToCore(taskNAU7802, "NAU7802_Task", 8192, nullptr, 3, &taskNAU7802Handle, 1);
  xTaskCreatePinnedToCore(taskFiller, "Filler_Task", 8192, nullptr, 2, &taskFillerHandle, 1);
  vTaskDelete(NULL);
}

void loop() {
  // Cette fonction ne sera jamais atteinte
}