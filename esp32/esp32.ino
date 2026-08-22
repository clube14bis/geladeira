/*
  Geladeira 14 BIS — ESP32 Dev Module

  Ligações que serão feitas quando os componentes chegarem:
  GPIO 2  -> resistor 220–330 ohms -> LED azul -> GND
  GPIO 4  -> resistor 220–330 ohms -> LED vermelho -> GND
  GPIO 26 -> IN do módulo relé (confirmar se o relé é ativo em LOW)
  GND do ESP32 e do módulo relé devem ser comuns.
*/
#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "secrets.h"

constexpr uint8_t LED_AZUL = 2;
constexpr uint8_t LED_VERMELHO = 4;
constexpr uint8_t RELE_TRAVA = 26;
constexpr uint8_t RELE_TRAVADO = HIGH; // confirme no módulo relé antes de ligar a trava
constexpr uint8_t RELE_DESTRAVADO = LOW;
constexpr unsigned long ESPERA_ANTES_DE_ABRIR_MS = 6000;
constexpr unsigned long TEMPO_DESTRAVADO_MS = 10000;
constexpr unsigned long INTERVALO_LEITURA_MS = 2000;

struct Rede { const char *ssid; const char *senha; };
Rede redes[] = {
  { WIFI_CLUBE_SSID, WIFI_CLUBE_PASSWORD },
  { WIFI_CASA_SSID, WIFI_CASA_PASSWORD }
};

UserAuth credenciais(FIREBASE_WEB_API_KEY, FIREBASE_DEVICE_EMAIL, FIREBASE_DEVICE_PASSWORD);
FirebaseApp firebase;
WiFiClientSecure ssl;
using AsyncClient = AsyncClientClass;
AsyncClient cliente(ssl);
RealtimeDatabase banco;
Preferences memoria;

enum EstadoTrava { AGUARDANDO, DESTRAVADA };
EstadoTrava estado = AGUARDANDO;
String pedidoAtual;
String ultimoPedido;
unsigned long proximaAcao = 0;
unsigned long ultimoPoll = 0;
unsigned long ultimaTrocaLed = 0;
bool baselineFeito = false;

void apagarLeds() { digitalWrite(LED_AZUL, LOW); digitalWrite(LED_VERMELHO, LOW); }

void conectarWiFi() {
  while (WiFi.status() != WL_CONNECTED) {
    for (auto &rede : redes) {
      if (!strlen(rede.ssid)) continue;
      Serial.printf("Tentando Wi-Fi: %s\n", rede.ssid);
      WiFi.begin(rede.ssid, rede.senha);
      unsigned long inicio = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) delay(250);
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Wi-Fi conectado: %s\n", WiFi.localIP().toString().c_str());
        return;
      }
      WiFi.disconnect(true);
    }
    delay(1000);
  }
}

void registrarEstado(const char *novoEstado) {
  JsonDocument doc;
  doc["state"] = novoEstado;
  doc["updatedAt"] = (uint64_t) millis();
  String json;
  serializeJson(doc, json);
  bool ok = banco.set<object_t>(cliente, "/orders/" + pedidoAtual + "/execution", object_t(json));
  if (!ok) Serial.printf("Falha ao registrar estado: %s\n", cliente.lastError().message().c_str());
}

void abrirTrava() {
  digitalWrite(RELE_TRAVA, RELE_DESTRAVADO);
  Serial.println("GELADEIRA ABERTA: 10 segundos");
  registrarEstado("opened");
  estado = DESTRAVADA;
  proximaAcao = millis() + TEMPO_DESTRAVADO_MS;
  ultimaTrocaLed = 0;
}

void trancarGeladeira() {
  digitalWrite(RELE_TRAVA, RELE_TRAVADO);
  Serial.println("GELADEIRA TRANCADA");
  registrarEstado("locked");
  memoria.putString("ultimoPedido", pedidoAtual);
  ultimoPedido = pedidoAtual;
  pedidoAtual = "";
  apagarLeds();
  estado = AGUARDANDO;
}

void alternarLeds() {
  if (estado != DESTRAVADA) return;
  unsigned long agora = millis();
  if (agora - ultimaTrocaLed >= 250) {
    ultimaTrocaLed = agora;
    bool azulLigado = digitalRead(LED_AZUL) == LOW;
    digitalWrite(LED_AZUL, azulLigado);
    digitalWrite(LED_VERMELHO, !azulLigado);
  }
}

void iniciarPedido(const String &id) {
  pedidoAtual = id;
  estado = AGUARDANDO;
  proximaAcao = millis() + ESPERA_ANTES_DE_ABRIR_MS;
  Serial.printf("Novo pedido %s. Abrindo em 6 segundos.\n", id.c_str());
}

void procurarPedidos() {
  String conteudo = banco.get<String>(cliente, "/orders");
  if (cliente.lastError().code() != 0) {
    Serial.printf("Falha ao ler pedidos: %s\n", cliente.lastError().message().c_str());
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, conteudo)) { Serial.println("Resposta de pedidos inválida."); return; }
  String maiorId, candidato;
  for (JsonPair pedido : doc.as<JsonObject>()) {
    String id = pedido.key().c_str();
    if (id > maiorId) maiorId = id;
    JsonObject dados = pedido.value().as<JsonObject>();
    if (id > ultimoPedido && !dados.containsKey("execution") && dados["status"] == "pending") {
      if (!candidato.length() || id < candidato) candidato = id;
    }
  }
  // Na primeira conexão apenas sincroniza os pedidos antigos para não abrir a geladeira por histórico.
  if (!baselineFeito) {
    baselineFeito = true;
    if (maiorId.length() && maiorId > ultimoPedido) { ultimoPedido = maiorId; memoria.putString("ultimoPedido", ultimoPedido); }
    Serial.println("Sincronização inicial concluída; aguardando novos pedidos.");
    return;
  }
  if (candidato.length()) iniciarPedido(candidato);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_AZUL, OUTPUT); pinMode(LED_VERMELHO, OUTPUT); apagarLeds();
  pinMode(RELE_TRAVA, OUTPUT); digitalWrite(RELE_TRAVA, RELE_TRAVADO);
  memoria.begin("geladeira", false);
  ultimoPedido = memoria.getString("ultimoPedido", "");
  WiFi.mode(WIFI_STA);
  conectarWiFi();
  ssl.setInsecure();
  ssl.setConnectionTimeout(1000); ssl.setHandshakeTimeout(5);
  initializeApp(cliente, firebase, getAuth(credenciais));
  firebase.getApp<RealtimeDatabase>(banco);
  banco.url(FIREBASE_DATABASE_URL);
  Serial.println("ESP32 preparado.");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) conectarWiFi();
  firebase.loop();
  unsigned long agora = millis();
  if (estado == AGUARDANDO && pedidoAtual.length() && agora >= proximaAcao) abrirTrava();
  if (estado == DESTRAVADA && agora >= proximaAcao) trancarGeladeira();
  alternarLeds();
  if (firebase.ready() && estado == AGUARDANDO && !pedidoAtual.length() && agora - ultimoPoll >= INTERVALO_LEITURA_MS) {
    ultimoPoll = agora;
    procurarPedidos();
  }
}
