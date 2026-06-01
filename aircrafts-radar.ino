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

const int BUTTON_PIN = 9;
// 8 = haut droite
// 9 = bas gauche
// 10 = bas droite

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
// GESTION DU THÈME (Jour / Nuit)
// --------------------------------------------------------
bool isLightTheme = false;
int buttonState = HIGH;
int lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
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
    color_grid_main = tft.color565(150, 200, 150); // Vert très clair
    color_grid_sub = TFT_LIGHTGREY;
    color_sweep = TFT_DARKGREEN;
    color_plane = TFT_BLUE; // Les avions bleus ressortent mieux sur fond blanc !
    color_zero = TFT_RED;   // Le jaune est illisible sur blanc
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
const float OFFSET = 0.15;

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

struct Plane {
  int x;
  int y;
  float heading;
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
  
  // Configuration du bouton
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  updateThemeColors(); // Initialise les couleurs au démarrage

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

  // --- GESTION DU BOUTON (Changement de thème) ---
  int reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonState) {
    lastDebounceTime = currentMillis;
  }
  if ((currentMillis - lastDebounceTime) > debounceDelay) {
    if (reading == LOW && buttonState == HIGH) { // On détecte l'appui
      isLightTheme = !isLightTheme;
      updateThemeColors();
      tft.fillScreen(color_bg); // Rafraîchissement total de l'écran avec le nouveau fond
    }
    buttonState = reading;
  }
  lastButtonState = reading;

  // --- CALL API ---
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
            
            // Effacement des anciens triangles avec la couleur de fond actuelle
            for (int i = 0; i < plane_count; i++) {
              drawAircraftTriangle(planes_list[i].x, planes_list[i].y, planes_list[i].heading, color_bg);
            }
            plane_count = 0; 
            
            if(!states.isNull()) {
                for (JsonArray state : states) {
                  if (plane_count < 50) { 
                    float lon = state[5].isNull() ? 0.0 : state[5].as<float>(); 
                    float lat = state[6].isNull() ? 0.0 : state[6].as<float>(); 
                    float heading = state[10].isNull() ? 0.0 : state[10].as<float>();
                    
                    if (lon != 0.0 && lat != 0.0) {
                      planes_list[plane_count].x = mapFloat(lon, LOMIN, LOMAX, 0, WIDTH);
                      planes_list[plane_count].y = mapFloat(lat, LAMIN, LAMAX, HEIGHT, 0);
                      planes_list[plane_count].heading = heading;
                      plane_count++;
                    }
                  }
                }
            }
          }
        }
        http.end();
      }
    }
    lastApiUpdate = currentMillis; 
  }

  // --- ANIMATION et DESSIN ---
  
  // Effacer l'ancienne ligne avec le fond actuel
  float old_rad = sweepAngle * PI / 180.0;
  int old_end_x = CENTER_X + RADIUS * sin(old_rad);
  int old_end_y = CENTER_Y - RADIUS * cos(old_rad);
  tft.drawLine(CENTER_X, CENTER_Y, old_end_x, old_end_y, color_bg);

  sweepAngle += SWEEP_SPEED;
  if (sweepAngle >= 360.0) sweepAngle = 0.0;

  // Redessiner les cercles avec les couleurs du thème
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

  // Dessiner les avions avec la couleur du thème
  for (int i = 0; i < plane_count; i++) {
    drawAircraftTriangle(planes_list[i].x, planes_list[i].y, planes_list[i].heading, color_plane);
  }

  // Tracer le faisceau radar
  float rad = sweepAngle * PI / 180.0;
  int end_x = CENTER_X + RADIUS * sin(rad);
  int end_y = CENTER_Y - RADIUS * cos(rad);
  tft.drawLine(CENTER_X, CENTER_Y, end_x, end_y, color_sweep);

  // Affichage du nombre d'avions
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

  delay(15); 
}
