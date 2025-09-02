#include "bottle_filler.h"
#include <M5Unified.h>
#include <Wire.h>
#include <M5GFX.h>
#include <Preferences.h>
#include <FIFObuf.h>
#include <SimpleKalmanFilter.h>
#include <Adafruit_NAU7802.h>



// Board
// https://docs.m5stack.com/en/arduino/arduino_board

// Librairies
// M5Unified
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


const uint8_t dataPin = 16;
const uint8_t clockPin = 17;
QueueHandle_t weightQueue;
float currentWeight = 0;
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
  image_in_memory.fillScreen(TFT_BLACK);
  image_in_memory.setTextDatum(middle_left);
  image_in_memory.setFont(&fonts::FreeMonoBold18pt7b);
  image_in_memory.setTextSize(1);

  int menu_item_height = 37;
  for (int i = 0; i < menuSize; i++) {
    image_in_memory.setTextColor((i == currentSelection) ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
    image_in_memory.drawString(menuItems[i], 10, 20 + menu_item_height + i * menu_item_height);
  }
  bottle_scaling = 1;
  beer_in_memory.createSprite(1 + 24 * bottle_scaling, 1 + bottle_scaling * (72));

  draw_beer_bottle(280, 50, 33, 33, TFT_BEER, 0x80, true);
  image_in_memory.pushSprite(0, 0);
  drawWeight();
}

void intro() {

  // MAGENTA => VERT
  // FFFF => bleu
  // 000 => noir
  // 0F0 => bleu
  // FFF => bleu fort
  // TFT_NAVY => NOIR

  image_in_memory.fillScreen(TFT_WHITE);

  image_in_memory.setFont(&fonts::FreeMonoBold24pt7b);
  image_in_memory.setTextSize(1);

  image_in_memory.setTextColor(TFT_BLUE);
  image_in_memory.setCursor(5, 30);
  image_in_memory.println("BOTTLE");

  image_in_memory.setTextColor(TFT_RED);
  image_in_memory.setCursor(5, 100);
  image_in_memory.println("FILLER");
  image_in_memory.pushSprite(0, 0);


  bottle_position_x = 250, bottle_position_y = 100;
  bottle_scaling = 1.7;


  beer_in_memory.createSprite(1 + 24 * bottle_scaling, 1 + bottle_scaling * (72));

  // Remplissage virtuel
  for (int i = 0; i < 100; i++) {
    draw_beer_bottle(bottle_position_x, bottle_position_y, i, 100, TFT_BEER, TFT_BLACK, false);
    delay(20);
  }
  delay(1000);

  // Clear intro screen
  image_in_memory.fillScreen(TFT_BLACK);
  image_in_memory.pushSprite(0, 0);
  appState = STATE_MAIN_MENU;
  drawMenu();
}

