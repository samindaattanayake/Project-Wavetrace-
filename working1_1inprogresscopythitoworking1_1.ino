#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <RF24.h>
#include <ELECHOUSE_CC1101_ESP32DIV.h>
#include <SPI.h>
#include <Wire.h>
#include <EEPROM.h>
#include <RCSwitch.h>
#include <PCF8574.h>
#include <arduinoFFT.h>
#include "shared.h" // For feature_active, readBatteryVoltage, drawStatusBar


#include "Touchscreen.h"  // Provides setupTouchscreen() declaration


#include "icon.h"
#include "utils.h"
#include "wificonfig.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"

// Pin Definitions
#define CC1101_CS 5
#define CC1101_MOSI 23
#define CC1101_MISO 19
#define CC1101_SCK 18
#define CC1101_GDO0 4
#define CC1101_GDO2 17
#define TFT_BL 27

#define TOUCH_CLK 25
#define TOUCH_CS 33
#define TOUCH_DIN 32
#define TOUCH_DOUT 34
#define TOUCH_IRQ -1

#define I2C_SDA 21
#define I2C_SCL 22
#define PCF8574_ADDR 0x20
#define RX_PIN 35
#define TX_PIN 0
#define NRF_CE 16
#define NRF_CSN 26

#define BTN_LEFT   4
#define BTN_RIGHT  5
#define BTN_UP     6
#define BTN_DOWN   3
#define BTN_SELECT   7


// Color definitions
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#define TFT_RED 0xF800
#define TFT_GREEN 0x07E0
#define TFT_BLUE 0x001F
#define TFT_CYAN 0x07FF
#define TFT_YELLOW 0xFFE0
#define TFT_MAGENTA 0xF81F
#define ORANGE 0xFD20
#define DARK_GRAY 0x4208

// Screen dimensions
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

#define STATUS_BAR_Y_OFFSET 20
#define STATUS_BAR_HEIGHT 16
#define ICON_SIZE 16

// EEPROM Configuration
#define EEPROM_SIZE 1440
#define ADDR_VALUE 1280
#define ADDR_BITLEN 1284
#define ADDR_PROTO 1286
#define ADDR_FREQ 1288
#define ADDR_PROFILE_START 1300
#define MAX_PROFILES 5
#define MAX_NAME_LENGTH 16
#define PROFILE_SIZE sizeof(Profile)

// Touch calibration
#define TS_MINX 300
#define TS_MAXX 3800
#define TS_MINY 300
#define TS_MAXY 3800
#define MAP_X(x) map(x, TS_MINX, TS_MAXX, 0, 240)
#define MAP_Y(y) map(y, TS_MINY, TS_MAXY, 0, 320)

// Global Objects
TFT_eSPI tft = TFT_eSPI();
//XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
RF24 radio(NRF_CE, NRF_CSN);
PCF8574 pcf(PCF8574_ADDR);
RCSwitch mySwitch = RCSwitch();

// FFT Objects
const uint16_t samplesSUB = 256;
double vRealSUB[samplesSUB];
double vImagSUB[samplesSUB];
const double FrequencySUB = 5000;
ArduinoFFT FFTSUB = ArduinoFFT(vRealSUB, vImagSUB, samplesSUB, FrequencySUB, true);

// Main mode enumeration
enum MainMode { 
    MODE_MENU, 
    MODE_CC1101_REPLAY, 
    MODE_CC1101_PROFILE, 
    MODE_CC1101_JAMMER, 
    MODE_NRF_SPECTRUM,
    MODE_SCANNER_24GHZ, // <--- Add this
 MODE_WIFI_MENU,         // <-- NEW
    MODE_WIFI_PACKETMON     // <-- NEW
    

};
MainMode currentMode = MODE_MENU;

// Global Variables
//bool feature_active = true;
bool feature_exit_requested = false;

// Menu button layout
#define BTN_WIDTH 200
#define BTN_HEIGHT 35
#define BTN_X 20
#define BTN_Y_START 40
#define BTN_SPACING 45

// Battery voltage reading (placeholder)


// Status bar drawing function
/*void drawStatusBar(float voltage, bool charging) {
    tft.fillRect(0, 0, SCREEN_WIDTH, 19, TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(5, 5);
    tft.print("SubGHz Tool");
    tft.setCursor(SCREEN_WIDTH - 50, 5);
    tft.printf("%.1fV", voltage);
}*/

void drawButton(int x, int y, const char* label, uint16_t color) {
        tft.fillRect(x, y, BTN_WIDTH, BTN_HEIGHT, color);
        tft.setTextColor(TFT_WHITE, color);
        tft.setTextSize(2);
        tft.setCursor(x + 10, y + 10);
        tft.print(label);
    }

/* Touchscreen setup
void setupTouchscreen() {
    touchscreenSPI.begin(TOUCH_CLK, TOUCH_DOUT, TOUCH_DIN, TOUCH_CS);
    ts.begin(touchscreenSPI);
    ts.setRotation(0);
}
*/



// Profile structure
struct Profile {
    uint32_t frequency;
    unsigned long value;
    int bitLength;
    int protocol;
    char name[MAX_NAME_LENGTH];
};

// Safely reinitialize TFT after CC1101 operations
void reinitializeTFT() {
    delay(10);
    tft.init();
    tft.setRotation(0);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
}

// Main menu UI
void drawModeSelection() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(40, 30);
    
    // Draw menu buttons
    String menuItems[] = {"Replay Attack", "Saved Profiles", "SubGHz Jammer", "NRF24L01+ Spectrum" , "2.4GHz Scanner","WiFi"};
    
    for (int i = 0; i < 6; i++) {
        int y = BTN_Y_START + (i * BTN_SPACING);
        tft.fillRoundRect(BTN_X, y, BTN_WIDTH, BTN_HEIGHT, 8, TFT_BLUE);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(1);
        int textX = BTN_X + (BTN_WIDTH - menuItems[i].length() * 6) / 2;
        tft.setCursor(textX, y + 12);
        tft.println(menuItems[i]);
    }
    
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, 280);
    //tft.println("Touch to select mode");
}

// Handle menu touch
void handleMenuTouch() {
    if (ts.touched()) {
        TS_Point p = ts.getPoint();
        int x = MAP_X(p.x);
        int y = MAP_Y(p.y);
        
        if (x >= BTN_X && x <= BTN_X + BTN_WIDTH) {
            for (int i = 0; i < 6; i++) {
                int 
                btnY = BTN_Y_START + (i * BTN_SPACING);
                if (y >= btnY && y <= btnY + BTN_HEIGHT) {
                    switch (i) {
                        case 0: currentMode = MODE_CC1101_REPLAY; break;
                        case 1: currentMode = MODE_CC1101_PROFILE; break;
                        case 2: currentMode = MODE_CC1101_JAMMER; break;
                        case 3: currentMode = MODE_NRF_SPECTRUM; break;
                        case 4: currentMode = MODE_SCANNER_24GHZ; break;
                        case 5: currentMode = MODE_WIFI_MENU; break; // <-- New WiFi menu


                    }
                    break;
                }
            }
        }
        delay(300);
    }
}

void drawWiFiMenu() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(40, 30);
    tft.println("WiFi Tools");
    String wifiMenuItems[] = {
        "WiFi Packet Monitor",
        "Back"
    };
    for (int i = 0; i < 2; i++) {
        int y = BTN_Y_START + (i * BTN_SPACING);
        tft.fillRoundRect(BTN_X, y, BTN_WIDTH, BTN_HEIGHT, 8, TFT_BLUE);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(1);
        int textX = BTN_X + (BTN_WIDTH - wifiMenuItems[i].length() * 6) / 2;
        tft.setCursor(textX, y + 12);
        tft.println(wifiMenuItems[i]);
    }
}

void handleWiFiMenuTouch() {
    if (ts.touched()) {
        TS_Point p = ts.getPoint();
        int x = MAP_X(p.x);
        int y = MAP_Y(p.y);
        if (x >= BTN_X && x <= BTN_X + BTN_WIDTH) {
            for (int i = 0; i < 2; i++) {
                int btnY = BTN_Y_START + (i * BTN_SPACING);
                if (y >= btnY && y <= btnY + BTN_HEIGHT) {
                    switch (i) {
                        case 0: currentMode = MODE_WIFI_PACKETMON; break; // WiFi Packet Monitor
                        case 1: currentMode = MODE_MENU; drawModeSelection(); break; // Back
                    }
                    break;
                }
            }
        }
        delay(300);
    }
}


