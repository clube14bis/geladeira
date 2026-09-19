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
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include "secrets.h"

// Cada placa precisa de um identificador próprio. Na placa de testes,
// use DEVICE_ID "teste" e MODO_TESTE true no secrets.h local.
#ifndef DEVICE_ID
#define DEVICE_ID "geladeira"
#endif
#ifndef MODO_TESTE
#define MODO_TESTE false
#endif

// O LED azul integrado costuma usar GPIO 2. O vermelho é apenas de alimentação.
constexpr uint8_t LED_INDICADOR = 2;
constexpr uint8_t RELE_TRAVA = 26;
constexpr uint8_t RELE_TRAVADO = HIGH; // confirme no módulo relé antes de ligar a trava
constexpr uint8_t RELE_DESTRAVADO = LOW;
constexpr unsigned long ESPERA_ANTES_DE_ABRIR_MS = 6000;
constexpr unsigned long TEMPO_DESTRAVADO_MS = 10000;
constexpr unsigned long INTERVALO_HEARTBEAT_MS = 30000;
constexpr unsigned long INTERVALO_RECONEXAO_STREAM_MS = 3000;
constexpr unsigned long INTERVALO_VERIFICACAO_STREAM_MS = 5000;
constexpr unsigned long LIMITE_SEM_EVENTO_STREAM_MS = 120000;
constexpr unsigned long INTERVALO_REINICIO_PREVENTIVO_MS = 5UL * 60UL * 60UL * 1000UL;
constexpr unsigned long LIMITE_WIFI_SEM_RETORNO_MS = 5UL * 60UL * 1000UL;
constexpr unsigned long LIMITE_FIREBASE_SEM_RETORNO_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t WATCHDOG_TIMEOUT_MS = 60000;
constexpr size_t TAMANHO_HISTORICO_EVENTOS = 900;
constexpr char VERSAO_FIRMWARE[] = "2.5.1";

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
DatabaseOptions filtroUltimoPedido;

enum EstadoTrava { AGUARDANDO, DESTRAVADA };
EstadoTrava estado = AGUARDANDO;
String pedidoAtual;
String ultimoPedido;
String pedidoNaFila;
unsigned long proximaAcao = 0;
unsigned long ultimaTrocaLed = 0;
bool baselineFeito = false;
bool firebaseConfirmado = false;
bool watchdogConfigurado = false;
bool streamIniciado = false;
bool sincronizacaoSolicitada = false;
bool recuperacaoStreamPendente = false;
bool reinicioPreventivoPendente = false;
unsigned long proximaTentativaSincronizacao = 0;
unsigned long proximaTentativaStream = 0;
unsigned long ultimoHeartbeat = 0;
unsigned long ultimoEventoStream = 0;
unsigned long ultimaVerificacaoStream = 0;
unsigned long inicioSessao = 0;
unsigned long inicioWifiIndisponivel = 0;
unsigned long inicioFirebaseIndisponivel = 0;
uint32_t totalInicializacoes = 0;
uint32_t totalRecuperacoesStream = 0;
String ultimoMotivoRecuperacao = "NENHUMA";
String motivoInicializacao = "DESCONHECIDO";
char caminhoStatus[48] = "/devices/geladeira";
char historicoEventos[TAMANHO_HISTORICO_EVENTOS] = "";

struct DiagnosticoMemoria {
  bool pronto = false;
  bool medicaoRecuperacaoPendente = false;
  uint32_t heapPronto = 0;
  uint32_t maiorBlocoPronto = 0;
  uint32_t menorHeapDesdePronto = 0;
  uint32_t menorMaiorBlocoDesdePronto = 0;
  uint32_t heapAntesRecuperacao = 0;
  uint32_t maiorBlocoAntesRecuperacao = 0;
  uint32_t heapDepoisRecuperacao = 0;
  uint32_t maiorBlocoDepoisRecuperacao = 0;
};
DiagnosticoMemoria diagnosticoMemoria;

