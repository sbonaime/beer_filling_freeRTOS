#include <M5Unified.h>
#include <M5GFX.h>
#include "bottle.h"
#include <HX711.h>
#include <Preferences.h>
#include <FIFObuf.h>

Preferences preferences;

LGFX_Button button_save;
LGFX_Button button_decrease;
LGFX_Button button_increase;

//  HX711
const uint8_t dataPin = 16;
const uint8_t clockPin = 17;
HX711 scale;
QueueHandle_t weightQueue;
float currentWeight = 0;


// FIFO object for float
FIFObuf<float> fifo_scale(MAX_FIFO_SIZE);

// ---------- Sprite (frame buffer en RAM, 8 bits pour CoreS3) ----------
M5Canvas image_in_memory(&M5.Display);
M5Canvas beer_in_memory(&M5.Display);
M5Canvas weight_in_memory(&M5.Display);
M5Canvas value_canvas(&M5.Display);

// ---------- Menu ----------
const char* menuItems[] = { "Filler", "Density", "Tare", "Calibration" };
const int menuSize = sizeof(menuItems) / sizeof(menuItems[0]);
volatile int currentSelection = 0;

// ---------- Etat global ----------
enum AppState {
  STATE_MAIN_MENU,
  STATE_FILLER,
  STATE_GRAVITY,
  STATE_TARE,
  STATE_CALIBRATION
};
volatile AppState appState = STATE_MAIN_MENU;

// ---------- FreeRTOS ----------
TaskHandle_t taskButtonsHandle = nullptr;
TaskHandle_t taskMenuHandle = nullptr;
TaskHandle_t taskDisplayHandle = nullptr;
TaskHandle_t taskHX711Handle = nullptr;

QueueHandle_t buttonQueue;  // events boutons -> menu

enum ButtonEvent { BTN_A,
                   BTN_B,
                   BTN_C };

