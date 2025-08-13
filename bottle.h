// Bottle parameters
int bottle_position_x, bottle_position_y, D1, D2, H1, HG, H2;
int new_beer_height, old_beer_height;
int b, ax, ay, bx, by, cx, cy, dx, dy, ex, ey, fx, fy, gx, gy, hx, hy;
int i = 0;
float bottle_scaling = 1.7;
float poids = 0.0, old_poids = 0.0, poids_bouteille = 0.0, poids_final = 0.0, poids_estime = 0.0, old_poids_estime = 0.0, max_tmp_h = 0.0;
float mg_to_fill = 0.0, ml_to_fill = 360.0;

// Elements pour UI
#define TFT_GREY 0x5AEB
#define TFT_RED_STOP 0xE36D  // Couleur boutton stop