void apagarLeds() { digitalWrite(LED_INDICADOR, LOW); }
void acenderIndicador() { digitalWrite(LED_INDICADOR, HIGH); }

void iniciarPedido(const String &id);
void processarSincronizacaoInicial(AsyncResult &resultado);

void carregarHistoricoEventos() {
  String historicoSalvo = memoria.getString("eventLog", "");
  historicoSalvo.toCharArray(historicoEventos, sizeof(historicoEventos));
}

void registrarEvento(const char *evento) {
  char item[100];
  snprintf(item, sizeof(item), "B%lu/U%lus:%s",
           static_cast<unsigned long>(totalInicializacoes),
           millis() / 1000UL, evento);

  size_t tamanhoAtual = strlen(historicoEventos);
  while (tamanhoAtual && tamanhoAtual + 3 + strlen(item) >= sizeof(historicoEventos)) {
    char *separador = strstr(historicoEventos, " | ");
    if (!separador) {
      historicoEventos[0] = '\0';
      break;
    }
    memmove(historicoEventos, separador + 3, strlen(separador + 3) + 1);
    tamanhoAtual = strlen(historicoEventos);
  }
  size_t inicio = strlen(historicoEventos);
  snprintf(historicoEventos + inicio, sizeof(historicoEventos) - inicio,
           "%s%s", inicio ? " | " : "", item);
  memoria.putString("eventLog", historicoEventos);
  Serial.printf("Evento persistente: %s\n", item);
}

