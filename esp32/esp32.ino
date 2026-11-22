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
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "ControleTrava.h"
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
// Eventos de abertura/fechamento levam o status no mesmo PATCH.
// Em repouso, um único status a cada cinco minutos.
constexpr unsigned long INTERVALO_HEARTBEAT_MS = 300000;
constexpr unsigned long INTERVALO_DIAGNOSTICO_MS = 60000;
constexpr unsigned long INTERVALO_REPETICAO_ENVIO_MS = 5000;
constexpr unsigned long LIMITE_ENVIO_MS = 15000;
constexpr unsigned long INTERVALO_RECONEXAO_STREAM_MS = 3000;
constexpr unsigned long INTERVALO_VERIFICACAO_STREAM_MS = 5000;
constexpr unsigned long LIMITE_SEM_EVENTO_STREAM_MS = 120000;
constexpr unsigned long INTERVALO_REINICIO_PREVENTIVO_MS = 5UL * 60UL * 60UL * 1000UL;
constexpr unsigned long LIMITE_WIFI_SEM_RETORNO_MS = 5UL * 60UL * 1000UL;
constexpr unsigned long LIMITE_FIREBASE_SEM_RETORNO_MS = 5UL * 60UL * 1000UL;
constexpr unsigned long INTERVALO_ESTABILIZACAO_PANIC_MS = 2UL * 60UL * 1000UL;
constexpr uint32_t WATCHDOG_TIMEOUT_MS = 60000;
constexpr uint32_t LIMITE_MAIOR_BLOCO_CRITICO = 10UL * 1024UL;
constexpr uint8_t AMOSTRAS_BLOCO_CRITICO = 3;
constexpr uint8_t LIMITE_PANICS_CONSECUTIVOS = 3;
constexpr char VERSAO_FIRMWARE[] = "2.8.3";

