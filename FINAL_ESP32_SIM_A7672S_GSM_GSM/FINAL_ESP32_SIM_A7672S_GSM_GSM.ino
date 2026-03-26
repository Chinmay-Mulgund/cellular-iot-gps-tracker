/* * ======================================================================================
 * PROJECT: DIMENSION SIX TECHNOLOGIES - LIVE GPS BIKE TRACKER 
 * ======================================================================================
 * * SYSTEM OVERVIEW:
 * 1. ESP32 + A7672S reads LIVE GPS coordinates from satellites.
 * 2. Sends JSON data over 4G Airtel to EMQX Broker.
 * * ======================================================================================
 */

#include <HardwareSerial.h>

#define RX_PIN 17
#define TX_PIN 16
#define BAUDRATE 115200

// --- SETTINGS ---
String broker = "tcp://broker.emqx.io:1883"; 
String clientID = "dim-six-bike-" + String(random(1000, 9999)); 
String topic = "dim-six/live/gps"; 

HardwareSerial gsm(2);

// Variables to hold the live coordinates
float liveLat = 0.0;
float liveLng = 0.0;

void setup() {
  Serial.begin(115200);
  gsm.begin(BAUDRATE, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(1000);

  Serial.println("\n=== DIMENSION SIX: LIVE GPS TRACKER ===");
  sendAT("AT"); 

  // 1. TURN ON GPS
  Serial.println("1. Powering on GPS Chip...");
  sendAT("AT+CGNSSPWR=1"); 
  delay(2000);

  // 2. NETWORK
  Serial.println("2. Configuring 4G Network...");
  sendAT("AT+CGDCONT=1,\"IP\",\"airtelgprs.com\""); 
  sendAT("AT+CGACT=1,1"); 
  delay(1000);
  
  // 3. MQTT START
  Serial.println("3. Starting MQTT...");
  sendAT("AT+CMQTTSTOP"); delay(500); 
  sendAT("AT+CMQTTSTART"); delay(500); 

  // 4. ACQUIRE CLIENT & CONNECT
  gsm.println("AT+CMQTTACCQ=0,\"" + clientID + "\",0"); 
  delay(500); readResponse();

  Serial.println("4. Connecting to Broker...");
  gsm.println("AT+CMQTTCONNECT=0,\"" + broker + "\",60,1"); 
  delay(2000); readResponse();
}

void loop() {
  Serial.println("\n--------------------------------");

  // 1. RECONNECT CHECK
  if (!isConnected()) {
    Serial.println("! Reconnecting to MQTT...");
    gsm.println("AT+CMQTTCONNECT=0,\"" + broker + "\",60,1");
    delay(2000); readResponse();
  }

  // 2. READ LIVE GPS
  Serial.println("Searching for GPS Satellites...");
  if (updateGPS()) {
    Serial.print("   📍 FIX ACQUIRED! Lat: "); Serial.print(liveLat, 6);
    Serial.print(", Lng: "); Serial.println(liveLng, 6);

    // 3. CREATE JSON PAYLOAD
    String payload = "{\"lat\":" + String(liveLat, 6) + ",\"lng\":" + String(liveLng, 6) + "}";
    
    // 4. FAST PUBLISH SEQUENCE
    gsm.print("AT+CMQTTTOPIC=0,"); 
    gsm.println(topic.length());
    delay(50); readResponse(); 
    gsm.println(topic); 
    delay(50); readResponse();

    gsm.print("AT+CMQTTPAYLOAD=0,"); 
    gsm.println(payload.length());
    delay(50); readResponse();
    gsm.println(payload); 
    delay(50); readResponse();

    gsm.println("AT+CMQTTPUB=0,0,60"); 
    
    // Wait for Publish Success
    long start = millis();
    while(millis() - start < 2000) {
      if(gsm.available()) {
        String r = gsm.readString();
        if(r.indexOf("+CMQTTPUB: 0,0") != -1) {
           Serial.println("   >>> Published to EMQX successfully.");
           break; 
        }
      }
    }
  } else {
    // If no GPS fix, it will wait 3 seconds and try again.
    // It will NOT push empty data to Firebase to prevent map crashes.
    Serial.println("   ⏳ Waiting for GPS Fix (Ensure antenna is outside!)");
  }
  
  delay(3000); 
}

// ==================================================================
// --- GPS PARSING ENGINE ---
// Converts SIMCom NMEA string to Standard Decimal Degrees
// ==================================================================
bool updateGPS() {
  gsm.println("AT+CGNSSINFO");
  delay(500);
  String r = "";
  while(gsm.available()) {
    r += (char)gsm.read();
  }
  
  // Example valid response: +CGNSSINFO: 2,06,,,1907.5000,N,07254.1000,E,...
  // Example empty response: +CGNSSINFO: ,,,,,,,,,,,,,,,
  
  if (r.indexOf(",,,,,") != -1) return false; // No GPS Fix yet
  
  int idx = r.indexOf("+CGNSSINFO: ");
  if (idx == -1) return false;

  String data = r.substring(idx + 12);
  
  // Find commas to separate data
  int c1 = data.indexOf(',');
  int c2 = data.indexOf(',', c1+1);
  int c3 = data.indexOf(',', c2+1);
  int c4 = data.indexOf(',', c3+1);
  int c5 = data.indexOf(',', c4+1);
  int c6 = data.indexOf(',', c5+1);
  int c7 = data.indexOf(',', c6+1);

  // Extract raw Lat/Lng
  String rawLat = data.substring(c4+1, c5);
  String ns = data.substring(c5+1, c6);
  String rawLng = data.substring(c6+1, c7);
  String ew = data.substring(c7+1, data.indexOf(',', c7+1));

  if (rawLat.length() < 2 || rawLng.length() < 3) return false;

  // MATH: Convert from "Degrees-Minutes" to "Decimal Degrees"
  
  // Latitude: 1907.5000 -> 19 Degrees, 07.5000 Minutes
  float latDeg = rawLat.substring(0, 2).toFloat();
  float latMin = rawLat.substring(2).toFloat();
  liveLat = latDeg + (latMin / 60.0);
  if (ns == "S") liveLat *= -1.0;

  // Longitude: 07254.1000 -> 072 Degrees, 54.1000 Minutes
  float lngDeg = rawLng.substring(0, 3).toFloat();
  float lngMin = rawLng.substring(3).toFloat();
  liveLng = lngDeg + (lngMin / 60.0);
  if (ew == "W") liveLng *= -1.0;

  return true;
}

// --- STANDARD HELPER FUNCTIONS ---
bool isConnected() {
  gsm.println("AT+CMQTTCONNECT?"); 
  delay(100); 
  String r = "";
  while(gsm.available()) r += (char)gsm.read();
  if(r.indexOf("tcp://") != -1) return true;
  return false;
}

void sendAT(String cmd) {
  gsm.println(cmd);
  delay(200);
  while(gsm.available()) Serial.write(gsm.read());
}

void readResponse() {
  while(gsm.available()) Serial.write(gsm.read()); 
}