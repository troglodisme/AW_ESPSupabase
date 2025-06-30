#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPSupabaseRealtime.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

SupabaseRealtime realtime;

void HandleChanges(String result) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, result);

  if (error) {
    Serial.println("JSON parsing failed");
    return;
  }

  // Only process actual database changes
  if (doc.containsKey("table") && doc.containsKey("type")) {
    String eventType = doc["type"];
    
    if (eventType == "UPDATE" && doc.containsKey("record")) {
      JsonObject newRecord = doc["record"];
      
      // Check if this is for our device
      if (newRecord.containsKey("device_id")) {
        String deviceId = newRecord["device_id"];
        Serial.println("Device " + deviceId + " settings updated!");
        
        // Show the new desired settings if they exist
        if (newRecord.containsKey("desired")) {
          String desired;
          serializeJson(newRecord["desired"], desired);
          Serial.println("New desired: " + desired);
        }
      }
    }
  }
  // Ignore system messages (Phoenix protocol stuff)
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting Supabase Realtime...");

  // Connect to WiFi
  WiFi.begin("STUDIO_L", "hackney!clouds24");
  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected to WiFi");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Initialize Supabase Realtime
  realtime.begin(
    "https://cszlzkwrpugdncexjkbd.supabase.co",
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImNzemx6a3dycHVnZG5jZXhqa2JkIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NDQ4MzM1ODYsImV4cCI6MjA2MDQwOTU4Nn0.8hurRr4Pk_oc4utH4Nce8B8GHTgU6m3VaBtTobRDGXs",
    HandleChanges);

  // Listen for all events on device_settings table
  realtime.addChangesListener("device_settings", "*", "public", "");

  // Start listening
  realtime.listen();

  Serial.println("Supabase connected. Listening for device updates...");
}

void loop() {
  realtime.loop();

  // Simple status check every 5 minutes (when things are working)
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 300000) {  // 5 minutes
    lastStatus = millis();
    if (realtime.isConnected()) {
      Serial.println("✓ Supabase connected and listening...");
    } else {
      Serial.println("✗ Supabase connection lost - attempting reconnect");
    }
  }
}