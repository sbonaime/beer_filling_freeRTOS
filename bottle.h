float saved_beer_gravity;
float saved_scale_calibration_weight;

float beer_gravity;
float scale_offset;
float scale_factor;
float scale_calibration_weight;
float bottle_weight;
float ml_to_fill;
String bottle_size;
float old_beer_height, new_beer_height;



// Bottle parameters
int bottle_position_x, bottle_position_y, D1, D2, H1, HG, H2;
int b, ax, ay, bx, by, cx, cy, dx, dy, ex, ey, fx, fy, gx, gy, hx, hy;
int i = 0;
float poids = 0.0, old_poids = 0.0, poids_bouteille = 0.0, poids_final = 0.0, poids_estime = 0.0, old_poids_estime = 0.0, max_tmp_h = 0.0;
float mg_to_fill = 0.0;
float bottle_scaling = 1.7;

uint32_t transparent_color = TFT_TRANSPARENT;

// Elements pour UI
#define TFT_GREY 0x5AEB
#define TFT_RED_STOP 0xE36D  // Couleur boutton stop
#define TFT_BEER 0xD9840D    // Couleur biere


// Fifo
#define MAX_FIFO_SIZE 10
float moving_average;
float moving_average_sum;

float lastDrawnWeight;
float lastDrawnAverage;
