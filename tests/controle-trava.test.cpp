#include "../esp32/ControleTrava.h"
#include <assert.h>
#include <stdio.h>

int main() {
  // Uma abertura só começa depois de 6 s e termina 20 s após abrir.
  TemporizadorTrava trava;
  assert(trava.iniciar(100));
  assert(!trava.iniciar(200));
  assert(trava.atualizar(6099) == TemporizadorTrava::NENHUM);
  assert(trava.atualizar(6100) == TemporizadorTrava::ABRIU);
  assert(trava.aberta());
  assert(trava.atualizar(26099) == TemporizadorTrava::NENHUM);
  assert(trava.atualizar(26100) == TemporizadorTrava::TRANCOU);
  assert(!trava.aberta());
  assert(trava.atualizar(30000) == TemporizadorTrava::NENHUM);
  assert(trava.iniciar(30100));

  // millis() transborda depois de ~49 dias, inclusive no meio de um ciclo.
  TemporizadorTrava rollover;
  const uint32_t inicio = UINT32_MAX - 3000;
  assert(rollover.iniciar(inicio));
  assert(rollover.atualizar(uint32_t(inicio + 6000)) == TemporizadorTrava::ABRIU);
  assert(rollover.atualizar(uint32_t(inicio + 25999)) == TemporizadorTrava::NENHUM);
  assert(rollover.atualizar(uint32_t(inicio + 26000)) == TemporizadorTrava::TRANCOU);

  // O processamento de rede para por todo um ciclo. A tarefa física continua
  // recebendo tempo e o fechamento substitui o aviso de abertura não enviado.
  TemporizadorTrava offline;
  FilaConfirmacoesTrava pendentes;
  assert(offline.iniciar(0));
  unsigned aberturas = 0, fechamentos = 0;
  for (uint32_t agora = 0; agora <= 30000; agora += 10) {
    auto evento = offline.atualizar(agora);
    if (evento == TemporizadorTrava::ABRIU) {
      ++aberturas;
      assert(pendentes.adicionar("pedido-1", true, false));
    } else if (evento == TemporizadorTrava::TRANCOU) {
      ++fechamentos;
      assert(agora == 26000);
      assert(pendentes.adicionar("pedido-1", false, false));
    }
  }
  assert(aberturas == 1 && fechamentos == 1 && !offline.aberta());
  assert(pendentes.tamanho() == 1 && !pendentes.primeira().abriu);
  assert(pendentes.adicionar("pedido-1", true, false));
  assert(!pendentes.primeira().abriu);

  // Uma abertura em trânsito não é alterada; o fechamento segue atrás dela.
  FilaConfirmacoesTrava transito;
  assert(transito.adicionar("pedido-1", true, false));
  transito.primeira().tentativas = 1;
  assert(transito.adicionar("pedido-1", false, true));
  assert(transito.tamanho() == 2 && transito.primeira().abriu);
  assert(transito.primeira().tentativas == 1);
  assert(transito.primeiraAberturaSuperada());
  transito.removerPrimeira();
  assert(!transito.primeira().abriu);

  // Sem rede não pode haver crescimento ilimitado nem perda de um fechamento
  // por falta de espaço após aceitar mais um atendimento.
  FilaConfirmacoesTrava cheia;
  assert(cheia.reservarCiclo());
  assert(cheia.adicionar("pedido-1", false, false));
  assert(cheia.adicionar("pedido-2", false, false));
  assert(cheia.reservarCiclo());
  assert(cheia.adicionar("pedido-3", true, false));
  assert(!cheia.reservarCiclo());
  assert(cheia.adicionar("pedido-3", false, true));
  assert(cheia.tamanho() == 3 && !cheia.reservarCiclo());
  assert(cheia.adicionar("pedido-4", false, true));
  assert(cheia.tamanho() == 4);
  assert(!cheia.adicionar("pedido-5", true, true));
  assert(!cheia.adicionar("", true, false));
  cheia.removerPrimeira();
  cheia.removerPrimeira();
  assert(cheia.reservarCiclo());
  puts("controle-trava: tempos, rollover, rede indisponível e fila limitada OK");
}