// Radio switching functions
void switchToCC1101() {
    if (currentMode == MODE_NRF_SPECTRUM) {
        radio.powerDown();
        delay(50);
    }
    
    ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
    ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
    ELECHOUSE_cc1101.Init();
    reinitializeTFT();
}

void switchToNRF() {
    ELECHOUSE_cc1101.setSidle();
    ELECHOUSE_cc1101.goSleep();
    delay(50);
    
    SPI.end();
    SPI.begin(18, 19, 23, NRF_CSN);
    delay(10);
    
    if (!radio.begin()) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_RED);
        tft.setTextSize(2);
        tft.setCursor(10, 10);
        tft.println("NRF24L01+ Init Failed!");
        delay(2000);
        currentMode = MODE_MENU;
        drawModeSelection();
        return;
    }
    
    radio.setAutoAck(false);
    radio.stopListening();
    radio.setDataRate(RF24_1MBPS);
    radio.setPALevel(RF24_PA_MIN);
}

/*
 * ReplayAttack Namespace - Complete Implementation
 */
namespace replayat {
    static bool uiDrawn = false;
    double attenuation_num = 10;
    unsigned int sampling_period;
    unsigned long micro_s;
    uint8_t red[128], green[128], blue[128];
    unsigned int epochSUB = 0;
    int rssi;
    int yshift = 20;
    unsigned long receivedValue = 0;
    int receivedBitLength = 0;
    int receivedProtocol = 0;
    const int rssi_threshold = -75;
    
    static const uint32_t subghz_frequency_list[] = {
        300000000, 303875000, 304250000, 310000000, 315000000, 318000000,
        390000000, 418000000, 433075000, 433420000, 433920000, 434420000,
        434775000, 438900000, 868350000, 915000000, 925000000
    };
    
    int currentFrequencyIndex = 0; // Default to 433.075MHz
    int profileCount = 0;
    
    // Keyboard layout
    const char* keyboardLayout[] = {
        "1234567890",
        "QWERTYUIOP",
        "ASDFGHJKL",
        "ZXCVBNM<-"
    };
    
    const char* randomNames[] = {
        "Signal", "Remote", "KeyFob", "GateOpener", "DoorLock",
        "RFTest", "Profile", "Control", "Switch", "Beacon"
    };
    
    const int numRandomNames = 10;
    int randomNameIndex = 0;
    const int keyWidth = 22;
    const int keyHeight = 22;
    const int keySpacing = 2;
    const int yOffsetStart = 95;
    static bool cursorState = true;
    static unsigned long lastCursorBlink = 0;
    const unsigned long cursorBlinkInterval = 500;
    