void draw_beer_bottle(int x_position, int y_position, int poids_actuel, int poids_final, uint32_t liquid_color, uint32_t bottle_color, boolean menu) {
  //                  A   D2    B
  //             0  b ----------
  //                  |        |
  //         H2       |        |
  //                H |        | C
  //                 /          \
  //         HG     /            \
  //               /              \
  //            G |                | D
  //              |                |
  //         H1   |                |
  //              |                |
  //              |                |
  //            F  ---------------- E
  //                      D1


  // x_sprite_dimension = 1+ 24 * bottle_scaling
  // y_sprite_dimension = 1+  bottle_scaling  * ( 72);
  // bottle_scaling = 1 -> sprite de 25x73
  // bottle_scaling = 1.7 -> sprite de 41x124
  // bottle_scaling = 2.5 -> sprite de 61x181


  D1 = 24 * bottle_scaling;
  D2 = 9 * bottle_scaling;
  H1 = 45 * bottle_scaling;
  HG = 18 * bottle_scaling;
  H2 = 9 * bottle_scaling;

  b = (D1 - D2) / 2, ax = b, ay = 0, bx = b + D2, by = 0, cx = bx, cy = H2, dx = D1;
  dy = H2 + HG, ex = D1, ey = H1 + HG + H2, fx = 0, fy = H1 + HG + H2, gx = 0, gy = H2 + HG, hx = ax, hy = H2;


  if ((poids_actuel <= poids_final) && (poids_actuel >= 0)) {
    new_beer_height = poids_actuel * (H1 + HG) / poids_final;

    if (new_beer_height >= old_beer_height) {
      // Create a 8 bit sprite 80 pixels wide, 35 high (2800 bytes of RAM needed)

      // beer_in_memory.createSprite(D1 + 1, H1 + HG + H2 + 1);

      // Fill it with black (this will be the transparent colour this time)
      beer_in_memory.fillSprite(transparent_color);

      // beer rectangle
      beer_in_memory.fillRect(0, H1 + H2 + HG - new_beer_height, D1, new_beer_height, liquid_color);

      // Dessin de la forme negative de la bouteille
      beer_in_memory.fillRect(0, 0, ax, hy, transparent_color);
      beer_in_memory.fillRect(bx, by, b + 2, hy, transparent_color);

      beer_in_memory.fillTriangle(0, hy, gx, gy, hx, hy, transparent_color);
      beer_in_memory.fillTriangle(cx, cy, dx, dy, ex, cy, transparent_color);

      // Bouteille
      beer_in_memory.drawLine(ax + 1, ay, bx - 1, by, bottle_color);
      beer_in_memory.drawLine(bx - 1, by, cx - 1, cy, bottle_color);
      beer_in_memory.drawLine(cx - 1, cy, dx - 1, dy, bottle_color);
      beer_in_memory.drawLine(dx - 1, dy, ex - 1, ey, bottle_color);
      beer_in_memory.drawLine(ex, ey - 1, fx + 1, fy - 1, bottle_color);
      beer_in_memory.drawLine(fx + 1, fy, gx + 1, gy, bottle_color);
      beer_in_memory.drawLine(gx + 1, gy, hx, hy, bottle_color);
      beer_in_memory.drawLine(hx, hy, ax, ay, bottle_color);

      // double ligne
      beer_in_memory.drawLine(ax + 1, ay + 1, bx - 1, by + 1, bottle_color);
      beer_in_memory.drawLine(bx, by - 1, cx, cy, bottle_color);
      beer_in_memory.drawLine(cx, cy, dx, dy, bottle_color);
      beer_in_memory.drawLine(dx, dy, ex, ey + 1, bottle_color);
      beer_in_memory.drawLine(ex, ey, fx + 1, fy, bottle_color);
      beer_in_memory.drawLine(fx, fy + 1, gx, gy, bottle_color);
      beer_in_memory.drawLine(gx, gy, hx - 1, hy, bottle_color);
      beer_in_memory.drawLine(hx - 1, hy, ax - 1, ay, bottle_color);



      // Push sprite to TFT screen CGRAM at coordinate x,y (top left corner)
      // Specify what colour is to be treated as transparent.
      if (menu == false) {
        beer_in_memory.pushSprite(x_position, y_position, transparent_color);
      } else {
        beer_in_memory.pushSprite(&image_in_memory, x_position, y_position, transparent_color);
      }
    }
  }
}

static inline void drawWeight() {
  // Serial.println("drawWeight");
  weight_in_memory.fillScreen(TFT_BLACK);
  weight_in_memory.setFont(&fonts::FreeMonoBold12pt7b);
  weight_in_memory.setTextSize(1);
  weight_in_memory.setCursor(0, 0);
  weight_in_memory.printf("%.1fg", fabs(currentWeight));

  weight_in_memory.setCursor(100, 0);
  weight_in_memory.printf("Avg : %.1fg", fabs(moving_average));

  //  weight_in_memory.pushSprite(M5.Display.width(), 10);
  //weight_in_memory.pushSprite(M5.Display.width(), 10);
  weight_in_memory.pushSprite(10, 10);
}