uint32_t maiorBlocoLivre() {
  return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

void atualizarDiagnosticoMemoria() {
  if (!diagnosticoMemoria.pronto) return;
  uint32_t heap = ESP.getFreeHeap();
  uint32_t maiorBloco = maiorBlocoLivre();
  if (!diagnosticoMemoria.menorHeapDesdePronto || heap < diagnosticoMemoria.menorHeapDesdePronto) {
    diagnosticoMemoria.menorHeapDesdePronto = heap;
  }
  if (!diagnosticoMemoria.menorMaiorBlocoDesdePronto || maiorBloco < diagnosticoMemoria.menorMaiorBlocoDesdePronto) {
    diagnosticoMemoria.menorMaiorBlocoDesdePronto = maiorBloco;
  }
}

void iniciarDiagnosticoMemoria() {
  if (diagnosticoMemoria.pronto) return;
  diagnosticoMemoria.pronto = true;
  diagnosticoMemoria.heapPronto = ESP.getFreeHeap();
  diagnosticoMemoria.maiorBlocoPronto = maiorBlocoLivre();
  diagnosticoMemoria.menorHeapDesdePronto = diagnosticoMemoria.heapPronto;
  diagnosticoMemoria.menorMaiorBlocoDesdePronto = diagnosticoMemoria.maiorBlocoPronto;
  Serial.printf("Diagnóstico de memória iniciado: heap %u, maior bloco %u.\n",
                diagnosticoMemoria.heapPronto, diagnosticoMemoria.maiorBlocoPronto);
}

const char *nomeMotivoReset(esp_reset_reason_t motivo) {
  switch (motivo) {
    case ESP_RST_POWERON: return "ENERGIA";
    case ESP_RST_EXT: return "RESET_EXTERNO";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "FALHA_DO_PROGRAMA";
    case ESP_RST_INT_WDT: return "WATCHDOG_INTERNO";
    case ESP_RST_TASK_WDT: return "WATCHDOG_DA_TAREFA";
    case ESP_RST_WDT: return "WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP";
    case ESP_RST_BROWNOUT: return "QUEDA_DE_TENSAO";
    case ESP_RST_SDIO: return "SDIO";
    default: return "DESCONHECIDO";
  }
}

void alimentarWatchdog() {
  if (watchdogConfigurado) esp_task_wdt_reset();
}

void configurarWatchdog() {
  esp_task_wdt_config_t config = {
    .timeout_ms = WATCHDOG_TIMEOUT_MS,
    // Monitoramos apenas este loop. As tarefas de Wi-Fi/Firebase ocupam os
    // núcleos internos durante conexões TLS e não devem provocar reset falso.
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_err_t resultado = esp_task_wdt_init(&config);
  if (resultado == ESP_ERR_INVALID_STATE) resultado = esp_task_wdt_reconfigure(&config);
  if (resultado != ESP_OK) {
    Serial.printf("Watchdog não pôde ser ativado: %d\n", resultado);
    return;
  }
  resultado = esp_task_wdt_add(NULL);
  if (resultado == ESP_OK || resultado == ESP_ERR_INVALID_STATE) {
    watchdogConfigurado = true;
    Serial.println("Watchdog de 60 segundos ativado para o loop principal.");
  } else {
    Serial.printf("Watchdog não pôde monitorar o loop: %d\n", resultado);
  }
}

void piscarIndicador(uint8_t vezes) {
  for (uint8_t i = 0; i < vezes; i++) {
    digitalWrite(LED_INDICADOR, HIGH); delay(140);
    digitalWrite(LED_INDICADOR, LOW); delay(140);
  }
}

bool aguardarWiFi(unsigned long timeoutMs) {
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < timeoutMs) {
    alimentarWatchdog();
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  // Uma passagem por vez: assim a trava e o watchdog continuam responsivos
  // se o roteador cair enquanto a geladeira está em uso.
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
    if (aguardarWiFi(8000)) {
      WiFi.scanDelete();
      Serial.printf("Wi-Fi conectado: %s\n", WiFi.localIP().toString().c_str());
      registrarEvento("WIFI_CONNECTED");
      piscarIndicador(3);
      acenderIndicador();
      return true;
    }
    WiFi.disconnect(true);
  }
  WiFi.scanDelete();
  // Reserva para redes ocultas: tenta somente uma rede por ciclo, sem travar o loop.
  static uint8_t proximaRedeOculta = 0;
  for (uint8_t tentativa = 0; tentativa < sizeof(redes) / sizeof(redes[0]); tentativa++) {
    Rede &rede = redes[proximaRedeOculta++ % (sizeof(redes) / sizeof(redes[0]))];
    if (!strlen(rede.ssid)) continue;
    Serial.printf("Tentando Wi-Fi oculto: %s\n", rede.ssid);
    WiFi.begin(rede.ssid, rede.senha);
    if (aguardarWiFi(5000)) {
      Serial.printf("Wi-Fi conectado: %s\n", WiFi.localIP().toString().c_str());
      registrarEvento("WIFI_CONNECTED");
      piscarIndicador(3);
      acenderIndicador();
      return true;
    }
    WiFi.disconnect(true);
    break;
  }
  return false;
}

const char *estadoDispositivo() {
  if (estado == DESTRAVADA) return "open";
  if (pedidoAtual.length()) return "waiting_to_open";
  return "locked";
}

void enviarHeartbeat(bool imediato = false) {
  if (!firebase.ready() || !baselineFeito) return;
  unsigned long agora = millis();
  if (!imediato && agora - ultimoHeartbeat < INTERVALO_HEARTBEAT_MS) return;
  ultimoHeartbeat = agora;
  atualizarDiagnosticoMemoria();
  JsonDocument doc;
  doc["online"] = true;
  doc["state"] = estadoDispositivo();
  doc["firmware"] = VERSAO_FIRMWARE;
  doc["uptimeSeconds"] = agora / 1000;
  doc["bootCount"] = totalInicializacoes;
  doc["firebaseConnected"] = firebase.ready();
  doc["streamActive"] = streamIniciado;
  doc["streamLastEventSecondsAgo"] = streamIniciado && ultimoEventoStream
    ? (agora - ultimoEventoStream) / 1000 : -1;
  doc["streamRecoveries"] = totalRecuperacoesStream;
  doc["lastStreamRecovery"] = ultimoMotivoRecuperacao;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["minFreeHeap"] = ESP.getMinFreeHeap();
  doc["heapAtReady"] = diagnosticoMemoria.heapPronto;
  doc["minHeapSinceReady"] = diagnosticoMemoria.menorHeapDesdePronto;
  doc["largestFreeBlock"] = maiorBlocoLivre();
  doc["largestBlockAtReady"] = diagnosticoMemoria.maiorBlocoPronto;
  doc["minLargestFreeBlock"] = diagnosticoMemoria.menorMaiorBlocoDesdePronto;
  doc["heapBeforeStreamRecovery"] = diagnosticoMemoria.heapAntesRecuperacao;
  doc["largestBlockBeforeStreamRecovery"] = diagnosticoMemoria.maiorBlocoAntesRecuperacao;
  doc["heapAfterStreamRecovery"] = diagnosticoMemoria.heapDepoisRecuperacao;
  doc["largestBlockAfterStreamRecovery"] = diagnosticoMemoria.maiorBlocoDepoisRecuperacao;
  doc["deviceMode"] = MODO_TESTE ? "TESTE" : "PRODUCAO";
  doc["eventLog"] = historicoEventos;
  doc["resetReason"] = motivoInicializacao;
  doc["safeRestartPending"] = reinicioPreventivoPendente;
  doc["lastOrderId"] = ultimoPedido;
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["connected"] = WiFi.status() == WL_CONNECTED;
  wifi["ssid"] = WiFi.SSID();
  wifi["rssi"] = WiFi.RSSI();
  JsonObject ultimoSinal = doc["lastSeen"].to<JsonObject>();
  ultimoSinal[".sv"] = "timestamp";
  String json;
  serializeJson(doc, json);
  if (!banco.set<object_t>(cliente, caminhoStatus, object_t(json))) {
    Serial.printf("Falha no heartbeat: %s\n", cliente.lastError().message().c_str());
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
  registrarEvento("LOCK_OPENED");
  proximaAcao = millis() + TEMPO_DESTRAVADO_MS;
  ultimaTrocaLed = 0;
  enviarHeartbeat(true);
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
  registrarEvento("LOCKED");
  enviarHeartbeat(true);
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
  if (MODO_TESTE) {
    Serial.printf("Pedido %s ignorado: placa em modo de teste.\n", id.c_str());
    return;
  }
  pedidoAtual = id;
  estado = AGUARDANDO;
  proximaAcao = millis() + ESPERA_ANTES_DE_ABRIR_MS;
  registrarEvento("ORDER_RECEIVED");
  Serial.printf("Novo pedido %s. Abrindo em 6 segundos.\n", id.c_str());
}

void agendarRecuperacaoStream(const char *motivo) {
  if (recuperacaoStreamPendente) return;
  atualizarDiagnosticoMemoria();
  if (diagnosticoMemoria.pronto) {
    diagnosticoMemoria.heapAntesRecuperacao = ESP.getFreeHeap();
    diagnosticoMemoria.maiorBlocoAntesRecuperacao = maiorBlocoLivre();
    diagnosticoMemoria.medicaoRecuperacaoPendente = true;
  }
  ultimoMotivoRecuperacao = motivo;
  char evento[90];
  snprintf(evento, sizeof(evento), "STREAM_%s", motivo);
  registrarEvento(evento);
  totalRecuperacoesStream++;
  memoria.putUInt("streamRecoveries", totalRecuperacoesStream);
  memoria.putString("lastStreamRecovery", ultimoMotivoRecuperacao);
  clienteStream.stopAsync(true);
  streamIniciado = false;
  recuperacaoStreamPendente = true;
  sincronizacaoSolicitada = false;
  proximaTentativaSincronizacao = millis() + INTERVALO_RECONEXAO_STREAM_MS;
  Serial.printf("Recuperação do stream agendada: %s\n", motivo);
}

void verificarSaudeStream(unsigned long agora) {
  if (!firebase.ready() || !streamIniciado || recuperacaoStreamPendente) return;
  if (agora - ultimaVerificacaoStream < INTERVALO_VERIFICACAO_STREAM_MS) return;
  ultimaVerificacaoStream = agora;
  if (ultimoEventoStream && agora - ultimoEventoStream > LIMITE_SEM_EVENTO_STREAM_MS) {
    agendarRecuperacaoStream("STREAM_SEM_SINAL");
  }
}

void reiniciarComSeguranca(const char *motivo) {
  // A saída do relé volta ao estado trancado antes do reset do processador.
  digitalWrite(RELE_TRAVA, RELE_TRAVADO);
  apagarLeds();
  char evento[90];
  snprintf(evento, sizeof(evento), "RESET_%s", motivo);
  registrarEvento(evento);
  memoria.putString("reinicioPlanejado", motivo);
  Serial.printf("Reinício seguro: %s\n", motivo);
  delay(150);
  ESP.restart();
}

void verificarReinicioPreventivo(unsigned long agora) {
  if (agora - inicioSessao >= INTERVALO_REINICIO_PREVENTIVO_MS) {
    reinicioPreventivoPendente = true;
  }
  // Nunca interrompe a abertura de uma bebida nem o intervalo de 6 segundos.
  if (reinicioPreventivoPendente && estado == AGUARDANDO && !pedidoAtual.length() && !pedidoNaFila.length()) {
    reiniciarComSeguranca("PREVENTIVO_5H");
  }
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
  // Na primeira conexão só registra o pedido mais recente para não abrir a geladeira por histórico.
  if (!baselineFeito) {
    baselineFeito = true;
    if (maiorId.length() && maiorId > ultimoPedido) { ultimoPedido = maiorId; memoria.putString("ultimoPedido", ultimoPedido); }
    apagarLeds(); // pronto para uso: LED apagado até uma abertura
    registrarEvento("ORDERS_BASELINED");
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
    Serial.printf("Falha na sincronização de pedidos: %s\n", resultado.error().message().c_str());
    sincronizacaoSolicitada = false;
    proximaTentativaSincronizacao = millis() + 3000;
    return;
  }
  if (!resultado.available()) return;

  const char *conteudo = resultado.c_str();
  JsonDocument doc;
  DeserializationError erroJson = conteudo ? deserializeJson(doc, conteudo) : DeserializationError::InvalidInput;
  if (!conteudo || erroJson) {
    // Diagnóstico seguro: mostra somente o tipo e o tamanho da resposta, nunca
    // o seu conteúdo (os pedidos podem conter dados de usuários).
    Serial.printf("Resposta inicial de pedidos inválida (%s, %u bytes); tentando novamente.\n",
                  erroJson.c_str(), conteudo ? strlen(conteudo) : 0U);
    sincronizacaoSolicitada = false;
    proximaTentativaSincronizacao = millis() + 3000;
    return;
  }

  // A consulta traz somente o último pedido. Baixar o histórico completo pode
  // ultrapassar a memória disponível conforme a lista de pedidos cresce.
  if (doc.isNull()) {
    baselineFeito = true;
    apagarLeds();
    registrarEvento("ORDERS_BASELINED");
    Serial.println("Sincronização inicial concluída; nenhum pedido pendente. ESP32 pronto para uso.");
  } else if (doc.is<JsonObject>()) {
    analisarPedidos(doc.as<JsonObject>());
  } else {
    Serial.println("Formato inicial de pedidos inesperado; tentando novamente.");
    sincronizacaoSolicitada = false;
    proximaTentativaSincronizacao = millis() + 3000;
    return;
  }
  sincronizacaoSolicitada = false;
  recuperacaoStreamPendente = false;
}

void processarStream(AsyncResult &resultado) {
  if (!resultado.isResult()) return;
  if (resultado.isError()) {
    Serial.printf("Stream Firebase: %s\n", resultado.error().message().c_str());
    agendarRecuperacaoStream("ERRO_DO_STREAM");
    return;
  }
  if (!resultado.available()) return;
  RealtimeDatabaseResult &stream = resultado.to<RealtimeDatabaseResult>();
  if (!stream.isStream()) return;
  ultimoEventoStream = millis();
  if (stream.eventTimeout()) {
    agendarRecuperacaoStream("TIMEOUT_DO_STREAM");
    return;
  }
  String evento = stream.event();
  if (evento == "keep-alive") return;
  if (evento != "put" && evento != "patch") return;
  String caminho = stream.dataPath();
  if (caminho != "/" && caminho.indexOf('/', 1) >= 0) return;
  const char *conteudo = stream.to<const char *>();
  if (!conteudo || !strlen(conteudo)) return;
  // Na abertura ou recuperação do stream o Firebase envia um snapshot de toda
  // a coleção. A sincronização do último pedido já aconteceu antes; ignorar
  // esse snapshot impede uma alocação grande e fragmentação do heap.
  if (baselineFeito && caminho == "/") return;
  JsonDocument doc;
  if (deserializeJson(doc, conteudo)) {
    Serial.println("Evento de pedidos ignorado: dados vazios.");
    return;
  }
  if (caminho == "/") {
    if (doc.isNull()) {
      baselineFeito = true;
      apagarLeds(); // pronto para uso: LED apagado até uma abertura
      registrarEvento("ORDERS_BASELINED");
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
  inicioSessao = millis();
  snprintf(caminhoStatus, sizeof(caminhoStatus), "/devices/%s", DEVICE_ID);
  pinMode(LED_INDICADOR, OUTPUT); acenderIndicador();
  pinMode(RELE_TRAVA, OUTPUT); digitalWrite(RELE_TRAVA, RELE_TRAVADO);
  memoria.begin("geladeira", false);
  String reinicioPlanejado = memoria.getString("reinicioPlanejado", "");
  memoria.remove("reinicioPlanejado");
  motivoInicializacao = reinicioPlanejado.length() ? reinicioPlanejado : nomeMotivoReset(esp_reset_reason());
  ultimoPedido = memoria.getString("ultimoPedido", "");
  totalInicializacoes = memoria.getUInt("bootCount", 0) + 1;
  memoria.putUInt("bootCount", totalInicializacoes);
  totalRecuperacoesStream = memoria.getUInt("streamRecoveries", 0);
  ultimoMotivoRecuperacao = memoria.getString("lastStreamRecovery", "NENHUMA");
  carregarHistoricoEventos();
  char eventoBoot[90];
  snprintf(eventoBoot, sizeof(eventoBoot), "BOOT_%s", motivoInicializacao.c_str());
  registrarEvento(eventoBoot);
  configurarWatchdog();
  WiFi.mode(WIFI_STA);
  while (!conectarWiFi()) {
    Serial.println("Nenhuma rede disponível; nova tentativa em 2 segundos.");
    alimentarWatchdog();
    delay(2000);
  }
  ssl.setInsecure();
  sslStream.setInsecure();
  ssl.setConnectionTimeout(1000); ssl.setHandshakeTimeout(5);
  sslStream.setConnectionTimeout(1000); sslStream.setHandshakeTimeout(5);
  initializeApp(cliente, firebase, getAuth(credenciais));
  firebase.getApp<RealtimeDatabase>(banco);
  banco.url(FIREBASE_DATABASE_URL);
  filtroUltimoPedido.filter.orderBy("$key").limitToLast(1);
  Serial.printf("ESP32 %s preparado (%s). Motivo da inicialização: %s\n",
                DEVICE_ID, MODO_TESTE ? "TESTE" : "PRODUCAO", motivoInicializacao.c_str());
}

void loop() {
  alimentarWatchdog();
  unsigned long agora = millis();
  // O tempo de abertura nunca depende da internet: a porta volta a travar
  // mesmo durante uma reconexão de Wi-Fi/Firebase.
  if (estado == DESTRAVADA && agora >= proximaAcao) trancarGeladeira();
  alternarLeds();

  if (WiFi.status() != WL_CONNECTED) {
    if (!inicioWifiIndisponivel) {
      inicioWifiIndisponivel = agora;
      registrarEvento("WIFI_DISCONNECTED");
    }
    if (streamIniciado || (baselineFeito && !recuperacaoStreamPendente)) {
      agendarRecuperacaoStream("WIFI_DESCONECTADO");
    }
    firebaseConfirmado = false;
    inicioFirebaseIndisponivel = 0;
    // Se a pilha Wi-Fi ficar presa tentando reconectar, a placa volta ao
    // estado seguro e reinicia. O motivo será enviado no próximo heartbeat.
    if (agora - inicioWifiIndisponivel >= LIMITE_WIFI_SEM_RETORNO_MS) {
      reiniciarComSeguranca("WIFI_SEM_RETORNO");
    }
    if (!conectarWiFi()) {
      delay(1000);
      return;
    }
    inicioWifiIndisponivel = 0;
  }
  firebase.loop();
  if (firebase.ready() && !firebaseConfirmado) {
    firebaseConfirmado = true;
    Serial.println("Firebase conectado.");
    registrarEvento("FIREBASE_CONNECTED");
    piscarIndicador(5); // confirma a conexão com o Firebase
    acenderIndicador(); // permanece aceso até a sincronização inicial dos pedidos
  }
  if (firebase.ready()) {
    inicioFirebaseIndisponivel = 0;
  } else {
    if (!inicioFirebaseIndisponivel) {
      inicioFirebaseIndisponivel = agora;
      registrarEvento("FIREBASE_UNAVAILABLE");
    }
    // Wi-Fi funcionando sem Firebase por muito tempo também pode deixar a
    // placa incapaz de receber pedidos; reiniciar é a recuperação segura.
    if (agora - inicioFirebaseIndisponivel >= LIMITE_FIREBASE_SEM_RETORNO_MS) {
      reiniciarComSeguranca("FIREBASE_SEM_RETORNO");
    }
  }
  verificarSaudeStream(agora);
  if (firebase.ready() && (!baselineFeito || recuperacaoStreamPendente) && !sincronizacaoSolicitada && millis() >= proximaTentativaSincronizacao) {
    sincronizacaoSolicitada = true;
    banco.get(cliente, "/orders", filtroUltimoPedido, processarSincronizacaoInicial, "sincronizacaoInicial");
    Serial.println(recuperacaoStreamPendente ? "Sincronizando pedidos após recuperar stream." : "Sincronizando pedidos iniciais.");
  }
  if (firebase.ready() && baselineFeito && !streamIniciado && millis() >= proximaTentativaStream) {
    // O stream só é aberto depois da autenticação: assim todo pedido novo é recebido.
    clienteStream.setSSEFilters("get,put,patch,keep-alive,cancel,auth_revoked");
    banco.get(clienteStream, "/orders", processarStream, true /* stream SSE */, "pedidosStream");
    streamIniciado = true;
    ultimoEventoStream = millis();
    iniciarDiagnosticoMemoria();
    if (diagnosticoMemoria.medicaoRecuperacaoPendente) {
      diagnosticoMemoria.heapDepoisRecuperacao = ESP.getFreeHeap();
      diagnosticoMemoria.maiorBlocoDepoisRecuperacao = maiorBlocoLivre();
      diagnosticoMemoria.medicaoRecuperacaoPendente = false;
      Serial.printf("Memória após recuperar stream: heap %u, maior bloco %u.\n",
                    diagnosticoMemoria.heapDepoisRecuperacao,
                    diagnosticoMemoria.maiorBlocoDepoisRecuperacao);
    }
    Serial.println("Monitoramento de pedidos ativado.");
    registrarEvento("STREAM_ACTIVE");
    enviarHeartbeat(true);
  }
  if (estado == AGUARDANDO && pedidoAtual.length() && agora >= proximaAcao) abrirTrava();
  enviarHeartbeat();
  verificarReinicioPreventivo(agora);
}