    void updateDisplay() {
        uiDrawn = false;
        tft.fillRect(0, 40, 240, 40, TFT_BLACK);
        tft.drawLine(0, 80, 240, 80, TFT_WHITE);
        
        // Frequency
        tft.setCursor(5, 20 + yshift);
        tft.setTextColor(TFT_CYAN);
        tft.print("Freq:");
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(50, 20 + yshift);
        tft.print(subghz_frequency_list[currentFrequencyIndex] / 1000000.0, 2);
        tft.print(" MHz");
        
        // Bit Length
        tft.setCursor(5, 35 + yshift);
        tft.setTextColor(TFT_CYAN);
        tft.print("Bit:");
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(50, 35 + yshift);
        tft.printf("%d", receivedBitLength);
        
        // RSSI
        tft.setCursor(130, 35 + yshift);
        tft.setTextColor(TFT_CYAN);
        tft.print("RSSI:");
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(170, 35 + yshift);
        tft.printf("%d", ELECHOUSE_cc1101.getRssi());
        
        // Protocol
        tft.setCursor(130, 20 + yshift);
        tft.setTextColor(TFT_CYAN);
        tft.print("Ptc:");
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(170, 20 + yshift);
        tft.printf("%d", receivedProtocol);
        
        // Received Value
        tft.setCursor(5, 50 + yshift);
        tft.setTextColor(TFT_CYAN);
        tft.print("Val:");
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(50, 50 + yshift);
        tft.print(receivedValue);
        
        ELECHOUSE_cc1101.setSidle();
        ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[currentFrequencyIndex] / 1000000.0);
        ELECHOUSE_cc1101.SetRx();
    }
    
    void drawInputField(String& inputName) {
        tft.fillRect(10, 55, 220, 25, DARK_GRAY);
        tft.drawRect(9, 54, 222, 27, ORANGE);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(2);
        tft.setCursor(15, 60);
        String displayText = inputName;
        if (cursorState) {
            displayText += "|";
        }
        tft.println(displayText);
    }
    
    void drawKeyboard(String& inputName) {
        tft.fillRect(0, 37, SCREEN_WIDTH, SCREEN_HEIGHT - 37, TFT_BLACK);
        
        // Instructional text
        tft.setTextColor(ORANGE);
        tft.setTextSize(1);
        tft.setCursor(1, 235);
        tft.println("[!] Set a name for the saved profile.");
        tft.setCursor(23, 250);
        tft.println("(max 15 chars)");
        tft.setCursor(1, 275);
        tft.println("[!] Shuffle: Suggests random profile");
        tft.setCursor(23, 290);
        tft.println("names for your signal.");
        
        drawInputField(inputName);
        
        // Draw keyboard
        int yOffset = yOffsetStart;
        for (int row = 0; row < 4; row++) {
            int xOffset = 1;
            for (int col = 0; col < strlen(keyboardLayout[row]); col++) {
                tft.fillRect(xOffset, yOffset, keyWidth, keyHeight, DARK_GRAY);
                tft.setTextColor(TFT_WHITE);
                tft.setTextSize(1);
                tft.setCursor(xOffset + 6, yOffset + 5);
                tft.print(keyboardLayout[row][col]);
                xOffset += keyWidth + keySpacing;
            }
            yOffset += keyHeight + keySpacing;
        }
        // Draw buttons (add this after drawing all keyboard keys)
tft.setTextColor(ORANGE);
tft.setTextSize(1);
tft.setTextDatum(MC_DATUM);

// Back Button
tft.fillRoundRect(5, 195, 70, 25, 4, DARK_GRAY);
tft.drawRoundRect(5, 195, 70, 25, 4, ORANGE);
tft.drawString("Back", 40, 208);

// Shuffle Button
tft.fillRoundRect(85, 195, 70, 25, 4, DARK_GRAY);
tft.drawRoundRect(85, 195, 70, 25, 4, ORANGE);
tft.drawString("Shuffle", 120, 208);

// OK Button
tft.fillRoundRect(165, 195, 70, 25, 4, DARK_GRAY);
tft.drawRoundRect(165, 195, 70, 25, 4, ORANGE);
tft.drawString("OK", 200, 208);

tft.setTextDatum(TL_DATUM); // Reset for other text

    }
    
    String getUserInputName() {
        String inputName = "";
        bool keyboardActive = true;
        drawKeyboard(inputName);
        
        while (keyboardActive) {
            // Blink cursor
            if (millis() - lastCursorBlink >= cursorBlinkInterval) {
                cursorState = !cursorState;
                drawInputField(inputName);
                lastCursorBlink = millis();
            }
            
            if (ts.touched()) {
                TS_Point p = ts.getPoint();
               int x = map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_WIDTH);
    int y = map(p.y, TS_MINY, TS_MAXY, 0, SCREEN_HEIGHT);
                
                // Handle keyboard keys
                int yOffset = yOffsetStart;
                for (int row = 0; row < 4; row++) {
                    int xOffset = 1;
                    for (int col = 0; col < strlen(keyboardLayout[row]); col++) {
                        if (x >= xOffset && x <= xOffset + keyWidth && y >= yOffset && y <= yOffset + keyHeight) {
                            char c = keyboardLayout[row][col];
                            
                            // Highlight key
                            tft.fillRect(xOffset, yOffset, keyWidth, keyHeight, ORANGE);
                            tft.setTextColor(TFT_WHITE);
                            tft.setTextSize(1);
                            tft.setCursor(xOffset + 6, yOffset + 5);
                            tft.print(c);
                            delay(100);
                            tft.fillRect(xOffset, yOffset, keyWidth, keyHeight, DARK_GRAY);
                            tft.setTextColor(TFT_WHITE);
                            tft.setCursor(xOffset + 6, yOffset + 5);
                            tft.print(c);
                            
                            if (c == '<') { // Backspace
                                if (inputName.length() > 0) {
                                    inputName = inputName.substring(0, inputName.length() - 1);
                                }
                            } else if (c == '-') { // Clear
                                inputName = "";
                            } else if (inputName.length() < MAX_NAME_LENGTH - 1) {
                                inputName += c;
                            }
                            
                            drawInputField(inputName);
                            delay(200);
                        }
                        xOffset += keyWidth + keySpacing;
                    }
                    yOffset += keyHeight + keySpacing;
                }
                
                // Handle buttons
                if (x >= 5 && x <= 75 && y >= 195 && y <= 210) { // Back
                    keyboardActive = false;
                    inputName = "";
                    tft.fillScreen(TFT_BLACK);
                    updateDisplay();
                }
                
                if (x >= 85 && x <= 155 && y >= 195 && y <= 210) { // Shuffle
                    inputName = randomNames[randomNameIndex];
                    randomNameIndex = (randomNameIndex + 1) % numRandomNames;
                    drawInputField(inputName);
                    delay(200);
                }
                
                if (x >= 165 && x <= 235 && y >= 195 && y <= 210) { // OK
                    if (inputName.length() > 0) {
                        keyboardActive = false;
                        return inputName;
                    } else {
                        tft.fillRect(10, 80, 220, 10, TFT_BLACK);
                        tft.setTextColor(TFT_RED);
                        tft.setTextSize(1);
                        tft.setCursor(10, 85);
                        tft.println("Name cannot be empty!");
                        tft.setTextColor(TFT_WHITE);
                        delay(500);
                        drawInputField(inputName);
                    }
                    delay(200);
                }
            }
            delay(10);
        }
        return inputName;
    }
    
    void sendSignal() {
        mySwitch.disableReceive();
        delay(100);
        mySwitch.enableTransmit(TX_PIN);
        ELECHOUSE_cc1101.SetTx();
        
        tft.fillRect(0, 40, 240, 37, TFT_BLACK);
        tft.setCursor(10, 30 + yshift);
        tft.print("Sending...");
        tft.setCursor(10, 40 + yshift);
        tft.print(receivedValue);
        
        mySwitch.setProtocol(receivedProtocol);
        mySwitch.send(receivedValue, receivedBitLength);
        delay(500);
        
        tft.fillRect(0, 40, 240, 37, TFT_BLACK);
        tft.setCursor(10, 30 + yshift);
        tft.print("Done!");
        
        ELECHOUSE_cc1101.SetRx();
        mySwitch.disableTransmit();
        delay(100);
        mySwitch.enableReceive(RX_PIN);
        delay(500);
        updateDisplay();
    }
    
    void do_sampling() {
        micro_s = micros();
        #define ALPHA 0.2
        float ewmaRSSI = -50;
        
        for (int i = 0; i < samplesSUB; i++) {
            int rssi = ELECHOUSE_cc1101.getRssi();
            rssi += 100;
            ewmaRSSI = (ALPHA * rssi) + ((1 - ALPHA) * ewmaRSSI);
            vRealSUB[i] = ewmaRSSI * 2;
            vImagSUB[i] = 1;
            while (micros() < micro_s + sampling_period);
            micro_s += sampling_period;
        }
        
        double mean = 0;
        for (uint16_t i = 0; i < samplesSUB; i++)
            mean += vRealSUB[i];
        mean /= samplesSUB;
        for (uint16_t i = 0; i < samplesSUB; i++)
            vRealSUB[i] -= mean;
        
        micro_s = micros();
        FFTSUB.windowing(vRealSUB, samplesSUB, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
        FFTSUB.compute(vRealSUB, vImagSUB, samplesSUB, FFT_FORWARD);
        FFTSUB.complexToMagnitude(vRealSUB, vImagSUB, samplesSUB);
        
        unsigned int left_x = 120;
        unsigned int graph_y_offset = 81;
        int max_k = 0;
        
        for (int j = 0; j < samplesSUB >> 1; j++) {
            int k = vRealSUB[j] / attenuation_num;
            if (k > max_k) max_k = k;
            if (k > 127) k = 127;
            
            unsigned int color = red[k] << 11 | green[k] << 5 | blue[k];
            unsigned int vertical_x = left_x + j;
            tft.drawPixel(vertical_x, epochSUB + graph_y_offset, color);
        }
        
        for (int j = 0; j < samplesSUB >> 1; j++) {
            int k = vRealSUB[j] / attenuation_num;
            if (k > max_k) max_k = k;
            if (k > 127) k = 127;
            
            unsigned int color = red[k] << 11 | green[k] << 5 | blue[k];
            unsigned int mirrored_x = left_x - j;
            tft.drawPixel(mirrored_x, epochSUB + graph_y_offset, color);
        }
        
        double tattenuation = max_k / 127.0;
        if (tattenuation > attenuation_num)
            attenuation_num = tattenuation;
        delay(10);
    }
    
    void readProfileCount() {
        EEPROM.get(ADDR_PROFILE_START - sizeof(int), profileCount);
        if (profileCount > MAX_PROFILES || profileCount < 0) {
            profileCount = 0;
        }
    }
    
    void saveProfile() {
        readProfileCount();
        if (profileCount < MAX_PROFILES) {
            String customName = getUserInputName();
            tft.setTextSize(1);
            
            Profile newProfile;
            newProfile.frequency = subghz_frequency_list[currentFrequencyIndex];
            newProfile.value = receivedValue;
            newProfile.bitLength = receivedBitLength;
            newProfile.protocol = receivedProtocol;
            strncpy(newProfile.name, customName.c_str(), MAX_NAME_LENGTH - 1);
            newProfile.name[MAX_NAME_LENGTH - 1] = '\0';
            
            int addr = ADDR_PROFILE_START + (profileCount * PROFILE_SIZE);
            EEPROM.put(addr, newProfile);
            EEPROM.commit();
            
            profileCount++;
            EEPROM.put(ADDR_PROFILE_START - sizeof(int), profileCount);
            EEPROM.commit();
            
            tft.fillScreen(TFT_BLACK);
            tft.setCursor(10, 30 + yshift);
            tft.print("Profile saved!");
            tft.setCursor(10, 40 + yshift);
            tft.print("Name: ");
            tft.print(newProfile.name);
            tft.setCursor(10, 50 + yshift);
            tft.print("Profiles saved: ");
            tft.println(profileCount);
        } else {
            tft.fillScreen(TFT_BLACK);
            tft.setCursor(10, 30 + yshift);
            tft.print("Profile storage full!");
        }
        
        delay(2000);
        updateDisplay();
        float currentBatteryVoltage = readBatteryVoltage();
        drawStatusBar(currentBatteryVoltage, false);
    }

    void loadProfileCount() {
    EEPROM.get(ADDR_PROFILE_START, profileCount);
    if (profileCount > MAX_PROFILES) {
        profileCount = MAX_PROFILES;  
    }
}

    
   void runUI() {
    #define ICON_NUM 5
    static int iconX[ICON_NUM] = {90, 130, 170, 210, 10};
    static int iconY = STATUS_BAR_Y_OFFSET;
    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_sort_up_plus,
        bitmap_icon_sort_down_minus,
        bitmap_icon_antenna,
        bitmap_icon_floppy,
        bitmap_icon_go_back
    };

    if (!uiDrawn) {
        tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);
        for (int i = 0; i < ICON_NUM; i++) {
            tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_WHITE);
        }
        uiDrawn = true;
    }

    static unsigned long lastAnimationTime = 0;
    static int animationState = 0;
    static int activeIcon = -1;

    if (animationState > 0 && millis() - lastAnimationTime >= 150) {
        if (animationState == 1) {
            tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, TFT_WHITE);
            animationState = 2;
            switch (activeIcon) {
                case 0:
                    currentFrequencyIndex = (currentFrequencyIndex + 1) % (sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]));
                    ELECHOUSE_cc1101.setSidle();
                    ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[currentFrequencyIndex] / 1000000.0);
                    ELECHOUSE_cc1101.SetRx();
                    EEPROM.put(ADDR_FREQ, currentFrequencyIndex);  
                    EEPROM.commit();
                    updateDisplay();
                    break;
                case 1: 
                    currentFrequencyIndex = (currentFrequencyIndex - 1 + (sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]))) % (sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]));
                    ELECHOUSE_cc1101.setSidle();
                    ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[currentFrequencyIndex] / 1000000.0);
                    ELECHOUSE_cc1101.SetRx();
                    EEPROM.put(ADDR_FREQ, currentFrequencyIndex);  
                    EEPROM.commit();
                    updateDisplay();
                    break;
                case 2: 
                    sendSignal();
                    break;
                case 3: 
                    saveProfile();
                    break;
                case 4: // Back icon action (exit to submenu)
                    feature_exit_requested = true;
                    break;
            }
        } else if (animationState == 2) {
            animationState = 0;
            activeIcon = -1;
        }
        lastAnimationTime = millis();  
    }

    if (ts.touched() && feature_active) {
        TS_Point p = ts.getPoint();
        int x = map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_WIDTH);
        int y = map(p.y, TS_MINY, TS_MAXY, 0, SCREEN_HEIGHT);
           //Serial.printf("Touch at: x=%d, y=%d\n", x, y);

        
        if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
            for (int i = 0; i < ICON_NUM; i++) {
                if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
                    tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
                    animationState = 1;
                    activeIcon = i;
                    lastAnimationTime = millis();
                    break;
                }
            }
        }
    }
}

    
    void ReplayAttackSetup() {

        

    pcf.pinMode(4, INPUT_PULLUP); // Left button
    pcf.pinMode(5, INPUT_PULLUP); // Right button
    pcf.pinMode(6, INPUT_PULLUP); // Up button
    pcf.pinMode(3, INPUT_PULLUP); // Down butto
        Serial.begin(115200);
        tft.fillScreen(TFT_BLACK);
        tft.setRotation(0);
       :: setupTouchscreen();
        
        ELECHOUSE_cc1101.Init();
        ELECHOUSE_cc1101.SetRx();
        mySwitch.enableReceive(RX_PIN);
        mySwitch.enableTransmit(TX_PIN);
        
        EEPROM.begin(EEPROM_SIZE);
        readProfileCount();
        
        EEPROM.get(ADDR_VALUE, receivedValue);
        EEPROM.get(ADDR_BITLEN, receivedBitLength);
        EEPROM.get(ADDR_PROTO, receivedProtocol);
        EEPROM.get(ADDR_FREQ, currentFrequencyIndex);
        
        sampling_period = round(1000000 * (1.0 / FrequencySUB));
        
        // Initialize color palette
        for (int i = 0; i < 32; i++) {
            red[i] = i / 2;
            green[i] = 0;
            blue[i] = i;
        }
        
        for (int i = 32; i < 64; i++) {
            red[i] = i / 2;
            green[i] = 0;
            blue[i] = 63 - i;
        }
        
        for (int i = 64; i < 96; i++) {
            red[i] = 31;
            green[i] = (i - 64) * 2;
            blue[i] = 0;
        }
        
        for (int i = 96; i < 128; i++) {
            red[i] = 31;
            green[i] = 63;
            blue[i] = i - 96;
        }
        
        float currentBatteryVoltage = readBatteryVoltage();
        drawStatusBar(currentBatteryVoltage, false);
        updateDisplay();
        uiDrawn = false;
    }
    
    void ReplayAttackLoop() {
        runUI();
        static unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 200;

    int btnLeftState = pcf.digitalRead(BTN_LEFT);
    int btnRightState = pcf.digitalRead(BTN_RIGHT);
    int btnSelectState = pcf.digitalRead(BTN_UP);
    int btndownState = pcf.digitalRead(BTN_DOWN);
    
    do_sampling();
    delay(10);
    epochSUB++;
    
    if (epochSUB >= tft.width())
      epochSUB = 0;

    if (btnRightState == LOW && millis() - lastDebounceTime > debounceDelay) {
        currentFrequencyIndex = (currentFrequencyIndex + 1) % (sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]));
        ELECHOUSE_cc1101.setSidle();
        ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[currentFrequencyIndex] / 1000000.0);
        ELECHOUSE_cc1101.SetRx();
        EEPROM.put(ADDR_FREQ, currentFrequencyIndex);  
        EEPROM.commit();
        updateDisplay();
        lastDebounceTime = millis();
    }
    
    if (btnLeftState == LOW && millis() - lastDebounceTime > debounceDelay) {       
        currentFrequencyIndex = (currentFrequencyIndex - 1 + (sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]))) % (sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]));
        ELECHOUSE_cc1101.setSidle();
        ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[currentFrequencyIndex] / 1000000.0);
        ELECHOUSE_cc1101.SetRx();
        EEPROM.put(ADDR_FREQ, currentFrequencyIndex);  
        EEPROM.commit();
        updateDisplay();
        lastDebounceTime = millis();
    }

    if (mySwitch.available()) { 
        receivedValue = mySwitch.getReceivedValue(); 
        receivedBitLength = mySwitch.getReceivedBitlength(); 
        receivedProtocol = mySwitch.getReceivedProtocol(); 

        EEPROM.put(ADDR_VALUE, receivedValue);
        EEPROM.put(ADDR_BITLEN, receivedBitLength);
        EEPROM.put(ADDR_PROTO, receivedProtocol);
        EEPROM.commit();
        
        updateDisplay();
        mySwitch.resetAvailable(); 
    }

     if (btnSelectState == LOW && receivedValue != 0 && millis() - lastDebounceTime > debounceDelay) {
        sendSignal();
        lastDebounceTime = millis();
    }
     if (pcf.digitalRead(BTN_DOWN) == LOW && millis() - lastDebounceTime > debounceDelay) {
         saveProfile();
         lastDebounceTime = millis();
    }
  } 
}


