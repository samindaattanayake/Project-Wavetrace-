#include "wificonfig.h"
#include "shared.h"
#include "icon.h"
#include "Touchscreen.h"
#include <Preferences.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include <arduinoFFT.h>



namespace PacketMonitor {

// FFT parameters and objects
const uint16_t samples = 256;
const double samplingFrequency = 5000;
double vReal[samples];
double vImag[samples];
ArduinoFFT FFT(vReal, vImag, samples, samplingFrequency, true);

#define MAX_CH 14
#define SNAP_LEN 2324
#define MAX_X 240
#define MAX_Y 320

// Color palette for waterfall
uint8_t palette_red[128], palette_green[128], palette_blue[128];

// WiFi/monitoring state
uint32_t tmpPacketCounter = 0;
uint32_t pkts[MAX_X] = {0};
uint32_t deauths = 0;
unsigned int ch = 1;
int rssiSum = 0;
unsigned int epoch = 0;

// UI state
static bool uiDrawn = false;
bool btnLeftPressed = false;
bool btnRightPressed = false;

// Preferences for channel storage
Preferences preferences;

// Button pin definitions (use your global ones)
//extern const int BTN_LEFT;
//extern const int BTN_RIGHT;

// --- FFT Waterfall Drawing ---
void do_sampling_FFT() {
    unsigned long microseconds = micros();
    for (int i = 0; i < samples; i++) {
        vReal[i] = tmpPacketCounter * 300;
        vImag[i] = 1;
        while (micros() - microseconds < (1000000 / samplingFrequency)) {}
        microseconds += (1000000 / samplingFrequency);
    }

    // Remove DC offset
    double mean = 0;
    for (uint16_t i = 0; i < samples; i++) mean += vReal[i];
    mean /= samples;
    for (uint16_t i = 0; i < samples; i++) vReal[i] -= mean;

    FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT.compute(FFT_FORWARD);
    FFT.complexToMagnitude();

    unsigned int left_x = 120;
    unsigned int graph_y_offset = 91;
    int max_k = 0;

    for (int j = 0; j < samples >> 1; j++) {
        int k = vReal[j] / 10; // attenuation
        if (k > max_k) max_k = k;
        if (k > 127) k = 127;
        unsigned int color = palette_red[k] << 11 | palette_green[k] << 5 | palette_blue[k];
        unsigned int vertical_x = left_x + j;
        tft.drawPixel(vertical_x, epoch + graph_y_offset, color);
    }
    for (int j = 0; j < samples >> 1; j++) {
        int k = vReal[j] / 10;
        if (k > max_k) max_k = k;
        if (k > 127) k = 127;
        unsigned int color = palette_red[k] << 11 | palette_green[k] << 5 | palette_blue[k];
        unsigned int mirrored_x = left_x - j;
        tft.drawPixel(mirrored_x, epoch + graph_y_offset, color);
    }

    // Area graph (top)
    unsigned int area_graph_x_offset = 120;
    unsigned int area_graph_height = 50;
    unsigned int area_graph_y_offset = 38;
    static int last_y[samples >> 1] = {0};
    tft.fillRect(area_graph_x_offset, area_graph_y_offset, (samples >> 1), area_graph_height, TFT_BLACK);

    for (int j = 0; j < samples >> 1; j++) {
        int k = vReal[j] / 10;
        if (k > 127) k = 127;
        unsigned int color = palette_red[k] << 11 | palette_green[k] << 5 | palette_blue[k];
        int current_y = area_graph_height - map(k, 0, 127, 0, area_graph_height) + area_graph_y_offset;
        unsigned int x = area_graph_x_offset + j;
        if (j > 0) {
            tft.fillTriangle(x - 1, area_graph_y_offset + area_graph_height, x, area_graph_y_offset + area_graph_height, x - 1, last_y[j - 1], color);
            tft.fillTriangle(x - 1, last_y[j - 1], x, area_graph_y_offset + area_graph_height, x, current_y, color);
        }
        last_y[j] = current_y;
    }

    // Area graph (bottom, mirrored)
    unsigned int area_graph_width = (samples >> 1);
    unsigned int area_graph_x_offset_flipped = -7;
    tft.fillRect(area_graph_x_offset_flipped, area_graph_y_offset, area_graph_width, area_graph_height, TFT_BLACK);

    for (int j = 0; j < samples >> 1; j++) {
        int k = vReal[j] / 10;
        if (k > 127) k = 127;
        unsigned int color = palette_red[k] << 11 | palette_green[k] << 5 | palette_blue[k];
        int current_y = area_graph_height - map(k, 0, 127, 0, area_graph_height) + area_graph_y_offset;
        unsigned int x = area_graph_x_offset_flipped + area_graph_width - j - 1;
        if (j > 0) {
            tft.fillTriangle(x + 1, area_graph_y_offset + area_graph_height, x, area_graph_y_offset + area_graph_height, x + 1, last_y[j - 1], color);
            tft.fillTriangle(x + 1, last_y[j - 1], x, area_graph_y_offset + area_graph_height, x, current_y, color);
        }
        last_y[j] = current_y;
    }

    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setTextFont(1);
    tft.fillRect(30, 20, 130, 16, DARK_GRAY);
    tft.setCursor(35, 24);
    tft.print("Ch:");
    tft.print(ch);
    tft.setCursor(80, 24);
    tft.print("Packet:");
    tft.print(tmpPacketCounter);

    delay(10);
}

// --- WiFi promiscuous callback ---
void wifi_promiscuous(void* buf, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    wifi_pkt_rx_ctrl_t ctrl = (wifi_pkt_rx_ctrl_t)pkt->rx_ctrl;

    if (type == WIFI_PKT_MGMT && (pkt->payload[0] == 0xA0 || pkt->payload[0] == 0xC0)) deauths++;
    if (type == WIFI_PKT_MISC) return;
    if (ctrl.sig_len > SNAP_LEN) return;

    if (type == WIFI_PKT_MGMT) ctrl.sig_len -= 4;
    tmpPacketCounter++;
    rssiSum += ctrl.rssi;
}

// --- Channel switching ---
void setChannel(int newChannel) {
    ch = newChannel;
    if (ch > MAX_CH || ch < 1) ch = 1;

    preferences.begin("packetmonitor32", false);
    preferences.putUInt("channel", ch);
    preferences.end();

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous);
    esp_wifi_set_promiscuous(true);
}

