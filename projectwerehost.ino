#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DFRobotDFPlayerMini.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const char* WIFI_SSID   = "Ohayoo";
const char* WIFI_PASS   = "ford362f";
const char* TB_SERVER   = "thingsboard.cloud";
const int   TB_PORT     = 1883;
const char* TB_TOKEN    = "Z8r7WgGk9DgZY3yJrwAB";


#define DF_RX_PIN    16  
#define DF_TX_PIN    17  
#define DF_BUSY_PIN  4   
#define SERVO_L_PIN  5   
#define SERVO_R_PIN  6  
#define OLED_SCL     7  
#define OLED_SDA     15  

#define SERVO_POS_0   1638
#define SERVO_POS_45  3277

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
DFRobotDFPlayerMini myDFPlayer;
Adafruit_SSD1306 display(128, 64, &Wire, -1);

String lastEventKey = "";
unsigned long lastServoMove = 0;
bool servoUp = false;

void drawSun() {
  display.clearDisplay();
  int cx = 64, cy = 28, r = 12;
  display.fillCircle(cx, cy, r, SSD1306_WHITE);
  for (int i = 0; i < 360; i += 45) {
    float rad = i * PI / 180.0;
    display.drawLine(
      cx + cos(rad) * (r + 4), cy + sin(rad) * (r + 4),
      cx + cos(rad) * (r + 9), cy + sin(rad) * (r + 9),
      SSD1306_WHITE
    );
  }
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(34, 52);
  display.print("DAY PHASE");
  display.display();
}

void drawMoon() {
  display.clearDisplay();
  int cx = 64, cy = 28, r = 14;
  display.fillCircle(cx, cy, r, SSD1306_WHITE);
  display.fillCircle(cx + 6, cy - 4, r - 2, SSD1306_BLACK);
  display.drawPixel(35, 18, SSD1306_WHITE);
  display.drawPixel(92, 15, SSD1306_WHITE);
  display.drawPixel(96, 36, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(28, 52);
  display.print("NIGHT PHASE");
  display.display();
}

void handleGameState(JsonObject data) {
  String phase     = data["phase"] | "";
  String nightStep = data["nightStep"] | "";
  int roundNum     = data["round"] | 0;
  String winner    = data["winner"] | "";
  bool hasDeath    = data["hasDeath"] | false;

  String eventKey = phase + "_" + nightStep + "_" + String(roundNum) + "_" + winner + "_" + String(hasDeath);
  if (eventKey == lastEventKey || phase == "") return;
  lastEventKey = eventKey;

  Serial.println("New Event: " + eventKey);

  int trackToPlay = 0;

  if (phase == "reveal" || phase == "lobby") {
    drawSun();
    trackToPlay = 1;
  } 
  else if (phase == "night") {
    drawMoon();
    if (nightStep == "werewolf")       trackToPlay = 3;
    else if (nightStep == "bodyguard") trackToPlay = 4;
    else if (nightStep == "seer")      trackToPlay = 5;
    else                               trackToPlay = 2;
  } 
  else if (phase == "dawn") {
    drawSun();
    trackToPlay = hasDeath ? 7 : 6;
  } 
  else if (phase == "discuss") {
    drawSun();
    trackToPlay = 8;
  } 
  else if (phase == "vote") {
    drawSun();
    trackToPlay = 9;
  } 
  else if (phase == "end") {
    drawSun();
    trackToPlay = (winner == "werewolf") ? 11 : 10;
  }

  if (trackToPlay > 0) {
    myDFPlayer.play(trackToPlay);
    delay(150);
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) return;

  if (doc.containsKey("shared")) {
    handleGameState(doc["shared"].as<JsonObject>());
  } else {
    handleGameState(doc.as<JsonObject>());
  }
}

void connectWiFiAndMQTT() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("\n[OK] WiFi Connected");
  }

  while (!mqtt.connected()) {
    if (mqtt.connect("WEREHOST_ESP32S3", TB_TOKEN, NULL)) {
      Serial.println("[OK] ThingsBoard MQTT Connected");
      mqtt.subscribe("v1/devices/me/attributes");
    } else {
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(DF_BUSY_PIN, INPUT);

  Wire.begin(OLED_SDA, OLED_SCL);
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    drawSun();
  }

  ledcAttach(SERVO_L_PIN, 50, 16);
  ledcAttach(SERVO_R_PIN, 50, 16);
  ledcWrite(SERVO_L_PIN, SERVO_POS_0);
  ledcWrite(SERVO_R_PIN, SERVO_POS_0);

  Serial2.begin(9600, SERIAL_8N1, DF_RX_PIN, DF_TX_PIN);
  if (myDFPlayer.begin(Serial2)) {
    myDFPlayer.volume(22);
  }

  mqtt.setServer(TB_SERVER, TB_PORT);
  mqtt.setCallback(mqttCallback);
  connectWiFiAndMQTT();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED || !mqtt.connected()) {
    connectWiFiAndMQTT();
  }
  mqtt.loop();

  bool isPlaying = (digitalRead(DF_BUSY_PIN) == LOW);
  if (isPlaying) {
    if (millis() - lastServoMove >= 300) {
      lastServoMove = millis();
      servoUp = !servoUp;
      int duty = servoUp ? SERVO_POS_45 : SERVO_POS_0;
      ledcWrite(SERVO_L_PIN, duty);
      ledcWrite(SERVO_R_PIN, duty);
    }
  } else if (servoUp) {
    servoUp = false;
    ledcWrite(SERVO_L_PIN, SERVO_POS_0);
    ledcWrite(SERVO_R_PIN, SERVO_POS_0);
  }
}