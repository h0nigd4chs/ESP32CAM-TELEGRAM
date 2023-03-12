/*
ESP32-CAM und Telegram Bot IoT
Author : h0nigd4chs Stuttgart,Germany 12.03.2023
*/

//WIFI-Verbindung herstellen und mit Telegrambot verbinden
const char* ssid     = "FRITZ!Box 6590 Cable MJ";   //deine Netzwerk SSID 
const char* password = "66129488961618186828";   //dein Netzwerk Passwort

String myToken = "5880437840:AAFl6p_GYZbTLXZAXgMap45ud9CN69zlZMo";   // Erstelle deinen eigenen Bot und hol dir das Token -> https://telegram.me/fatherbot
String myChatId = "5016805884";        // Chat ID ermitteln -> https://telegram.me/get-id-bot or https://telegram.me/userinfobot

/*
Wenn die boolsche Variable "sendHelp" auf "true" gestellt ist, bekommt man bei jedem
Boot-Vorgang eine Liste mit den Befehlen im Telegram-Chat. Sollte man sie auf "false" setzen,
bekommt man die Befehle nur noch in dem man "/help" im Chat schreibt.
*/
boolean sendHelp = true;   


#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"             //Benötigen wir für eine Stromversorgung und Neustartfunktion
#include "soc/rtc_cntl_reg.h"    //Benötigen wir für eine Stromversorgung und Neustartfunktion 
#include "esp_camera.h"          //Benötigen wir für die Videofunktion
#include <ArduinoJson.h>         //Benötigen wir um Json-Formate zu verarbeiten

//Konfiguration der ESP32-CAM 
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

int pinPIR = 13; // Falls ein PIR Sensor benutzt wird, an diesen Pin anschließen

WiFiClientSecure client_tcp;
long message_id_last = 0;

void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  //Soll Neustarten falls Stromversorgung instabil
    
  Serial.begin(115200);
  Serial.setDebugOutput(true);  //Im Serial Monitor sollen Debugergebnisse ausgegeben werden
  Serial.println();

  //Einstellung für die spätere Videokonfiguration
  camera_config_t config;
  config.grab_mode = CAMERA_GRAB_LATEST;  // h0nigd4chs hat diese zeile geaddet weil immer erst das dritte bild geschickt wurde. DAS HIER LÖST DAS PROBLEM!
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

  
  //
  // WARNING!!! PSRAM IC required for UXGA resolution and high JPEG quality
  //            Ensure ESP32 Wrover Module or other board with PSRAM is selected
  //            Partial images will be transmitted if image exceeds buffer size
  //   
  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.

  
  if(psramFound()){  //Hier wird überprüft ob es PSRAM gibt
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  //Start des Videos(Initialisierung)
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    ESP.restart();
  }

  //Die Standardgröße des Videoframes kann angepasst werden (Auflösung)
  sensor_t * s = esp_camera_sensor_get();
  // Sensoren sind vertikal gedreht und die Farb Sättigung leicht angepasst
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1); // zurückdrehen vertikal
    s->set_brightness(s, 1); // erhöht ganz leicht die Helligkeit
    s->set_saturation(s, -2); // senkt die Sättigung
  }
  // niedrige Auflösung um hohe Framerate zu gewährleisten beim Start
  s->set_framesize(s, FRAMESIZE_VGA);    //Auflösungsoptionen: UXGA(1600x1200), SXGA(1280x1024), XGA(1024x768), SVGA(800x600), VGA(640x480), CIF(400x296), QVGA(320x240), HQVGA(240x176), QQVGA(160x120), QXGA(2048x1564 for OV3660)

  //s->set_vflip(s, 1);  //Vertikale Drehung
  //s->set_hmirror(s, 1);  //Horizontale Spiegelung
          
  //FLASH-LED(GPIO4) bzw. Blitzlicht
  ledcAttachPin(4, 4);  
  ledcSetup(4, 5000, 8); 
  
  WiFi.mode(WIFI_STA);  //mögliche WIFI Modis: WiFi.mode(WIFI_AP); WiFi.mode(WIFI_STA);

  //Hier wird die Statische IP konfiguriert( falls der router keine vergibt)
  //WiFi.config(IPAddress(192, 168, 201, 100), IPAddress(192, 168, 201, 2), IPAddress(255, 255, 255, 0));

  for (int i=0;i<2;i++) {
    WiFi.begin(ssid, password);    //Hier wird die Netzwerkverbindung durchgeführt
  
    delay(1000);
    Serial.println("");
    Serial.print("Verbinden mit ");
    Serial.println(ssid);
    
    long int StartTime=millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        if ((StartTime+5000) < millis()) break;    //Dauert ca. 10 Sekunden in der Regel
    } 
  
    if (WiFi.status() == WL_CONNECTED) {    //Folgendes wird ausgegeben wenn die Verbindung hergestellt wurde
      Serial.println("");
      Serial.println("STAIP (statische IP) addresse: ");
      Serial.println(WiFi.localIP());
      Serial.println("");
  
      for (int i=0;i<5;i++) {   //Die FLASH-LED soll ein paar mal SCHNELL blinken sobald mit WIFI verbunden
        ledcWrite(4,10);
        delay(200);
        ledcWrite(4,0);
        delay(200);    
      }
      break;
    }
  } 

  if (WiFi.status() != WL_CONNECTED) {    //Falls WIFI-Verbindung nicht klappt
    for (int i=0;i<2;i++) {    //Soll die FLASH-LED ein paar mal LANGSAM blinken
      ledcWrite(4,10);
      delay(1000);
      ledcWrite(4,0);
      delay(1000);    
    }
  } 

  //Hier wird die FLASH-LED auf LOW(AUS) geschaltet
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);
                
  //Hier ist ESP32CAM bereit Nachrichten vom Telegram Bot zu erhalten
  getTelegramMessage(myToken);  
}