/*
 * SavedProfile Namespace - Complete Implementation
 */
namespace SavedProfile {
    static bool uiDrawn = false;
    int profileCount = 0;
    int currentProfileIndex = 0;
    int yshift = 40;
    
    void updateDisplay() {
        tft.fillRect(0, 40, 240, 280, TFT_BLACK);
        tft.setCursor(5, 5 + yshift);
        tft.setTextColor(TFT_YELLOW);
        tft.print("Saved Profiles");
        
        if (profileCount == 0) {
            tft.setCursor(10, 35 + yshift);
            tft.setTextColor(TFT_WHITE);
            tft.print("No profiles saved.");
            return;
        }
        
        Profile selectedProfile;
        int addr = ADDR_PROFILE_START + (currentProfileIndex * PROFILE_SIZE);
        EEPROM.get(addr, selectedProfile);
        
        if (selectedProfile.value == 0) {
            tft.setCursor(10, 50 + yshift);
            tft.setTextColor(TFT_WHITE);
            tft.print("No valid profile.");
            return;
        }
        
        tft.setCursor(10, 30 + yshift);
        tft.setTextColor(TFT_CYAN);
        tft.printf("Profile %d/%d", currentProfileIndex + 1, profileCount);
        
        tft.setCursor(10, 50 + yshift);
        tft.setTextColor(TFT_WHITE);
        tft.print("Name: ");
        tft.print(selectedProfile.name);
        
        tft.setCursor(10, 70 + yshift);
        tft.printf("Freq: %.2f MHz", selectedProfile.frequency / 1000000.0);
        
        tft.setCursor(10, 90 + yshift);
        tft.printf("Val: %lu", selectedProfile.value);
        
        tft.setCursor(10, 110 + yshift);
        tft.printf("BitLen: %d", selectedProfile.bitLength);
        
        tft.setCursor(10, 130 + yshift);
        tft.printf("Protocol: %d", selectedProfile.protocol);
    }
    
    void transmitProfile(int index) {
        Profile profileToSend;
        int addr = ADDR_PROFILE_START + (index * PROFILE_SIZE);
        EEPROM.get(addr, profileToSend);
        
        ELECHOUSE_cc1101.setSidle();
        ELECHOUSE_cc1101.setMHZ(profileToSend.frequency / 1000000.0);
        
        mySwitch.disableReceive();
        delay(100);
        mySwitch.enableTransmit(TX_PIN);
        ELECHOUSE_cc1101.SetTx();
        
        tft.fillRect(0, 40, 240, 280, TFT_BLACK);
        tft.setCursor(10, 30 + yshift);
        tft.setTextColor(TFT_WHITE);
        tft.print("Sending ");
        tft.print(profileToSend.name);
        tft.print("...");
        tft.setCursor(10, 50 + yshift);
        tft.print("Value: ");
        tft.print(profileToSend.value);
        
        mySwitch.setProtocol(profileToSend.protocol);
        mySwitch.send(profileToSend.value, profileToSend.bitLength);
        delay(500);
        
        tft.fillRect(0, 40, 240, 280, TFT_BLACK);
        tft.setCursor(10, 30 + yshift);
        tft.print("Done!");
        
        ELECHOUSE_cc1101.SetRx();
        mySwitch.disableTransmit();
        delay(100);
        mySwitch.enableReceive(RX_PIN);
        delay(500);
        updateDisplay();
    }
    
