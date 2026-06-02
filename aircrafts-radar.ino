#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h> 
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <math.h>

// --------------------------------------------------------
// CONFIGURATION MATÉRIELLE
// --------------------------------------------------------
TFT_eSPI tft = TFT_eSPI(); 
#define TFT_BL 11 

const int BUTTON_THEME_PIN = 10; // Bouton bas-droite
const int BUTTON_PAGE_PIN = 8;  // Bouton haut-droite 
// 10 = Bouton bas-gauche

const char* ssid = "Wifi-name"; 
const char* password = "Wifi-password"; 

const char* clientID = "opensky-network-clientID";
const char* clientSecret = "opensky-network-secret";

const int WIDTH = 128;
const int HEIGHT = 128;
const int CENTER_X = 64; 
const int CENTER_Y = 64;
const int RADIUS = 60;  

// --------------------------------------------------------
// GESTION DU THÈME ET DES PAGES
// --------------------------------------------------------
bool isLightTheme = false;
int currentPage = 0; // 0 = Radar, 1 = Tableau d'affichage
bool needRedrawList = true; 

// Variables anti-rebond 
int themeButtonState = HIGH;
int lastThemeButtonState = HIGH;
unsigned long lastThemeDebounceTime = 0;
int pageButtonState = HIGH;
int lastPageButtonState = HIGH;
unsigned long lastPageDebounceTime = 0;

unsigned long debounceDelay = 50;

// Variables de couleurs
uint16_t color_bg;
uint16_t color_text;
uint16_t color_grid_main;
uint16_t color_grid_sub;
uint16_t color_sweep;
uint16_t color_plane;
uint16_t color_zero;

void updateThemeColors() {
  if (isLightTheme) {
    color_bg = TFT_WHITE;
    color_text = TFT_BLACK;
    color_grid_main = tft.color565(150, 200, 150); 
    color_grid_sub = TFT_LIGHTGREY;
    color_sweep = TFT_DARKGREEN;
    color_plane = TFT_BLUE; 
    color_zero = TFT_RED;   
  } else {
    color_bg = TFT_BLACK;
    color_text = TFT_WHITE;
    color_grid_main = TFT_DARKGREEN;
    color_grid_sub = TFT_DARKGREY;
    color_sweep = TFT_GREEN;
    color_plane = TFT_RED;
    color_zero = TFT_YELLOW;
  }
}

// --------------------------------------------------------
// CONFIGURATION API OPENSKY (Paris)
// --------------------------------------------------------
const float CENTER_LAT = 48.862392;
const float CENTER_LON = 2.467920;
const float OFFSET = 0.20;

const float LAMIN = CENTER_LAT - OFFSET; 
const float LAMAX = CENTER_LAT + OFFSET; 
const float LOMIN = CENTER_LON - OFFSET;  
const float LOMAX = CENTER_LON + OFFSET;  

String apiUrl = "https://opensky-network.org/api/states/all?lamin=" + String(LAMIN, 6) + "&lomin=" + String(LOMIN, 6) + "&lamax=" + String(LAMAX, 6) + "&lomax=" + String(LOMAX, 6);

// --------------------------------------------------------
// VARIABLES GLOBALES 
// --------------------------------------------------------
unsigned long lastApiUpdate = 0;
const unsigned long UPDATE_INTERVAL = 22000; 

String accessToken = ""; 
unsigned long tokenFetchTime = 0; 

float sweepAngle = 0.0;
const float SWEEP_SPEED = 4.0; 

// STRUCTURE AVION 
struct Plane {
  int x;
  int y;
  float heading;
  String callsign;
  int altitude;
  int speed;
};
Plane planes_list[50]; 
int plane_count = 0;

// --------------------------------------------------------
// FONCTIONS 
// --------------------------------------------------------
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void drawAircraftTriangle(int x, int y, float heading, uint16_t color) {
  int size = 4;
  float h_rad = heading * PI / 180.0; 
  
  float px1 = 0, py1 = -size;
  float px2 = -size / 1.5, py2 = size;
  float px3 = size / 1.5, py3 = size;

  int rx1 = round(x + (px1 * cos(h_rad) - py1 * sin(h_rad)));
  int ry1 = round(y + (px1 * sin(h_rad) + py1 * cos(h_rad)));
  int rx2 = round(x + (px2 * cos(h_rad) - py2 * sin(h_rad)));
  int ry2 = round(y + (px2 * sin(h_rad) + py2 * cos(h_rad)));
  int rx3 = round(x + (px3 * cos(h_rad) - py3 * sin(h_rad)));
  int ry3 = round(y + (px3 * sin(h_rad) + py3 * cos(h_rad)));

  tft.fillTriangle(rx1, ry1, rx2, ry2, rx3, ry3, color);
  tft.drawTriangle(rx1, ry1, rx2, ry2, rx3, ry3, color);
}

