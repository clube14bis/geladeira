#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Não depende de Wi-Fi, Firebase, Strings ou memória dinâmica.
class TemporizadorTrava {
 public:
  static constexpr uint32_t ESPERA_MS = 6000;
  static constexpr uint32_t ABERTURA_MS = 20000;
  enum Evento : uint8_t { NENHUM, ABRIU, TRANCOU };

  bool iniciar(uint32_t agora) {
    if (fase != PARADA) return false;
    fase = ESPERANDO;
    desde = agora;
    return true;
  }

  Evento atualizar(uint32_t agora) {
    if (fase == ESPERANDO && uint32_t(agora - desde) >= ESPERA_MS) {
      fase = ABERTA;
      desde = agora;
      return ABRIU;
    }
    if (fase == ABERTA && uint32_t(agora - desde) >= ABERTURA_MS) {
      fase = PARADA;
      return TRANCOU;
    }
    return NENHUM;
  }

  bool aberta() const { return fase == ABERTA; }

 private:
  enum Fase : uint8_t { PARADA, ESPERANDO, ABERTA };
  Fase fase = PARADA;
  uint32_t desde = 0;
};

struct ConfirmacaoTrava {
  char pedido[101] = {};
  bool abriu = false;
  uint8_t tentativas = 0;
};

// Apenas o loop de rede acessa esta fila. A cabeça fica imutável em trânsito.
// Um fechamento substitui uma abertura ainda não enviada do mesmo pedido.
class FilaConfirmacoesTrava {
 public:
  static constexpr size_t CAPACIDADE = 4;

  bool reservarCiclo() const { return quantidade <= CAPACIDADE - 2; }
  bool vazia() const { return quantidade == 0; }
  size_t tamanho() const { return quantidade; }
  ConfirmacaoTrava &primeira() { return itens[0]; }

  bool adicionar(const char *pedido, bool abriu, bool primeiraEmTransito) {
    if (!pedido || !pedido[0] || strlen(pedido) > 100) return false;
    for (size_t i = primeiraEmTransito ? 1 : 0; i < quantidade; ++i) {
      if (strcmp(itens[i].pedido, pedido) == 0) {
        // Não ressuscitar uma abertura se o fechamento já foi registrado.
        itens[i].abriu = itens[i].abriu && abriu;
        itens[i].tentativas = 0;
        return true;
      }
    }
    if (quantidade == CAPACIDADE) return false;
    ConfirmacaoTrava &item = itens[quantidade++];
    strcpy(item.pedido, pedido);
    item.abriu = abriu;
    item.tentativas = 0;
    return true;
  }

  bool primeiraAberturaSuperada() const {
    if (!quantidade || !itens[0].abriu) return false;
    for (size_t i = 1; i < quantidade; ++i) {
      if (!itens[i].abriu && strcmp(itens[0].pedido, itens[i].pedido) == 0) return true;
    }
    return false;
  }

  void removerPrimeira() {
    if (!quantidade) return;
    for (size_t i = 1; i < quantidade; ++i) itens[i - 1] = itens[i];
    --quantidade;
  }

 private:
  ConfirmacaoTrava itens[CAPACIDADE];
  size_t quantidade = 0;
};