    void loadProfileCount() {
        EEPROM.get(ADDR_PROFILE_START - sizeof(int), profileCount);
        if (profileCount < 0 || profileCount > MAX_PROFILES) {
            profileCount = 0;
            EEPROM.put(ADDR_PROFILE_START - sizeof(int), profileCount);
            EEPROM.commit();
        }
    }
    
    void deleteProfile(int index) {
        if (index >= profileCount || index < 0) return;
        
        Profile deletedProfile;
        int addr = ADDR_PROFILE_START + (index * PROFILE_SIZE);
        EEPROM.get(addr, deletedProfile);
        
        for (int i = index; i < profileCount - 1; i++) {
            Profile nextProfile;
            int addr = ADDR_PROFILE_START + ((i + 1) * PROFILE_SIZE);
            EEPROM.get(addr, nextProfile);
            
            addr = ADDR_PROFILE_START + (i * PROFILE_SIZE);
            EEPROM.put(addr, nextProfile);
        }
        
        Profile emptyProfile = {0, 0, 0, 0, ""};
        EEPROM.put(ADDR_PROFILE_START + ((profileCount - 1) * PROFILE_SIZE), emptyProfile);
        
        profileCount--;
        EEPROM.put(ADDR_PROFILE_START - sizeof(int), profileCount);
        EEPROM.commit();
        
        if (currentProfileIndex >= profileCount) {
            currentProfileIndex = profileCount - 1;
        }
        
        tft.fillRect(0, 40, 240, 280, TFT_BLACK);
        tft.setCursor(10, 30 + yshift);
        tft.setTextColor(TFT_WHITE);
        tft.print("Removed: ");
        tft.print(deletedProfile.name);
        delay(1000);
        updateDisplay();
    }
    
    void runUI() {
    #define ICON_NUM 5
    static int iconX[ICON_NUM] = {90, 130, 170, 210, 10};
    static int iconY = STATUS_BAR_Y_OFFSET;
    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_sort_up_plus,
        bitmap_icon_sort_down_minus,
        bitmap_icon_antenna,
        bitmap_icon_recycle,
        bitmap_icon_go_back
    };

    if (!uiDrawn) {
        tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);
        for (int i = 0; i < ICON_NUM; i++) {
            tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_WHITE);
        }
        uiDrawn = true;
    }

    static unsigned long lastAnimationTime = 0;
    static int animationState = 0;
    static int activeIcon = -1;

    if (animationState > 0 && millis() - lastAnimationTime >= 150) {
        if (animationState == 1) {
            tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, TFT_WHITE);
            animationState = 2;
            switch (activeIcon) {
                case 0: currentProfileIndex--; break;
                case 1: currentProfileIndex++; break;
                case 2: transmitProfile(currentProfileIndex); break;
                case 3: deleteProfile(currentProfileIndex); break;
                case 4: feature_exit_requested = true; break;
            }
        } else if (animationState == 2) {
            animationState = 0;
            activeIcon = -1;
        }
        lastAnimationTime = millis();
    }

    if (ts.touched() && feature_active) {
        TS_Point p = ts.getPoint();
        int x = map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_WIDTH);
        int y = map(p.y, TS_MINY, TS_MAXY, 0, SCREEN_HEIGHT);
            Serial.printf("Touch at: x=%d, y=%d\n", x, y);

        
        if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
            for (int i = 0; i < ICON_NUM; i++) {
                if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
                    tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
                    animationState = 1;
                    activeIcon = i;
                    lastAnimationTime = millis();
                    break;
                }
            }
        }
    }    //lastTouchCheck = millis();

}

    
    void saveSetup() {
        Serial.begin(115200);
        EEPROM.begin(EEPROM_SIZE);
        loadProfileCount();
        
        tft.setRotation(0);
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_WHITE);
        setupTouchscreen();
        
        float currentBatteryVoltage = readBatteryVoltage();
        drawStatusBar(currentBatteryVoltage, false);
        uiDrawn = false;
        
        ELECHOUSE_cc1101.Init();
        ELECHOUSE_cc1101.SetRx();
        mySwitch.enableReceive(RX_PIN);
        mySwitch.enableTransmit(TX_PIN);
        
        updateDisplay();
    }
    
    void saveLoop() {
        runUI();
        
        static unsigned long lastDebounceTime = 0;
        const unsigned long debounceDelay = 200;
        
        int btnLeftState = pcf.digitalRead(4);
        int btnRightState = pcf.digitalRead(5);
        int btnSelectState = pcf.digitalRead(3);
        int btnUpState = pcf.digitalRead(6);
        
        if (profileCount > 0) {
            if (btnRightState == LOW && millis() - lastDebounceTime > debounceDelay) {
                currentProfileIndex = (currentProfileIndex + 1) % profileCount;
                updateDisplay();
                lastDebounceTime = millis();
            }
            
            if (btnLeftState == LOW && millis() - lastDebounceTime > debounceDelay) {
                currentProfileIndex = (currentProfileIndex - 1 + profileCount) % profileCount;
                updateDisplay();
                lastDebounceTime = millis();
            }
            
            if (btnSelectState == LOW && millis() - lastDebounceTime > debounceDelay) {
                transmitProfile(currentProfileIndex);
                lastDebounceTime = millis();
            }
            
            if (btnUpState == LOW && millis() - lastDebounceTime > debounceDelay) {
                deleteProfile(currentProfileIndex);
                lastDebounceTime = millis();
            }
        }
    }
} // namespace SavedProfile

/*
 * SubGHz Jammer Namespace - Complete Implementation
 */
namespace subjammer {
    static bool uiDrawn = false;
    static unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 200;
    bool jammingRunning = false;
    bool continuousMode = true;
    bool autoMode = false;
    unsigned long lastSweepTime = 0;
    const unsigned long sweepInterval = 1000;
    
    static const uint32_t subghz_frequency_list[] = {
        300000000, 303875000, 304250000, 310000000, 315000000, 318000000,
        390000000, 418000000, 433075000, 433420000, 433920000, 434420000,
        434775000, 438900000, 868350000, 915000000, 925000000
    };
    
