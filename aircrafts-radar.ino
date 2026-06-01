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
  Serial.println("\n--- DEMARRAGE RADAR DEBOGAGE ---");
  
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.invertDisplay(false); 
  tft.setRotation(0); 
  tft.fillScreen(TFT_BLACK);
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Connexion WiFi...", 10, 60);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] Connecté avec succès !");
  tft.fillScreen(TFT_BLACK);
}

// --------------------------------------------------------
// BOUCLE MAIN 
// --------------------------------------------------------
void loop() {
  unsigned long currentMillis = millis();

  // CALL API
  if (currentMillis - lastApiUpdate >= UPDATE_INTERVAL || lastApiUpdate == 0) {
    Serial.println("\n--- [API] Lancement du cycle de mise à jour ---");
    
    if (WiFi.status() == WL_CONNECTED) {
      
      WiFiClientSecure client;
      client.setInsecure(); 

      // Token API pour Opensky Network
      if (accessToken == "" || currentMillis - tokenFetchTime > 3600000) {
        Serial.println("[OAuth2] Demande d'un nouveau jeton d'accès...");
        HTTPClient authHttp;
        authHttp.begin(client, "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token");
        authHttp.addHeader("Content-Type", "application/x-www-form-urlencoded");
        
        String authPayload = "grant_type=client_credentials&client_id=" + String(clientID) + "&client_secret=" + String(clientSecret);
        
        int authCode = authHttp.POST(authPayload);
        Serial.print("[OAuth2] Code HTTP reçu : ");
        Serial.println(authCode);

        if (authCode == HTTP_CODE_OK) {
          String authResponse = authHttp.getString();
          DynamicJsonDocument authDoc(8192);
          DeserializationError err = deserializeJson(authDoc, authResponse);
          
          if (!err) {
            accessToken = authDoc["access_token"].as<String>();
            tokenFetchTime = currentMillis;
            Serial.println("[OAuth2] Jeton récupéré avec succès !");
          } else {
            Serial.print("[OAuth2] Erreur décodage JSON jeton : ");
            Serial.println(err.c_str());
          }
        } else {
          Serial.println("[OAuth2] ÉCHEC AUTHENTIFICATION. Vérifie impérativement ton ID et SECRET.");
          String errResponse = authHttp.getString();
          Serial.print("[OAuth2] Réponse serveur : ");
          Serial.println(errResponse);
        }
        authHttp.end();
      }

      // RÉCUPÉRATION DES AVIONS
      if (accessToken != "") {
        Serial.println("[Avions] Requête des données de vol...");
        HTTPClient http;
        http.begin(client, apiUrl);
        http.setUserAgent("Mozilla/5.0 (ESP32 Radar)");
        http.addHeader("Authorization", "Bearer " + accessToken); 
        
        int httpResponseCode = http.GET();
        Serial.print("[Avions] Code HTTP reçu : ");
        Serial.println(httpResponseCode);
        
        if (httpResponseCode == HTTP_CODE_OK) {
          String payload = http.getString();
          Serial.print("[Avions] Taille des données : ");
          Serial.print(payload.length());
          Serial.println(" octets.");

          DynamicJsonDocument doc(8192); 
          DeserializationError error = deserializeJson(doc, payload);
          
          if (!error) {
            JsonArray states = doc["states"];
            
            // Effacement des anciens triangles
            for (int i = 0; i < plane_count; i++) {
              drawAircraftTriangle(planes_list[i].x, planes_list[i].y, planes_list[i].heading, TFT_BLACK);
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
                Serial.print("[Avions] Nombre d'avions décodés : ");
                Serial.println(plane_count);
            } else {
                Serial.println("[Avions] Zone vide (states est null).");
            }
          } else {
            Serial.print("[Avions] Erreur de parsing JSON: ");
            Serial.println(error.c_str());
          }
        } else {
          Serial.print("[Avions] Échec de la requête. Code d'erreur : ");
          Serial.println(httpResponseCode);
        }
        http.end();
      } else {
        Serial.println("[Avions] Requête annulée : Pas de jeton d'accès valide.");
      }
    } else {
      Serial.println("[WiFi] Erreur : Déconnecté du réseau.");
    }
    lastApiUpdate = currentMillis; 
  }

  // ANIMATION et DESSIN
  float old_rad = sweepAngle * PI / 180.0;
  int old_end_x = CENTER_X + RADIUS * sin(old_rad);
  int old_end_y = CENTER_Y - RADIUS * cos(old_rad);
  tft.drawLine(CENTER_X, CENTER_Y, old_end_x, old_end_y, TFT_BLACK);

  sweepAngle += SWEEP_SPEED;
  if (sweepAngle >= 360.0) sweepAngle = 0.0;

  tft.drawCircle(CENTER_X, CENTER_Y, RADIUS, TFT_DARKGREEN);
  tft.drawCircle(CENTER_X, CENTER_Y, (RADIUS * 2) / 3, TFT_DARKGREY); 
  tft.drawCircle(CENTER_X, CENTER_Y, RADIUS / 3, TFT_DARKGREY);       
  
  tft.drawLine(CENTER_X, 0, CENTER_X, HEIGHT, TFT_DARKGREY); 
  tft.drawLine(0, CENTER_Y, WIDTH, CENTER_Y, TFT_DARKGREY); 
  tft.fillCircle(CENTER_X, CENTER_Y, 2, TFT_RED); 

  tft.setTextColor(TFT_WHITE, TFT_BLACK); 
  tft.drawString("N", CENTER_X - 2, 0);
  tft.drawString("S", CENTER_X - 2, HEIGHT - 8);
  tft.drawString("E", WIDTH - 6, CENTER_Y - 4);
  tft.drawString("W", 0, CENTER_Y - 4);

  tft.setTextColor(TFT_DARKGREY);
  int max_km = round(OFFSET * 111.0); 
  tft.drawString(String(max_km / 3) + "km", CENTER_X + 2, CENTER_Y - (RADIUS / 3) - 8); 
  tft.drawString(String((max_km * 2) / 3) + "km", CENTER_X + 8, CENTER_Y - ((RADIUS * 2) / 3) - 8);
  tft.drawString(String(max_km) + "km", CENTER_X + 15, CENTER_Y - RADIUS - 2); 

  for (int i = 0; i < plane_count; i++) {
    drawAircraftTriangle(planes_list[i].x, planes_list[i].y, planes_list[i].heading, TFT_RED);
  }

  float rad = sweepAngle * PI / 180.0;
  int end_x = CENTER_X + RADIUS * sin(rad);
  int end_y = CENTER_Y - RADIUS * cos(rad);
  tft.drawLine(CENTER_X, CENTER_Y, end_x, end_y, TFT_GREEN);

  if (plane_count == 0) {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  } else {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
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
