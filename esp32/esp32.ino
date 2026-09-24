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
constexpr unsigned long TEMPO_DESTRAVADO_MS = 20000;
// A abertura notifica o painel imediatamente; fora isso, um status por minuto
// preserva a telemetria sem criar tráfego e buffers TLS desnecessários.
constexpr unsigned long INTERVALO_HEARTBEAT_MS = 60000;
constexpr unsigned long INTERVALO_RECONEXAO_STREAM_MS = 3000;
constexpr unsigned long INTERVALO_VERIFICACAO_STREAM_MS = 5000;
constexpr unsigned long LIMITE_SEM_EVENTO_STREAM_MS = 120000;
constexpr unsigned long INTERVALO_REINICIO_PREVENTIVO_MS = 5UL * 60UL * 60UL * 1000UL;
constexpr unsigned long LIMITE_WIFI_SEM_RETORNO_MS = 5UL * 60UL * 1000UL;
constexpr unsigned long LIMITE_FIREBASE_SEM_RETORNO_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t WATCHDOG_TIMEOUT_MS = 60000;
constexpr uint32_t LIMITE_MAIOR_BLOCO_CRITICO = 10UL * 1024UL;
constexpr uint8_t AMOSTRAS_BLOCO_CRITICO = 3;
constexpr char VERSAO_FIRMWARE[] = "2.7.0";

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
TaskHandle_t tarefaLedAbertura = nullptr;
volatile bool piscarLedAberturaAtivo = false;
bool baselineFeito = false;
bool firebaseConfirmado = false;
bool watchdogConfigurado = false;
bool streamIniciado = false;
bool sincronizacaoSolicitada = false;
bool recuperacaoStreamPendente = false;
bool reinicioPreventivoPendente = false;
bool reinicioMemoriaPendente = false;
uint8_t amostrasBlocoCritico = 0;
unsigned long proximaTentativaSincronizacao = 0;
unsigned long proximaTentativaStream = 0;
unsigned long ultimoHeartbeat = 0;
unsigned long ultimoEventoStream = 0;
unsigned long ultimaVerificacaoStream = 0;
unsigned long inicioSessao = 0;
unsigned long inicioWifiIndisponivel = 0;
unsigned long inicioFirebaseIndisponivel = 0;
uint32_t totalInicializacoes = 0;
String motivoInicializacao = "DESCONHECIDO";
String caminhoStatus;
String caminhoExecucao;
String caminhoComandos;
char bufferHeartbeat[480];
char bufferCaminho[112];

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