    const int numFrequencies = sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]);
    int currentFrequencyIndex = 4;
    float targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
    
    void updateDisplay() {
        int yshift = 20;
        tft.fillRect(0, 40, 240, 80, TFT_BLACK);
        tft.drawLine(0, 79, 235, 79, TFT_WHITE);
        
        // Frequency section
        tft.setTextSize(1);
        tft.setCursor(5, 22 + yshift);
        tft.setTextColor(TFT_CYAN);
        tft.print("Freq:");
        tft.setCursor(40, 22 + yshift);
        
        if (autoMode) {
            tft.setTextColor(ORANGE);
            tft.print("Auto: ");
            tft.setTextColor(TFT_WHITE);
            tft.print(targetFrequency, 1);
            
            // Progress bar
            int progress = map(currentFrequencyIndex, 0, numFrequencies - 1, 0, 240);
            tft.fillRect(0, 60 + yshift, 240, 4, TFT_BLACK);
            tft.fillRect(0, 60 + yshift, progress, 4, ORANGE);
            
            // Blinking sweep indicator
            if (jammingRunning && millis() % 1000 < 500) {
                tft.fillCircle(220, 22 + yshift, 2, TFT_GREEN);
            }
        } else {
            tft.setTextColor(TFT_WHITE);
            tft.print(targetFrequency, 2);
            tft.print(" MHz");
        }
        
        // Mode section
        tft.setCursor(130, 22 + yshift);
        tft.setTextColor(TFT_CYAN);
        tft.print("Mode:");
        tft.setCursor(165, 22 + yshift);
        tft.setTextColor(continuousMode ? TFT_GREEN : TFT_YELLOW);
        tft.print(continuousMode ? "Cont" : "Noise");
        
        // Status section
        tft.setCursor(5, 42 + yshift);
        tft.setTextColor(TFT_CYAN);
        tft.print("Status:");
        tft.setCursor(50, 42 + yshift);
        
        if (jammingRunning) {
            tft.setTextColor(TFT_RED);
            tft.print("Jamming");
        } else {
            tft.setTextColor(TFT_GREEN);
            tft.print("Idle ");
        }
    }
    
    void runUI() {
    #define ICON_NUM 6
    static int iconX[ICON_NUM] = {50, 90, 130, 170, 210, 10};
    static int iconY = STATUS_BAR_Y_OFFSET;
    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_power,
        bitmap_icon_antenna,
        bitmap_icon_random,
        bitmap_icon_sort_down_minus,
        bitmap_icon_sort_up_plus,
        bitmap_icon_go_back
    };

    if (!uiDrawn) {
        tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);
        for (int i = 0; i < ICON_NUM; i++) {
            tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_WHITE);
        }
        uiDrawn = true;
    }

    static unsigned long lastAnimationTime = 0;
    static int animationState = 0;
    static int activeIcon = -1;

    if (animationState > 0 && millis() - lastAnimationTime >= 150) {
        if (animationState == 1) {
            tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, TFT_WHITE);
            animationState = 2;
 switch (activeIcon) {
                case 0:
                  jammingRunning = !jammingRunning;
                    if (jammingRunning) {
                        Serial.println("Jamming started");
                        ELECHOUSE_cc1101.setMHZ(targetFrequency);
                        ELECHOUSE_cc1101.SetTx();
                    } else {
                        Serial.println("Jamming stopped");
                        ELECHOUSE_cc1101.setSidle();
                        digitalWrite(TX_PIN, LOW);
                    }
                    updateDisplay();
                    lastDebounceTime = millis();
                    break;
                case 1: 
                 continuousMode = !continuousMode;
                  Serial.print("Jamming mode: ");
                  Serial.println(continuousMode ? "Continuous Carrier" : "Noise");
                  updateDisplay();
                  lastDebounceTime = millis();
                    break;
                case 2: 
                  autoMode = !autoMode;
                  Serial.print("Frequency mode: ");
                  Serial.println(autoMode ? "Automatic" : "Manual");
                  if (autoMode) {
                      currentFrequencyIndex = 0;
                      targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
                      ELECHOUSE_cc1101.setMHZ(targetFrequency);
                  }
                  updateDisplay();
                  lastDebounceTime = millis();
                    break;
                case 3: 
                  currentFrequencyIndex = (currentFrequencyIndex - 1 + numFrequencies) % numFrequencies;
                  targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
                  ELECHOUSE_cc1101.setMHZ(targetFrequency);
                  Serial.print("Switched to: ");
                  Serial.print(targetFrequency);
                  Serial.println(" MHz");
                  updateDisplay();
                  lastDebounceTime = millis();
                    break;
                 case 4: 
                  currentFrequencyIndex = (currentFrequencyIndex + 1) % numFrequencies;
                  targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
                  ELECHOUSE_cc1101.setMHZ(targetFrequency);
                  Serial.print("Switched to: ");
                  Serial.print(targetFrequency);
                  Serial.println(" MHz");
                  updateDisplay();
                  lastDebounceTime = millis();
                    break;
                case 5: // Back icon action (exit to submenu)
                    feature_exit_requested = true;
                    break;
            }
        } else if (animationState == 2) {
            animationState = 0;
            activeIcon = -1;
        }
        lastAnimationTime = millis();  
    }

    if (ts.touched() && feature_active) {
        TS_Point p = ts.getPoint();
        int x = map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_WIDTH);
        int y = map(p.y, TS_MINY, TS_MAXY, 0, SCREEN_HEIGHT);
        
        if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
            for (int i = 0; i < ICON_NUM; i++) {
                if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
                    tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
                    animationState = 1;
                    activeIcon = i;
                    lastAnimationTime = millis();
                    break;
                }
            }
        }
    }
}

    
    void subjammerSetup() {
        Serial.begin(115200);
        
        ELECHOUSE_cc1101.Init();
        ELECHOUSE_cc1101.setModulation(0);
        ELECHOUSE_cc1101.setRxBW(500.0);
        ELECHOUSE_cc1101.setPA(12);
        ELECHOUSE_cc1101.setMHZ(targetFrequency);
        ELECHOUSE_cc1101.SetTx();
        
        randomSeed(analogRead(0));
        
        tft.setRotation(0);
        tft.fillScreen(TFT_BLACK);
        setupTouchscreen();
        
        float currentBatteryVoltage = readBatteryVoltage();
        drawStatusBar(currentBatteryVoltage, false);
        updateDisplay();
        uiDrawn = false;
    }
    
    void subjammerLoop() {
        runUI();
        
        int btnLeftState = pcf.digitalRead(4);
        int btnRightState = pcf.digitalRead(5);
        int btnUpState = pcf.digitalRead(6);
        int btnDownState = pcf.digitalRead(3);
        
        if (btnUpState == LOW && millis() - lastDebounceTime > debounceDelay) {
            jammingRunning = !jammingRunning;
            if (jammingRunning) {
                Serial.println("Jamming started");
                ELECHOUSE_cc1101.setMHZ(targetFrequency);
                ELECHOUSE_cc1101.SetTx();
            } else {
                Serial.println("Jamming stopped");
                ELECHOUSE_cc1101.setSidle();
                digitalWrite(TX_PIN, LOW);
            }
            updateDisplay();
            lastDebounceTime = millis();
        }
        
        if (btnRightState == LOW && !autoMode && millis() - lastDebounceTime > debounceDelay) {
            currentFrequencyIndex = (currentFrequencyIndex + 1) % numFrequencies;
            targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
            ELECHOUSE_cc1101.setMHZ(targetFrequency);
            Serial.print("Switched to: ");
            Serial.print(targetFrequency);
            Serial.println(" MHz");
            updateDisplay();
            lastDebounceTime = millis();
        }
        
        if (btnLeftState == LOW && millis() - lastDebounceTime > debounceDelay) {
            continuousMode = !continuousMode;
            Serial.print("Jamming mode: ");
            Serial.println(continuousMode ? "Continuous Carrier" : "Noise");
            updateDisplay();
            lastDebounceTime = millis();
        }
        
        if (btnDownState == LOW && millis() - lastDebounceTime > debounceDelay) {
            autoMode = !autoMode;
            Serial.print("Frequency mode: ");
            Serial.println(autoMode ? "Automatic" : "Manual");
            if (autoMode) {
                currentFrequencyIndex = 0;
                targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
                ELECHOUSE_cc1101.setMHZ(targetFrequency);
            }
            updateDisplay();
            lastDebounceTime = millis();
        }
        
        if (jammingRunning) {
            if (autoMode) {
                if (millis() - lastSweepTime >= sweepInterval) {
                    currentFrequencyIndex = (currentFrequencyIndex + 1) % numFrequencies;
                    targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
                    ELECHOUSE_cc1101.setMHZ(targetFrequency);
                    Serial.print("Sweeping: ");
                    Serial.print(targetFrequency);
                    Serial.println(" MHz");
                    updateDisplay();
                    lastSweepTime = millis();
                }
            }
            
            ELECHOUSE_cc1101.SetTx();
            
            if (continuousMode) {
                ELECHOUSE_cc1101.SpiWriteReg(0x3F, 0xFF);
                ELECHOUSE_cc1101.SpiStrobe(0x35);
                digitalWrite(TX_PIN, HIGH);
            } else {
                for (int i = 0; i < 10; i++) {
                    uint32_t noise = random(16777216);
                    ELECHOUSE_cc1101.SpiWriteReg(0x3F, noise >> 16);
                    ELECHOUSE_cc1101.SpiWriteReg(0x3F, (noise >> 8) & 0xFF);
                    ELECHOUSE_cc1101.SpiWriteReg(0x3F, noise & 0xFF);
                    ELECHOUSE_cc1101.SpiStrobe(0x35);
                    delayMicroseconds(50);
                }
            }
        }
    }
} 