struct Rede { const char *ssid; const char *senha; };
Rede redes[] = {
  { WIFI_BOX_POLEZI_SSID, WIFI_BOX_POLEZI_PASSWORD },
  { WIFI_CLUBE_EXT_SSID, WIFI_CLUBE_EXT_PASSWORD },
  { WIFI_CLUBE_PRINCIPAL_SSID, WIFI_CLUBE_PRINCIPAL_PASSWORD },
  { WIFI_CASA_SSID, WIFI_CASA_PASSWORD },
  { WIFI_FORUM_SSID, WIFI_FORUM_PASSWORD },
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
// Callbacks apenas copiam eventos. Rede, NVS e cancelamento de tarefas são
// processados pelo loop, depois que FirebaseClient devolve o controle.
String comandoRecebido;
bool reinicioRecebido = false;
bool sincronizacaoConcluidaPendente = false;
bool cancelamentoStreamPendente = false;
TaskHandle_t tarefaControleTrava = nullptr;
QueueHandle_t eventosTrava = nullptr;
struct EventoTrava { TemporizadorTrava::Evento tipo; };
FilaConfirmacoesTrava confirmacoes;
bool statusPendente = false;
bool envioEmAndamento = false;
bool envioTemConfirmacao = false;
bool envioIncluiReinicio = false;
bool limpezaReinicioPendente = false;
uint8_t resultadoEnvio = 0;
uint32_t sequenciaEnvio = 0;
unsigned long inicioEnvio = 0;
unsigned long proximoEnvio = 0;
unsigned long ultimoDiagnostico = 0;
char uidEnvio[24] = {};
bool baselineFeito = false;
bool firebaseConfirmado = false;
bool firebaseInicializado = false;
bool watchdogConfigurado = false;
bool streamIniciado = false;
bool sincronizacaoSolicitada = false;
bool recuperacaoStreamPendente = false;
bool reinicioPreventivoPendente = false;
bool reinicioMemoriaPendente = false;
bool reinicioRemotoPendente = false;
bool modoSeguroPanico = false;
uint8_t amostrasBlocoCritico = 0;
uint8_t panicsConsecutivos = 0;
unsigned long proximaTentativaSincronizacao = 0;
unsigned long proximaTentativaStream = 0;
unsigned long ultimoHeartbeat = 0;
unsigned long ultimoEventoStream = 0;
unsigned long ultimaVerificacaoStream = 0;
unsigned long inicioSessao = 0;
unsigned long inicioWifiIndisponivel = 0;
unsigned long inicioFirebaseIndisponivel = 0;
unsigned long inicioModoSeguroPanico = 0;
uint32_t totalInicializacoes = 0;
String motivoInicializacao = "DESCONHECIDO";
String etapaAnteriorReinicio;
String etapaAtual;
String caminhoStatus;
String caminhoComandos;
char bufferHeartbeat[768];
char bufferEnvio[1152];

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

void tarefaAcionarTrava(void *) {
  // Esta tarefa é a única responsável pelo ciclo físico. Não faz rede,
  // Serial, NVS, alocações ou chamadas Firebase. Dorme entre as verificações.
  TemporizadorTrava temporizador;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    temporizador.iniciar(millis());
    unsigned long ultimoPiscar = millis();
    bool ledAceso = false;
    for (;;) {
      const unsigned long agora = millis();
      EventoTrava evento{temporizador.atualizar(agora)};
      if (evento.tipo == TemporizadorTrava::ABRIU) {
        digitalWrite(RELE_TRAVA, RELE_DESTRAVADO);
        ledAceso = true;
        ultimoPiscar = agora;
        digitalWrite(LED_INDICADOR, HIGH);
        xQueueSend(eventosTrava, &evento, 0);
      } else if (evento.tipo == TemporizadorTrava::TRANCOU) {
        // Primeiro a saída física; a comunicação pode continuar indisponível.
        digitalWrite(RELE_TRAVA, RELE_TRAVADO);
        digitalWrite(LED_INDICADOR, LOW);
        xQueueSend(eventosTrava, &evento, 0);
        break;
      }
      if (temporizador.aberta() && agora - ultimoPiscar >= 250) {
        ultimoPiscar = agora;
        ledAceso = !ledAceso;
        digitalWrite(LED_INDICADOR, ledAceso ? HIGH : LOW);
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

void apagarLeds() { digitalWrite(LED_INDICADOR, LOW); }
void acenderIndicador() { digitalWrite(LED_INDICADOR, HIGH); }

void iniciarPedido(const String &id);
void processarSincronizacaoInicial(AsyncResult &resultado);

void registrarEvento(const char *evento) {
  // Logs completos ficam no Serial Monitor. Não os persistimos no Firebase:
  // a operação contínua da trava tem prioridade sobre telemetria detalhada.
  Serial.printf("B%lu/U%lus:%s\n", static_cast<unsigned long>(totalInicializacoes),
                millis() / 1000UL, evento);
}

void marcarEtapa(const char *etapa) {
  if (etapaAtual == etapa) return;
  etapaAtual = etapa;
  // Persistimos apenas transições importantes. Isso permite saber em qual
  // etapa a placa estava antes de um panic sem gravar a cada heartbeat.
  memoria.putString("ultimaEtapa", etapaAtual);
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
  return estado == AGUARDANDO && !pedidoAtual.length() && !pedidoNaFila.length() &&
         confirmacoes.vazia() && !envioEmAndamento;
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
    // núcleos internos durante conexões TLS. O watchdog de interrupções
    // continua independente desta configuração.
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
      marcarEtapa("WIFI_CONECTADO");
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
      marcarEtapa("WIFI_CONECTADO");
      piscarIndicador(3);
      acenderIndicador();
      return true;
    }
    WiFi.disconnect(true);
    break;
  }
  return false;
}

void iniciarFirebaseSeNecessario() {
  if (firebaseInicializado || WiFi.status() != WL_CONNECTED) return;
  ssl.setInsecure();
  sslStream.setInsecure();
  ssl.setConnectionTimeout(1000); ssl.setHandshakeTimeout(5);
  sslStream.setConnectionTimeout(1000); sslStream.setHandshakeTimeout(5);
  initializeApp(cliente, firebase, getAuth(credenciais));
  firebase.getApp<RealtimeDatabase>(banco);
  banco.url(FIREBASE_DATABASE_URL);
  firebaseInicializado = true;
  marcarEtapa("FIREBASE_INICIADO");
  Serial.println("Firebase inicializado; aguardando autenticação.");
}

const char *estadoDispositivo() {
  if (estado == DESTRAVADA) return "open";
  if (pedidoAtual.length()) return "waiting_to_open";
  return "locked";
}

void enviarHeartbeat(bool imediato = false) {
  // Apenas solicita o envio. Não cria tarefas Firebase nem espera pela rede.
  if (imediato || millis() - ultimoHeartbeat >= INTERVALO_HEARTBEAT_MS) statusPendente = true;
}

void registrarEstado(const char *novoEstado) {
  // O ID é copiado antes de pedidoAtual ser liberado para o próximo da fila.
  if (!confirmacoes.adicionar(pedidoAtual.c_str(), strcmp(novoEstado, "opened") == 0,
                              envioEmAndamento && envioTemConfirmacao)) {
    registrarEvento("FILA_CONFIRMACOES_CHEIA");
  }
}

void processarResultadoEnvio(AsyncResult &resultado) {
  if (!envioEmAndamento || resultado.uid() != uidEnvio || !resultado.isResult()) return;
  if (resultado.isError()) resultadoEnvio = 2;
  else if (resultado.available()) resultadoEnvio = 1;
}

// Escapa o SSID para que aspas e barras não invalidem o JSON do status.
void escaparSSID(const String &ssid, char *destino) {
  static const char hex[] = "0123456789abcdef";
  char *saida = destino;
  for (size_t i = 0; i < ssid.length() && i < 32; ++i) {
    const unsigned char c = ssid[i];
    if (c == '"' || c == '\\') { *saida++ = '\\'; *saida++ = c; }
    else if (c < 0x20) {
      *saida++ = '\\'; *saida++ = 'u'; *saida++ = '0'; *saida++ = '0';
      *saida++ = hex[c >> 4]; *saida++ = hex[c & 15];
    } else *saida++ = c;
  }
  *saida = 0;
}

void processarEnvios(unsigned long agora) {
  if (envioEmAndamento && (resultadoEnvio || agora - inicioEnvio >= LIMITE_ENVIO_MS)) {
    const bool ok = resultadoEnvio == 1;
    envioEmAndamento = false;
    if (!ok) {
      // Fora do callback, apenas o PATCH em trânsito é cancelado.
      cliente.stopAsync(String(uidEnvio));
      registrarEvento("ENVIO_STATUS_FALHOU");
    }
    if (envioTemConfirmacao && (ok || confirmacoes.primeiraAberturaSuperada() ||
        confirmacoes.primeira().tentativas >= 3)) {
      if (!ok && confirmacoes.primeira().tentativas >= 3) registrarEvento("CONFIRMACAO_SEM_RETORNO");
      confirmacoes.removerPrimeira();
    }
    if (ok && envioIncluiReinicio) limpezaReinicioPendente = false;
    // Um heartbeat de repouso que falhou aguarda o próximo intervalo normal.
    // Confirmações e consumo de reinício têm as tentativas prioritárias.
    if (!ok && (envioTemConfirmacao || limpezaReinicioPendente)) statusPendente = true;
    proximoEnvio = agora + (ok ? 0 : INTERVALO_REPETICAO_ENVIO_MS);
  }
  if (envioEmAndamento || !firebase.ready() || !baselineFeito ||
      WiFi.status() != WL_CONNECTED || cliente.taskCount() ||
      static_cast<int32_t>(agora - proximoEnvio) < 0) return;
  if (confirmacoes.vazia() && !statusPendente) return;

  char ssidJSON[193];
  escaparSSID(WiFi.SSID(), ssidJSON);
  int escrito = snprintf(
    bufferHeartbeat, sizeof(bufferHeartbeat),
    "{\"online\":true,\"state\":\"%s\",\"firmware\":\"%s\",\"uptimeSeconds\":%lu,"
    "\"firebaseConnected\":true,\"streamActive\":%s,\"remoteRestartPending\":%s,\"resetReason\":\"%s\","
    "\"wifi\":{\"connected\":true,\"ssid\":\"%s\",\"rssi\":%d},"
    "\"lastSeen\":{\".sv\":\"timestamp\"}}",
    estadoDispositivo(), VERSAO_FIRMWARE, agora / 1000UL,
    streamIniciado ? "true" : "false", reinicioRemotoPendente ? "true" : "false",
    motivoInicializacao.c_str(), ssidJSON, WiFi.RSSI());
  if (escrito < 0 || static_cast<size_t>(escrito) >= sizeof(bufferHeartbeat)) return;

  envioTemConfirmacao = !confirmacoes.vazia();
  // Uma confirmação de pedido tem prioridade; o reinício vai no próximo PATCH.
  envioIncluiReinicio = !envioTemConfirmacao && limpezaReinicioPendente;
  if (envioTemConfirmacao) {
    ConfirmacaoTrava &item = confirmacoes.primeira();
    escrito = snprintf(bufferEnvio, sizeof(bufferEnvio),
      "{\"devices/%s\":%s,\"orders/%s/execution/state\":\"%s\",\"commands/%s/%s\":null}",
      DEVICE_ID, bufferHeartbeat, item.pedido, item.abriu ? "opened" : "locked", DEVICE_ID, item.pedido);
  } else if (envioIncluiReinicio) {
    escrito = snprintf(bufferEnvio, sizeof(bufferEnvio),
      "{\"devices/%s\":%s,\"commands/%s/system/restart\":null}", DEVICE_ID, bufferHeartbeat, DEVICE_ID);
  } else {
    escrito = snprintf(bufferEnvio, sizeof(bufferEnvio), "{\"devices/%s\":%s}", DEVICE_ID, bufferHeartbeat);
  }
  if (escrito < 0 || static_cast<size_t>(escrito) >= sizeof(bufferEnvio)) return;
  if (envioTemConfirmacao) confirmacoes.primeira().tentativas++;
  snprintf(uidEnvio, sizeof(uidEnvio), "status_%lu", static_cast<unsigned long>(++sequenciaEnvio));
  resultadoEnvio = 0;
  inicioEnvio = agora;
  ultimoHeartbeat = agora;
  statusPendente = false;
  envioEmAndamento = true;
  // Um único PATCH assíncrono atualiza pedido, status e limpeza do comando.
  banco.update<object_t>(cliente, "/", object_t(bufferEnvio), processarResultadoEnvio, uidEnvio);
}

void abrirTrava() {
  // O relé e o temporizador já foram acionados pela tarefa independente.
  estado = DESTRAVADA;
  registrarEstado("opened");
  registrarEvento("LOCK_OPENED");
}

void trancarGeladeira() {
  // O relé já voltou ao estado trancado antes de qualquer NVS ou comunicação.
  estado = AGUARDANDO;
  registrarEstado("locked");
  memoria.putString("ultimoPedido", pedidoAtual);
  ultimoPedido = pedidoAtual;
  pedidoAtual = "";
  marcarEtapa("PRONTO");
  registrarEvento("LOCKED");
}

void processarEventosTrava() {
  EventoTrava evento;
  while (eventosTrava && xQueueReceive(eventosTrava, &evento, 0) == pdTRUE) {
    if (evento.tipo == TemporizadorTrava::ABRIU) abrirTrava();
    else if (evento.tipo == TemporizadorTrava::TRANCOU) trancarGeladeira();
  }
}

void iniciarPedido(const String &id) {
  if (MODO_TESTE) {
    Serial.printf("Pedido %s ignorado: placa em modo de teste.\n", id.c_str());
    return;
  }
  if (modoSeguroPanico || !tarefaControleTrava || !confirmacoes.reservarCiclo()) {
    if (!pedidoNaFila.length() || id < pedidoNaFila) pedidoNaFila = id;
    return;
  }
  pedidoAtual = id;
  estado = AGUARDANDO;
  // Nenhuma gravação ou operação de rede antecede o início da espera local.
  xTaskNotifyGive(tarefaControleTrava);
  marcarEtapa("PEDIDO_EM_ANDAMENTO");
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
  cancelamentoStreamPendente = true;
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

bool verificarModoSeguroPanico(unsigned long agora) {
  if (!modoSeguroPanico) return false;
  if (reinicioRemotoPendente && !limpezaReinicioPendente && !envioEmAndamento && prontoParaReinicioSeguro()) {
    reiniciarComSeguranca("REMOTO");
  }
  if (!firebase.ready() || !streamIniciado) return true;
  if (!inicioModoSeguroPanico) {
    inicioModoSeguroPanico = agora;
    registrarEvento("PANIC_SAFE_MODE_ONLINE");
    Serial.println("Modo seguro após panics repetidos: trava fechada, novos pedidos em espera.");
  }
  // Dois minutos com stream ativo sem novo panic mostram que o boot, Wi-Fi e
  // Firebase estabilizaram. A placa então volta ao modo normal sem descartar
  // pedidos que chegaram durante a recuperação.
  if (agora - inicioModoSeguroPanico >= INTERVALO_ESTABILIZACAO_PANIC_MS) {
    memoria.putUChar("panicCount", 0);
    modoSeguroPanico = false;
    registrarEvento("PANIC_SAFE_MODE_RECOVERED");
    Serial.println("Modo seguro concluído; comandos voltaram a ser processados.");
    if (pedidoNaFila.length()) {
      String pedidoPendente = pedidoNaFila;
      pedidoNaFila = "";
      iniciarPedido(pedidoPendente);
    }
    return false;
  }
  return true;
}

void verificarReinicioPreventivo(unsigned long agora) {
  if (agora - inicioSessao >= INTERVALO_REINICIO_PREVENTIVO_MS) {
    reinicioPreventivoPendente = true;
  }
  // Nunca interrompe a abertura de uma bebida nem o intervalo de 6 segundos.
  if (reinicioRemotoPendente && !limpezaReinicioPendente && !envioEmAndamento && prontoParaReinicioSeguro()) {
    reiniciarComSeguranca("REMOTO");
  }
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
  marcarEtapa("COMANDOS_SINCRONIZADOS");
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

void solicitarReinicioRemoto() {
  // O comando só é aceito após o baseline. Ele é consumido imediatamente para
  // que nunca sobreviva como uma solicitação pendente depois de um boot.
  if (!baselineFeito || reinicioRemotoPendente) return;
  reinicioRemotoPendente = true;
  limpezaReinicioPendente = true;
  registrarEvento("REMOTE_RESTART_REQUESTED");
  Serial.println("Reinício remoto solicitado; aguardando a trava ficar fechada.");
  enviarHeartbeat(true);
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
  sincronizacaoConcluidaPendente = true;
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
  if (caminho == "/") {
    sincronizacaoConcluidaPendente = true;
    return;
  }
  // Cada novo comando é somente o valor JSON literal `true`. O ID vem do
  // caminho SSE; nenhum pedido, item, preço ou dados do usuário são baixados.
  const char *conteudo = stream.to<const char *>();
  if (!conteudo || strcmp(conteudo, "true") != 0) return;
  if (caminho == "/system/restart") {
    if (baselineFeito) reinicioRecebido = true;
    return;
  }
  if (caminho.indexOf('/', 1) >= 0) return;
  String id = caminho.substring(1);
  if (!id.length() || id.length() > 100) return;
  for (size_t i = 0; i < id.length(); ++i) {
    const char c = id[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_')) return;
  }
  if (baselineFeito && id > ultimoPedido &&
      (!comandoRecebido.length() || id < comandoRecebido)) comandoRecebido = id;
}

void processarEventosFirebase() {
  // Nunca chamar esta função de um callback: as operações síncronas abaixo
  // podem fazer a biblioteca processar novos resultados antes de retornarem.
  if (cancelamentoStreamPendente) {
    cancelamentoStreamPendente = false;
    clienteStream.stopAsync(true);
  }
  if (sincronizacaoConcluidaPendente) {
    sincronizacaoConcluidaPendente = false;
    concluirSincronizacaoInicial();
    sincronizacaoSolicitada = false;
    recuperacaoStreamPendente = false;
  }
  if (reinicioRecebido) {
    reinicioRecebido = false;
    solicitarReinicioRemoto();
  }
  if (comandoRecebido.length()) {
    String id = comandoRecebido;
    comandoRecebido = "";
    analisarComandoNovo(id);
  }
}

void setup() {
  Serial.begin(115200);
  inicioSessao = millis();
  pedidoAtual.reserve(40);
  ultimoPedido.reserve(40);
  pedidoNaFila.reserve(40);
  comandoRecebido.reserve(40);
  motivoInicializacao.reserve(64);
  etapaAnteriorReinicio.reserve(32);
  etapaAtual.reserve(32);
  caminhoStatus.reserve(48);
  caminhoComandos.reserve(48);
  caminhoStatus = "/devices/";
  caminhoStatus += DEVICE_ID;
  caminhoComandos = "/commands/";
  caminhoComandos += DEVICE_ID;
  pinMode(LED_INDICADOR, OUTPUT);
  acenderIndicador();
  pinMode(RELE_TRAVA, OUTPUT); digitalWrite(RELE_TRAVA, RELE_TRAVADO);
  // Dois eventos por ciclo; o próximo pedido só começa após drenar ambos.
  eventosTrava = xQueueCreate(2, sizeof(EventoTrava));
  if (eventosTrava && xTaskCreatePinnedToCore(tarefaAcionarTrava, "controle_trava", 2048,
      nullptr, 2, &tarefaControleTrava, 1) != pdPASS) tarefaControleTrava = nullptr;
  if (!tarefaControleTrava) Serial.println("Controle da trava indisponível; relé mantido trancado.");
  memoria.begin("geladeira", false);
  String reinicioPlanejado = memoria.getString("reinicioPlanejado", "");
  memoria.remove("reinicioPlanejado");
  etapaAnteriorReinicio = memoria.getString("ultimaEtapa", "DESCONHECIDA");
  const esp_reset_reason_t motivoHardware = esp_reset_reason();
  panicsConsecutivos = memoria.getUChar("panicCount", 0);
  if (motivoHardware == ESP_RST_PANIC) {
    if (panicsConsecutivos < 255) panicsConsecutivos++;
    memoria.putUChar("panicCount", panicsConsecutivos);
    motivoInicializacao = "FALHA_DO_PROGRAMA_";
    motivoInicializacao += etapaAnteriorReinicio;
    if (panicsConsecutivos >= LIMITE_PANICS_CONSECUTIVOS) {
      modoSeguroPanico = true;
      motivoInicializacao = "PANIC_REPETIDO";
    }
  } else {
    // Um boot que não foi panic quebra a sequência. O contador só serve para
    // identificar falhas consecutivas durante a recuperação automática.
    panicsConsecutivos = 0;
    memoria.putUChar("panicCount", 0);
    motivoInicializacao = reinicioPlanejado.length() ? reinicioPlanejado : nomeMotivoReset(motivoHardware);
  }
  marcarEtapa(modoSeguroPanico ? "PANIC_SAFE_MODE" : "BOOT");
  ultimoPedido = memoria.getString("ultimoPedido", "");
  totalInicializacoes = memoria.getUInt("bootCount", 0) + 1;
  memoria.putUInt("bootCount", totalInicializacoes);
  char eventoBoot[90];
  snprintf(eventoBoot, sizeof(eventoBoot), "BOOT_%s", motivoInicializacao.c_str());
  registrarEvento(eventoBoot);
  configurarWatchdog();
  WiFi.mode(WIFI_STA);
  // A placa fica alimentada continuamente. Sem modem-sleep, o rádio não
  // atrasa o recebimento de comandos/keep-alives do stream do Firebase.
  WiFi.setSleep(false);
  if (!conectarWiFi()) {
    inicioWifiIndisponivel = millis();
    registrarEvento("BOOT_WIFI_PENDING");
    Serial.println("Nenhuma rede disponível; a reconexão continuará no loop.");
  } else {
    iniciarFirebaseSeNecessario();
  }
  Serial.printf("ESP32 %s preparado (%s). Motivo da inicialização: %s\n",
                DEVICE_ID, MODO_TESTE ? "TESTE" : "PRODUCAO", motivoInicializacao.c_str());
}

void loop() {
  alimentarWatchdog();
  unsigned long agora = millis();
  // O ciclo físico roda em outra tarefa, inclusive durante TLS/reconexões.
  processarEventosTrava();
  if (agora - ultimoDiagnostico >= INTERVALO_DIAGNOSTICO_MS) {
    ultimoDiagnostico = agora;
    atualizarDiagnosticoMemoria();
    verificarFragmentacaoMemoria();
  }
  if (!pedidoAtual.length() && pedidoNaFila.length() && !modoSeguroPanico &&
      tarefaControleTrava && confirmacoes.reservarCiclo()) {
    String proximo = pedidoNaFila;
    pedidoNaFila = "";
    iniciarPedido(proximo);
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (!inicioWifiIndisponivel) {
      inicioWifiIndisponivel = agora;
      registrarEvento("WIFI_DISCONNECTED");
      marcarEtapa("WIFI_DESCONECTADO");
    }
    if (streamIniciado || (baselineFeito && !recuperacaoStreamPendente)) {
      agendarRecuperacaoStream("WIFI_DESCONECTADO");
    }
    firebaseConfirmado = false;
    inicioFirebaseIndisponivel = 0;
    // Se a pilha Wi-Fi ficar presa tentando reconectar, a placa volta ao
    // estado seguro e reinicia. O motivo será enviado no próximo heartbeat.
    if (agora - inicioWifiIndisponivel >= LIMITE_WIFI_SEM_RETORNO_MS && !pedidoAtual.length()) {
      reiniciarComSeguranca("WIFI_SEM_RETORNO");
    }
    if (!conectarWiFi()) {
      delay(1000);
      return;
    }
    inicioWifiIndisponivel = 0;
  }
  iniciarFirebaseSeNecessario();
  if (!firebaseInicializado) return;
  firebase.loop();
  processarEventosTrava();
  processarEventosFirebase();
  if (firebase.ready() && !firebaseConfirmado) {
    firebaseConfirmado = true;
    Serial.println("Firebase conectado.");
    registrarEvento("FIREBASE_CONNECTED");
    marcarEtapa("FIREBASE_CONECTADO");
    if (!pedidoAtual.length()) { piscarIndicador(5); acenderIndicador(); }
  }
  if (firebase.ready()) {
    inicioFirebaseIndisponivel = 0;
  } else {
    if (!inicioFirebaseIndisponivel) {
      inicioFirebaseIndisponivel = agora;
      registrarEvento("FIREBASE_UNAVAILABLE");
      marcarEtapa("FIREBASE_INDISPONIVEL");
    }
    // Wi-Fi funcionando sem Firebase por muito tempo também pode deixar a
    // placa incapaz de receber comandos; reiniciar é a recuperação segura.
    if (agora - inicioFirebaseIndisponivel >= LIMITE_FIREBASE_SEM_RETORNO_MS && !pedidoAtual.length()) {
      reiniciarComSeguranca("FIREBASE_SEM_RETORNO");
    }
  }
  verificarSaudeStream(agora);
  if (firebase.ready() && (!baselineFeito || recuperacaoStreamPendente) && !sincronizacaoSolicitada && millis() >= proximaTentativaSincronizacao) {
    sincronizacaoSolicitada = true;
    banco.get(cliente, caminhoComandos, processarSincronizacaoInicial, false /* leitura única */, "sincronizacaoInicial");
    Serial.println(recuperacaoStreamPendente ? "Sincronizando comandos após recuperar stream." : "Sincronizando comandos iniciais.");
  }
  if (firebase.ready() && baselineFeito && !recuperacaoStreamPendente && !cancelamentoStreamPendente && !streamIniciado && millis() >= proximaTentativaStream) {
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
    marcarEtapa("STREAM_ATIVO");
    enviarHeartbeat(true);
  }
  verificarModoSeguroPanico(millis());
  processarEventosTrava();
  enviarHeartbeat();
  processarEnvios(millis());
  verificarReinicioPreventivo(millis());
  delay(1);
}
