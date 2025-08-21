#include <M5Unified.h>
#include <M5GFX.h>
#include "bottle.h"
#include <HX711.h>

//  HX711
const uint8_t dataPin = 16;
const uint8_t clockPin = 17;
HX711 scale;
QueueHandle_t weightQueue;
float currentWeight = 0;
float calibrationWeight = 180.0;


// ---------- Sprite (frame buffer en RAM, 8 bits pour CoreS3) ----------
M5Canvas image_in_memory(&M5.Display);
M5Canvas beer_in_memory(&M5.Display);
M5Canvas weight_in_memory(&M5.Display);

// ---------- Menu ----------
const char* menuItems[] = { "Filling", "Scale", "Tare" };
const int menuSize = sizeof(menuItems) / sizeof(menuItems[0]);
volatile int currentSelection = 0;

// ---------- Etat global ----------
enum AppState {
  STATE_MENU,
  STATE_BOTTLE_FILLING,
  STATE_SCALE,
  STATE_TARE
};
volatile AppState appState = STATE_MENU;

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
  image_in_memory.setTextDatum(middle_center);
  image_in_memory.setFont(&fonts::FreeMonoBold18pt7b);
  image_in_memory.setTextSize(1);

  for (int i = 0; i < menuSize; i++) {
    image_in_memory.setTextColor((i == currentSelection) ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
    //image_in_memory.drawString(menuItems[i], M5.Display.width() / 2, 60 + i * 70);
    image_in_memory.drawString(menuItems[i], M5.Display.width() / 2, 60 + i * 60);
  }
  bottle_scaling = 1;
  draw_beer(280, 20, 33, 33, TFT_BEER, TFT_WHITE, true);

  // Récupérer nouvelle mesure


  // image_in_memory.drawString(String(currentWeight), 30, 10);
  // image_in_memory.setCursor(30, 30);
  // image_in_memory.printf("Poids: %.1fg", currentWeight);

  image_in_memory.pushSprite(0, 0);
}

static inline void drawWeight() {
  // Serial.println("drawWeight");
  weight_in_memory.fillScreen(TFT_BLACK);
  weight_in_memory.setFont(&fonts::Font2);
  weight_in_memory.setTextSize(1);

  weight_in_memory.setCursor(0, 0);
  weight_in_memory.printf("Poids: %.1fg", currentWeight);
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

// ---------- Tâches ----------
void taskHX711(void*) {
  Serial.println("Tâche HX711 démarrée");
  for (;;) {
    // if (hx711Initialized && scale.is_ready()) {
    if (scale.is_ready()) {
      float weight = scale.get_units(1);

      if (weightQueue != NULL) {
        xQueueSend(weightQueue, &weight, 0);
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
      if (appState == STATE_MENU) {
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
            appState = (currentSelection == 0)   ? STATE_BOTTLE_FILLING
                       : (currentSelection == 1) ? STATE_SCALE
                                                 : STATE_TARE;
            break;
        }
      } else {
        if (evt == BTN_B) {
          Serial.println("[Screen] Retour menu");
          if (appState == STATE_TARE) {
            scale.tare();
          }
          appState = STATE_MENU;
        }
      }
    }
  }
}

void taskDisplay(void*) {
  AppState lastState = (AppState)-1;
  int lastSelection = -1;

  for (;;) {

    if (appState != lastState || currentSelection != lastSelection) {
      switch (appState) {
        // case STATE_BOTTLE_FILLING: drawScreen("Ecran Option 1", TFT_RED); break;
        case STATE_MENU: drawMenu(); break;
        case STATE_BOTTLE_FILLING: intro(); break;
        case STATE_SCALE: drawScreen("Scale", TFT_NAVY); break;
        case STATE_TARE: drawScreen("Tare", TFT_RED); break;
      }
      lastState = appState;
      lastSelection = currentSelection;
    }
    // Récupérer nouvelle mesure
    float newWeight;
    if (weightQueue != NULL && xQueueReceive(weightQueue, &newWeight, 0) == pdTRUE) {
      currentWeight = newWeight;
      drawWeight();
    }



    vTaskDelay(pdMS_TO_TICKS(40));
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
  if (!weight_in_memory.createSprite(100, 30)) {
    Serial.println("Erreur: Impossible de créer le sprite weight_in_memory !");
  } else {
    Serial.println("Sprite weight_in_memory OK");
  }
  // HX711 INIT
  scale.begin(dataPin, clockPin);
  // Serial.print("UNITS: ");
  // Serial.println(scale.get_units(10));
  delay(100);


  scale.set_scale(-1121.379150);
  scale.set_offset(-954218);

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
  intro();

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