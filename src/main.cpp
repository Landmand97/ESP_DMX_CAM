#include "Arduino.h"
#include "logger.h"
#include <esp_dmx.h>

#include "esp_camera.h"
#include "FS.h"               // SD Card ESP32
#include "SD_MMC.h"           // SD Card ESP32
#include "soc/soc.h"          // Disable brownour problems
#include "soc/rtc_cntl_reg.h" // Disable brownour problems
#include "driver/rtc_io.h"
#include <EEPROM.h> // read and write from flash memory

// define the number of bytes you want to access
#define EEPROM_SIZE 1

// Pin definition for CAMERA_MODEL_AI_THINKER
#include "pin_config.h"




int pictureNumber = 0;
bool newSettings, newPicture, newFlashState;

uint8_t fixtureStartChannel = 100; // Needs to be set to the correct channel in the dmx chain
struct dmxPayload
{
  uint8_t pictureNumber;
  int brightness;     // -2 to 2
  int contrast;       // -2 to 2
  int saturation;     // -2 to 2
  int special_effect; // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
  int hmirror;        // 0 = disable , 1 = enable
  int vflip;          // 0 = disable , 1 = enable
  int flashState;     // 0 = disable , 1 = enable
};

bool setupCamera();
bool setupMC();
bool takePicture();
void setCameraSettings(dmxPayload dmx);
void toggleFlash();
bool setupDMX();
void recieveDMX();
bool buildPayload();
void deepSleep();
/* Define hardware pins for the ESP32.
  We need to define a transmit pin, a receive pin, and a enable pin. */
int transmitPin = 47;
int receivePin = 21;
int enablePin = 10;

/* Define the DMX port to use. The ESP32 has either 2 or 3 ports.
  Port 0 is typically used to transmit serial data back to your Serial Monitor,
  so we shouldn't use that port. Lets use port 1! */
dmx_port_t dmxPort = 1;

/* Define a byte data array for storing the DMX data. A single packet of DMX
  can be up to 513 bytes long*/
byte data[DMX_PACKET_SIZE];

/* This variable will allow us to update our packet and print to the Serial
  Monitor at a regular interval. */
unsigned long lastUpdate = millis();

dmxPayload dmx;

void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // disable brownout detector

  Serial.begin(115200);
  set_log_level(log_levels::INFO);
  log(log_levels::INFO, "Beginning the setup phase\n", 0);
  pinMode(BUILTIN_LED, OUTPUT);
  setupDMX();

  setupCamera();

  setupMC();

  log(log_levels::INFO, "Setup done, now listening for DMX data\n", 0);
}

void loop()
{
  recieveDMX();
}

bool setupCamera()
{
  log(log_levels::INFO, "Setting up the camera\n", 0);
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound())
  {
    config.frame_size = FRAMESIZE_UXGA; // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
    config.jpeg_quality = 4;
    config.fb_count = 2;
  }
  else
  {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // Init Camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    return false;
  }
  return true;
}

bool setupMC()
{
  log(log_levels::INFO, "Setting up the memory card\n", 0);
  // Serial.println("Starting SD Card");
  if (!SD_MMC.setPins(39, 38, 40))
  {
    Serial.println("set pins failed");
    return false;
  }
  // mode1bit must equal true for the ESP32-S3-USB-OTG
  if (!SD_MMC.begin("/sdcard", true))
  {
    Serial.println("SD Card Mount Failed");
    return false;
  }

  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE)
  {
    Serial.println("No SD Card attached");
    return false;
  }
  return true;
}

bool takePicture()
{
  log(log_levels::INFO, "Taking picture\n", 0);
  camera_fb_t *fb = NULL;

  // Take Picture with Camera
  fb = esp_camera_fb_get();
  if (!fb)
  {
    Serial.println("Camera capture failed");
    return false;
  }
  // initialize EEPROM with predefined size
  EEPROM.begin(EEPROM_SIZE);
  pictureNumber = EEPROM.read(0) + 1;

  // Path where new picture will be saved in SD Card
  String path = "/dmx_channel_" + String(fixtureStartChannel) + "_" + String(dmx.pictureNumber) + ".jpg";

  fs::FS &fs = SD_MMC;
  Serial.printf("Picture file name: %s\n", path.c_str());

  bool success = false;
  File file = fs.open(path.c_str(), FILE_WRITE);
  if (!file)
  {
    Serial.println("Failed to open file in writing mode");
  }
  else
  {
    file.write(fb->buf, fb->len); // payload (image), payload length
    Serial.printf("Saved file to path: %s\n", path.c_str());
    EEPROM.write(0, pictureNumber);
    success = EEPROM.commit();
  }
  file.close();
  esp_camera_fb_return(fb);
  return success;
}

