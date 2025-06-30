#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPSupabaseRealtime.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

SupabaseRealtime realtime;

void HandleChanges(String result)
{
  Serial.println("=== REALTIME EVENT RECEIVED ===");
  Serial.println("Raw result: " + result);
  
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, result);
  
  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    return;
  }
  
  // Pretty print the JSON for debugging
  String prettyJson;
  serializeJsonPretty(doc, prettyJson);
  Serial.println("Parsed JSON:");
  Serial.println(prettyJson);
  
  // Check if this is a postgres_changes event
  if (doc.containsKey("table") && doc.containsKey("type")) {
    String tableName = doc["table"];
    String eventType = doc["type"];
    
    Serial.println("Table: " + tableName);
    Serial.println("Event Type: " + eventType);
    
    // Handle different event types
    if (eventType == "INSERT") {
      if (doc.containsKey("record")) {
        JsonObject newRecord = doc["record"];
        Serial.println("NEW RECORD INSERTED:");
        printDeviceSettings(newRecord);
      }
    }
    else if (eventType == "UPDATE") {
      Serial.println("RECORD UPDATED:");
      if (doc.containsKey("record")) {
        JsonObject newRecord = doc["record"];
        Serial.println("  New values:");
        printDeviceSettings(newRecord);
      }
      if (doc.containsKey("old_record")) {
        JsonObject oldRecord = doc["old_record"];
        Serial.println("  Previous values:");
        printDeviceSettings(oldRecord);
      }
    }
    else if (eventType == "DELETE") {
      if (doc.containsKey("old_record")) {
        JsonObject deletedRecord = doc["old_record"];
        Serial.println("RECORD DELETED:");
        printDeviceSettings(deletedRecord);
      }
    }
  } else {
    Serial.println("This is not a database change event (probably a system message)");
  }
  
  Serial.println("=== END EVENT ===\n");
}

// Remove the helper function since we're not using it anymore

void setup()
{
  Serial.begin(115200); // Increased baud rate for better debugging
  Serial.println("Starting Supabase Realtime Example...");
  
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
    HandleChanges
  );
  
  // Enable authentication for RLS-protected table
  // Create a device-specific user in Supabase auth for each ESP32
  int loginResult = realtime.login_email("device_7c2c6746eedc@yourapp.com", "your_device_password");
  if (loginResult != 200) {
    Serial.printf("Login failed with code: %d\n", loginResult);
    // Handle login failure - maybe fall back to anonymous mode
  } else {
    Serial.println("Device authenticated successfully");
  }
  
  // Add listeners for different events
  // With authentication, you can filter to just this device's settings
  realtime.addChangesListener("device_settings", "*", "public", "device_id=eq.7c2c6746eedc");
  
  // Or listen to all devices if your RLS policies allow it
  // realtime.addChangesListener("device_settings", "*", "public", "");
  
  // You can add filters too, for example:
  // realtime.addChangesListener("device_settings", "*", "public", "device_id=eq.7c2c6746eedc");
  
  // Start listening
  realtime.listen();
  
  Serial.println("Supabase connected. Listening for device updates...");
}

void loop()
{
  // Main realtime loop - this must be called continuously
  realtime.loop();
  
  // Optional: Print connection status every 30 seconds
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 30000) {
    lastStatus = millis();
    realtime.printConnectionStatus();
  }
  
  // Optional: Add other code here, but avoid long delays
  // Any delays here will affect the realtime connection
}

/*
 * TESTING:
 * 
 * To test this code:
 * 1. Upload this sketch to your ESP32
 * 2. Open Serial Monitor at 115200 baud
 * 3. Go to your Supabase dashboard
 * 4. Navigate to the device_settings table
 * 5. Click "Insert" and add a new row
 * 6. You should see the event appear in the Serial Monitor
 * 
 * TROUBLESHOOTING:
 * 
 * If you don't see events:
 * 1. Make sure your table has realtime enabled in Supabase
 * 2. Check that your API key has the correct permissions
 * 3. Verify the table name matches exactly (case sensitive)
 * 4. Check the Serial Monitor for connection status messages
 * 
 * FILTERS:
 * 
 * You can add filters to only listen for specific records:
 * - Filter by device_id: "device_id=eq.7c2c6746eedc"
 * - Filter by multiple conditions: "device_id=eq.7c2c6746eedc&desired=eq.true"
 * - More filter options: https://supabase.com/docs/guides/realtime/postgres-changes
 */