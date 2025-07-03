#ifndef SHARED_H
#define SHARED_H


// Screen dimensions and UI layout
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320
#define STATUS_BAR_Y_OFFSET 20
#define STATUS_BAR_HEIGHT 16
#define ICON_SIZE 16

// Touch mapping macros
#define TS_MINX 300
#define TS_MAXX 3800
#define TS_MINY 300
#define TS_MAXY 3800
#define MAP_X(x) map(x, TS_MINX, TS_MAXX, 0, SCREEN_WIDTH)
#define MAP_Y(y) map(y, TS_MINY, TS_MAXY, 0, SCREEN_HEIGHT)

// Button pin numbers
#define BTN_LEFT 4
#define BTN_RIGHT 5
#define BTN_UP 6
#define BTN_DOWN 3
#define BTN_SELECT   7


const uint16_t ORANGE = 0xfbe4;
const uint16_t GRAY = 0x8410;
const uint16_t BLUE = 0x001F;
const uint16_t RED = 0xF800;
const uint16_t GREEN = 0x07E0;
const uint16_t BLACK = 0x0000;
const uint16_t WHITE = 0xFFFF;
const uint16_t LIGHT_GRAY = 0xC618;
const uint16_t DARK_GRAY = 0x4208;

#define TFT_DARKBLUE  0x3166  
#define TFT_LIGHTBLUE 0x051F  
#define TFTWHITE     0xFFFF  
#define TFT_GRAY      0x8410  
#define SELECTED_ICON_COLOR 0xfbe4

void displaySubmenu();

extern bool in_sub_menu;                
extern bool feature_active;             
extern bool submenu_initialized;        
extern bool is_main_menu;              
extern bool feature_exit_requested;
float readBatteryVoltage(); // Function declaration
//void drawStatusBar(float voltage, bool charging = false); // Function declaration



#endif // SHARED_H
