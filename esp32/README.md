# ESP32 — Geladeira 14 BIS

1. Copie `secrets.example.h` para `secrets.h` e preencha as credenciais.
2. No Arduino IDE, abra `geladeira_esp32.ino` e selecione **ESP32 Dev Module**.
3. Antes de ligar a trava, confirme a tensão e se o relé é ativo em nível baixo.

O projeto mantém a trava no estado travado ao iniciar. Ao receber um novo pedido, aguarda 6 s, destrava 10 s enquanto alterna os LEDs azul (GPIO 2) e vermelho (GPIO 4), e então trava novamente.
