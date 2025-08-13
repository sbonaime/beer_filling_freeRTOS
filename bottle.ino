
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

  image_in_memory.setTextColor(TFT_BLUE );
  image_in_memory.setCursor(5, 30);
  image_in_memory.println("BOTTLE");

  image_in_memory.setTextColor(TFT_RED );
  image_in_memory.setCursor(5, 100);
  image_in_memory.println("FILLING");
  image_in_memory.pushSprite(0, 0);


  bottle_position_x = 250, bottle_position_y = 100;
  bottle_scaling = 1.7;



  // Remplissage virtuel
  for (int i = 0; i < 100; i++) {
    draw_beer(bottle_position_x, bottle_position_y, i, 100, TFT_BEER,  TFT_BLACK);
    delay(20);
  }
  delay(1000);

  // Clear intro screen
  image_in_memory.createSprite(M5.Display.width(), M5.Display.height());
  image_in_memory.fillScreen(TFT_BLACK);
  image_in_memory.pushSprite(0, 0);
  appState = STATE_MENU;
  drawMenu();
}

void draw_beer(int x_position, int y_position, int poids_actuel, int poids_final, uint32_t liquid_color, uint32_t bottle_color) {
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

  D1 = 24 * bottle_scaling;
  D2 = 9 * bottle_scaling;
  H1 = 45 * bottle_scaling;
  HG = 18 * bottle_scaling;
  H2 = 9 * bottle_scaling;

  b = (D1 - D2) / 2, ax = b, ay = 0, bx = b + D2, by = 0, cx = bx, cy = H2, dx = D1;
  dy = H2 + HG, ex = D1, ey = H1 + HG + H2, fx = 0, fy = H1 + HG + H2, gx = 0, gy = H2 + HG, hx = ax, hy = H2;

  beer_in_memory.fillScreen(TFT_WHITE);

  if ((poids_actuel <= poids_final) && (poids_actuel >= 0)) {
    new_beer_height = poids_actuel * (H1 + HG) / poids_final;

    if (new_beer_height >= old_beer_height) {
      // Create a 8 bit sprite 80 pixels wide, 35 high (2800 bytes of RAM needed)
      beer_in_memory.setColorDepth(8);

      // image_in_memory.createSprite(IWIDTH, IHEIGHT);
      beer_in_memory.createSprite(D1 + 1, H1 + HG + H2 + 1);

      // Fill it with black (this will be the transparent colour this time)
      beer_in_memory.fillSprite(transparent_color);

      // image_in_memory.fillRect(position_x+1, position_y + H1 + H2 + HG - new_h, D1 - 1, new_h, TFT_ORANGE);
      beer_in_memory.fillRect(1, H1 + H2 + HG - new_beer_height, D1 - 1, new_beer_height, liquid_color);

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
      beer_in_memory.pushSprite(x_position, y_position, transparent_color);

      // if (beer) {
      //   // We put the bottle at the right place
      //   beer_in_memory.setPivot(bottle_position_x + (D1 / 2) + 5, bottle_position_y + H2 + H1 - 15);  // Set pivot to middle of TFT screen

      //   // Delete it to free memory
      //   beer_in_memory.deleteSprite();

      //   old_beer_height = new_beer_height;

      //   // BEER Sprite
      //   beer_in_memory.setColorDepth(1);
      //   // beer_in_memory.createSprite(IWIDTH, IHEIGHT);
      //   beer_in_memory.createSprite(H1, D1);
      //   beer_in_memory.setFont(&fonts::Font4);
      //   // Fill it with black (this will be the transparent colour this time)
      //   beer_in_memory.fillSprite(TFT_BLACK);

      //   beer_in_memory.setTextColor(TFT_WHITE);
      //   beer_in_memory.setTextDatum(ML_DATUM);
      //   beer_in_memory.drawString("BEER", 20, D1 / 2, 4);  // Plot text, font 4, in Sprite at 30, 15
      //   beer_in_memory.pushRotated(270, TFT_BLACK);
      // }
    }
  }
}