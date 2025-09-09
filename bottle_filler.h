// Couleurs 8bits
// https://www.rapidtables.com/web/color/RGB_Color.html




// Pins
const uint8_t pin_SDA = 16;
const uint8_t pin_SCL = 17;
const int pwmPin = 26;         // GPIO de sortie (ex: 26, 25, 19... éviter 34-39 car entrée seule)



// PWM parameters
const int pwmChannel = 0;      // Canal PWM (0-15)
const int pwmFreq = 10000;     // Fréquence : 10 kHz
const int pwmResolution = 10;  // Résolution : 10 bits (0-1023)

const int low_duty = 70;    // Résolution : 10 bits (0-1023)
const int full_duty = 100;  // Résolution : 10 bits (0-1023)
int last_percent_duty;


// Bottle parameters
int bottle_position_x, bottle_position_y, D1, D2, H1, HG, H2;
int b, ax, ay, bx, by, cx, cy, dx, dy, ex, ey, fx, fy, gx, gy, hx, hy;
int i = 0;
float poids = 0.0, old_poids = 0.0, poids_bouteille = 0.0, poids_final = 0.0, poids_estime = 0.0, old_poids_estime = 0.0, max_tmp_h = 0.0;
float mg_to_fill = 0.0;
float bottle_scaling = 1.7;


// Elements pour UI
#define TFT_GREY 0x5AEB
#define TFT_RED_STOP 0xE36D  // Red boutton stop color
#define TFT_BEER 0xD9840D    // Beer color
const int transparent_color = 0x0120;

float kf_weight;

// Fifo
float moving_average;
float last_moving_average;
float moving_average_sum;

float lastDrawnWeight;
float lastDrawnAverage;
float currentWeight;
float weight;

// scale and NAU702 Parameters
float scale, offset;
int calib_weight;
int raw_data;
bool calibrationInProgress;

float bottle_weight;
float ml_to_fill;
float old_beer_height, new_beer_height;
int fill_percentage, last_fill_percentage;
int nb_values_read;
float saved_beer_gravity;
int saved_calib_weight;
float beer_gravity;


constexpr int SCALE_SPS = 40;
constexpr int MAX_FIFO_SIZE = SCALE_SPS;

String bottle_size;
constexpr float BOTTLE_33CL_MIN = 190.0f;
constexpr float BOTTLE_33CL_MAX = 265.0f;
constexpr float BOTTLE_50CL_MIN = 265.0f;
constexpr float BOTTLE_50CL_MAX = 430.0f;
constexpr float BOTTLE_75CL_MIN = 500.0f;
constexpr float BOTTLE_75CL_MAX = 600.0f;