// ---------- Rendu ----------
static inline void drawMenu() {
  image_in_memory.fillScreen(TFT_BLACK);
  image_in_memory.setTextDatum(middle_left);
  image_in_memory.setFont(&fonts::FreeMonoBold18pt7b);
  image_in_memory.setTextSize(1);

  int menu_item_height = 45;
  for (int i = 0; i < menuSize; i++) {
    image_in_memory.setTextColor((i == currentSelection) ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
    image_in_memory.drawString(menuItems[i], 10, 30 + menu_item_height + i * menu_item_height);
  }
  bottle_scaling = 1;
  draw_beer(280, 50, 33, 33, TFT_BEER, TFT_WHITE, true);
  image_in_memory.pushSprite(0, 0);
  drawWeight();
}

static inline void drawWeight() {
  // Serial.println("drawWeight");
  weight_in_memory.fillScreen(TFT_BLACK);
  weight_in_memory.setFont(&fonts::FreeMonoBold12pt7b);
  weight_in_memory.setTextSize(1);
  weight_in_memory.setCursor(0, 0);
  weight_in_memory.printf("%.1fg", fabs(currentWeight));

  weight_in_memory.setCursor(100, 0);
  weight_in_memory.printf("Avge : %.1fg", fabs(moving_average));

  //  weight_in_memory.pushSprite(M5.Display.width(), 10);
  //weight_in_memory.pushSprite(M5.Display.width(), 10);
  weight_in_memory.pushSprite(10, 10);
}

static inline void drawScreen(const char* title, uint16_t color) {
  image_in_memory.fillScreen(color);
  image_in_memory.setTextDatum(middle_center);
  image_in_memory.setTextColor(TFT_WHITE, color);
  image_in_memory.setFont(&fonts::Font4);

  image_in_memory.drawString(title, M5.Display.width() / 2, M5.Display.height() / 2);
  image_in_memory.setFont(&fonts::Font2);

  image_in_memory.drawString("Appuie sur B pour revenir", M5.Display.width() / 2, M5.Display.height() - 18);
  image_in_memory.pushSprite(0, 0);
}

static inline void updateValue(float value, uint16_t background_color) {
  // value_canvas.setColorDepth(16);
  value_canvas.createSprite(100, 50);
  value_canvas.fillScreen(background_color);
  value_canvas.setTextColor(TFT_WHITE, background_color);
  value_canvas.setFont(&fonts::Font4);

  if (value < 2) {  // Gravity
    value_canvas.drawFloat(value, 3, 0, 0);
  } else {  // Calibration weight
    value_canvas.drawNumber(value, 0, 0);
  }
  value_canvas.pushSprite((M5.Display.width() / 2) - 10, (M5.Display.height() / 2) - 10);
  value_canvas.deleteSprite();
}



static inline void drawSettingMenu(const char* title, float value, uint16_t background_color) {
  M5.Display.fillScreen(background_color);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_WHITE, background_color);
  M5.Display.setFont(&fonts::Font4);

  M5.Display.drawString(title, (M5.Display.width() / 2), (M5.Display.height() / 2) - 80);
  updateValue(value, background_color);
  // if (value < 2) {  // Gravity
  //   image_in_memory.drawFloat(value, 3, M5.Display.width() / 2, M5.Display.height() / 2);
  // } else {  // Calibration weight
  //   image_in_memory.drawNumber(value, M5.Display.width() / 2, M5.Display.height() / 2);
  // }
  // bottom button menu
  int button_height = 40;
  int button_width = M5.Display.width() / 3;


  LGFX_Button button_save;
  LGFX_Button button_decrease;
  LGFX_Button button_increase;

  button_decrease.initButtonUL(&M5.Display, 0, M5.Display.height() - button_height, button_width, button_height, TFT_RED, TFT_BEER, TFT_BLACK, "-", 1, 1);
  button_decrease.drawButton();
  button_save.initButtonUL(&M5.Display, button_width, M5.Display.height() - button_height, button_width, button_height, TFT_RED, TFT_BEER, TFT_BLACK, "Save", 1, 1);
  button_save.drawButton();
  button_increase.initButtonUL(&M5.Display, button_width * 2, M5.Display.height() - button_height, button_width, button_height, TFT_RED, TFT_BEER, TFT_BLACK, "+", 1, 1);
  button_increase.drawButton();

  // Buttons
  // M5.Display.fillRoundRect(0, M5.Display.height() - button_height, button_width, button_height, 5, TFT_GREY);
  // M5.Display.fillRoundRect(button_width, M5.Display.height() - button_height, button_width, button_height, 5, TFT_BLUE);
  // M5.Display.fillRoundRect(button_width * 2, M5.Display.height() - button_height, button_width, button_height, 5, TFT_GREY);

  // Text
  // M5.Display.drawString("-", button_width / 2, M5.Display.height() - button_height / 2, TFT_WHITE);
  // M5.Display.drawString("Ok", button_width + button_width / 2, M5.Display.height() - button_height / 2, TFT_WHITE);
  // M5.Display.drawString("+", 2 * button_width + button_width / 2, M5.Display.height() - button_height / 2, TFT_WHITE);
}

