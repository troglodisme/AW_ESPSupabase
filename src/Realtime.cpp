#include "ESPSupabaseRealtime.h"

// Internal functions
String getEventTable(JsonDocument result)
{
  if (result["payload"]["data"].containsKey("table")) {
    String table = result["payload"]["data"]["table"];
    Serial.printf("[DEBUG] Event table: %s\n", table.c_str());
    return table;
  }
  Serial.println("[DEBUG] No table found in event");
  return "null";
}

String getEventType(JsonDocument result)
{
  if (result["payload"]["data"].containsKey("type")) {
    String type = result["payload"]["data"]["type"];
    Serial.printf("[DEBUG] Event type: %s\n", type.c_str());
    return type;
  }
  Serial.println("[DEBUG] No type found in event");
  return "null";
}

int SupabaseRealtime::_login_process()
{
  Serial.println("[DEBUG] Starting login process...");
  
  HTTPClient Loginhttps;
  WiFiClientSecure *clientLogin = new WiFiClientSecure();

  // Better SSL configuration - still insecure but with debugging
  clientLogin->setInsecure();
  clientLogin->setTimeout(10000); // 10 second timeout
  
  int httpCode = 0;
  JsonDocument doc;
  String url = "https://" + hostname + "/auth/v1/token?grant_type=password";
  Serial.printf("[DEBUG] Login URL: %s\n", url.c_str());

  if (Loginhttps.begin(*clientLogin, url))
  {
    Loginhttps.addHeader("apikey", key);
    Loginhttps.addHeader("Content-Type", "application/json");
    Loginhttps.setTimeout(10000); // 10 second timeout

    String query = "{\"" + loginMethod + "\": \"" + phone_or_email + "\", \"password\": \"" + password + "\"}";
    Serial.printf("[DEBUG] Login payload: %s\n", query.c_str());
    
    httpCode = Loginhttps.POST(query);
    Serial.printf("[DEBUG] Login HTTP code: %d\n", httpCode);

    if (httpCode > 0)
    {
      String data = Loginhttps.getString();
      Serial.printf("[DEBUG] Login response: %s\n", data.c_str());
      
      DeserializationError error = deserializeJson(doc, data);
      if (error) {
        Serial.printf("[ERROR] JSON deserialization failed: %s\n", error.c_str());
        httpCode = -200; // Custom error code for JSON parsing failure
      }
      else if (doc.containsKey("access_token") && !doc["access_token"].isNull() && doc["access_token"].is<String>() && !doc["access_token"].as<String>().isEmpty())
      {
        String USER_TOKEN = doc["access_token"].as<String>();
        authTimeout = doc["expires_in"].as<int>() * 1000;
        Serial.printf("[DEBUG] Login Success - Token length: %d, Expires in: %d seconds\n", 
                     USER_TOKEN.length(), authTimeout / 1000);

        JsonDocument authConfig;
        DeserializationError authError = deserializeJson(authConfig, tokenConfig);
        if (authError) {
          Serial.printf("[ERROR] Auth config JSON error: %s\n", authError.c_str());
        } else {
          authConfig["payload"]["access_token"] = USER_TOKEN;
          serializeJson(authConfig, configAUTH);
          Serial.printf("[DEBUG] Auth config prepared: %s\n", configAUTH.c_str());
        }
      }
      else
      {
        Serial.println("[ERROR] Login Failed: Invalid access token in response");
        if (doc.containsKey("error")) {
          Serial.printf("[ERROR] Server error: %s\n", doc["error"].as<String>().c_str());
        }
        httpCode = -201; // Custom error code for invalid token
      }
    }
    else
    {
      Serial.printf("[ERROR] Login Failed with HTTP code: %d\n", httpCode);
      String errorMsg = Loginhttps.errorToString(httpCode);
      Serial.printf("[ERROR] HTTP Error: %s\n", errorMsg.c_str());
    }

    Loginhttps.end();
    delete clientLogin;
    clientLogin = nullptr;

    loginTime = millis();
    Serial.printf("[DEBUG] Login time recorded: %lu\n", loginTime);
  }
  else
  {
    Serial.println("[ERROR] Failed to begin HTTPS connection for login");
    delete clientLogin;
    return -100;
  }

  return httpCode;
}

void SupabaseRealtime::addChangesListener(String table, String event, String schema, String filter)
{
  Serial.printf("[DEBUG] Adding changes listener - Table: %s, Event: %s, Schema: %s, Filter: %s\n", 
                table.c_str(), event.c_str(), schema.c_str(), filter.c_str());
  
  JsonDocument tableObj;
  
  tableObj["event"] = event;
  tableObj["schema"] = schema;
  tableObj["table"] = table;

  if (filter != "")
  {
    tableObj["filter"] = filter;
  }

  postgresChanges.add(tableObj);
  
  String debugOutput;
  serializeJson(tableObj, debugOutput);
  Serial.printf("[DEBUG] Added listener config: %s\n", debugOutput.c_str());
}

