# ESP32 — Geladeira 14 BIS

1. Copie `secrets.example.h` para `secrets.h` e preencha as credenciais.
2. No Arduino IDE, abra `esp32.ino` e selecione **ESP32 Dev Module**.
3. Antes de ligar a trava, confirme a tensão e se o relé é ativo em nível baixo.

O projeto mantém a trava no estado travado ao iniciar. Ele se conecta automaticamente à primeira rede Wi-Fi cadastrada que estiver disponível e, após autenticar no Firebase, mantém um monitoramento contínuo dos pedidos. Ao receber um pedido novo, aguarda 6 s, destrava 10 s enquanto pisca o LED azul integrado (GPIO 2), e então trava novamente. O LED vermelho da placa normalmente é apenas de alimentação e permanece aceso.

Ao ligar, o LED azul integrado pisca 3 vezes quando o Wi-Fi conecta e 5 vezes quando o Firebase fica pronto. Assim é possível testar somente com a placa ligada à energia e à rede.

O monitoramento contínuo evita consultas periódicas ao Firebase: o ESP32 é avisado quando um pedido é criado. O monitoramento só é ativado após a autenticação confirmar conexão com Firebase, garantindo que os comandos novos sejam recebidos corretamente.
