static inline void drawSettingMenu(const char* title, float value) {

  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setFont(&fonts::FreeMonoBold18pt7b);
  M5.Display.setTextSize(1);

  M5.Display.drawString(title, (M5.Display.width() / 2), (M5.Display.height() / 2) - 80);
  updateValue(value);
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
}

static inline void updateValue(float value) {
  value_canvas.fillScreen(TFT_BLACK);
  value_canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  value_canvas.setFont(&fonts::Font4);

  if (value < 2) {  // Gravity
    value_canvas.drawFloat(value, 3, 0, 0);
  } else {  // Calibration weight
    value_canvas.drawNumber(value, 0, 0);
  }
  value_canvas.pushSprite((M5.Display.width() / 2) - 10, (M5.Display.height() / 2) - 10);
}


static inline void drawFilling(int poids, int poids_final) {
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

  draw_beer_bottle(bottle_position_x, bottle_position_y, poids, poids_final, TFT_BEER, TFT_BLACK, true);
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


static inline void drawTwoLines(const char* line_1, const char* line_2) {

  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setFont(&fonts::FreeMonoBold18pt7b);
  M5.Display.setTextSize(1);

  M5.Display.drawString(line_1, 10, M5.Display.height() / 2);
  M5.Display.drawString(line_2, 10, 37 + M5.Display.height() / 2);
}


static inline void drawWeight() {
  weight_in_memory.fillScreen(TFT_BLACK);
  weight_in_memory.setFont(&fonts::FreeMonoBold12pt7b);
  weight_in_memory.setTextSize(1);
  weight_in_memory.setCursor(0, 0);

  if (xSemaphoreTake(xMutex, (TickType_t)10) == pdTRUE) {
    float localCurrentWeight = kf_weight;
    float localMovingAverage = moving_average;
    xSemaphoreGive(xMutex);

    weight_in_memory.printf("%.1fg", fabs(localCurrentWeight));

    weight_in_memory.setCursor(100, 0);
    weight_in_memory.printf("Avg : %.1fg", fabs(localMovingAverage));

    //  weight_in_memory.pushSprite(M5.Display.width(), 10);
    //weight_in_memory.pushSprite(M5.Display.width(), 10);
    weight_in_memory.pushSprite(10, 10);
  } else {
    Serial.println("Timeout mutex drawWeight");
  }
}