void loop()
{
}

//Befehlausführung
void executeCommand(String text) {
  if (!text||text=="") return;
    
  // Benutzerdefinierte Befehle
  if (text=="help"||text=="/help"||text=="/start"||text=="/hilfe") {
    String command = "h0nigd4chs ESP32CAM Telegram h0nig4uge\n/HELP Liste aller Befehle\n/FOTO Live-Screenshot erhalten\n/AN FLASH-LED anschalten\n/AUS FLASH-LED ausschalten\n/NEUSTART Neustart des Boards";

    //Ein Custom Keyboard für Telegram
    //Nur eine Reihe
    //String keyboard = "{\"keyboard\":[[{\"text\":\"/on\"},{\"text\":\"/off\"},{\"text\":\"/capture\"},{\"text\":\"/restart\"}]],\"one_time_keyboard\":false}";
    //Zwei Reihen(Standard)
    String keyboard = "{\"keyboard\":[[{\"text\":\"/AN\"},{\"text\":\"/AUS\"}], [{\"text\":\"/FOTO\"},{\"text\":\"/NEUSTART\"}]],\"one_time_keyboard\":false}";
    
    sendMessage2Telegram(myToken, myChatId, command, keyboard);  //Liste der Übertragungsfunktionen
  } else if (text=="/FOTO") {  //Ein aktuellen Screenshot vom Video bekommen
    sendMessage2Telegram(myToken, myChatId, "Live-Screenshot", "");
    sendCapturedImage2Telegram(myToken, myChatId);
  } else if (text=="/AN") {  //FLASH-LED(Blitz) einschalten
    ledcAttachPin(4, 3);
    ledcSetup(3, 5000, 8);
    ledcWrite(3,10);
    sendMessage2Telegram(myToken, myChatId, "FLASH-LED anschalten", "");
  } else if (text=="/AUS") {  //FLASH-LED(Blitz) ausschalten
    ledcAttachPin(4, 3);
    ledcSetup(3, 5000, 8);
    ledcWrite(3,0);
    sendMessage2Telegram(myToken, myChatId, "FLASH-LED ausschalten", "");
  } else if (text=="/NEUSTART") {  //Das komplette Modul(Gerät) neu starten
    sendMessage2Telegram(myToken, myChatId, "Neustart des Boards", "");
    ESP.restart();
  } else if (text=="null") {   //Diese Zeile muss drinbleiben sonst schickt der Server quatschtexte etc. !!!
    client_tcp.stop();
    getTelegramMessage(myToken);
  } else
    sendMessage2Telegram(myToken, myChatId, "Diesen Befehl gibt es nicht.", "");
}