namespace scanner24ghz {

#define CE  16
#define CSN 26
#define BTN_SELECT 7

#define CHANNELS 128
int channel[CHANNELS] = {0};
int backgroundNoise[CHANNELS] = {0};
volatile bool scanning = true;
static bool uiDrawn = false;

uint8_t getRegister(uint8_t r) {
    digitalWrite(CSN, LOW);
    SPI.transfer(r & 0x1F);
    uint8_t c = SPI.transfer(0);
    digitalWrite(CSN, HIGH);
    return c;
}
void setRegister(uint8_t r, uint8_t v) {
    digitalWrite(CSN, LOW);
    SPI.transfer((r & 0x1F) | 0x20);
    SPI.transfer(v);
    digitalWrite(CSN, HIGH);
}
void setChannel(uint8_t ch) { setRegister(0x05, ch); }
void powerUp() { setRegister(0x00, getRegister(0x00) | 0x02); delayMicroseconds(130); }
void enable() { digitalWrite(CE, HIGH); }
void disable() { digitalWrite(CE, LOW); }
void setRX() { setRegister(0x00, getRegister(0x00) | 0x01); enable(); delayMicroseconds(100); }
bool carrierDetected() { return getRegister(0x09) & 0x01; }
bool isSelectButtonPressed() { return pcf.digitalRead(BTN_SELECT) == LOW; }

void Print(String text, uint16_t color, bool extraSpace = false) {
    static const int LOG_HEIGHT = 180;
    static const int LINE_HEIGHT = 12;
    static const int MAX_LINES = LOG_HEIGHT / LINE_HEIGHT;
    static String Buffer[MAX_LINES];
    static uint16_t Buffercolor[MAX_LINES];
    static int Index = 0;
    if (Index >= MAX_LINES - 1) {
        for (int i = 3; i < MAX_LINES - 1; i++) {
            Buffer[i] = Buffer[i + 1];
            Buffercolor[i] = Buffercolor[i + 1];
        }
        Index = MAX_LINES - 1;
    }
    Buffer[Index] = text;
    Buffercolor[Index] = color;
    Index++;
    if (extraSpace && Index < MAX_LINES) {
        Buffer[Index] = "";
        Buffercolor[Index] = TFT_WHITE;
        Index++;
    }
    for (int i = 3; i < Index; i++) {
        int yPos = i * LINE_HEIGHT + 15;
        tft.fillRect(5, yPos, tft.width() - 10, LINE_HEIGHT, TFT_BLACK);
        tft.setTextColor(Buffercolor[i], TFT_BLACK);
        tft.setCursor(5, yPos);
        tft.print(Buffer[i]);
    }
}

void calibrateBackgroundNoise() {
    Print("[!] Calibrating background noise", TFT_ORANGE, false);
    for (int i = 0; i < 2; i++) {
        disable();
        for (int j = 0; j < 50; j++) {
            for (int i = 0; i < CHANNELS; i++) {
                setRegister(0x05, (128 * i) / CHANNELS);
                setRX();
                delayMicroseconds(50);
                disable();
                if (getRegister(0x09) > 0) channel[i]++;
            }
        }
        Print(".", TFT_WHITE, false);
        for (int j = 0; j < CHANNELS; j++) backgroundNoise[j] += channel[j];
    }
    for (int i = 0; i < CHANNELS; i++) backgroundNoise[i] /= 5;
    Print("[+] Background noise calibration", TFT_WHITE, false);
    Print("[+] done.", TFT_WHITE, false);
}

void scanChannels() {
    disable();
    scanning = true;
    for (int j = 0; j < 100 && scanning; j++) {
        if (isSelectButtonPressed()) {
            scanning = false;
            Print("Scan interrupted by user", TFT_YELLOW, true);
            return;
        }
        for (int i = 0; i < CHANNELS && scanning; i++) {
            setRegister(0x05, (128 * i) / CHANNELS);
            setRX();
            delayMicroseconds(50);
            disable();
            if (getRegister(0x09) > 0) channel[i]++;
        }
    }
}

void display() {
    tft.fillRect(0, 190, 240, 200, TFT_BLACK);
    int barWidth = 1;
    int maxBarHeight = 120;
    int x = 10;
    int xx = 120;
    for (int i = 0; i < 64; ++i) {
        int barHeight = channel[i] * 3;
        if (barHeight > maxBarHeight) barHeight = maxBarHeight;
        tft.fillRect(x, 308 - barHeight, barWidth, barHeight, TFT_WHITE);
        x += barWidth;
    }
    for (int i = 64; i < 128; ++i) {
        int barHeight = channel[i] * 3;
        if (barHeight > maxBarHeight) barHeight = maxBarHeight;
        tft.fillRect(xx, 308 - barHeight, barWidth, barHeight, TFT_WHITE);
        xx += barWidth;
    }
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 310);
    tft.print("1..5.10..20..40..50..80..90..110..128");
    int midX = 10;
    int midY = tft.height() - 13;
    tft.drawLine(midX, 200, midX, 305, TFT_WHITE);
    tft.drawLine(midX, midY, 230, midY, TFT_WHITE);
    tft.fillCircle(midX, midY, 1, TFT_RED);
    tft.fillCircle(10, 200, 1, TFT_RED);
    tft.fillCircle(230, midY, 1, TFT_RED);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.drawString("Y", midX + 5, 200);
    tft.drawString("X", tft.width() - 15, midY - 9);
}

void runUI() {
    #define ICON_NUM 3
    static int iconX[ICON_NUM] = {170, 210, 10};
    static int iconY = STATUS_BAR_Y_OFFSET;
    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_undo,
        bitmap_icon_start,
        bitmap_icon_go_back
    };

    if (!uiDrawn) {
        tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);
        for (int i = 0; i < ICON_NUM; i++) {
            tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_WHITE);
        }
        uiDrawn = true;
    }

    static unsigned long lastAnimationTime = 0;
    static int animationState = 0;
    static int activeIcon = -1;

    if (animationState > 0 && millis() - lastAnimationTime >= 150) {
        if (animationState == 1) {
            tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, TFT_WHITE);
            animationState = 2;
            switch (activeIcon) {
                case 0: calibrateBackgroundNoise(); break;
                case 1: scanChannels(); break;
                case 2: feature_exit_requested = true; break;
            }
        } else if (animationState == 2) {
            animationState = 0;
            activeIcon = -1;
        }
        lastAnimationTime = millis();
    }

    if (ts.touched() && feature_active) {
        TS_Point p = ts.getPoint();
        int x = map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_WIDTH);
        int y = map(p.y, TS_MINY, TS_MAXY, 0, SCREEN_HEIGHT);
        
        if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
            for (int i = 0; i < ICON_NUM; i++) {
                if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
                    tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
                    animationState = 1;
                    activeIcon = i;
                    lastAnimationTime = millis();
                    break;
                }
            }
        }
    }
}


void scannerSetup() {
    // --- SPI: switch to raw NRF mode ---
    SPI.end();
    SPI.begin(18, 19, 23, CSN);
    pinMode(CE, OUTPUT);
    pinMode(CSN, OUTPUT);
    pcf.pinMode(BTN_SELECT, INPUT_PULLUP);

    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    float currentBatteryVoltage = readBatteryVoltage();
    drawStatusBar(currentBatteryVoltage, false);
    uiDrawn = false;
    display();
    setupTouchscreen();

    Print(" ", TFT_GREEN, false);
    Print("[*] 2.4GHz Scanner Initialized...", TFT_GREEN, false);

    disable();
    powerUp();
    setRegister(0x01, 0x0);
    setRegister(0x06, 0x0F);

    scanning = true;
}

void scannerLoop() {
    scanning = true;
    while (scanning && !feature_exit_requested) {
        if (isSelectButtonPressed()) {
    scanning = false;
    //Print("Scan interrupted by user", TFT_YELLOW, true);
    return;
        }
        runUI();
        scanChannels();
        display();
        delay(5);
    }
    // --- On exit: clean up SPI and reinit TFT/touch for other modes ---
    SPI.end();
    tft.init();
    tft.setRotation(0);
    setupTouchscreen();
}

} // namespace scanner24ghz
// namespace scanner24ghz


/*
 * NRF24L01+ Spectrum Analyzer - Complete Implementation
 *//*
 * NRF24L01+ Spectrum Analyzer - Unified Icon UI
 */

/*
 * NRF24L01+ Spectrum Analyzer - Complete Implementation
 */
namespace nrf_spectrum {
    const int NUM_CHANNELS = 128;
    int channelData[NUM_CHANNELS];
    unsigned long lastScan = 0;
    const int SCAN_INTERVAL = 120; // ms
    
    enum PlotType {PLOT_POWER, PLOT_SNR, PLOT_RSSI};
    PlotType currentPlot = PLOT_POWER;
    
    #define BTN_HEIGHT 40
    #define BTN_WIDTH 80
    #define BTN_Y 270
    #define BTN_X0 10
    #define BTN_X1 90
    #define BTN_X2 170
    
    void drawUI() {
        tft.fillScreen(TFT_BLACK);
        tft.setTextSize(2);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setCursor(20, 8);
        tft.println("2.4GHz Spectrum");
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setCursor(70, 28);
        tft.println("Analyzer");
        
        drawButton(BTN_X0, BTN_Y, "Power", currentPlot == PLOT_POWER ? ORANGE : TFT_BLUE);
        drawButton(BTN_X1, BTN_Y, "SNR", currentPlot == PLOT_SNR ? ORANGE : TFT_BLUE);
        drawButton(BTN_X2, BTN_Y, "RSSI", currentPlot == PLOT_RSSI ? ORANGE : TFT_BLUE);
        
        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(5, 240);
        tft.print("2400");
        tft.setCursor(180, 240);
        tft.print("2527");
        
        // Draw back button
        tft.fillRoundRect(200, 10, 35, 25, 4, TFT_RED);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(1);
        tft.setCursor(208, 18);
        tft.print("BACK");
    }
    
    
    