static inline void drawScreen(const char* title, uint16_t color) {
  M5.Display.fillScreen(color);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_WHITE, color);
  M5.Display.setFont(&fonts::Font4);

  M5.Display.drawString(title, M5.Display.width() / 2, M5.Display.height() / 2);
  M5.Display.setFont(&fonts::Font2);

  M5.Display.drawString("B button for main menu", M5.Display.width() / 2, M5.Display.height() - 18);
}

static inline void drawPump(uint16_t color) {
  M5.Display.fillScreen(color);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_WHITE, color);
  M5.Display.setFont(&fonts::Font4);

  M5.Display.drawString("Pump On", M5.Display.width() / 2, M5.Display.height() / 2);
  M5.Display.setFont(&fonts::Font2);
  LGFX_Button button_stop;

  int button_height = 40;
  int button_width = M5.Display.width();
  button_stop.initButtonUL(&M5.Display, 0, M5.Display.height() - button_height, button_width, button_height, TFT_RED, TFT_BEER, TFT_BLACK, "STOP", 1, 1);
  button_stop.drawButton();
  // Pump on
  setPumpPWMpercent(full_duty);
}



static inline void drawFilller() {
  if (debug_print) Serial.println("drawFilller start ");
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_RED, TFT_WHITE);
  M5.Display.setFont(&fonts::FreeMonoBold24pt7b);
  M5.Display.setTextSize(1);

  M5.Display.drawString("Detecting", M5.Display.width() / 2, M5.Display.height() / 2 - 50, &fonts::FreeMonoBold24pt7b);
  M5.Display.drawString("Bottle", M5.Display.width() / 2, M5.Display.height() / 2 - 10, &fonts::FreeMonoBold24pt7b);

  LGFX_Button button_stop;

  int button_height = 40;
  int button_width = M5.Display.width();
  button_stop.initButtonUL(&M5.Display, 0, M5.Display.height() - button_height, button_width, button_height, TFT_RED, TFT_BEER, TFT_BLACK, "STOP", 1, 1);
  button_stop.drawButton();
  fillerState = STATE_WAITING_BOTTLE;
}

static inline void drawFilling() {
  if (debug_print) Serial.println("drawFilling start ");

  image_in_memory.fillScreen(TFT_WHITE);
  image_in_memory.setFont(&fonts::FreeMonoBold24pt7b);
  image_in_memory.setTextSize(1);
  image_in_memory.setTextColor(TFT_BLUE);
  image_in_memory.drawString(bottle_size, 5, 30, &fonts::FreeMonoBold24pt7b);


  bottle_position_x = 220, bottle_position_y = 10;
  bottle_scaling = 2.5;

  LGFX_Button button_stop;

  int button_height = 40;
  int button_width = M5.Display.width();
  button_stop.initButtonUL(&image_in_memory, 0, M5.Display.height() - button_height, button_width, button_height, TFT_RED, TFT_BEER, TFT_BLACK, "STOP", 1, 1);
  button_stop.drawButton();
  beer_in_memory.createSprite(1 + 24 * bottle_scaling, 1 + bottle_scaling * (72));

  draw_beer_bottle(bottle_position_x, bottle_position_y, abs(currentWeight - bottle_weight), mg_to_fill, TFT_BEER, TFT_BLACK, true);
  image_in_memory.pushSprite(0, 0);
}

static inline void drawFinish() {
  if (debug_print) Serial.println("drawFinish start ");

  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_RED);
  M5.Display.setTextDatum(TL_DATUM);

  M5.Display.drawString("Finish", 5, 30, &fonts::FreeMonoBold24pt7b);
  M5.Display.setFont(&fonts::FreeMonoBold24pt7b);


  bottle_position_x = 220, bottle_position_y = 10;
  bottle_scaling = 2.5;

  LGFX_Button button_stop;

  int button_height = 40;
  int button_width = M5.Display.width();
  button_stop.initButtonUL(&M5.Display, 0, M5.Display.height() - button_height, button_width, button_height, TFT_RED, TFT_BEER, TFT_BLACK, "STOP", 1, 1);
  button_stop.drawButton();
  beer_in_memory.createSprite(1 + 24 * bottle_scaling, 1 + bottle_scaling * (72));

  // Remplissage virtuel
  for (int i = 0; i < 100; i++) {
    draw_beer_bottle(bottle_position_x, bottle_position_y, i, 100, 0xFF, TFT_BLACK, false);
    // delay(10);
  }
}