//Erhalten der neuesten Nachrichten von Telegram
void getTelegramMessage(String token) {
  const char* myDomain = "api.telegram.org";
  String getAll="", getBody = ""; 
  JsonObject obj;
  DynamicJsonDocument doc(1024);
  String result;
  long update_id;
  String message;
  long message_id;
  String text;  

  client_tcp.setInsecure();   //run version 1.0.5 or above
  if (message_id_last == 0) Serial.println("Verbinden mit " + String(myDomain));
  if (client_tcp.connect(myDomain, 443)) {
    if (message_id_last == 0) Serial.println("Verbindung erfolgreich!"); // FREE PALESTINE

    while (client_tcp.connected()) { 
      /*
        //PIR-Sensor hinzufügen
        //Solltest du einen PIR Bewegungssensor in deinem Projekt benutzen, dann kommentiere folgenden Code aus:
        pinMode(pinPIR, INPUT_PULLUP);
        if (digitalRead(pinPIR)==1) {
          sendCapturedImage2Telegram();
        }        
      */
      
      getAll = "";
      getBody = "";

      String request = "limit=1&offset=-1&allowed_updates=message";
      client_tcp.println("POST /bot"+token+"/getUpdates HTTP/1.1");
      client_tcp.println("Host: " + String(myDomain));
      client_tcp.println("Content-Length: " + String(request.length()));
      client_tcp.println("Content-Type: application/x-www-form-urlencoded");
      client_tcp.println("Connection: keep-alive");
      client_tcp.println();
      client_tcp.print(request);
      
      int waitTime = 5000;   // timeout 5 seconds
      long startTime = millis();
      boolean state = false;
      
      while ((startTime + waitTime) > millis()) {
        //Serial.print(".");
        delay(100);      
        while (client_tcp.available()) {
            char c = client_tcp.read();
            if (state==true) getBody += String(c);
            if (c == '\n') {
              if (getAll.length()==0) state=true; 
              getAll = "";
            } else if (c != '\r')
              getAll += String(c);
            startTime = millis();
         }
         if (getBody.length()>0) break;
      }

      //Die letzte Nachricht im JSON Format abrufen
      deserializeJson(doc, getBody);
      obj = doc.as<JsonObject>();
      //result = obj["result"].as<String>();
      //update_id =  obj["result"][0]["update_id"].as<String>().toInt();
      //message = obj["result"][0]["message"].as<String>();
      message_id = obj["result"][0]["message"]["message_id"].as<String>().toInt();
      text = obj["result"][0]["message"]["text"].as<String>();

      if (message_id!=message_id_last&&message_id) {
        int id_last = message_id_last;
        message_id_last = message_id;
        if (id_last==0) {
          message_id = 0;
          if (sendHelp == true)   // Schickt die Liste der Befehle im Telegramchat sobald das Modul bootet.
            text = "/help";
          else
            text = "";
        } else {
          Serial.println(getBody);
          Serial.println();
        }
        
        if (text!="") {
          Serial.println("["+String(message_id)+"] "+text);
          executeCommand(text);
        }
      }
      delay(1000);
    }
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Verbindung gescheitert.");
    WiFi.begin(ssid, password);  
    long int StartTime=millis();
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      if ((StartTime+10000) < millis())  {
        StartTime=millis();
        WiFi.begin(ssid, password);
      }
    }
    Serial.println("Wiederverbindung war erfolgreich.");
  }

  //Die Verbindung zum Server wird getrennt und nach 3 Minuten wieder verbunden
  getTelegramMessage(myToken);   // Client verbindet sich wieder nach dem die Verbindung 3 Minuten getrennt war.
}