    void scanChannels() {
        for (int i = 0; i < NUM_CHANNELS; i++) {
            radio.setChannel(i);
            delayMicroseconds(200);
            radio.startListening();
            delayMicroseconds(100);
            
            int signalSum = 0;
            for (int j = 0; j < 10; j++) {
                if (radio.testCarrier()) signalSum += 100;
                else if (radio.testRPD()) signalSum += 50;
                delayMicroseconds(10);
            }
            
            channelData[i] = signalSum / 10;
            radio.stopListening();
        }
    }
    
    void plotSpectrum() {
        tft.fillRect(0, 60, 240, 180, TFT_BLACK);
        
        int maxVal = 1;
        int minVal = 255;
        
        for (int i = 0; i < NUM_CHANNELS; i++) {
            if (channelData[i] > maxVal) maxVal = channelData[i];
            if (channelData[i] < minVal) minVal = channelData[i];
        }
        
        for (int i = 0; i < NUM_CHANNELS; i++) {
            int x = map(i, 0, NUM_CHANNELS - 1, 8, 232);
            int val;
            
            switch (currentPlot) {
                case PLOT_POWER:
                    val = channelData[i];
                    break;
                case PLOT_SNR:
                    val = channelData[i] - minVal;
                    break;
                case PLOT_RSSI:
                    val = channelData[i];
                    break;
            }
            
            int barHeight = map(val, 0, maxVal, 0, 170);
            uint16_t color = (currentPlot == PLOT_SNR) ? TFT_YELLOW : (currentPlot == PLOT_RSSI) ? TFT_CYAN : TFT_GREEN;
            tft.drawFastVLine(x, 230 - barHeight, barHeight, color);
        }
    }
    
    void showStats() {
        int maxVal = 0, minVal = 255, peakCh = 0;
        
        for (int i = 0; i < NUM_CHANNELS; i++) {
            if (channelData[i] > maxVal) {
                maxVal = channelData[i];
                peakCh = i;
            }
            if (channelData[i] < minVal) minVal = channelData[i];
        }
        
        float snr = maxVal - minVal;
        
        tft.fillRect(0, 240, 240, 30, TFT_BLACK);
        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(10, 242);
        tft.print("Peak:"); tft.print(maxVal);
        tft.setCursor(90, 242);
        tft.print("Ch:"); tft.print(peakCh);
        tft.setCursor(10, 255);
        tft.print("SNR:"); tft.print(snr, 1);
        tft.setCursor(90, 255);
        tft.print("Min:"); tft.print(minVal);
    }
    
    void handleTouch() {
        if (ts.touched()) {
            TS_Point p = ts.getPoint();
            int x = MAP_X(p.x);
            int y = MAP_Y(p.y);
            
            // Check back button
            if (x >= 200 && x <= 235 && y >= 10 && y <= 35) {
                feature_exit_requested = true;
                delay(300);
                return;
            }
            
            if (y > BTN_Y && y < BTN_Y + BTN_HEIGHT) {
                if (x > BTN_X0 && x < BTN_X0 + BTN_WIDTH && currentPlot != PLOT_POWER) {
                    currentPlot = PLOT_POWER;
                    drawUI();
                } else if (x > BTN_X1 && x < BTN_X1 + BTN_WIDTH && currentPlot != PLOT_SNR) {
                    currentPlot = PLOT_SNR;
                    drawUI();
                } else if (x > BTN_X2 && x < BTN_X2 + BTN_WIDTH && currentPlot != PLOT_RSSI) {
                    currentPlot = PLOT_RSSI;
                    drawUI();
                }
                delay(400); // debounce
            }
        }
    }
    
    void sendSerialData() {
        Serial.print("FFT_DATA:");
        for (int i = 0; i < NUM_CHANNELS; i++) {
            Serial.print(channelData[i]);
            if (i < NUM_CHANNELS - 1) Serial.print(",");
        }
        Serial.println();
        
        int maxVal = 0, minVal = 255, peakCh = 0;
        for (int i = 0; i < NUM_CHANNELS; i++) {
            if (channelData[i] > maxVal) {
                maxVal = channelData[i];
                peakCh = i;
            }
            if (channelData[i] < minVal) minVal = channelData[i];
        }
        
        float snr = maxVal - minVal;
        Serial.print("STATS:");
        Serial.print(minVal);
        Serial.print(",");
        Serial.print(maxVal);
        Serial.print(",");
        Serial.print(snr);
        Serial.print(",");
        Serial.println(peakCh);
    }
    
    void nrfSetup() {
        drawUI();
    }
    
    void nrfLoop() {
        if (millis() - lastScan > SCAN_INTERVAL) {
            scanChannels();
            plotSpectrum();
            showStats();
            sendSerialData();
            lastScan = millis();
        }
        
        handleTouch();
    }
} // namespace nrf_spectrum


// Main Setup Function
void setup() {

    

    pcf.pinMode(4, INPUT_PULLUP); // Left button
    pcf.pinMode(5, INPUT_PULLUP); // Right button
    pcf.pinMode(6, INPUT_PULLUP); // Up button
    pcf.pinMode(3, INPUT_PULLUP); // Down butto
    
    
    

    Serial.begin(115200);
    Serial.println("ESP32 SubGHz Tool Starting...");
    
    // Initialize I2C for PCF8574
    Wire.begin(I2C_SDA, I2C_SCL);
    pcf.begin();

    // Initialize CC1101 SPI
    ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
    ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
    ELECHOUSE_cc1101.Init();
    
    // Initialize TFT
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    
    // Initialize TFT backlight
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    
    // Initialize touchscreen
    setupTouchscreen();
    
    // Initialize EEPROM
    EEPROM.begin(EEPROM_SIZE);
    
    // Draw main menu
    currentMode = MODE_MENU;
    drawModeSelection();
    
    Serial.println("Setup complete!");
}

// Main Loop Function
void loop() {
    switch (currentMode) {
        case MODE_MENU:
            handleMenuTouch();
            break;
            
        case MODE_CC1101_REPLAY:
            switchToCC1101();
            replayat::ReplayAttackSetup();
            while (!feature_exit_requested) {
                replayat::ReplayAttackLoop();
                delay(10);
            }
            feature_exit_requested = false;
            currentMode = MODE_MENU;
            drawModeSelection();
            break;
            
        case MODE_CC1101_PROFILE:
            switchToCC1101();
            SavedProfile::saveSetup();
            while (!feature_exit_requested) {
                SavedProfile::saveLoop();
                delay(10);
            }
            feature_exit_requested = false;
            currentMode = MODE_MENU;
            drawModeSelection();
            break;
            
        case MODE_CC1101_JAMMER:
            switchToCC1101();
            subjammer::subjammerSetup();
            while (!feature_exit_requested) {
                subjammer::subjammerLoop();
                delay(10);
            }
            feature_exit_requested = false;
            currentMode = MODE_MENU;
            drawModeSelection();
            break;
            
        case MODE_NRF_SPECTRUM:
            switchToNRF();
            nrf_spectrum::nrfSetup();
            while (!feature_exit_requested) {
                nrf_spectrum::nrfLoop();
                delay(10);
            }
            feature_exit_requested = false;
            currentMode = MODE_MENU;
            drawModeSelection();
            break;

        case MODE_SCANNER_24GHZ:
            scanner24ghz::scannerSetup();
            while (!feature_exit_requested) {
                scanner24ghz::scannerLoop();
                delay(10);
            }
            feature_exit_requested = false;
            currentMode = MODE_MENU;
            drawModeSelection();
            break;
         case MODE_WIFI_MENU:
            drawWiFiMenu();
            while (currentMode == MODE_WIFI_MENU) {
                handleWiFiMenuTouch();
                delay(10);
            }
            break;
        case MODE_WIFI_PACKETMON:
            PacketMonitor::ptmSetup();
            while (!feature_exit_requested) {
                PacketMonitor::ptmLoop();
                delay(10);
            }
            feature_exit_requested = false;
            currentMode = MODE_WIFI_MENU;
            drawWiFiMenu();
            break;

    }
    
    delay(10);
}
