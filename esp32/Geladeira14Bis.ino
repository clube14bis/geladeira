#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "secrets.h"

constexpr uint8_t RELAY_PIN = 26;
constexpr bool RELAY_ACTIVE_LOW = true;
constexpr uint8_t STATUS_LED_PIN = 2;
constexpr bool STATUS_LED_ACTIVE_HIGH = true;
constexpr uint32_t OPEN_TIME_MS = 6000;
constexpr uint32_t POLL_INTERVAL_MS = 2000;
constexpr uint32_t LED_BLINK_MS = 150;

static const char GOOGLE_ROOT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFVzCCAz+gAwIBAgINAgPlk28xsBNJiGuiFzANBgkqhkiG9w0BAQwFADBHMQsw
CQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEU
MBIGA1UEAxMLR1RTIFJvb3QgUjEwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAw
MDAwWjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZp
Y2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjEwggIiMA0GCSqGSIb3DQEBAQUA
A4ICDwAwggIKAoICAQC2EQKLHuOhd5s73L+UPreVp0A8of2C+X0yBoJx9vaMf/vo
27xqLpeXo4xL+Sv2sfnOhB2x+cWX3u+58qPpvBKJXqeqUqv4IyfLpLGcY9vXmX7w
Cl7raKb0xlpHDU0QM+NOsROjyBhsS+z8CZDfnWQpJSMHobTSPS5g4M/SCYe7zUjw
TcLCeoiKu7rPWRnWr4+wB7CeMfGCwcDfLqZtbBkOtdh+JhpFAz2weaSUKK0Pfybl
qAj+lug8aJRT7oM6iCsVlgmy4HqMLnXWnOunVmSPlk9orj2XwoSPwLxAwAtcvfaH
szVsrBhQf4TgTM2S0yDpM7xSma8ytSmzJSq0SPly4cpk9+aCEI3oncKKiPo4Zor8
Y/kB+Xj9e1x3+naH+uzfsQ55lVe0vSbv1gHR6xYKu44LtcXFilWr06zqkUspzBmk
MiVOKvFlRNACzqrOSbTqn3yDsEB750Orp2yjj32JgfpMpf/VjsPOS+C12LOORc92
wO1AK/1TD7Cn1TsNsYqiA94xrcx36m97PtbfkSIS5r762DL8EGMUUXLeXdYWk70p
aDPvOmbsB4om3xPXV2V4J95eSRQAogB/mqghtqmxlbCluQ0WEdrHbEg8QOB+DVrN
VjzRlwW5y0vtOUucxD/SVRNuJLDWcfr0wbrM7Rv1/oFB2ACYPTrIrnqYNxgFlQID
AQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4E
FgQU5K8rJnEaK0gnhS9SZizv8IkTcT4wDQYJKoZIhvcNAQEMBQADggIBAJ+qQibb
C5u+/x6Wki4+omVKapi6Ist9wTrYggoGxval3sBOh2ZofmmWJyq+bXmYOfg6LEe
QkEzCzc9zolwFcq1JKjPa7XSQCGYzyI0zzvFIoTgxQ6KfF2I5DUkzps+GlQebtuy
h6f88/qBVRRiClmpIgUxPoLW7ttXNLwzldMXG+gnoot7TiYaelpkttGsN/H9oPM4
7HLwEXWdyzRSjeZ2axfG34arJ45JK3VmgRAhpuo+9K4l/3wV3s6MJT/KYnAK9y8J
ZgfIPxz88NtFMN9iiMG1D53Dn0reWVlHxYciNuaCp+0KueIHoI17eko8cdLiA6Ef
MgfdG+RCzgwARWGAtQsgWSl4vflVy2PFPEz0tv/bal8xa5meLMFrUKTX5hgUvYU/
Z6tGn6D/Qqc6f1zLXbBwHSs09dR2CQzreExZBfMzQsNhFRAbd03OIozUhfJFfbdT
6u9AWpQKXCBfTkBdYiJ23//OYb2MI3jSNwLgjt7RETeJ9r/tSQdirpLsQBqvFAnZ
0E6yove+7u7Y/9waLd64NnHi/Hm3lCXRSHNboTXns5lndcEZOitHTtNCjv0xyBZm
2tIMPNuzjsmhDYAPexZ3FL//2wmUspO8IFgV6dtxQ/PeEMMA3KgqlbbC1j+Qa3bb
bP6MvPJwNQzcmRk13NfIRmPVNnGuV/u3gm3c
-----END CERTIFICATE-----
)EOF";

WiFiClientSecure tls;
String idToken;
unsigned long tokenExpiresAt=0,lastPoll=0;

void relayWrite(bool open){digitalWrite(RELAY_PIN,(RELAY_ACTIVE_LOW?!open:open)?HIGH:LOW);}
void statusLedWrite(bool on){digitalWrite(STATUS_LED_PIN,(STATUS_LED_ACTIVE_HIGH?on:!on)?HIGH:LOW);}
void blinkStatusLed(){
  for(uint8_t i=0;i<5;i++){
    statusLedWrite(true);
    delay(LED_BLINK_MS);
    statusLedWrite(false);
    delay(LED_BLINK_MS);
  }
}

