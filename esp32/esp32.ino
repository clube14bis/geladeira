/*
  Geladeira 14 BIS — ESP32 Dev Module

  Ligações que serão feitas quando os componentes chegarem:
  GPIO 2  -> LED azul integrado (quando disponível na placa)
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

// O LED azul integrado costuma usar GPIO 2. O vermelho é apenas de alimentação.
constexpr uint8_t LED_INDICADOR = 2;
constexpr uint8_t RELE_TRAVA = 26;
constexpr uint8_t RELE_TRAVADO = HIGH; // confirme no módulo relé antes de ligar a trava
constexpr uint8_t RELE_DESTRAVADO = LOW;
constexpr unsigned long ESPERA_ANTES_DE_ABRIR_MS = 6000;
constexpr unsigned long TEMPO_DESTRAVADO_MS = 10000;

struct Rede { const char *ssid; const char *senha; };
Rede redes[] = {
  { WIFI_CLUBE_SSID, WIFI_CLUBE_PASSWORD },
  { WIFI_CASA_SSID, WIFI_CASA_PASSWORD },
  { WIFI_CLUBE_EXT_SSID, WIFI_CLUBE_EXT_PASSWORD },
  { WIFI_FORUM_SSID, WIFI_FORUM_PASSWORD },
  { WIFI_SECRETARIA_SSID, WIFI_SECRETARIA_PASSWORD }
};

UserAuth credenciais(FIREBASE_WEB_API_KEY, FIREBASE_DEVICE_EMAIL, FIREBASE_DEVICE_PASSWORD);
FirebaseApp firebase;
WiFiClientSecure ssl, sslStream;
using AsyncClient = AsyncClientClass;
AsyncClient cliente(ssl), clienteStream(sslStream);
RealtimeDatabase banco;
Preferences memoria;

enum EstadoTrava { AGUARDANDO, DESTRAVADA };
EstadoTrava estado = AGUARDANDO;
String pedidoAtual;
String ultimoPedido;
String pedidoNaFila;
unsigned long proximaAcao = 0;
unsigned long ultimaTrocaLed = 0;
bool baselineFeito = false;
bool firebaseConfirmado = false;
bool streamIniciado = false;
bool sincronizacaoSolicitada = false;
unsigned long proximaTentativaSincronizacao = 0;

void apagarLeds() { digitalWrite(LED_INDICADOR, LOW); }
void acenderIndicador() { digitalWrite(LED_INDICADOR, HIGH); }

void iniciarPedido(const String &id);
void processarSincronizacaoInicial(AsyncResult &resultado);

void piscarIndicador(uint8_t vezes) {
  for (uint8_t i = 0; i < vezes; i++) {
    digitalWrite(LED_INDICADOR, HIGH); delay(140);
    digitalWrite(LED_INDICADOR, LOW); delay(140);
  }
}

void conectarWiFi() {
  while (WiFi.status() != WL_CONNECTED) {
    // Procura as redes conhecidas antes de tentar conectar. Assim, no clube,
    // na Secretaria ou em casa o ESP32 não precisa aguardar as outras redes.
    int totalRedes = WiFi.scanNetworks();
    for (auto &rede : redes) {
      if (!strlen(rede.ssid)) continue;
      bool redeEncontrada = false;
      for (int i = 0; i < totalRedes; i++) {
        if (WiFi.SSID(i) == rede.ssid) { redeEncontrada = true; break; }
      }
      if (!redeEncontrada) continue;
      Serial.printf("Tentando Wi-Fi: %s\n", rede.ssid);
      WiFi.begin(rede.ssid, rede.senha);
      unsigned long inicio = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) delay(250);
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Wi-Fi conectado: %s\n", WiFi.localIP().toString().c_str());
        piscarIndicador(3); // confirma que conectou ao Wi-Fi
        acenderIndicador(); // permanece aceso enquanto o Firebase sincroniza
        return;
      }
      WiFi.disconnect(true);
    }
    WiFi.scanDelete();
    // Mantém uma tentativa de reserva para redes ocultas que não aparecem no scan.
    if (totalRedes == 0) {
      for (auto &rede : redes) {
        if (!strlen(rede.ssid)) continue;
        Serial.printf("Tentando Wi-Fi oculto: %s\n", rede.ssid);
        WiFi.begin(rede.ssid, rede.senha);
        unsigned long inicio = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - inicio < 7000) delay(250);
        if (WiFi.status() == WL_CONNECTED) {
          Serial.printf("Wi-Fi conectado: %s\n", WiFi.localIP().toString().c_str());
          piscarIndicador(3);
          acenderIndicador();
          return;
        }
        WiFi.disconnect(true);
      }
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
  if (pedidoNaFila.length()) {
    String proximo = pedidoNaFila;
    pedidoNaFila = "";
    iniciarPedido(proximo);
  }
}

void alternarLeds() {
  if (estado != DESTRAVADA) return;
  unsigned long agora = millis();
  if (agora - ultimaTrocaLed >= 250) {
    ultimaTrocaLed = agora;
    digitalWrite(LED_INDICADOR, digitalRead(LED_INDICADOR) == LOW ? HIGH : LOW);
  }
}

void iniciarPedido(const String &id) {
  pedidoAtual = id;
  estado = AGUARDANDO;
  proximaAcao = millis() + ESPERA_ANTES_DE_ABRIR_MS;
  Serial.printf("Novo pedido %s. Abrindo em 6 segundos.\n", id.c_str());
}

void analisarPedidos(JsonObject pedidos) {
  String maiorId, candidato;
  for (JsonPair pedido : pedidos) {
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
    apagarLeds(); // pronto para uso: LED apagado até uma abertura
    Serial.println("Sincronização inicial concluída; ESP32 pronto para uso.");
    return;
  }
  if (candidato.length()) iniciarPedido(candidato);
}

void analisarPedidoNovo(const String &id, JsonObject dados) {
  if (!baselineFeito || id <= ultimoPedido || dados.containsKey("execution") || dados["status"] != "pending") return;
  if (!pedidoAtual.length()) {
    iniciarPedido(id);
  } else if (!pedidoNaFila.length() || id < pedidoNaFila) {
    pedidoNaFila = id;
    Serial.printf("Pedido %s colocado na fila.\n", id.c_str());
  }
}

void processarSincronizacaoInicial(AsyncResult &resultado) {
  if (!resultado.isResult()) return;
  if (resultado.isError()) {
    Serial.printf("Falha na sincronização inicial: %s\n", resultado.error().message().c_str());
    sincronizacaoSolicitada = false;
    proximaTentativaSincronizacao = millis() + 3000;
    return;
  }
  if (!resultado.available()) return;

  const char *conteudo = resultado.c_str();
  JsonDocument doc;
  if (!conteudo || deserializeJson(doc, conteudo)) {
    Serial.println("Resposta inicial de pedidos inválida; tentando novamente.");
    sincronizacaoSolicitada = false;
    proximaTentativaSincronizacao = millis() + 3000;
    return;
  }

  // Esta leitura única estabelece a base antes de abrir o stream. Isso evita
  // depender do primeiro evento SSE, que alguns servidores só enviam após uma alteração.
  if (doc.isNull()) {
    baselineFeito = true;
    apagarLeds();
    Serial.println("Sincronização inicial concluída; nenhum pedido pendente. ESP32 pronto para uso.");
  } else if (doc.is<JsonObject>()) {
    analisarPedidos(doc.as<JsonObject>());
  } else {
    Serial.println("Formato inicial de pedidos inesperado; tentando novamente.");
    sincronizacaoSolicitada = false;
    proximaTentativaSincronizacao = millis() + 3000;
  }
}

void processarStream(AsyncResult &resultado) {
  if (!resultado.isResult()) return;
  if (resultado.isError()) {
    Serial.printf("Stream Firebase: %s\n", resultado.error().message().c_str());
    return;
  }
  if (!resultado.available()) return;
  RealtimeDatabaseResult &stream = resultado.to<RealtimeDatabaseResult>();
  if (!stream.isStream()) return;
  String evento = stream.event();
  if (evento != "put" && evento != "patch") return;
  String caminho = stream.dataPath();
  if (caminho != "/" && caminho.indexOf('/', 1) >= 0) return;
  const char *conteudo = stream.to<const char *>();
  if (!conteudo || !strlen(conteudo)) return;
  JsonDocument doc;
  if (deserializeJson(doc, conteudo)) {
    Serial.println("Evento de pedidos ignorado: dados vazios.");
    return;
  }
  if (caminho == "/") {
    if (doc.isNull()) {
      baselineFeito = true;
      apagarLeds(); // pronto para uso: LED apagado até uma abertura
      Serial.println("Sincronização inicial concluída; nenhum pedido pendente. ESP32 pronto para uso.");
      return;
    }
    if (doc.is<JsonObject>()) analisarPedidos(doc.as<JsonObject>());
    return;
  }
  String id = caminho.substring(1);
  if (doc.is<JsonObject>()) analisarPedidoNovo(id, doc.as<JsonObject>());
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_INDICADOR, OUTPUT); acenderIndicador();
  pinMode(RELE_TRAVA, OUTPUT); digitalWrite(RELE_TRAVA, RELE_TRAVADO);
  memoria.begin("geladeira", false);
  ultimoPedido = memoria.getString("ultimoPedido", "");
  WiFi.mode(WIFI_STA);
  conectarWiFi();
  ssl.setInsecure();
  sslStream.setInsecure();
  ssl.setConnectionTimeout(1000); ssl.setHandshakeTimeout(5);
  sslStream.setConnectionTimeout(1000); sslStream.setHandshakeTimeout(5);
  initializeApp(cliente, firebase, getAuth(credenciais));
  firebase.getApp<RealtimeDatabase>(banco);
  banco.url(FIREBASE_DATABASE_URL);
  Serial.println("ESP32 preparado.");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) conectarWiFi();
  firebase.loop();
  if (firebase.ready() && !firebaseConfirmado) {
    firebaseConfirmado = true;
    Serial.println("Firebase conectado.");
    piscarIndicador(5); // confirma a conexão com o Firebase
    acenderIndicador(); // permanece aceso até a sincronização inicial dos pedidos
  }
  if (firebase.ready() && !baselineFeito && !sincronizacaoSolicitada && millis() >= proximaTentativaSincronizacao) {
    sincronizacaoSolicitada = true;
    banco.get(cliente, "/orders", processarSincronizacaoInicial, false, "sincronizacaoInicial");
    Serial.println("Sincronizando pedidos iniciais.");
  }
  if (firebase.ready() && baselineFeito && !streamIniciado) {
    // O stream só é aberto depois da autenticação: assim todo pedido novo é recebido.
    clienteStream.setSSEFilters("get,put,patch,keep-alive,cancel,auth_revoked");
    banco.get(clienteStream, "/orders", processarStream, true /* stream SSE */, "pedidosStream");
    streamIniciado = true;
    Serial.println("Monitoramento de pedidos ativado.");
  }
  unsigned long agora = millis();
  if (estado == AGUARDANDO && pedidoAtual.length() && agora >= proximaAcao) abrirTrava();
  if (estado == DESTRAVADA && agora >= proximaAcao) trancarGeladeira();
  alternarLeds();
}