// --- UI drawing and touch ---
void runUI() {
    static const int ICON_NUM = 3;
    static int iconX[ICON_NUM] = {170, 210, 10};
    static int iconY = STATUS_BAR_Y_OFFSET;
    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_sort_up_plus,    // Increase channel
        bitmap_icon_sort_down_minus, // Decrease channel
        bitmap_icon_go_back          // Back icon
    };

    if (!uiDrawn) {
        tft.fillRect(160, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH - 160, STATUS_BAR_HEIGHT, DARK_GRAY);
        for (int i = 0; i < ICON_NUM; i++) {
            if (icons[i] != NULL) {
                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_WHITE);
            }
        }
        tft.drawLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, ORANGE);
        uiDrawn = true;
    }

    static unsigned long lastAnimationTime = 0;
    static int animationState = 0;
    static int activeIcon = -1;

    if (animationState > 0 && millis() - lastAnimationTime >= 150) {
        if (animationState == 1) {
            tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, TFT_WHITE);
            animationState = 2;
        } else if (animationState == 2) {
            animationState = 0;
            activeIcon = -1;
        }
        lastAnimationTime = millis();
    }

    static unsigned long lastTouchCheck = 0;
    const unsigned long touchCheckInterval = 50;

    if (millis() - lastTouchCheck >= touchCheckInterval) {
        if (ts.touched() && feature_active) {
            TS_Point p = ts.getPoint();
            int x = MAP_X(p.x);
            int y = MAP_Y(p.y);

            if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
                for (int i = 0; i < ICON_NUM; i++) {
                    if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
                        if (icons[i] != NULL && animationState == 0) {
                            tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
                            animationState = 1;
                            activeIcon = i;
                            lastAnimationTime = millis();

                            switch (i) {
                                case 0: setChannel(ch + 1); break;  // Increase channel
                                case 1: setChannel(ch - 1); break;  // Decrease channel
                                case 2: feature_exit_requested = true; break; // Back
                            }
                        }
                        break;
                    }
                }
            }
        }
        lastTouchCheck = millis();
    }
}

// --- Setup ---
void ptmSetup() {
    Serial.begin(115200);

    pcf.pinMode(BTN_UP, INPUT_PULLUP);
    pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
    pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
    pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);

    tft.fillScreen(TFT_BLACK);
    setupTouchscreen();

    // Color palette for FFT
    for (int i = 0; i < 32; i++) {
        palette_red[i] = i / 2;
        palette_green[i] = 0;
        palette_blue[i] = i;
    }
    for (int i = 32; i < 64; i++) {
        palette_red[i] = i / 2;
        palette_green[i] = 0;
        palette_blue[i] = 63 - i;
    }
    for (int i = 64; i < 96; i++) {
        palette_red[i] = 31;
        palette_green[i] = (i - 64) * 2;
        palette_blue[i] = 0;
    }
    for (int i = 96; i < 128; i++) {
        palette_red[i] = 31;
        palette_green[i] = 63;
        palette_blue[i] = i - 96;
    }

    preferences.begin("packetmonitor32", false);
    ch = preferences.getUInt("channel", 1);
    preferences.end();

    nvs_flash_init();
    tcpip_adapter_if_t();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());

    float currentBatteryVoltage = readBatteryVoltage();
    drawStatusBar(currentBatteryVoltage, false);

    uiDrawn = false;
    tft.fillRect(0, 20, 160, 16, DARK_GRAY);
}

// --- Main Loop ---
void ptmLoop() {
    runUI();
    updateStatusBar();

    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous_rx_cb(&wifi_promiscuous);
    esp_wifi_set_promiscuous(true);

    tft.drawLine(0, 90, 240, 90, TFT_WHITE);
    tft.drawLine(0, 19, 240, 19, TFT_WHITE);

    do_sampling_FFT();
    delay(10);
    epoch++;
    if (epoch >= tft.width()) epoch = 0;

    static uint32_t lastButtonTime = 0;
    const uint32_t debounceDelay = 200;

    bool leftButtonState = !pcf.digitalRead(BTN_LEFT);
    bool rightButtonState = !pcf.digitalRead(BTN_RIGHT);

    uint32_t currentTime = millis();

    if (leftButtonState && !btnLeftPressed && (currentTime - lastButtonTime > debounceDelay)) {
        btnLeftPressed = true;
        setChannel(ch - 1);
        lastButtonTime = currentTime;
    } else if (!leftButtonState) {
        btnLeftPressed = false;
    }

    if (rightButtonState && !btnRightPressed && (currentTime - lastButtonTime > debounceDelay)) {
        btnRightPressed = true;
        setChannel(ch + 1);
        lastButtonTime = currentTime;
    } else if (!rightButtonState) {
        btnRightPressed = false;
    }

    pkts[127] = tmpPacketCounter;
    tmpPacketCounter = 0;
    deauths = 0;
    rssiSum = 0;
}

} // namespace PacketMonitor