bool requestHttps(const String& method,const String& url,const String& body,String& response,bool acceptRedirect=false){
  HTTPClient http;
  // O Apps Script registra a linha e devolve um redirecionamento do Google.
  // Não seguimos esse redirect para não reenviar o POST em outro endereço.
  http.setFollowRedirects(acceptRedirect ? HTTPC_DISABLE_FOLLOW_REDIRECTS : HTTPC_FORCE_FOLLOW_REDIRECTS);
  if(!http.begin(tls,url)) return false;
  http.addHeader("Content-Type","application/json");
  int code=-1;
  if(method=="GET") code=http.GET();
  else if(method=="PUT") code=http.PUT(body);
  else if(method=="POST") code=http.POST(body);
  else if(method=="DELETE") code=http.sendRequest("DELETE");
  response=http.getString(); http.end();
  return (code>=200 && code<300)||(acceptRedirect && code>=300 && code<400);
}

void connectWifi(){
  if(WiFi.status()==WL_CONNECTED)return;
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  for(uint8_t i=0;WiFi.status()!=WL_CONNECTED&&i<30;i++)delay(500);
  if(WiFi.status()==WL_CONNECTED)return;
  WiFi.disconnect(true);
  delay(200);
  WiFi.begin(WIFI_BACKUP_SSID,WIFI_BACKUP_PASSWORD);
  for(uint8_t i=0;WiFi.status()!=WL_CONNECTED&&i<30;i++)delay(500);
}

bool authenticateDevice(){
  if(idToken.length()&&millis()<tokenExpiresAt)return true;
  DynamicJsonDocument in(512),out(2048);
  in["email"]=DEVICE_EMAIL;
  in["password"]=DEVICE_PASSWORD;
  in["returnSecureToken"]=true;
  String body,response;
  serializeJson(in,body);
  String url=String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=")+FIREBASE_API_KEY;
  if(!requestHttps("POST",url,body,response))return false;
  if(deserializeJson(out,response))return false;
  idToken=out["idToken"].as<String>();
  unsigned long seconds=out["expiresIn"].as<unsigned long>();
  tokenExpiresAt=millis()+(seconds>60?seconds-60:300)*1000UL;
  return idToken.length()>0;
}

String firebaseUrl(const String& path){
  return String("https://")+FIREBASE_DB_HOST+path+(path.indexOf("?")>=0?"&auth=":"?auth=")+idToken;
}

bool updateStatus(const String& orderId,const char* status){
  String response;
  return requestHttps("PUT",firebaseUrl("/orders/"+orderId+"/status.json"),String("\"")+status+"\"",response);
}

bool sendToSheets(const String& orderId,const String& fullName,JsonArray items){
  DynamicJsonDocument doc(4096);
  doc["secret"]=DEVICE_SECRET;
  doc["orderId"]=orderId;
  doc["fullName"]=fullName;
  JsonArray dst=doc.createNestedArray("items");
  for(JsonVariant item:items){
    JsonObject src=item.as<JsonObject>(), n=dst.createNestedObject();
    n["drink"]=src["drink"].as<String>();
    n["quantity"]=src["quantity"].as<int>();
  }
  String body,response;
  serializeJson(doc,body);
  if(!requestHttps("POST",APPS_SCRIPT_URL,body,response,true))return false;
  DynamicJsonDocument ret(256);
  // Uma resposta 302 não traz JSON, mas o POST já foi aceito pelo Apps Script.
  if(deserializeJson(ret,response))return true;
  return ret["ok"]==true;
}

void processOrder(const String& orderId,JsonObject pedido){
  String name=pedido["fullName"].as<String>();
  JsonArray items=pedido["items"].as<JsonArray>();
  if(items.isNull()||items.size()==0){updateStatus(orderId,"log_failed");return;}
  if(!updateStatus(orderId,"processing"))return;
  const unsigned long openedAt=millis();
  relayWrite(true);
  blinkStatusLed();
  const uint32_t elapsed=millis()-openedAt;
  if(elapsed<OPEN_TIME_MS)delay(OPEN_TIME_MS-elapsed);
  relayWrite(false);
  String response;
  // A página já registra o pedido na planilha antes de criar este comando.
  requestHttps("DELETE",firebaseUrl("/orders/"+orderId+".json"),"",response);
}

void pollOrders(){
  if(!authenticateDevice())return;
  String response;
  if(!requestHttps("GET",firebaseUrl("/orders.json?orderBy=%22status%22&equalTo=%22pending%22"),"",response))return;
  DynamicJsonDocument doc(16384);
  if(deserializeJson(doc,response))return;
  if(doc.isNull())return;
  for(JsonPair item:doc.as<JsonObject>()){
    processOrder(item.key().c_str(),item.value().as<JsonObject>());
    break;
  }
}

void setup(){
  Serial.begin(115200);
  pinMode(RELAY_PIN,OUTPUT);
  pinMode(STATUS_LED_PIN,OUTPUT);
  relayWrite(false);
  statusLedWrite(false);
  tls.setCACert(GOOGLE_ROOT_CA);
  connectWifi();
}

void loop(){
  connectWifi();
  if(WiFi.status()==WL_CONNECTED&&millis()-lastPoll>=POLL_INTERVAL_MS){
    lastPoll=millis();
    pollOrders();
  }
}
