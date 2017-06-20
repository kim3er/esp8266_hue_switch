#include <string.h>

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_STMPE610.h>

#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>

#include "jsmn.h"
#include "config.h"

#ifdef ESP8266
   #define STMPE_CS 16
   #define TFT_CS   0
   #define TFT_DC   15
   #define SD_CS    2
#endif
#ifdef __AVR_ATmega32U4__
   #define STMPE_CS 6
   #define TFT_CS   9
   #define TFT_DC   10
   #define SD_CS    5
#endif
#ifdef ARDUINO_SAMD_FEATHER_M0
   #define STMPE_CS 6
   #define TFT_CS   9
   #define TFT_DC   10
   #define SD_CS    5
#endif
#ifdef TEENSYDUINO
   #define TFT_DC   10
   #define TFT_CS   4
   #define STMPE_CS 3
   #define SD_CS    8
#endif
#ifdef ARDUINO_STM32_FEATHER
   #define TFT_DC   PB4
   #define TFT_CS   PA15
   #define STMPE_CS PC7
   #define SD_CS    PC5
#endif

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);
Adafruit_STMPE610 ts = Adafruit_STMPE610(STMPE_CS);

#define TS_MINX 3800
#define TS_MAXX 100
#define TS_MINY 100
#define TS_MAXY 3750

#define STATE_KEY_COUNT 2

bool _lightPressed = false;
bool _lightsOn = false;
uint16_t _brightness = 0;
int _step = 10;

char *_action_url;
char *_status_url;

ESP8266WiFiMulti WiFiMulti;
HTTPClient http;

unsigned long previousMillis = 0;
const long interval = 15000;

typedef struct {
  bool is_on;
  uint16_t brightness;
} GroupState;

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  WiFiMulti.addAP(ssid, passphrase);
  http.setReuse(true);

  ts.begin();
  tft.begin();

  //tft.setRotation(1);

  tft.fillScreen(ILI9341_BLACK);
  
  http.setReuse(true);

  char *pre_sta = "http://192.168.1.239/api/";
  char *pos_sta = "/groups/2";
  _status_url = (char *)malloc(strlen(pre_sta) + strlen(hue_user) + strlen(pos_sta) + 1);
  strcpy(_status_url, pre_sta);
  strcat(_status_url, hue_user);
  strcat(_status_url, pos_sta);
  strcat(_status_url, "\0");

  char *pos_act = "/action";
  _action_url = (char *)malloc(strlen(_status_url) + strlen(pos_act) + 1);
  strcpy(_action_url, _status_url);
  strcat(_action_url, pos_act);
  strcat(_action_url, "\0");

  delay(1000);
}

void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    GroupState state = checkStatus();

    if (state.is_on != _lightsOn || previousMillis == 0) {
      drawToggleButton(state.is_on);
      _lightsOn = state.is_on;
    }

    printf("Brightness %d, On %d\n", state.brightness, state.is_on);
    drawBrightness(state.brightness);
    _brightness = state.brightness;

    previousMillis = currentMillis;
  }
  
  if (ts.touched()) {
    TS_Point p;

    while (!ts.bufferEmpty() ) {
      p = ts.getPoint();
      p.x = map(p.x, TS_MINX, TS_MAXX, 0, tft.width());
      p.y = map(p.y, TS_MINY, TS_MAXY, 0, tft.height());
    }

    ts.writeRegister8(STMPE_INT_STA, 0xFF);

    bool interaction = false;
    // Light switch
    if (p.x >= 10 && p.x <= 230 && p.y >= 10 && p.y <= 155) {
      if (!_lightPressed) {
        Serial.println("Toggle");
        _lightPressed = true;
        interaction = true;
      }
    }
    // Low brightness
    else if (p.x >= 10 && p.x <= 55 && p.y >= 165 && p.y <= 210) {
      if (!_lightPressed) {
        _lightPressed = true;
        
        if (_brightness > 0) {
          Serial.println("Low");

          _brightness -= _step;
          if (_brightness < 0) {
            _brightness = 0;
          }

          if (_lightsOn) {
            _lightsOn = false;
          }
          
          interaction = true;
        }
      }
    }
    // High brightness
    else if (p.x >= 185 && p.x <= 230 && p.y >= 165 && p.y <= 210) {
      if (!_lightPressed) {
        _lightPressed = true;
        
        if (_brightness < 254) {
          Serial.println("High");

          _brightness += _step;
          if (_brightness > 254) {
            _brightness = 254;
          }
          
          if (_lightsOn) {
            _lightsOn = false;
          }
          
          interaction = true;
        }
      }
    }

    if (interaction) {
        String message = "{\"on\":";
        message += (_lightsOn ? "false" : "true");
        message += ",\"bri\":";
        message += String(_brightness);
        message += "}";
        
        Serial.print(message);
        
        http.begin(_action_url);
        http.addHeader("Content-Type", "application/json");
        int httpCode = http.sendRequest("PUT", message);

        if(httpCode <= 0) {
            Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
        }

        http.end();
    }
  }
  else {
    if (_lightPressed) {
      _lightPressed = false;
      previousMillis = 0;
    }
  }
}

void drawToggleButton(bool isOn) {
  uint16_t colour;
  if (isOn) {
    colour = ILI9341_GREEN;
  }
  else {
    colour = ILI9341_RED;
  }
  
  tft.fillRoundRect(10, 10, 220, 145, 5, colour);
}

void drawBrightness(uint16_t brightness) {
  uint16_t width = (brightness / 254.0) * 110;
  tft.fillRoundRect(10, 165, 45, 45, 5, ILI9341_RED);
  tft.fillRoundRect(185, 165, 45, 45, 5, ILI9341_RED);
  tft.fillRoundRect(65, 165, 110, 45, 5, ILI9341_RED);

  if (width > 0) {
    tft.fillRoundRect(65, 165, width, 45, 5, ILI9341_GREEN);
  }
}

GroupState checkStatus() {
  GroupState state = {
    false,
    0
  };
  
  http.begin(_status_url);
  //http.addHeader("Content-Type", "application/json");
  
  int httpCode = http.GET();
  if(httpCode != HTTP_CODE_OK) {
    return state;
  }
  
  String payload = http.getString();

  http.end();
  
  const char *js = payload.c_str();

  jsmntok_t tokens[128];
  jsmn_parser parser;

  jsmn_init(&parser);

  int r = jsmn_parse(&parser, js, strlen(js), tokens, 128);

  int i;
  int keys_found = 0;
  for (i = 1; i < r; i++) {
    jsmntok_t token = tokens[i];
    
    if (token.type == JSMN_STRING) {
      char *key = getTokenValue(token, js);
      
      if (strcmp(key, "on") == 0) {
        int n = i + 1;
        char *val = getTokenValue(tokens[n], js);
        
        if (strcmp(val, "true") == 0) {
          state.is_on = true;
        }

        if (++keys_found == STATE_KEY_COUNT) {
          break;
        }
      }
      else if (strcmp(key, "bri") == 0) {
        int n = i + 1;

        state.brightness = atoi(getTokenValue(tokens[n], js));

        if (++keys_found == STATE_KEY_COUNT) {
          break;
        }
      }
    }
  }

  return state;
}

char *getTokenValue(jsmntok_t token, const char *js) {
  int tokenSize = (token.end - token.start);
  char *val = (char *) malloc(sizeof(char) * (tokenSize + 1));

  memcpy(val, &js[token.start], tokenSize);
  val[tokenSize] = '\0';
  
  return val;
}