//Schickt ein Videoscreenshot an Telegram
void sendCapturedImage2Telegram(String token, String chat_id) {
  const char* myDomain = "api.telegram.org";
  String getAll="", getBody = "";

  camera_fb_t * fb = NULL;
  fb = esp_camera_fb_get();  
  if(!fb) {
    Serial.println("Kamera Screenshot gescheitert");
    delay(1000);
    ESP.restart();
  }  
  
  String head = "--Taiwan\r\nContent-Disposition: form-data; name=\"chat_id\"; \r\n\r\n" + chat_id + "\r\n--Taiwan\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"esp32-cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--Taiwan--\r\n";

  uint16_t imageLen = fb->len;
  uint16_t extraLen = head.length() + tail.length();
  uint16_t totalLen = imageLen + extraLen;

  client_tcp.println("POST /bot"+token+"/sendPhoto HTTP/1.1");
  client_tcp.println("Host: " + String(myDomain));
  client_tcp.println("Content-Length: " + String(totalLen));
  client_tcp.println("Content-Type: multipart/form-data; boundary=Taiwan");
  client_tcp.println("Connection: keep-alive");
  client_tcp.println();
  client_tcp.print(head);

  uint8_t *fbBuf = fb->buf;
  size_t fbLen = fb->len;
  for (size_t n=0;n<fbLen;n=n+1024) {
    if (n+1024<fbLen) {
      client_tcp.write(fbBuf, 1024);
      fbBuf += 1024;
    } else if (fbLen%1024>0) {
      size_t remainder = fbLen%1024;
      client_tcp.write(fbBuf, remainder);
    }
  }  
  
  client_tcp.print(tail);
  
  esp_camera_fb_return(fb);
  
  int waitTime = 10000;   // 10 Sekunden warten
  long startTime = millis();
  boolean state = false;
  
  while ((startTime + waitTime) > millis()) {
    Serial.print(".");
    delay(100);      
    while (client_tcp.available()) {
        char c = client_tcp.read();
        if (state==true) getBody += String(c);      
        if (c == '\n') {
          if (getAll.length()==0) state=true; 
          getAll = "";
        } else if (c != '\r')
          getAll += String(c);
        startTime = millis();
     }
     if (getBody.length()>0) break;
  }
  Serial.println(getBody);
  Serial.println();
}

//Senden Sie eine Nachricht mit der Tase vom benutzerdefinierten Keyboard
void sendMessage2Telegram(String token, String chat_id, String text, String keyboard) {
  const char* myDomain = "api.telegram.org";
  String getAll="", getBody = "";
  
  String request = "parse_mode=HTML&chat_id="+chat_id+"&text="+text;
  if (keyboard!="") request += "&reply_markup="+keyboard;
  
  client_tcp.println("POST /bot"+token+"/sendMessage HTTP/1.1");
  client_tcp.println("Host: " + String(myDomain));
  client_tcp.println("Content-Length: " + String(request.length()));
  client_tcp.println("Content-Type: application/x-www-form-urlencoded");
  client_tcp.println("Connection: keep-alive");
  client_tcp.println();
  client_tcp.print(request);
  
  int waitTime = 5000;   // 5 Sekunden warten
  long startTime = millis();
  boolean state = false;
  
  while ((startTime + waitTime) > millis()) {
    Serial.print(".");
    delay(100);      
    while (client_tcp.available()) {
        char c = client_tcp.read();
        if (state==true) getBody += String(c);      
        if (c == '\n') {
          if (getAll.length()==0) state=true; 
          getAll = "";
        } else if (c != '\r')
          getAll += String(c);
        startTime = millis();
     }
     if (getBody.length()>0) break;
  }
  Serial.println(getBody);
  Serial.println();
} 
