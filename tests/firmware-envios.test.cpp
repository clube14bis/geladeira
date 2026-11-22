// A rotina de produção é incluída pelo runner, com rede controlada em memória.
#include "../esp32/ControleTrava.h"
#include <assert.h>
#include <stdio.h>
#include <string>
#include <vector>
using String = std::string;
using object_t = std::string;
struct AsyncResult {
  String tarefa;
  bool erro = false;
  String uid() { return tarefa; }
  bool isResult() { return true; }
  bool isError() { return erro; }
  int available() { return erro ? 0 : 4; }
};
using Callback = void (*)(AsyncResult &);
struct Cliente {
  unsigned tarefas = 0, cancelamentos = 0;
  unsigned taskCount() { return tarefas; }
  void stopAsync(const String &) { ++cancelamentos; tarefas = 0; }
} cliente;
struct {
  bool conectado = true;
  bool ready() { return conectado; }
} firebase;
constexpr int WL_CONNECTED = 3;
struct {
  bool conectado = true;
  int status() { return conectado ? WL_CONNECTED : 0; }
  String SSID() { return "Wi-Fi \"clube\"\\teste\n"; }
  int RSSI() { return -53; }
} WiFi;
struct Banco {
  std::vector<String> pacotes;
  String uid;
  Callback callback = nullptr;
  template <typename T> void update(Cliente &c, const String &path, const T &dados,
                                    Callback cb, const String &tarefa) {
    assert(path == "/");
    assert(c.tarefas == 0);
    pacotes.push_back(dados);
    callback = cb;
    uid = tarefa;
    c.tarefas = 1;
  }
  void terminar(bool erro = false) {
    AsyncResult r;
    r.tarefa = uid;
    r.erro = erro;
    cliente.tarefas = 0;
    callback(r);
  }
} banco;

#define DEVICE_ID "geladeira"
unsigned long relogio = 0;
unsigned long millis() { return relogio; }
void registrarEvento(const char *) {}
const char *estadoDispositivo() { return "locked"; }
constexpr unsigned long INTERVALO_HEARTBEAT_MS = 300000;
constexpr unsigned long INTERVALO_REPETICAO_ENVIO_MS = 5000;
constexpr unsigned long LIMITE_ENVIO_MS = 15000;
constexpr char VERSAO_FIRMWARE[] = "2.8.3";
FilaConfirmacoesTrava confirmacoes;
bool statusPendente = false, envioEmAndamento = false, envioTemConfirmacao = false;
bool envioIncluiReinicio = false, limpezaReinicioPendente = false;
bool baselineFeito = true, streamIniciado = true, reinicioRemotoPendente = false;
uint8_t resultadoEnvio = 0;
uint32_t sequenciaEnvio = 0;
unsigned long inicioEnvio = 0, proximoEnvio = 0, ultimoHeartbeat = 0;
String motivoInicializacao = "ENERGIA";
char uidEnvio[24] = {}, bufferHeartbeat[768] = {}, bufferEnvio[1152] = {};

#include "firmware-envios.inc"

int main() {
  // Um evento gera um PATCH, reunindo pedido, status e exclusão do comando.
  assert(confirmacoes.adicionar("pedido-1", true, false));
  processarEnvios(0);
  assert(envioEmAndamento && banco.pacotes.size() == 1);
  assert(banco.pacotes.back().find("orders/pedido-1/execution/state\":\"opened") != String::npos);
  assert(banco.pacotes.back().find("commands/geladeira/pedido-1\":null") != String::npos);
  puts(banco.pacotes.back().c_str());
  processarEnvios(100);
  assert(banco.pacotes.size() == 1); // Não acumula tarefas enquanto espera.
  banco.terminar();
  processarEnvios(101);
  assert(!envioEmAndamento && confirmacoes.vazia());

  relogio = 299999;
  enviarHeartbeat();
  processarEnvios(relogio);
  assert(banco.pacotes.size() == 1);
  relogio = 300000;
  enviarHeartbeat();
  processarEnvios(relogio);
  assert(banco.pacotes.size() == 2);
  banco.terminar();
  processarEnvios(300001);

  // Uma requisição que não retorna tem prazo; callbacks antigos são ignorados.
  assert(confirmacoes.adicionar("pedido-2", true, false));
  processarEnvios(300002);
  const String uidAntigo = uidEnvio;
  assert(confirmacoes.adicionar("pedido-2", false, true));
  processarEnvios(315002);
  assert(cliente.cancelamentos == 1 && !envioEmAndamento);
  assert(confirmacoes.tamanho() == 1 && !confirmacoes.primeira().abriu);
  processarEnvios(320002);
  assert(envioEmAndamento && banco.pacotes.back().find("\"locked\"") != String::npos);
  AsyncResult antigo;
  antigo.tarefa = uidAntigo;
  processarResultadoEnvio(antigo);
  assert(resultadoEnvio == 0);
  banco.terminar();
  processarEnvios(320003);
  assert(confirmacoes.vazia());

  // Falha persistente: no máximo três tentativas por confirmação, sem crescer.
  assert(confirmacoes.adicionar("pedido-3", false, false));
  for (unsigned tentativa = 0; tentativa < 3; ++tentativa) {
    unsigned long t = 330000 + tentativa * 6000;
    processarEnvios(t);
    assert(envioEmAndamento);
    banco.terminar(true);
    processarEnvios(t + 1);
  }
  assert(confirmacoes.vazia() && statusPendente);

  // Consumir um reinício exige confirmação, antes de autorizar o reset.
  limpezaReinicioPendente = reinicioRemotoPendente = true;
  processarEnvios(350000);
  assert(limpezaReinicioPendente && envioIncluiReinicio);
  assert(banco.pacotes.back().find("commands/geladeira/system/restart\":null") != String::npos);
  banco.terminar();
  processarEnvios(350001);
  assert(!limpezaReinicioPendente);

  // A falha de um heartbeat simples não vira tráfego a cada cinco segundos.
  statusPendente = true;
  processarEnvios(360000);
  banco.terminar(true);
  processarEnvios(360001);
  const size_t enviosAposFalha = banco.pacotes.size();
  assert(!statusPendente);
  relogio = 365001;
  enviarHeartbeat();
  processarEnvios(relogio);
  assert(banco.pacotes.size() == enviosAposFalha);
  // Um novo evento tem prioridade sobre o intervalo de status em repouso.
  assert(confirmacoes.adicionar("pedido-4", true, false));
  processarEnvios(relogio);
  assert(banco.pacotes.size() == enviosAposFalha + 1);
}