void SupabaseRealtime::listen()
{
  Serial.println("[DEBUG] Starting listen setup...");
  
  DeserializationError error = deserializeJson(jsonRealtimeConfig, config);
  if (error) {
    Serial.printf("[ERROR] Config JSON error: %s\n", error.c_str());
    return;
  }
  
  jsonRealtimeConfig["payload"]["config"]["postgres_changes"] = postgresChanges;
  serializeJson(jsonRealtimeConfig, configJSON);
  
  Serial.printf("[DEBUG] Final config JSON: %s\n", configJSON.c_str());

  String slug = "/realtime/v1/websocket?apikey=" + String(key) + "&vsn=1.0.0";
  Serial.printf("[DEBUG] WebSocket URL: wss://%s:443%s\n", hostname.c_str(), slug.c_str());

  // Configure WebSocket with better settings
  webSocket.beginSSL(hostname.c_str(), 443, slug.c_str());
  
  // Set WebSocket options for better stability
  webSocket.setReconnectInterval(5000);  // 5 seconds
  webSocket.enableHeartbeat(15000, 3000, 2);  // ping every 15s, timeout 3s, 2 retries
  
  Serial.println("[DEBUG] WebSocket configuration complete");

  // Event handler
  webSocket.onEvent(std::bind(&SupabaseRealtime::webSocketEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

void SupabaseRealtime::processMessage(uint8_t *payload)
{
  Serial.printf("[DEBUG] Processing message: %s\n", (char*)payload);
  
  JsonDocument result;
  DeserializationError error = deserializeJson(result, payload);
  
  if (error) {
    Serial.printf("[ERROR] Message JSON parsing failed: %s\n", error.c_str());
    return;
  }
  
  // Debug: print the entire message structure
  String debugMsg;
  serializeJsonPretty(result, debugMsg);
  Serial.printf("[DEBUG] Parsed message structure:\n%s\n", debugMsg.c_str());
  
  String table = getEventTable(result);
  if (table != "null")
  {
    String data = result["payload"]["data"];
    Serial.printf("[DEBUG] Calling handler with data: %s\n", data.c_str());
    handler(data);
  }
  else
  {
    // Handle other message types
    if (result.containsKey("event")) {
      String event = result["event"];
      Serial.printf("[DEBUG] Received event: %s\n", event.c_str());
      
      if (event == "phx_reply") {
        String status = result["payload"]["status"];
        Serial.printf("[DEBUG] Phoenix reply status: %s\n", status.c_str());
        
        if (status == "ok") {
          isConnectedAndJoined = true;
          Serial.println("[DEBUG] Successfully joined realtime channel");
        } else {
          Serial.printf("[ERROR] Phoenix reply error: %s\n", result["payload"]["response"].as<String>().c_str());
        }
      }
      else if (event == "heartbeat") {
        Serial.println("[DEBUG] Heartbeat received");
      }
    }
  }
}

void SupabaseRealtime::webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case WStype_DISCONNECTED:
    Serial.printf("[WS] Disconnected! Reason: %s\n", payload ? (char*)payload : "Unknown");
    isConnectedAndJoined = false;
    connectionAttempts = 0; // Reset connection attempts on disconnect
    break;
    
  case WStype_CONNECTED:
    {
      Serial.printf("[WS] Connected to: %s\n", payload);
      
      // Send configuration immediately after connection
      Serial.printf("[DEBUG] Sending config: %s\n", configJSON.c_str());
      webSocket.sendTXT(configJSON);
      
      // Send auth if required
      if (useAuth && configAUTH.length() > 0)
      {
        Serial.printf("[DEBUG] Sending auth: %s\n", configAUTH.c_str());
        webSocket.sendTXT(configAUTH);
      }
      
      connectionAttempts++;
      Serial.printf("[DEBUG] Connection attempt #%d successful\n", connectionAttempts);
    }
    break;
    
  case WStype_TEXT:
    Serial.printf("[WS] Received text (%d bytes): %s\n", length, payload);
    processMessage(payload);
    break;
    
  case WStype_BIN:
    Serial.printf("[WS] Received binary data (%d bytes)\n", length);
    break;
    
  case WStype_ERROR:
    Serial.printf("[WS] Error: %s\n", payload);
    break;
    
  case WStype_PING:
    Serial.println("[WS] Received ping");
    break;
    
  case WStype_PONG:
    Serial.println("[WS] Received pong");
    break;
    
  case WStype_FRAGMENT_TEXT_START:
  case WStype_FRAGMENT_BIN_START:
  case WStype_FRAGMENT:
  case WStype_FRAGMENT_FIN:
    Serial.printf("[WS] Fragment received (type: %d)\n", type);
    break;
    
  default:
    Serial.printf("[WS] Unknown event type: %d\n", type);
    break;
  }
}

void SupabaseRealtime::loop()
{
  // Check auth token refresh - refresh at 80% of timeout instead of 50%
  if (useAuth && authTimeout > 0 && millis() - loginTime > (authTimeout * 0.8))
  {
    Serial.println("[DEBUG] Auth token needs refresh");
    
    // Don't disconnect, just refresh the token
    int loginResult = _login_process();
    if (loginResult > 0) {
      Serial.println("[DEBUG] Token refreshed successfully");
      // Send new auth token if connected
      if (webSocket.isConnected() && configAUTH.length() > 0) {
        webSocket.sendTXT(configAUTH);
      }
    } else {
      Serial.printf("[ERROR] Token refresh failed with code: %d\n", loginResult);
    }
  }

  // Handle WebSocket loop
  webSocket.loop();

  // Send heartbeat every 30 seconds, but only if connected and joined
  if (millis() - last_ms > 30000)
  {
    last_ms = millis();
    
    if (webSocket.isConnected())
    {
      Serial.println("[DEBUG] Sending heartbeat");
      webSocket.sendTXT(jsonRealtimeHeartbeat);
      
      // Only send auth with heartbeat if we're using auth and have a token
      if (useAuth && configAUTH.length() > 0 && isConnectedAndJoined)
      {
        Serial.println("[DEBUG] Sending auth with heartbeat");
        webSocket.sendTXT(configAUTH);
      }
    }
    else
    {
      Serial.println("[DEBUG] Skipping heartbeat - not connected");
    }
  }

  // Connection monitoring
  static unsigned long lastConnCheck = 0;
  if (millis() - lastConnCheck > 10000) // Check every 10 seconds
  {
    lastConnCheck = millis();
    Serial.printf("[DEBUG] Connection status - Connected: %s, Joined: %s, Attempts: %d\n", 
                  webSocket.isConnected() ? "YES" : "NO",
                  isConnectedAndJoined ? "YES" : "NO",
                  connectionAttempts);
    
    // Report memory usage
    Serial.printf("[DEBUG] Free heap: %d bytes\n", ESP.getFreeHeap());
  }
}

void SupabaseRealtime::begin(String hostname_param, String key_param, void (*func)(String))
{
  Serial.printf("[DEBUG] Initializing SupabaseRealtime with hostname: %s\n", hostname_param.c_str());
  
  hostname_param.replace("https://", "");
  this->hostname = hostname_param;
  this->key = key_param;
  this->handler = func;
  
  // Initialize state variables
  isConnectedAndJoined = false;
  connectionAttempts = 0;
  
  Serial.printf("[DEBUG] Hostname set to: %s\n", this->hostname.c_str());
  Serial.printf("[DEBUG] API key length: %d\n", this->key.length());
}

int SupabaseRealtime::login_email(String email_a, String password_a)
{
  Serial.printf("[DEBUG] Setting up email login for: %s\n", email_a.c_str());
  
  useAuth = true;
  loginMethod = "email";
  phone_or_email = email_a;
  password = password_a;

  int httpCode = 0;
  int attempts = 0;
  const int maxAttempts = 3;
  
  while (httpCode <= 0 && attempts < maxAttempts)
  {
    attempts++;
    Serial.printf("[DEBUG] Login attempt %d of %d\n", attempts, maxAttempts);
    httpCode = _login_process();
    
    if (httpCode <= 0) {
      Serial.printf("[DEBUG] Login attempt %d failed, waiting 2 seconds...\n", attempts);
      delay(2000); // Wait before retry
    }
  }
  
  if (httpCode <= 0) {
    Serial.printf("[ERROR] All login attempts failed. Final code: %d\n", httpCode);
  }
  
  return httpCode;
}

int SupabaseRealtime::login_phone(String phone_a, String password_a)
{
  Serial.printf("[DEBUG] Setting up phone login for: %s\n", phone_a.c_str());
  
  useAuth = true;
  loginMethod = "phone";
  phone_or_email = phone_a;
  password = password_a;

  int httpCode = 0;
  int attempts = 0;
  const int maxAttempts = 3;
  
  while (httpCode <= 0 && attempts < maxAttempts)
  {
    attempts++;
    Serial.printf("[DEBUG] Login attempt %d of %d\n", attempts, maxAttempts);
    httpCode = _login_process();
    
    if (httpCode <= 0) {
      Serial.printf("[DEBUG] Login attempt %d failed, waiting 2 seconds...\n", attempts);
      delay(2000); // Wait before retry
    }
  }
  
  if (httpCode <= 0) {
    Serial.printf("[ERROR] All login attempts failed. Final code: %d\n", httpCode);
  }
  
  return httpCode;
}

// Additional utility functions for debugging
void SupabaseRealtime::printConnectionStatus()
{
  Serial.println("=== Supabase Realtime Status ===");
  Serial.printf("WebSocket Connected: %s\n", webSocket.isConnected() ? "YES" : "NO");
  Serial.printf("Channel Joined: %s\n", isConnectedAndJoined ? "YES" : "NO");
  Serial.printf("Using Auth: %s\n", useAuth ? "YES" : "NO");
  Serial.printf("Connection Attempts: %d\n", connectionAttempts);
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  if (useAuth) {
    unsigned long timeRemaining = authTimeout - (millis() - loginTime);
    Serial.printf("Auth Token Time Remaining: %lu ms\n", timeRemaining);
  }
  Serial.println("===============================");
}

bool SupabaseRealtime::isConnected()
{
  return webSocket.isConnected() && isConnectedAndJoined;
}