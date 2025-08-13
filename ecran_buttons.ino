#include <M5Unified.h>
#include <M5GFX.h>
#include "bottle.h"
// ---------- Sprite (frame buffer en RAM, 8 bits pour CoreS3) ----------
M5Canvas image_in_memory(&M5.Display);

// ---------- Menu ----------
const char* menuItems[] = { "Option 1", "Option 2", "Option 3" };
const int menuSize = sizeof(menuItems) / sizeof(menuItems[0]);
volatile int currentSelection = 0;

// ---------- Etat global ----------
enum AppState {
  STATE_MENU,
  STATE_SCREEN1,
  STATE_SCREEN2,
  STATE_SCREEN3
};
volatile AppState appState = STATE_MENU;

// ---------- FreeRTOS ----------
TaskHandle_t taskButtonsHandle = nullptr;
TaskHandle_t taskMenuHandle = nullptr;
TaskHandle_t taskDisplayHandle = nullptr;

QueueHandle_t buttonQueue;  // events boutons -> menu

enum ButtonEvent { BTN_A,
                   BTN_B,
                   BTN_C };

// ---------- Rendu ----------
static inline void drawMenu() {
  image_in_memory.fillScreen(TFT_BLACK);
  image_in_memory.setTextDatum(middle_center);
  image_in_memory.setFont(&fonts::Font4);
  // image_in_memory.setTextSize(2);

  for (int i = 0; i < menuSize; i++) {
    image_in_memory.setTextColor((i == currentSelection) ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
    image_in_memory.drawString(menuItems[i], M5.Display.width() / 2, 60 + i * 50);
  }
  image_in_memory.pushSprite(0, 0);
}

static inline void drawScreen(const char* title, uint16_t color) {
  image_in_memory.fillScreen(color);
  image_in_memory.setTextDatum(middle_center);
  image_in_memory.setTextColor(TFT_WHITE, color);
  image_in_memory.setFont(&fonts::Font4);

  // image_in_memory.setTextSize(3);
  image_in_memory.drawString(title, M5.Display.width() / 2, M5.Display.height() / 2);
  image_in_memory.setFont(&fonts::Font2);

  // image_in_memory.setTextSize(1);
  image_in_memory.drawString("Appuie sur B pour revenir", M5.Display.width() / 2, M5.Display.height() - 18);
  image_in_memory.pushSprite(0, 0);
}

// ---------- Tâches ----------
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
            appState = (currentSelection == 0)   ? STATE_SCREEN1
                       : (currentSelection == 1) ? STATE_SCREEN2
                                                 : STATE_SCREEN3;
            break;
        }
      } else {
        if (evt == BTN_B) {
          Serial.println("[Screen] Retour menu");
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
        case STATE_MENU: drawMenu(); break;
        case STATE_SCREEN1: drawScreen("Ecran Option 1", TFT_RED); break;
        case STATE_SCREEN2: drawScreen("Ecran Option 2", TFT_GREEN); break;
        case STATE_SCREEN3: intro(); break;
      }
      lastState = appState;
      lastSelection = currentSelection;
    }
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);
  delay(300);

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);

  // Sprite 8 bits pour éviter 0x0
  image_in_memory.setColorDepth(8);
  if (!image_in_memory.createSprite(M5.Display.width(), M5.Display.height())) {
    Serial.println("Erreur: Impossible de créer le sprite!");
  } else {
    Serial.printf("Sprite OK: %dx%d\n", image_in_memory.width(), image_in_memory.height());
  }

  // Ecran d'accueil
  image_in_memory.setFont(&fonts::Font6);
  image_in_memory.fillScreen(TFT_DARKGREY);
  image_in_memory.setTextDatum(middle_center);
  image_in_memory.setTextColor(TFT_WHITE, TFT_DARKGREY);
  image_in_memory.setTextSize(2);
  image_in_memory.drawString("CoreS3 + FreeRTOS + Sprite", M5.Display.width() / 2, M5.Display.height() / 2);
  image_in_memory.pushSprite(0, 0);
  delay(1000);

  buttonQueue = xQueueCreate(10, sizeof(ButtonEvent));

  xTaskCreatePinnedToCore(taskButtons, "TaskButtons", 4096, nullptr, 3, &taskButtonsHandle, 0);
  xTaskCreatePinnedToCore(taskMenu, "TaskMenu", 4096, nullptr, 1, &taskMenuHandle, 1);
  xTaskCreatePinnedToCore(taskDisplay, "TaskDisplay", 4096, nullptr, 2, &taskDisplayHandle, 1);

  Serial.println("Systeme lance.");
}

void loop() {
  // Tout est géré par FreeRTOS
}