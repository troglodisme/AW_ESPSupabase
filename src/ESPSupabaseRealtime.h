#ifndef ESP_Supabase_Realtime_h
#define ESP_Supabase_Realtime_h

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>

#if defined(ESP8266)
#include <ESP8266HTTPClient.h>
#elif defined(ESP32)
#include <HTTPClient.h>
#else
#error "This library is not supported for your board! ESP32 and ESP8266"
#endif

class SupabaseRealtime
{
private:
  WebSocketsClient webSocket;
  String key;
  String hostname;
  String phone_or_email;
  String password;
  String data;
  String loginMethod;
  bool useAuth = false;
  
  // Connection state tracking
  bool isConnectedAndJoined = false;
  int connectionAttempts = 0;
  
  int _login_process();
  unsigned int authTimeout = 0;
  unsigned long loginTime = 0;
  String configAUTH;
  
  // Initial config
  const char *config = "{\"event\":\"phx_join\",\"topic\":\"realtime:ESP\",\"payload\":{\"config\":{\"postgres_changes\":[]}},\"ref\":\"sentRef\"}";
  JsonDocument postgresChanges;
  JsonDocument jsonRealtimeConfig;
  String configJSON;
  
  // Heartbeat
  unsigned long last_ms = 0;
  const char *jsonRealtimeHeartbeat = R"({"event": "heartbeat","topic": "phoenix","payload": {},"ref": "sentRef"})";
  const char *tokenConfig = R"({"topic": "realtime:ESP","event": "access_token","payload": {"access_token": ""},"ref": "sendRef"})";
  
  void processMessage(uint8_t *payload);
  void webSocketEvent(WStype_t type, uint8_t *payload, size_t length);
  std::function<void(String)> handler;

public:
  SupabaseRealtime() {}
  
  void begin(String hostname, String key, void (*func)(String));
  void addChangesListener(String table, String event, String schema = "public", String filter = "");
  void listen();
  void loop();
  
  // Authentication methods
  int login_email(String email_a, String password_a);
  int login_phone(String phone_a, String password_a);
  
  // Utility methods
  bool isConnected();
  void printConnectionStatus();
  
  // Get connection stats
  int getConnectionAttempts() { return connectionAttempts; }
  bool isChannelJoined() { return isConnectedAndJoined; }
  unsigned long getAuthTimeRemaining() { 
    if (!useAuth || authTimeout == 0) return 0;
    unsigned long elapsed = millis() - loginTime;
    return elapsed < authTimeout ? authTimeout - elapsed : 0;
  }
};

#endif