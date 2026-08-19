# Clube 14 Bis — Geladeira Inteligente

Interface da geladeira inteligente do Clube 14 Bis.

## Arquivos principais

- `index.html` — interface do usuário.
- `app.js` — login, cadastro, bebidas, carrinho, Firebase e clima.
- `firebase-config.js` — configuração privada do Firebase.
- `google-apps-script/Code.gs` — registro dos pedidos no Google Sheets.
- `esp32/Geladeira14Bis.ino` — firmware do ESP32.

## Configuração

Não publique credenciais privadas no GitHub.

Mantenha `firebase-config.js` e `esp32/secrets.h` fora do repositório público ou use variáveis/segredos apropriados.

O site usa Open-Meteo para exibir as condições atuais de Mirassol.

## Fluxo

Login → bebidas → carrinho → pedido Firebase → ESP32 → fechadura → Google Sheets.
