#include <WiFi.h>
#include "esp_camera.h"
#include "esp_wifi.h"
#include <TFT_eSPI.h>
#include <SD.h>
#include <FS.h>
#include <JPEGDecoder.h>
#include <bigiot.h>

// Return the minimum of two values a and b
#define minimum(a,b)     (((a) < (b)) ? (a) : (b))


#define UPLOAD_TIMESTAMP    10000


#define PWDN_GPIO_NUM       -1
#define RESET_GPIO_NUM      -1
#define XCLK_GPIO_NUM       4
#define SIOD_GPIO_NUM       18
#define SIOC_GPIO_NUM       23

#define Y9_GPIO_NUM         36
#define Y8_GPIO_NUM         37
#define Y7_GPIO_NUM         38
#define Y6_GPIO_NUM         39
#define Y5_GPIO_NUM         35
#define Y4_GPIO_NUM         26
#define Y3_GPIO_NUM         13
#define Y2_GPIO_NUM         34
#define VSYNC_GPIO_NUM      5
#define HREF_GPIO_NUM       27
#define PCLK_GPIO_NUM       25

#define TFT_MISO            22
#define TFT_MOSI            19
#define TFT_SCLK            21
#define TFT_CS              12
#define TFT_DC              15
#define TFT_RST             -1
#define TFT_BK              2

#define SDCARA_CS           0


TFT_eSPI tft = TFT_eSPI();              // Invoke library, pins defined in User_Setup.h
BIGIOT bigiot;


const char *ssid    =   "";             //wifi ssid
const char *password =  "";             //wifi password
const char *id =        "";             //platform device id
const char *apikey =    "";             //platform device api key
const char *usrkey =    "";             //platform user key , if you are not using encrypted login,you can leave it blank
const char *picId =     "";             //photo data stream id

void setup()
{
    char buff[256];
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    Serial.println();

    SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);

    pinMode(TFT_BK, OUTPUT);
    digitalWrite(TFT_BK, HIGH);

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);

    int x = tft.width() / 2;
    int y = tft.height() / 2 ;

    if (!SD.begin(SDCARA_CS)) {
        tft.drawString("SD Init Fail", x, y);
    } else {
        snprintf(buff, sizeof(buff), "SD Init Pass Type:%d Size:%lu\n", SD.cardType(), SD.cardSize() / 1024 / 1024);
        tft.drawString(buff, x, y);
        Serial.printf("totalBytes:%lu usedBytes:%lu\n", SD.totalBytes(), SD.usedBytes());
        delay(2000);
    }

    delay(1000);

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    //init with high specs to pre-allocate larger buffers
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;

    // camera init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x", err);
        tft.fillScreen(TFT_BLACK);
        snprintf(buff, sizeof(buff), "Camera init failed with error 0x%x", err);
        tft.drawString(buff, x, y);
        while (1);
    }

    //drop down frame size for higher initial frame rate
    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, FRAMESIZE_QVGA);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");

    // Login to bigiot.net
    if (!bigiot.login(id, apikey, usrkey)) {
        Serial.println("Login fail");
        delay(2000);
        esp_restart();
    }

    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    delay(1000);

}


void loop()
{
    static uint32_t last_upload_time = 0;
    camera_fb_t *fb = NULL;

    if (WiFi.status() == WL_CONNECTED) {
        //Wait for platform command release
        bigiot.handle();
        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.printf("Camera capture failed");
        } else {
            uint32_t now = millis();
            if (now - last_upload_time > UPLOAD_TIMESTAMP) {
                tft.fillScreen(TFT_BLACK);
                tft.drawString("Update Photo ... ", tft.width() / 2, tft.height() / 2);
                bool ret = bigiot.uploadPhoto( picId, "jpg", String(now).c_str(), (uint8_t *)fb->buf, fb->len);
                tft.fillScreen(TFT_BLACK);
                tft.drawString(ret ? "Upload Success" : "Upload error", tft.width() / 2, tft.height() / 2);
                last_upload_time = now;
            } else {
                drawArrayJpeg(fb->buf, fb->len, 0, 0);
            }
            esp_camera_fb_return(fb);
            fb = NULL;
        }
    }
}




//####################################################################################################
// Draw a JPEG on the TFT pulled from a program memory array
//####################################################################################################
void drawArrayJpeg(const uint8_t arrayname[], uint32_t array_size, int xpos, int ypos)
{
    int x = xpos;
    int y = ypos;
    JpegDec.decodeArray(arrayname, array_size);
    renderJPEG(x, y);
}

//####################################################################################################
// Draw a JPEG on the TFT, images will be cropped on the right/bottom sides if they do not fit
//####################################################################################################
// This function assumes xpos,ypos is a valid screen coordinate. For convenience images that do not
// fit totally on the screen are cropped to the nearest MCU size and may leave right/bottom borders.
void renderJPEG(int xpos, int ypos)
{

    // retrieve infomration about the image
    uint16_t *pImg;
    uint16_t mcu_w = JpegDec.MCUWidth;
    uint16_t mcu_h = JpegDec.MCUHeight;
    uint32_t max_x = JpegDec.width;
    uint32_t max_y = JpegDec.height;

    // Jpeg images are draw as a set of image block (tiles) called Minimum Coding Units (MCUs)
    // Typically these MCUs are 16x16 pixel blocks
    // Determine the width and height of the right and bottom edge image blocks
    uint32_t min_w = minimum(mcu_w, max_x % mcu_w);
    uint32_t min_h = minimum(mcu_h, max_y % mcu_h);

    // save the current image block size
    uint32_t win_w = mcu_w;
    uint32_t win_h = mcu_h;

    // save the coordinate of the right and bottom edges to assist image cropping
    // to the screen size
    max_x += xpos;
    max_y += ypos;

    // read each MCU block until there are no more
    while (JpegDec.readSwappedBytes()) {

        // save a pointer to the image block
        pImg = JpegDec.pImage ;

        // calculate where the image block should be drawn on the screen
        int mcu_x = JpegDec.MCUx * mcu_w + xpos;  // Calculate coordinates of top left corner of current MCU
        int mcu_y = JpegDec.MCUy * mcu_h + ypos;

        // check if the image block size needs to be changed for the right edge
        if (mcu_x + mcu_w <= max_x) win_w = mcu_w;
        else win_w = min_w;

        // check if the image block size needs to be changed for the bottom edge
        if (mcu_y + mcu_h <= max_y) win_h = mcu_h;
        else win_h = min_h;

        // copy pixels into a contiguous block
        if (win_w != mcu_w) {
            uint16_t *cImg;
            int p = 0;
            cImg = pImg + win_w;
            for (int h = 1; h < win_h; h++) {
                p += mcu_w;
                for (int w = 0; w < win_w; w++) {
                    *cImg = *(pImg + w + p);
                    cImg++;
                }
            }
        }

        // draw image MCU block only if it will fit on the screen
        if (( mcu_x + win_w ) <= tft.width() && ( mcu_y + win_h ) <= tft.height()) {
            tft.pushRect(mcu_x, mcu_y, win_w, win_h, pImg);
        } else if ( (mcu_y + win_h) >= tft.height()) JpegDec.abort(); // Image has run off bottom of screen so abort decoding
    }
}