// ---------- Tâches ----------
void taskHX711(void*) {
  Serial.println("Tâche HX711 démarrée");
  for (;;) {
    // if (hx711Initialized && scale.is_ready()) {
    if (scale.is_ready()) {
      float weight = scale.get_units(1);

      if (weightQueue != NULL) {
        xQueueSend(weightQueue, &weight, 0);

        // Fifo and moving average
        moving_average_sum -= fifo_scale.pop();
        fifo_scale.push(weight);
        moving_average_sum += weight;
        moving_average = moving_average_sum / MAX_FIFO_SIZE;
      }


      // static int counter = 0;
      // if (counter++ % 5 == 0) {  // Réduire la fréquence des messages debug
      //   Serial.printf("Poids: %.2fg\n", weight);
      //   counter = 1;
      // }
    } else {
      Serial.println("HX711 non prêt");
      vTaskDelay(pdMS_TO_TICKS(1000));
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void taskButtons(void*) {
  for (;;) {
    M5.update();
    if (M5.BtnA.wasPressed()) {
      ButtonEvent e = BTN_A;
      xQueueSend(buttonQueue, &e, 0);
    }
    if (M5.BtnB.wasPressed()) {
      ButtonEvent e = BTN_B;
      xQueueSend(buttonQueue, &e, 0);
    }
    if (M5.BtnC.wasPressed()) {
      ButtonEvent e = BTN_C;
      xQueueSend(buttonQueue, &e, 0);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
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
            Serial.printf("[Menu] A -> %s\n", menuItems[currentSelection]);
            break;
          case BTN_C:
            currentSelection = (currentSelection + 1) % menuSize;
            Serial.printf("[Menu] C -> %s\n", menuItems[currentSelection]);
            break;
          case BTN_B:
            Serial.printf("[Menu] B -> Ouvrir: %s\n", menuItems[currentSelection]);
            switch (currentSelection) {
              case 0:
                appState = STATE_FILLER;
                break;
              case 1:
                appState = STATE_GRAVITY;
                break;
              case 2:
                appState = STATE_TARE;
                break;
              case 3:
                appState = STATE_CALIBRATION;
                break;
            }
            break;
        }
      } else if (appState == STATE_GRAVITY) {
        switch (evt) {
          case BTN_A:
            beer_gravity = beer_gravity - 0.001;
            updateValue(beer_gravity, TFT_BLACK);
            break;
          case BTN_C:
            beer_gravity = beer_gravity + 0.001;
            updateValue(beer_gravity, TFT_BLACK);
            break;
          case BTN_B:
            // Save beer_gravity in EEPROM
            if (beer_gravity != saved_beer_gravity) {
              preferences.putFloat("beer_gravity", beer_gravity);
              saved_beer_gravity = beer_gravity;
              Serial.println("Nouvelle beer_gravity => sauvegarde");
            }
            appState = STATE_MAIN_MENU;
            break;
        }
      } else if (appState == STATE_CALIBRATION) {
        switch (evt) {
          case BTN_A:
            scale_calibration_weight = scale_calibration_weight - 1;
            updateValue(scale_calibration_weight, TFT_NAVY);
            break;
          case BTN_C:
            scale_calibration_weight = scale_calibration_weight + 1;
            updateValue(scale_calibration_weight, TFT_NAVY);
            break;
          case BTN_B:
            // Do the calibration with scale_calibration_weight
            if (scale_calibration_weight != saved_scale_calibration_weight) {
              saved_scale_calibration_weight = scale_calibration_weight;
              Serial.println("Nouvelle scale_calibration_weight => sauvegarde");
              scale.calibrate_scale(scale_calibration_weight, 10);

              // Get calibration parameters
              scale_factor = scale.get_scale();
              scale_offset = scale.get_offset();

              // Save values in EEPROM
              preferences.putFloat("scale_offset", scale_offset);
              preferences.putFloat("scale_factor", scale_factor);
              preferences.putFloat("scale_calibration_weight", scale_calibration_weight);
            }
            appState = STATE_MAIN_MENU;
            break;
        }
      } else {
        if (evt == BTN_B) {
          Serial.println("[Screen] back to main menu");
          if (appState == STATE_TARE) {
            scale.tare();
          }
          appState = STATE_MAIN_MENU;
        }
      }
    }
  }
}

void taskDisplay(void*) {
  AppState lastState = (AppState)-1;
  int lastSelection = -1;
  float lastDrawnWeight = -99999.0f;   // impossible value to force first draw
  float lastDrawnAverage = -99999.0f;  // impossible value to force first draw

  for (;;) {
    bool needRedraw = false;

    if (appState != lastState || currentSelection != lastSelection) {
      needRedraw = true;
      switch (appState) {
        case STATE_MAIN_MENU: drawMenu(); break;
        case STATE_FILLER: intro(); break;
        case STATE_GRAVITY: drawSettingMenu("Final Gravity", beer_gravity, TFT_BLACK); break;
        case STATE_TARE: drawScreen("Tare", TFT_NAVY); break;
        case STATE_CALIBRATION:
          drawSettingMenu("Calibration Weight", scale_calibration_weight, TFT_NAVY);
          break;
      }
      lastState = appState;
      lastSelection = currentSelection;
    }

    // Récupérer nouvelle mesure
    float newWeight;
    if (weightQueue != NULL && xQueueReceive(weightQueue, &newWeight, 0) == pdTRUE) {
      currentWeight = newWeight;
      // Only redraw weight/average if they changed significantly
      if (appState == STATE_MAIN_MENU && (fabs(currentWeight - lastDrawnWeight) > 0.1f || fabs(moving_average - lastDrawnAverage) > 0.1f)) {
        drawWeight();
        lastDrawnWeight = currentWeight;
        lastDrawnAverage = moving_average;
      }
    }

    // Only delay if something was redrawn
    if (needRedraw) {
      vTaskDelay(pdMS_TO_TICKS(40));
    } else {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}
// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);
  delay(100);


  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);


  // Sprite 8 bits pour éviter 0x0
  image_in_memory.setColorDepth(8);

  if (!image_in_memory.createSprite(M5.Display.width(), M5.Display.height())) {
    Serial.println("Erreur: Impossible de créer le sprite image_in_memory !");
  } else {
    Serial.printf("Sprite OK: %dx%d\n", image_in_memory.width(), image_in_memory.height());
  }

  weight_in_memory.setColorDepth(8);
  if (!weight_in_memory.createSprite(320, 24)) {
    Serial.println("Erreur: Impossible de créer le sprite weight_in_memory !");
  } else {
    Serial.println("Sprite weight_in_memory OK");
  }

  // INIT FIFO buffer
  for (int i = 0; i < MAX_FIFO_SIZE; ++i) {
    fifo_scale.push(0);
  }
  moving_average = 0.0;
  moving_average_sum = 0.0;

  // HX711 INIT
  scale.begin(dataPin, clockPin);
  // Serial.print("UNITS: ");
  // Serial.println(scale.get_units(10));
  delay(100);

  preferences.begin("filler", false);
  scale_offset = preferences.getFloat("scale_offset", -954218);
  scale_factor = preferences.getFloat("scale_factor", -1121.379150);
  saved_scale_calibration_weight = preferences.getFloat("scale_calibration_weight", 180);
  saved_beer_gravity = preferences.getFloat("beer_gravity", 1.015);


  scale_calibration_weight = saved_scale_calibration_weight;
  beer_gravity = saved_beer_gravity;

  scale.set_scale(scale_factor);
  scale.set_offset(scale_offset);

  Serial.println("Tare now !");
  scale.tare();

  // Créer la queue
  weightQueue = xQueueCreate(10, sizeof(float));
  if (weightQueue == NULL) {
    Serial.println("ERREUR: Impossible de créer la queue");
  } else {
    Serial.println("Queue créée avec succès");
  }

  // Intro animée
  //intro();

  buttonQueue = xQueueCreate(10, sizeof(ButtonEvent));

  xTaskCreatePinnedToCore(taskButtons, "TaskButtons", 4096, nullptr, 3, &taskButtonsHandle, 0);
  xTaskCreatePinnedToCore(taskMenu, "TaskMenu", 4096, nullptr, 1, &taskMenuHandle, 1);
  xTaskCreatePinnedToCore(taskDisplay, "TaskDisplay", 4096, nullptr, 2, &taskDisplayHandle, 1);
  xTaskCreatePinnedToCore(taskHX711, "HX711_Task", 4096, nullptr, 3, &taskHX711Handle, 1);

  Serial.println("Systeme lance.");
}

void loop() {
  // Tout est géré par FreeRTOS
}