// --------------------------------------------------------
// INITIALISATION
// --------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Configuration des boutons
  pinMode(BUTTON_THEME_PIN, INPUT_PULLUP);
  pinMode(BUTTON_PAGE_PIN, INPUT_PULLUP);
  
  updateThemeColors(); 

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.invertDisplay(false); 
  tft.setRotation(0); 
  tft.fillScreen(color_bg);
  
  tft.setTextColor(color_text, color_bg);
  tft.drawString("Connexion WiFi...", 10, 60);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  tft.fillScreen(color_bg);
}

// --------------------------------------------------------
// BOUCLE MAIN 
// --------------------------------------------------------
void loop() {
  unsigned long currentMillis = millis();

  // GESTION DU BOUTON THÈME 
  int themeReading = digitalRead(BUTTON_THEME_PIN);
  if (themeReading != lastThemeButtonState) lastThemeDebounceTime = currentMillis;
  if ((currentMillis - lastThemeDebounceTime) > debounceDelay) {
    if (themeReading == LOW && themeButtonState == HIGH) { 
      isLightTheme = !isLightTheme;
      updateThemeColors();
      tft.fillScreen(color_bg); 
      needRedrawList = true; // Force la liste à se redessiner
    }
    themeButtonState = themeReading;
  }
  lastThemeButtonState = themeReading;

  // GESTION DU BOUTON PAGE 
  int pageReading = digitalRead(BUTTON_PAGE_PIN);
  if (pageReading != lastPageButtonState) lastPageDebounceTime = currentMillis;
  if ((currentMillis - lastPageDebounceTime) > debounceDelay) {
    if (pageReading == LOW && pageButtonState == HIGH) { 
      currentPage = (currentPage == 0) ? 1 : 0; // Bascule entre 0 et 1
      tft.fillScreen(color_bg);
      needRedrawList = true; 
    }
    pageButtonState = pageReading;
  }
  lastPageButtonState = pageReading;

  // CALL API
  if (currentMillis - lastApiUpdate >= UPDATE_INTERVAL || lastApiUpdate == 0) {
    
    if (WiFi.status() == WL_CONNECTED) {
      WiFiClientSecure client;
      client.setInsecure(); 

      if (accessToken == "" || currentMillis - tokenFetchTime > 3600000) {
        HTTPClient authHttp;
        authHttp.begin(client, "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token");
        authHttp.addHeader("Content-Type", "application/x-www-form-urlencoded");
        
        String authPayload = "grant_type=client_credentials&client_id=" + String(clientID) + "&client_secret=" + String(clientSecret);
        
        int authCode = authHttp.POST(authPayload);
        if (authCode == HTTP_CODE_OK) {
          String authResponse = authHttp.getString();
          DynamicJsonDocument authDoc(8192);
          DeserializationError err = deserializeJson(authDoc, authResponse);
          if (!err) {
            accessToken = authDoc["access_token"].as<String>();
            tokenFetchTime = currentMillis;
          }
        }
        authHttp.end();
      }

      if (accessToken != "") {
        HTTPClient http;
        http.begin(client, apiUrl);
        http.setUserAgent("Mozilla/5.0 (ESP32 Radar)");
        http.addHeader("Authorization", "Bearer " + accessToken); 
        
        int httpResponseCode = http.GET();
        if (httpResponseCode == HTTP_CODE_OK) {
          String payload = http.getString();
          DynamicJsonDocument doc(8192); 
          DeserializationError error = deserializeJson(doc, payload);
          
          if (!error) {
            JsonArray states = doc["states"];
            
            // Effacement des anciens triangles sur le radar
            if (currentPage == 0) {
              for (int i = 0; i < plane_count; i++) {
                drawAircraftTriangle(planes_list[i].x, planes_list[i].y, planes_list[i].heading, color_bg);
              }
            }
            plane_count = 0; 
            
            if(!states.isNull()) {
                for (JsonArray state : states) {
                  if (plane_count < 50) { 
                    float lon = state[5].isNull() ? 0.0 : state[5].as<float>(); 
                    float lat = state[6].isNull() ? 0.0 : state[6].as<float>(); 
                    float heading = state[10].isNull() ? 0.0 : state[10].as<float>();
                    bool onGround = state[8].isNull() ? false : state[8].as<bool>();
                    
                    if (lon != 0.0 && lat != 0.0 && !onGround) {
                      planes_list[plane_count].x = mapFloat(lon, LOMIN, LOMAX, 0, WIDTH);
                      planes_list[plane_count].y = mapFloat(lat, LAMIN, LAMAX, HEIGHT, 0);
                      planes_list[plane_count].heading = heading;
                      
                      // MISE A JOUR DU TABLEAU 
                      String callsign = state[1].isNull() ? "????" : state[1].as<String>();
                      callsign.trim(); // Enlève les espaces inutiles
                      if(callsign == "") callsign = "Inconnu";
                      planes_list[plane_count].callsign = callsign;
                      
                      planes_list[plane_count].altitude = state[7].isNull() ? 0 : (int)state[7].as<float>();
                      // Convertion de la vitesse de m/s à km/h
                      planes_list[plane_count].speed = state[9].isNull() ? 0 : (int)(state[9].as<float>() * 3.6);
                      
                      plane_count++;
                    }
                  }
                }
                // Tri par vitesse de vol
                for (int i = 0; i < plane_count - 1; i++) {
                  for (int j = i + 1; j < plane_count; j++) {
                    if (planes_list[j].speed > planes_list[i].speed) {
                      Plane temp = planes_list[i];
                      planes_list[i] = planes_list[j];
                      planes_list[j] = temp;
                    }
                  }
                }
            }
            needRedrawList = true; // Refresh du tableau
          }
        }
        http.end();
      }
    }
    lastApiUpdate = currentMillis; 
  }

  // ANIMATION et DESSIN
  
  if (currentPage == 0) {
    // ==========================================
    // PAGE 0 : LE RADAR
    // ==========================================
    float old_rad = sweepAngle * PI / 180.0;
    int old_end_x = CENTER_X + RADIUS * sin(old_rad);
    int old_end_y = CENTER_Y - RADIUS * cos(old_rad);
    tft.drawLine(CENTER_X, CENTER_Y, old_end_x, old_end_y, color_bg);

    sweepAngle += SWEEP_SPEED;
    if (sweepAngle >= 360.0) sweepAngle = 0.0;

    tft.drawCircle(CENTER_X, CENTER_Y, RADIUS, color_grid_main);
    tft.drawCircle(CENTER_X, CENTER_Y, (RADIUS * 2) / 3, color_grid_sub); 
    tft.drawCircle(CENTER_X, CENTER_Y, RADIUS / 3, color_grid_sub);       
    
    tft.drawLine(CENTER_X, 0, CENTER_X, HEIGHT, color_grid_sub); 
    tft.drawLine(0, CENTER_Y, WIDTH, CENTER_Y, color_grid_sub); 
    tft.fillCircle(CENTER_X, CENTER_Y, 2, color_plane); 

    tft.setTextColor(color_text, color_bg); 
    tft.drawString("N", CENTER_X - 2, 0);
    tft.drawString("S", CENTER_X - 2, HEIGHT - 8);
    tft.drawString("E", WIDTH - 6, CENTER_Y - 4);
    tft.drawString("W", 0, CENTER_Y - 4);

    tft.setTextColor(color_grid_sub, color_bg);
    int max_km = round(OFFSET * 111.0); 
    tft.drawString(String(max_km / 3) + "km", CENTER_X + 2, CENTER_Y - (RADIUS / 3) - 8); 
    tft.drawString(String((max_km * 2) / 3) + "km", CENTER_X + 8, CENTER_Y - ((RADIUS * 2) / 3) - 8);
    tft.drawString(String(max_km) + "km", CENTER_X + 15, CENTER_Y - RADIUS - 2); 

    for (int i = 0; i < plane_count; i++) {
      drawAircraftTriangle(planes_list[i].x, planes_list[i].y, planes_list[i].heading, color_plane);
    }

    float rad = sweepAngle * PI / 180.0;
    int end_x = CENTER_X + RADIUS * sin(rad);
    int end_y = CENTER_Y - RADIUS * cos(rad);
    tft.drawLine(CENTER_X, CENTER_Y, end_x, end_y, color_sweep);

    if (plane_count == 0) {
      tft.setTextColor(color_zero, color_bg);
    } else {
      tft.setTextColor(color_text, color_bg);
    }
    char buf[15];
    if (plane_count <= 1) {
      sprintf(buf, "%d avion   ", plane_count); 
    } else {
      sprintf(buf, "%d avions ", plane_count);
    }
    if (plane_count < 10) {
      tft.drawString(buf, 80, 115); 
    } else {
      tft.drawString(buf, 75, 115); 
    }
    
  } else if (currentPage == 1) {
    // ==========================================
    // PAGE 1 : LE TABLEAU D'AFFICHAGE
    // ==========================================
    if (needRedrawList) {
      tft.fillScreen(color_bg);
      
      // En-tête du tableau
      tft.setTextColor(color_grid_sub, color_bg);
      tft.drawString("VOL", 0, 5);
      tft.drawString("ALT.", 55, 5);
      tft.drawString("KM/H", 95, 5);
      tft.drawLine(0, 15, 128, 15, color_grid_sub); // Ligne de séparation

      // Liste des avions
      int yOffset = 20; // Marge de 20 pixels en haut
      
      if (plane_count == 0) {
        tft.setTextColor(color_zero, color_bg);
        tft.drawString("Aucun vol detecte", 10, 40);
      } else {
        // Maximum 10 lignes
        for (int i = 0; i < plane_count && i < 10; i++) {
          
          tft.setTextColor(color_plane, color_bg);
          tft.drawString(planes_list[i].callsign, 0, yOffset);
          
          tft.setTextColor(color_text, color_bg);
          tft.drawString(String(planes_list[i].altitude) + "m", 55, yOffset);
          tft.drawString(String(planes_list[i].speed), 95, yOffset);
          
          yOffset += 10; // On descend d'une ligne
        }
      }
      needRedrawList = false; // Attente de la prochaine mise à jour API
    }
  }

  delay(15); 
}
