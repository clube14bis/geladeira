# Geladeira 14 Bis — instalação completa

<img width="1245" height="175" alt="Captura de Tela 2026-08-19 às 08 44 22" src="https://github.com/user-attachments/assets/054bd1cc-4a9a-409b-aa35-eddedb4d49cc" />


O fluxo é: QR Code → site → login/cadastro único → escolha da bebida → ESP32 abre a fechadura → ESP32 registra data, hora, nome e bebida no Google Sheets.

## 1. Material

Geladeira + fechadura eletroímã instalada externamente, com suporte metálico feito para a porta. Em 12–16 Vcc, consome cerca de 345 mA e tem força de tração de 150 kgf. A instalação deve ficar fora da área refrigerada e não pode impedir a vedação ou a abertura manual da porta. modelo exemplo (https://suporte.intelbras.com.br/images/9/97/Datasheet_FE_20150_01-15_site1.pdf) 
 

- ESP32 DOIT DevKit / ESP32-WROOM-32;
- fonte **12 V / 2 A** certificada para a fechadura;
- conversor buck 12 V → 5 V, de boa qualidade, para o ESP32;
- módulo relé de 1 canal com entrada compatível com **GPIO 3,3 V**, optoacoplado e contato de pelo menos 5 A / 30 Vcc;
- porta-fusível e fusível de 1 A no positivo da fechadura;
- caixa isolante, bornes e cabos adequados.

> **Segurança:** Nunca conecte 12 V a um GPIO do ESP32.

## 2. Ligações elétricas

Use o GPIO **26** como sinal do relé. O contato **NC** mantém o eletroímã energizado e a porta travada no estado normal.

```text
FONTE 12 V (+) ── fusível 1 A ── COM do relé
NC do relé ───────────────────── (+) fechadura eletroímã
FONTE 12 V (-) ──────────────── (-) fechadura eletroímã

FONTE 12 V (+/-) ── entrada do buck 12 V → 5 V
buck 5 V ─────────── pino VIN/5V do ESP32 e VCC do relé*
buck GND ─────────── GND do ESP32 e GND do relé*
GPIO 26 do ESP32 ─── IN do relé
```

\* Siga o manual do módulo relé. Alguns módulos têm `VCC`, `GND`, `IN` e outros têm `JD-VCC`; use a alimentação externa indicada pelo fabricante. Não acione a bobina do relé pelo GPIO.

Antes de ligar a fechadura, teste o programa apenas com o LED/LED de status do relé. O relé deve ser acionado por 5 segundos quando um pedido válido chegar. Se ele agir ao contrário, altere `RELAY_ACTIVE_LOW` no firmware.

## 3. Criar e configurar o Firebase

1. Acesse o [Firebase Console](https://console.firebase.google.com/) e crie um projeto no plano **Spark**.
2. Em **Project overview → Web**, registre uma aplicação web. Copie o objeto `firebaseConfig`.
3. Cole os valores em [`firebase-config.js`](firebase-config.js). A `apiKey` web não é segredo; as regras do banco são a proteção real.
4. Em **Authentication → Sign-in method**, habilite **Email/Password**. A página continua mostrando somente “Usuário”; internamente ela gera um identificador técnico, como `adauto@usuarios.clube14bis.com`.
5. Em **Authentication → Settings → Authorized domains**, adicione `clube14bis.github.io`.
6. Em **Realtime Database**, crie o banco em **Locked mode**. Copie o conteúdo de [`firebase-rules.json`](firebase-rules.json) para a aba **Rules**, mas ainda não publique.
7. Em **Authentication → Users**, crie o usuário do dispositivo:
   - e-mail: `geladeira-esp32@usuarios.clube14bis.com`;
   - senha: longa, exclusiva e diferente das senhas dos clientes.
8. Copie o **UID** desse usuário e substitua `COLE_O_UID_DO_ESP32_AQUI` em **todas** as ocorrências de `firebase-rules.json`. Agora publique as regras.
9. Em **Project settings → General**, copie a `apiKey` e a `databaseURL`. A URL tem o formato `https://SEU-PROJETO-default-rtdb.firebaseio.com`.

As regras permitem que cada pessoa leia somente seu próprio pedido. O ESP32 tem uma conta exclusiva para ler pedidos pendentes, abrir a fechadura e apagar o pedido depois do registro.

## 4. Configurar a página do GitHub Pages

1. Edite [`firebase-config.js`](firebase-config.js) com o objeto do seu projeto Firebase.

2. pagina modelo de login: https://clube14bis.github.io/geladeira/


## 5. Planilha e o Apps Script

1. Crie uma planilha Google, renomeie a primeira aba para `Pedidos` (opcional) e coloque a primeira linha assim:

   `Data | Hora | Nome | Bebida | Pedido ID` | Pago |

2. O quinto campo evita registros duplicados. Você pode ocultar a coluna **Pedido ID** depois.
3. Copie o ID da planilha: é o trecho entre `/d/` e `/edit` na URL.
4. Na planilha, abra **Extensões → Apps Script** e substitua o conteúdo por [`google-apps-script/Code.gs`](google-apps-script/Code.gs).
5. Em **Project Settings → Script properties**, crie:
   - `SPREADSHEET_ID`: ID copiado da planilha;
   - `DEVICE_SECRET`: uma senha aleatória com 32 ou mais caracteres.
6. Em **Project Settings**, defina o fuso horário como `America/Sao_Paulo`.
7. Clique em **Deploy → New deployment → Web app**. Selecione **Execute as: Me** e acesso para **Anyone**. Autorize o script e copie a URL terminada em `/exec`.

O endpoint é público para o ESP32 poder alcançá-lo, mas só aceita registros com o `DEVICE_SECRET`. Não coloque esse segredo no site.

Adicione aba na planilha com nome da bebidas e o valor delas. (para ser total transparência publique a planilha online e compartilhe o link dela para visualização) 

## 6. Gravar o ESP32

1. Instale o [Arduino IDE 2](https://www.arduino.cc/en/software/).
2. Em **Preferences → Additional boards manager URLs**, adicione:

   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`

3. Em **Boards Manager**, instale **esp32 by Espressif Systems**. Selecione **DOIT ESP32 DEVKIT V1** em **Tools → Board**.
4. Em **Library Manager**, instale **ArduinoJson** (versão 7 ou superior).
5. Crie uma pasta de sketch chamada `Geladeira14Bis`. Copie para ela [`esp32/Geladeira14Bis.ino`](esp32/Geladeira14Bis.ino).
6. Na mesma pasta, copie [`esp32/secrets.example.h`](esp32/secrets.example.h), renomeie para `secrets.h` e preencha:
   - Wi-Fi da geladeira;
   - `FIREBASE_API_KEY` e `FIREBASE_DB_HOST`;
   - e-mail/senha do usuário técnico do ESP32;
   - URL `/exec` do Apps Script;
   - o mesmo `DEVICE_SECRET` das Script properties.
7. Ligue o ESP32 por USB. Selecione a porta em **Tools → Port**, clique em **Upload**. Se ficar em “Connecting…”, mantenha o botão **BOOT** pressionado até iniciar o envio.
8. Abra o **Serial Monitor** em 115200 baud. Ele deve mostrar o IP do ESP32 e não deve exibir erros HTTP.

O firmware usa HTTPS com certificado raiz do Google; não use `setInsecure()` em um dispositivo que destrava uma fechadura.

## 7. Teste obrigatório antes da instalação final

1. Deixe a fechadura desconectada e ligue apenas o módulo relé.
2. Crie um usuário no site, entre, escolha uma bebida e clique em **Concluir pedido**.
3. Confirme no Serial Monitor que aparece `Abrindo para ...`.
4. Confirme que o relé muda de estado por 5 segundos.
5. Confirme a nova linha no Google Sheets.
6. Só então conecte a fechadura e teste com a porta aberta.
7. Teste queda/reinício do ESP32: com o relé usando **NC**, a fechadura deve voltar ao estado travado quando a fonte 12 V estiver presente.

Se surgir `log_failed` no Firebase, a fechadura abriu mas o Apps Script não confirmou o registro. Revise a URL `/exec`, o segredo e a autorização da implantação antes de usar o sistema com clientes.