void tarefaPiscarLedAbertura(void *) {
  // Esta tarefa permanece independente das transmissões Wi-Fi/Firebase do
  // loop principal. Assim o sinal luminoso acompanha a abertura real do relé.
  while (piscarLedAberturaAtivo) {
    digitalWrite(LED_INDICADOR, HIGH);
    vTaskDelay(pdMS_TO_TICKS(250));
    if (!piscarLedAberturaAtivo) break;
    digitalWrite(LED_INDICADOR, LOW);
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  digitalWrite(LED_INDICADOR, LOW);
  tarefaLedAbertura = nullptr;
  vTaskDelete(nullptr);
}

void iniciarPiscarLedAbertura() {
  piscarLedAberturaAtivo = true;
  digitalWrite(LED_INDICADOR, HIGH);
  if (!tarefaLedAbertura) {
    BaseType_t criada = xTaskCreatePinnedToCore(
      tarefaPiscarLedAbertura, "led_abertura", 2048, nullptr, 1,
      &tarefaLedAbertura, 0
    );
    if (criada != pdPASS) {
      tarefaLedAbertura = nullptr;
      Serial.println("Não foi possível iniciar a tarefa do LED.");
    }
  }
}

void pararPiscarLedAbertura() {
  piscarLedAberturaAtivo = false;
  digitalWrite(LED_INDICADOR, LOW);
}

void apagarLeds() { pararPiscarLedAbertura(); }
void acenderIndicador() { digitalWrite(LED_INDICADOR, HIGH); }

void iniciarPedido(const String &id);
void processarSincronizacaoInicial(AsyncResult &resultado);

void registrarEvento(const char *evento) {
  // Logs completos ficam no Serial Monitor. Não os persistimos no Firebase:
  // a operação contínua da trava tem prioridade sobre telemetria detalhada.
  Serial.printf("B%lu/U%lus:%s\n", static_cast<unsigned long>(totalInicializacoes),
                millis() / 1000UL, evento);
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

bool prontoParaReinicioSeguro() {
  return estado == AGUARDANDO && !pedidoAtual.length() && !pedidoNaFila.length();
}

void verificarFragmentacaoMemoria() {
  if (!diagnosticoMemoria.pronto) return;
  uint32_t maiorBloco = maiorBlocoLivre();
  if (maiorBloco < LIMITE_MAIOR_BLOCO_CRITICO) {
    if (amostrasBlocoCritico < AMOSTRAS_BLOCO_CRITICO) amostrasBlocoCritico++;
    if (amostrasBlocoCritico >= AMOSTRAS_BLOCO_CRITICO && !reinicioMemoriaPendente) {
      reinicioMemoriaPendente = true;
      registrarEvento("MEMORIA_BLOCO_CRITICO");
      Serial.printf("Maior bloco livre crítico: %u bytes. Reinício seguro pendente.\n", maiorBloco);
    }
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
  // Só contam as leituras periódicas. Estados imediatos de abrir/trancar não
  // podem provocar um reinício por uma queda transitória de memória.
  if (!imediato) verificarFragmentacaoMemoria();
  const int escrito = snprintf(
    bufferHeartbeat, sizeof(bufferHeartbeat),
    "{\"online\":true,\"state\":\"%s\",\"firmware\":\"%s\",\"uptimeSeconds\":%lu,"
    "\"firebaseConnected\":%s,\"streamActive\":%s,\"resetReason\":\"%s\","
    "\"wifi\":{\"connected\":%s,\"ssid\":\"%s\",\"rssi\":%d},"
    "\"lastSeen\":{\".sv\":\"timestamp\"}}",
    estadoDispositivo(), VERSAO_FIRMWARE, agora / 1000UL,
    firebase.ready() ? "true" : "false", streamIniciado ? "true" : "false",
    motivoInicializacao.c_str(), WiFi.status() == WL_CONNECTED ? "true" : "false",
    WiFi.SSID().c_str(), WiFi.RSSI()
  );
  if (escrito < 0 || static_cast<size_t>(escrito) >= sizeof(bufferHeartbeat)) {
    Serial.println("Heartbeat ignorado: buffer de status insuficiente.");
    return;
  }
  if (!banco.update<object_t>(cliente, caminhoStatus, object_t(bufferHeartbeat))) {
    Serial.printf("Falha no heartbeat: %s\n", cliente.lastError().message().c_str());
  }
}

void registrarEstado(const char *novoEstado) {
  caminhoExecucao = "/orders/";
  caminhoExecucao += pedidoAtual;
  caminhoExecucao += "/execution/state";
  bool ok = banco.set<string_t>(cliente, caminhoExecucao, string_t(novoEstado));
  if (!ok) Serial.printf("Falha ao registrar estado: %s\n", cliente.lastError().message().c_str());
}

void removerComando(const String &id) {
  const int escrito = snprintf(bufferCaminho, sizeof(bufferCaminho), "/commands/%s/%s", DEVICE_ID, id.c_str());
  if (escrito < 0 || static_cast<size_t>(escrito) >= sizeof(bufferCaminho)) return;
  if (!banco.remove(cliente, bufferCaminho)) {
    Serial.printf("Falha ao limpar comando: %s\n", cliente.lastError().message().c_str());
  }
}

void abrirTrava() {
  digitalWrite(RELE_TRAVA, RELE_DESTRAVADO);
  iniciarPiscarLedAbertura();
  Serial.println("GELADEIRA ABERTA: 20 segundos");
  registrarEstado("opened");
  estado = DESTRAVADA;
  registrarEvento("LOCK_OPENED");
  proximaAcao = millis() + TEMPO_DESTRAVADO_MS;
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

void iniciarPedido(const String &id) {
  if (MODO_TESTE) {
    Serial.printf("Pedido %s ignorado: placa em modo de teste.\n", id.c_str());
    return;
  }
  pedidoAtual = id;
  // O comando de abertura é descartado assim que foi aceito. Desse modo, o
  // próximo snapshot do stream permanece vazio e não acumula pedidos antigos.
  removerComando(id);
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
  char evento[90];
  snprintf(evento, sizeof(evento), "STREAM_%s", motivo);
  registrarEvento(evento);
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
  if (reinicioMemoriaPendente && prontoParaReinicioSeguro()) {
    reiniciarComSeguranca("MEMORIA_FRAGMENTADA");
  }
  if (reinicioPreventivoPendente && prontoParaReinicioSeguro()) {
    reiniciarComSeguranca("PREVENTIVO_5H");
  }
}

void concluirSincronizacaoInicial() {
  if (baselineFeito) return;
  // Nunca executamos um comando que já estava no Firebase quando a placa
  // iniciou. Isso impede que uma reinicialização abra a trava sem uma ação
  // atual do usuário.
  baselineFeito = true;
  apagarLeds();
  registrarEvento("COMMANDS_BASELINED");
  Serial.println("Sincronização inicial concluída; ESP32 pronto para uso.");
}

void analisarComandoNovo(const String &id) {
  if (!baselineFeito || id <= ultimoPedido) return;
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
    Serial.printf("Falha na sincronização de comandos: %s\n", resultado.error().message().c_str());
    sincronizacaoSolicitada = false;
    proximaTentativaSincronizacao = millis() + 3000;
    return;
  }
  if (!resultado.available()) return;

  // O conteúdo inicial é propositalmente ignorado. A coleção contém somente
  // comandos mínimos; qualquer comando anterior ao boot é considerado antigo.
  concluirSincronizacaoInicial();
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
  if (evento == "keep-alive" || evento != "put") return;
  String caminho = stream.dataPath();
  if (caminho != "/" && caminho.indexOf('/', 1) >= 0) return;
  if (caminho == "/") {
    concluirSincronizacaoInicial();
    return;
  }
  // Cada novo comando é somente o valor JSON literal `true`. O ID vem do
  // caminho SSE; nenhum pedido, item, preço ou dados do usuário são baixados.
  const char *conteudo = stream.to<const char *>();
  if (!conteudo || strcmp(conteudo, "true") != 0) return;
  String id = caminho.substring(1);
  analisarComandoNovo(id);
}

void setup() {
  Serial.begin(115200);
  inicioSessao = millis();
  pedidoAtual.reserve(40);
  ultimoPedido.reserve(40);
  pedidoNaFila.reserve(40);
  motivoInicializacao.reserve(48);
  caminhoStatus.reserve(48);
  caminhoExecucao.reserve(96);
  caminhoComandos.reserve(48);
  caminhoStatus = "/devices/";
  caminhoStatus += DEVICE_ID;
  caminhoComandos = "/commands/";
  caminhoComandos += DEVICE_ID;
  pinMode(LED_INDICADOR, OUTPUT);
  acenderIndicador();
  pinMode(RELE_TRAVA, OUTPUT); digitalWrite(RELE_TRAVA, RELE_TRAVADO);
  memoria.begin("geladeira", false);
  String reinicioPlanejado = memoria.getString("reinicioPlanejado", "");
  memoria.remove("reinicioPlanejado");
  motivoInicializacao = reinicioPlanejado.length() ? reinicioPlanejado : nomeMotivoReset(esp_reset_reason());
  ultimoPedido = memoria.getString("ultimoPedido", "");
  totalInicializacoes = memoria.getUInt("bootCount", 0) + 1;
  memoria.putUInt("bootCount", totalInicializacoes);
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
  Serial.printf("ESP32 %s preparado (%s). Motivo da inicialização: %s\n",
                DEVICE_ID, MODO_TESTE ? "TESTE" : "PRODUCAO", motivoInicializacao.c_str());
}

void loop() {
  alimentarWatchdog();
  unsigned long agora = millis();
  // O tempo de abertura nunca depende da internet: a porta volta a travar
  // mesmo durante uma reconexão de Wi-Fi/Firebase.
  if (estado == DESTRAVADA && agora >= proximaAcao) trancarGeladeira();

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
    acenderIndicador(); // permanece aceso até a sincronização inicial dos comandos
  }
  if (firebase.ready()) {
    inicioFirebaseIndisponivel = 0;
  } else {
    if (!inicioFirebaseIndisponivel) {
      inicioFirebaseIndisponivel = agora;
      registrarEvento("FIREBASE_UNAVAILABLE");
    }
    // Wi-Fi funcionando sem Firebase por muito tempo também pode deixar a
    // placa incapaz de receber comandos; reiniciar é a recuperação segura.
    if (agora - inicioFirebaseIndisponivel >= LIMITE_FIREBASE_SEM_RETORNO_MS) {
      reiniciarComSeguranca("FIREBASE_SEM_RETORNO");
    }
  }
  verificarSaudeStream(agora);
  if (firebase.ready() && (!baselineFeito || recuperacaoStreamPendente) && !sincronizacaoSolicitada && millis() >= proximaTentativaSincronizacao) {
    sincronizacaoSolicitada = true;
    banco.get(cliente, caminhoComandos, processarSincronizacaoInicial, "sincronizacaoInicial");
    Serial.println(recuperacaoStreamPendente ? "Sincronizando comandos após recuperar stream." : "Sincronizando comandos iniciais.");
  }
  if (firebase.ready() && baselineFeito && !streamIniciado && millis() >= proximaTentativaStream) {
    // O stream só é aberto depois da autenticação: assim todo pedido novo é recebido.
    clienteStream.setSSEFilters("get,put,patch,keep-alive,cancel,auth_revoked");
    banco.get(clienteStream, caminhoComandos, processarStream, true /* stream SSE */, "comandosStream");
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
    Serial.println("Monitoramento de comandos ativado.");
    registrarEvento("STREAM_ACTIVE");
    enviarHeartbeat(true);
  }
  if (estado == AGUARDANDO && pedidoAtual.length() && agora >= proximaAcao) abrirTrava();
  enviarHeartbeat();
  verificarReinicioPreventivo(agora);
}
