# ESP32 — Geladeira 14 BIS

1. Copie `secrets.example.h` para `secrets.h` e preencha as credenciais.
2. No Arduino IDE, abra `esp32.ino` e selecione **ESP32 Dev Module**.
3. Antes de ligar a trava, confirme a tensão e se o relé é ativo em nível baixo.

O projeto mantém a trava no estado travado ao iniciar. Ele se conecta automaticamente à primeira rede Wi-Fi cadastrada que estiver disponível e, após autenticar no Firebase, mantém um monitoramento contínuo dos pedidos. Ao receber um pedido novo, aguarda 6 s, destrava 10 s enquanto pisca o LED azul integrado (GPIO 2), e então trava novamente. O LED vermelho da placa normalmente é apenas de alimentação e permanece aceso.

Ao ligar, o LED azul integrado pisca 3 vezes quando o Wi-Fi conecta e 5 vezes quando o Firebase fica pronto. Assim é possível testar somente com a placa ligada à energia e à rede.

O monitoramento contínuo evita consultas periódicas ao Firebase: o ESP32 é avisado quando um pedido é criado. O monitoramento só é ativado após a autenticação confirmar conexão com Firebase, garantindo que os comandos novos sejam recebidos corretamente.

## Confiabilidade e diagnóstico (firmware 2.3.0)

- O stream de pedidos do Firebase é verificado a cada 5 segundos. Se não houver evento ou `keep-alive` por 2 minutos, o ESP32 encerra a conexão antiga, consulta somente o pedido mais recente e abre um novo stream. Isso evita baixar todo o histórico de pedidos para a RAM.
- Após uma queda de Wi-Fi, ele executa a mesma conferência antes de voltar a monitorar.
- Há um watchdog de 60 segundos para recuperar travamentos completos do programa.
- A cada 5 horas de funcionamento, é feito um reinício preventivo **somente** quando a geladeira estiver trancada e sem pedido em andamento. Antes de reiniciar, a saída do relé é colocada no estado `RELE_TRAVADO`.
- O painel administrativo exibe memória livre e mínima, memória desde que o stream ficou pronto, maior bloco alocável atual e mínimo, além da memória antes/depois de recuperar o stream. A diferença entre RAM livre e maior bloco indica se há fragmentação.
- Cada placa usa `DEVICE_ID` no `secrets.h`. A placa da geladeira usa `"geladeira"`; a placa de laboratório usa `"teste"` e `MODO_TESTE true`. Nesse modo, ela acompanha Wi-Fi/Firebase/stream e envia diagnóstico, mas ignora pedidos e nunca aciona o relé.

> Antes de ligar a fechadura de verdade, confirme no módulo relé que `RELE_TRAVADO = HIGH` corresponde ao estado fisicamente trancado. Essa confirmação garante que qualquer reinício mantenha a fechadura segura.