void setCameraSettings(dmxPayload dmx)
{
  log(log_levels::INFO, "Changing camera settings\n", 0);
  sensor_t *s = esp_camera_sensor_get();
  if (dmx.brightness != 0)
  {
    int value = map(dmx.brightness, 1, 255, -2, 2);
    s->set_brightness(s, value); // -2 to 2
  }
  else
  {
    s->set_brightness(s, 0);
  }

  if (dmx.contrast != 0)
  {
    int value = map(dmx.contrast, 1, 255, -2, 2);
    s->set_contrast(s, value); // -2 to 2
  }
  else
  {
    s->set_contrast(s, 0);
  }

  if (dmx.saturation != 0)
  {
    int value = map(dmx.saturation, 1, 255, -2, 2);
    s->set_saturation(s, value); // -2 to 2
  }
  else
  {
    s->set_saturation(s, 0);
  }
  if (dmx.special_effect < 7)
  {
    s->set_special_effect(s, dmx.special_effect); // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
  }
  else
  {
    s->set_special_effect(s, 0);
  }


  s->set_whitebal(s, 1);                   // 0 = disable , 1 = enable
  s->set_awb_gain(s, 1);                   // 0 = disable , 1 = enable
  s->set_wb_mode(s, 0);                    // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
  s->set_exposure_ctrl(s, 1);              // 0 = disable , 1 = enable
  s->set_aec2(s, 1);                       // 0 = disable , 1 = enable
  s->set_ae_level(s, 0);                   // -2 to 2
  s->set_aec_value(s, 300);                // 0 to 1200
  s->set_gain_ctrl(s, 1);                  // 0 = disable , 1 = enable
  s->set_agc_gain(s, 0);                   // 0 to 30
  s->set_gainceiling(s, (gainceiling_t)0); // 0 to 6
  s->set_bpc(s, 0);                        // 0 = disable , 1 = enable
  s->set_wpc(s, 1);                        // 0 = disable , 1 = enable
  s->set_raw_gma(s, 1);                    // 0 = disable , 1 = enable
  s->set_lenc(s, 1);                       // 0 = disable , 1 = enable
  s->set_hmirror(s, dmx.hmirror);          // 0 = disable , 1 = enable
  s->set_vflip(s, dmx.vflip);              // 0 = disable , 1 = enable
  s->set_dcw(s, 1);                        // 0 = disable , 1 = enable
  s->set_colorbar(s, 0);                   // 0 = disable , 1 = enable
}

void toogleFlash()
{
  log(log_levels::INFO, "setting flash to %d \n", dmx.flashState);
  digitalWrite(BUILTIN_LED, dmx.flashState);
  rtc_gpio_hold_en(GPIO_NUM_48);
}

bool setupDMX()
{
  log(log_levels::INFO, "Setting up the DMX hardware and protocol\n", 0);
  dmx_config_t dmxConfig = DMX_CONFIG_DEFAULT;
  dmx_personality_t personalities[] = {};
  int personality_count = 0;
  bool success = false;
  success = dmx_driver_install(dmxPort, &dmxConfig, personalities, personality_count);
  success = dmx_set_pin(dmxPort, transmitPin, receivePin, enablePin);
  return success;
}

void recieveDMX()
{
  dmx_packet_t packet;
  int size = dmx_receive(dmxPort, &packet, DMX_TIMEOUT_TICK);

  if (size > 0)
  {
    dmx_read(dmxPort, data, size);
    log(log_levels::INFO, "Received DMX on channel: %d ", fixtureStartChannel);
    log(log_levels::INFO, "value: %d\n", data[fixtureStartChannel]);

    /*log(log_levels::INFO, "Received DMX on channel: %d ", fixtureStartChannel+1);
    log(log_levels::INFO, "value: %d\n", data[fixtureStartChannel+1]);
    log(log_levels::INFO, "Received DMX on channel: %d ", fixtureStartChannel+2);
    log(log_levels::INFO, "value: %d\n", data[fixtureStartChannel+2]);
    */

    buildPayload();

    if (newSettings)
    {
      setCameraSettings(dmx);
    }

    if (newFlashState)
    {
      toogleFlash();
    }

    if (newPicture)
    {
      takePicture();
    }

    newSettings = false, newFlashState = false;
    newPicture = false;
  }
  else
  {
    log(log_levels::INFO, "No data recieved, check DMX wiring\n", 0);
  }
}

bool buildPayload()
{
  // log(log_levels::INFO,"Building new payload from dmx data\n",0);
  if (data[fixtureStartChannel] != dmx.pictureNumber)
  {
    dmx.pictureNumber = data[fixtureStartChannel];
    log(log_levels::INFO, "new picture number set\n", 0);
    newPicture = true;
  }

  if (data[fixtureStartChannel + 1] != dmx.brightness)
  {
    dmx.brightness = data[fixtureStartChannel + 1];
    newSettings = true;
  }

  if (data[fixtureStartChannel + 2] != dmx.contrast)
  {
    dmx.contrast = data[fixtureStartChannel + 1];
    newSettings = true;
  }

  if (data[fixtureStartChannel + 3] != dmx.saturation)
  {
    dmx.saturation = data[fixtureStartChannel + 3];
    newSettings = true;
  }

  if (data[fixtureStartChannel + 4] != dmx.special_effect)
  {
    dmx.special_effect = data[fixtureStartChannel + 4];
    newSettings = true;
  }

  if (data[fixtureStartChannel + 5] != dmx.hmirror)
  {
    dmx.hmirror = data[fixtureStartChannel + 5];
    newSettings = true;
  }

  if (data[fixtureStartChannel + 6] != dmx.vflip)
  {
    dmx.vflip = data[fixtureStartChannel + 6];
    newSettings = true;
  }

  if (data[fixtureStartChannel + 7] != dmx.flashState)
  {
    dmx.flashState = data[fixtureStartChannel + 7];
    newFlashState = true;
  }

  return newSettings && newPicture && newFlashState;
}

void deepSleep()
{
  log(log_levels::INFO, "Going to sleep now\n", 0);
  delay(2000);
  esp_deep_sleep_start();
}