static inline void updateValue(float value, uint16_t background_color) {
  value_canvas.fillScreen(background_color);
  value_canvas.setTextColor(TFT_WHITE, background_color);
  value_canvas.setFont(&fonts::Font4);

  if (value < 2) {  // Gravity
    value_canvas.drawFloat(value, 3, 0, 0);
  } else {  // Calibration weight
    value_canvas.drawNumber(value, 0, 0);
  }
  value_canvas.pushSprite((M5.Display.width() / 2) - 10, (M5.Display.height() / 2) - 10);
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

void do_tare_scale() {
  // --- Tare ---
  int64_t sum = 0;
  for (int i = 0; i < SCALE_SPS / 2; i++) {
    while (!nau.available())
      ;
    sum += nau.read();
  }
  scale_offset = sum / SCALE_SPS / 2;
  preferences.putDouble("scale_offset", scale_offset);

  Serial.println("Tare done");
}


void do_scale_factor() {

  int64_t sum = 0;
  for (int i = 0; i < SCALE_SPS * 2; i++) {
    while (!nau.available()) {
      sum += nau.read();
    }
  }
  int32_t avgReading = sum / SCALE_SPS * 2;

  scale_factor = (avgReading - scale_offset) / calib_weight;
  Serial.println("Scale factor done");
}


void taskNAU7802(void*) {

  Serial.println("Tâche NAU7802 démarrée");

  for (;;) {
    if (nau.available()) {
      int32_t val = nau.read();
      float weight = (val - scale_offset) / scale_factor;

      // Kalman filter
      float kf_weight = kaman_filter.updateEstimate(weight);


      if (weightQueue != NULL) {
        xQueueSend(weightQueue, &kf_weight, 0);

        // Fifo and moving average
        moving_average_sum -= fifo_scale.pop();
        fifo_scale.push(kf_weight);
        moving_average_sum += kf_weight;
        last_moving_average = moving_average;
        moving_average = moving_average_sum / MAX_FIFO_SIZE;
      }

    } else {
      Serial.println("NAU7802 non prêt");
      vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // 40 SPS
    // vTaskDelay(pdMS_TO_TICKS(25));
    // 10 SPS
    vTaskDelay(pdMS_TO_TICKS(1000 / SCALE_SPS));
  }
}



void taskButtons(void*) {
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

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
void taskFiller(void*) {
  for (;;) {
    bool bottle_filled = false;

    if ((fillerState == STATE_WAITING_BOTTLE) && (appState == STATE_FILLER)) {
      // a new bottle is on the scale
      setPumpPWMpercent(0);
      ledcWrite(pwmPin, 0);
      // Lets find which kind
      if ((fabs(moving_average - last_moving_average) < 0.1f) && (fabs(moving_average - currentWeight) < 0.1f)) {
        if ((moving_average > BOTTLE_33CL_MIN) && (moving_average < BOTTLE_33CL_MAX)) {
          if (debug_print) Serial.println("33cl detected");
          // display_detected_bottle("330");
          bottle_size = "33cl";
          mg_to_fill = 325.0 * beer_gravity;
          bottle_weight = moving_average;
          fillerState = STATE_FILLING_BOTTLE;
          fill_percentage = 0;
          last_fill_percentage = -99999;
          // 50
        } else if ((moving_average > BOTTLE_50CL_MIN) && (moving_average < BOTTLE_50CL_MAX)) {
          if (debug_print) Serial.println("50cl detected");
          // display_detected_bottle("500");
          bottle_size = "50cl";
          mg_to_fill = 495.0 * beer_gravity;
          bottle_weight = moving_average;
          fillerState = STATE_FILLING_BOTTLE;
          fill_percentage = 0;
          last_fill_percentage = -99999;
          // 75
        } else if ((moving_average > BOTTLE_75CL_MIN) && (moving_average < BOTTLE_75CL_MAX)) {
          if (debug_print) Serial.println("75cl detected");
          // display_detected_bottle("750");
          bottle_size = "75cl";
          mg_to_fill = 745.0 * beer_gravity;
          bottle_weight = moving_average;
          fillerState = STATE_FILLING_BOTTLE;
          fill_percentage = 0;
          last_fill_percentage = -99999;
        }
      }
    } else if ((fillerState == STATE_FILLING_BOTTLE) && (appState == STATE_FILLER)) {
      if (debug_print) Serial.println("Pump On in taskFiller");

      if ((fabs(fill_percentage - last_fill_percentage)) > 0.5) drawFilling();

      last_fill_percentage = fill_percentage;
      fill_percentage = int(abs(currentWeight - bottle_weight) * 100 / mg_to_fill);

      // Smooth start
      if (fill_percentage < 5) setPumpPWMpercent(low_duty);

      // Full spedd
      if ((fill_percentage > 5) && (fill_percentage < 85)) setPumpPWMpercent(full_duty);

      // Smooth finish
      if (fill_percentage > 80) setPumpPWMpercent(low_duty);
      // Bottle is filled
      if (currentWeight >= (bottle_weight + mg_to_fill)) {
        setPumpPWMpercent(0);
        ledcWrite(pwmPin, 0);

        fillerState = STATE_FILLED_BOTTLE;
        drawFinish();
        bottle_filled = true;
      }
      // Waiting for the bottle to be removed
    } else if ((fillerState == STATE_FILLED_BOTTLE) && (appState == STATE_FILLER) && (moving_average < 10)) {
      setPumpPWMpercent(0);
      ledcWrite(pwmPin, 0);
      fillerState = STATE_START;
      bottle_filled = false;
      drawFilller();
    }
    if (bottle_filled) {
      vTaskDelay(pdMS_TO_TICKS(150));
    } else {
      vTaskDelay(pdMS_TO_TICKS(1000 / SCALE_SPS));
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
            if (debug_print) Serial.println("BTN_A");
            currentSelection = (currentSelection - 1 + menuSize) % menuSize;
            break;
          case BTN_C:
            if (debug_print) Serial.println("BTN_C");
            currentSelection = (currentSelection + 1) % menuSize;
            break;
          case BTN_B:
            if (debug_print) Serial.println("BTN_B");
            switch (currentSelection) {
              case 0:
                appState = STATE_FILLER;
                fillerState = STATE_START;
                break;
              case 1:
                appState = STATE_PUMP;
                break;
              case 2:
                appState = STATE_GRAVITY;
                break;
              case 3:
                appState = STATE_TARE;
                break;
              case 4:
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
          case BTN_A_LONG_1s:
            beer_gravity = beer_gravity - 0.01;
            updateValue(beer_gravity, TFT_BLACK);
            break;
          case BTN_C:
            beer_gravity = beer_gravity + 0.001;
            updateValue(beer_gravity, TFT_BLACK);
            break;
          case BTN_C_LONG_1s:
            beer_gravity = beer_gravity + 0.01;
            updateValue(beer_gravity, TFT_BLACK);
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
            updateValue(calib_weight, TFT_BLACK);
            break;
          case BTN_A_LONG_1s:
            calib_weight = calib_weight - 10;
            updateValue(calib_weight, TFT_BLACK);
            break;
          case BTN_C:
            calib_weight = calib_weight + 1;
            updateValue(calib_weight, TFT_BLACK);
            break;
          case BTN_C_LONG_1s:
            calib_weight = calib_weight + 10;
            updateValue(calib_weight, TFT_BLACK);
            break;
          case BTN_B:

            // Do the calibration with calib_weight
            if (calib_weight != saved_calib_weight) {

              // Get calibration parameters
              do_scale_factor();

              // Save values in EEPROM
              preferences.putDouble("scale_factor", scale_factor);
              preferences.putInt("calib_weight", calib_weight);

              Serial.println("Nouvelle calib_weight => sauvegarde");
              saved_calib_weight = calib_weight;
            }
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
      } else {
        if (evt == BTN_B) {
          // Serial.println("[Screen] back to main menu");
          if (appState == STATE_TARE) {
            // do_scale_factor();
            // Tare Scale
            do_tare_scale();
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

  FillerState lastFillerState = (FillerState)-1;

  for (;;) {
    bool needRedraw = false;

    if (appState != lastState || currentSelection != lastSelection) {
      needRedraw = true;
      switch (appState) {
        case STATE_MAIN_MENU:
          drawMenu();
          break;
        case STATE_FILLER: drawFilller(); break;
        case STATE_PUMP: drawPump(TFT_BLACK); break;
        case STATE_GRAVITY: drawSettingMenu("Final Gravity", beer_gravity, TFT_BLACK); break;
        case STATE_TARE: drawScreen("Tare", TFT_BLACK); break;
        case STATE_CALIBRATION:
          drawSettingMenu("Calibration Weight", calib_weight, TFT_BLACK);
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
        needRedraw = true;
        drawWeight();
        lastDrawnWeight = currentWeight;
        lastDrawnAverage = moving_average;
      }
    }

    // Only delay if something was redrawn
    vTaskDelay(pdMS_TO_TICKS(needRedraw ? 40 : 10));
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
  M5.begin(cfg);

  Serial.begin(115200);
  Wire.begin(21, 22, 100000);  // SDA=21, SCL=22, 100kHz
  delay(1000);

  Serial.println("Scan I2C...");
  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.print("Trouvé périphérique I2C à 0x");
      Serial.println(address, HEX);
    }
  }

  Wire.begin(21, 22);  // SDA, SCL



  if (!nau.begin()) {
    Serial.println("Erreur : NAU7802 non détecté !");
    while (1) vTaskDelay(1000);
  }

  // Config LDO, gain et rate
  nau.setLDO(NAU7802_3V0);
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

  Serial.println("NAU7802 initialisé !");
  M5.Display.setRotation(1);

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
  preferences.clear();

  scale_offset = preferences.getDouble("scale_offset", -954218);
  scale_factor = preferences.getDouble("scale_factor", -1121.379150);
  saved_calib_weight = preferences.getInt("calib_weight", 185);
  saved_beer_gravity = preferences.getFloat("beer_gravity", 1.00);


  calib_weight = saved_calib_weight;
  beer_gravity = saved_beer_gravity;

  // Scale Factor
  Serial.println("Scale Factor now !");
  do_scale_factor();

  Serial.println("Tare now !");
  // Tare Scale
  do_tare_scale();

  // Créer la queue
  weightQueue = xQueueCreate(SCALE_SPS, sizeof(float));
  if (weightQueue == NULL) {
    Serial.println("ERREUR: Impossible de créer la queue");
  } else {
    Serial.println("Queue créée avec succès");
  }

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
  xTaskCreatePinnedToCore(taskButtons, "TaskButtons", 4096, nullptr, 2, &taskButtonsHandle, 0);
  xTaskCreatePinnedToCore(taskMenu, "TaskMenu", 4096, nullptr, 1, &taskMenuHandle, 1);
  xTaskCreatePinnedToCore(taskDisplay, "TaskDisplay", 4096, nullptr, 2, &taskDisplayHandle, 1);
  xTaskCreatePinnedToCore(taskNAU7802, "NAU7802_Task", 4096, nullptr, 3, &taskNAU7802Handle, 1);
  xTaskCreatePinnedToCore(taskFiller, "Filler_Task", 4096, nullptr, 3, &taskFillerHandle, 1);

  Serial.println("Starting Filler Machine");
}


void loop() {
  // Tout est géré par FreeRTOS
}