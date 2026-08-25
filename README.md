# Geladeira Clube 14 BIS

<img width="1280" height="853" alt="Geladeira Clube 14 BIS" src="https://github.com/user-attachments/assets/02b59fa2-7ec7-47b4-98f5-437ef00a6a8e" />

Sistema de autosserviço para a geladeira do Clube 14 BIS. Pelo QR Code, o cliente abre o site, entra com nome de usuário e senha, escolhe os produtos e confirma o carrinho. O pedido é registrado no Firebase e no Google Sheets; o ESP32 recebe o comando e controla a fechadura eletromagnética.

## Componentes do sistema

| Parte | Responsabilidade |
| --- | --- |
| GitHub Pages | Hospeda o site público e o painel administrativo. |
| Firebase Authentication | Cadastro, login, sessão e recuperação de senha por e-mail. |
| Firebase Realtime Database | Perfis, catálogo, estoque, pedidos, histórico e permissões. |
| Cloudflare Worker | Permite login por nome de usuário, localizando o e-mail da conta no Firebase. |
| Google Apps Script + Sheets | Gera o relatório de retiradas. |
| ESP32 | Recebe pedido novo e controla o relé/trava. |

## Funções implementadas

### Site do cliente

- Login por nome de usuário e senha, com botão para mostrar/ocultar senha.
- Cadastro com nome completo, usuário, celular, CPF, e-mail e senha.
- Recuperação de senha por link enviado ao e-mail cadastrado.
- Catálogo por categorias, com foto, preço, estoque e botão de adicionar.
- Carrinho com quantidades, remoção, total em reais e Pix copia-e-cola.
- Tela verde: 6 segundos de espera, 10 segundos de abertura e aviso de fechamento.
- Histórico individual dos últimos três meses em formato de calendário, com consumo e total por dia.
- Registro público de uso por planilha publicada.

### Painel administrativo

- Acesso somente a usuários marcados como administradores no Firebase.
- Criar produto com nome, categoria, imagem por URL, preço e quantidade inicial.
- Alterar preço, estoque, imagem, categoria e visibilidade.
- Remover produto incorreto.
- Criar categorias e remover categorias vazias.
- Mudar a ordem de categorias e produtos; o site público reflete a ordem depois de salvar.
- Estoque informativo: diminui a cada pedido, mas produto com estoque zero continua selecionável por decisão do projeto.

## Fluxo de um pedido

1. O cliente faz login, seleciona produtos e confirma o carrinho.
2. O site envia uma linha para a planilha com cliente, itens e total.
3. O site cria pedido pendente, histórico individual e atualiza o estoque informativo no Firebase.
4. O ESP32 mantém um stream contínuo de pedidos e recebe a alteração imediatamente.
5. Depois de 6 segundos, o ESP32 aciona o relé por 10 segundos e pisca o LED azul.
6. O relé interrompe a alimentação da fechadura eletromagnética fail-safe, liberando a porta.
7. Ao término dos 10 segundos, a trava volta ao estado normal e o ESP32 grava os estados opened e locked.

### Comunicação otimizada do ESP32

O ESP32 mantém um stream contínuo para a área de pedidos do Firebase. Antes ele precisava consultar o banco repetidamente; agora recebe uma notificação assim que surge pedido novo. Isso reduz consumo de consultas, diminui atrasos e torna o comando do relé/LED mais confiável.

O stream só é iniciado após a autenticação do Firebase ser concluída. No boot, a placa sincroniza pedidos antigos e não abre a porta por histórico. Para testar, faça sempre pedido novo depois que o ESP32 estiver pronto.

## Endereços e arquivos

| Item | Endereço / arquivo |
| --- | --- |
| Site | [clube14bis.github.io/geladeira](https://clube14bis.github.io/geladeira/) |
| Painel | [admin.html](https://clube14bis.github.io/geladeira/admin.html) |
| Repositório | [github.com/clube14bis/geladeira](https://github.com/clube14bis/geladeira) |
| Firmware | [esp32/esp32.ino](esp32/esp32.ino) |
| Credenciais-modelo | [esp32/secrets.example.h](esp32/secrets.example.h) |
| Regras do banco | [firebase-rules.json](firebase-rules.json) |
| Apps Script | [google-apps-script/Code.gs](google-apps-script/Code.gs) |

## Uso pelo cliente

1. Escaneie o QR Code e abra o site.
2. Entre com nome de usuário e senha. Caso seja o primeiro acesso, escolha Criar cadastro.
3. No cadastro, informe nome, usuário, celular com DDD, CPF válido, e-mail e senha com pelo menos 6 caracteres.
4. Escolha produtos pelo botão mais. O botão Ver carrinho aparece após a primeira seleção.
5. No carrinho, confira itens, ajuste quantidades, remova o que não quiser e confirme.
6. A tela verde mostra total e botão Copiar chave Pix.
7. Após 6 segundos aparece Geladeira aberta com contador de 10 segundos. Retire os itens e feche a porta.
8. Em Histórico, o usuário vê seus pedidos dos últimos três meses e pode copiar o Pix novamente.

Em Esqueci minha senha, o usuário informa o e-mail do cadastro e recebe link de redefinição pelo Firebase. Não existe recuperação por CPF, porque CPF sozinho não é fator seguro de confirmação.

## Painel administrativo

Abra [admin.html](https://clube14bis.github.io/geladeira/admin.html) e entre com uma conta marcada como administradora no Firebase. O painel usa o mesmo nome de usuário e senha do site.

### Alterar catálogo

1. Marque Exibir para deixar produto visível no site.
2. Selecione a categoria.
3. Informe preço em reais, por exemplo 5,50.
4. Informe estoque inteiro igual ou superior a zero.
5. Use as setas para reordenar produtos.
6. Use o botão x para remover produto.
7. Clique em Salvar alterações. Somente esse passo grava mudanças no Firebase.

### Criar produto e categoria

1. Informe nome, categoria, URL pública direta da imagem, preço e estoque.
2. Clique em Adicionar produto.
3. Clique em Salvar alterações.

As imagens usam moldura quadrada e preservam proporção, mantendo o padrão visual. Categorias só podem ser removidas quando estiverem vazias; mova ou exclua os produtos antes.

### Estoque

Ao confirmar pedido, o site executa transação no Firebase e reduz o estoque sem permitir valor negativo. O contador vermelho no produto mostra a quantidade registrada. Como é controle informativo, chegar a zero não bloqueia a adição nem a confirmação do produto.

## Firebase e Cloudflare

### Configuração inicial do Firebase

1. Crie ou abra o projeto geladeira-14-bis.
2. Em Authentication, habilite E-mail/Senha.
3. Em Authentication Templates, configure em português o e-mail de recuperação.
4. Crie o Realtime Database.
5. Em Realtime Database Rules, publique o conteúdo de [firebase-rules.json](firebase-rules.json).
6. Em Project settings General, crie o aplicativo Web e complete firebase-config.js com a configuração pública gerada.

### Estrutura do banco

    users/<uid>                 perfil do cliente
    usernames/<nome>            índice do nome de usuário
    catalog/<id>                produto, foto, preço, estoque e exibição
    catalogConfig/categoryOrder ordem das categorias
    orders/<id>                 pedido recebido pelo ESP32
    userOrders/<uid>/<id>       histórico individual
    admins/<uid>                permissão do painel

Firebase Authentication usa e-mail internamente. O Cloudflare Worker converte o nome de usuário digitado para o e-mail associado, permitindo que o cliente use somente nome de usuário no login. Não publique tokens, segredos do Worker, senhas ou chaves administrativas.

Nunca substitua as regras por leitura ou escrita globalmente abertas.

## Google Sheets e Apps Script

### Formato da planilha

| Linha | Conteúdo |
| --- | --- |
| 1 | Título Geladeira 14 BIS |
| 2 | Data, Hora, Nome do Cliente, Bebida, Valor |
| 3 | Pedido mais recente |
| 4 em diante | Pedidos anteriores |

Cada pedido entra na linha 3. O script copia a formatação da linha seguinte quando há uma linha-modelo, então cores, fontes, larguras e alinhamento que o administrador ajustar no Sheets permanecem em registros futuros.

### Configurar Apps Script

1. Na planilha, abra Extensões e Apps Script.
2. Copie [google-apps-script/Code.gs](google-apps-script/Code.gs).
3. Nas Propriedades do script, configure SPREADSHEET_ID e FIREBASE_API_KEY. DEVICE_SECRET só é necessário para envio direto por dispositivo.
4. Execute autorizarIntegracaoFirebase uma vez e aceite as permissões.
5. Execute configurarPlanilha apenas para criar ou reorganizar a estrutura inicial.
6. Em Implantar, crie Aplicativo da web executando como o proprietário.
7. Copie a URL terminada em /exec para sheetsEndpoint em firebase-config.js.

O Apps Script valida token Firebase, evita fórmulas maliciosas na planilha e grava valores em reais.

## ESP32: instalação do firmware

### Comportamento

1. Conecta à primeira rede Wi-Fi cadastrada que estiver disponível.
2. Autentica no Firebase.
3. Abre o stream de pedidos.
4. Ao receber pedido novo, espera 6 segundos.
5. Destrava durante 10 segundos.
6. Pisca LED azul integrado durante a abertura.
7. Trava novamente e registra os estados no Firebase.

| Evento | LED azul no GPIO 2 |
| --- | --- |
| Wi-Fi conectado | 3 piscas |
| Firebase conectado | 5 piscas |
| Porta liberada | Pisca continuamente por 10 segundos |
| Porta trancada | Apaga |

O firmware procura automaticamente as redes cadastradas do clube, casa, extensão, fórum e Secretaria. Senhas ficam somente em esp32/secrets.h, que não deve ser publicado.

### Arduino IDE

1. Instale [Arduino IDE 2](https://www.arduino.cc/en/software/).
2. Em Preferences ou Settings, adicione a URL abaixo nas URLs adicionais do Gerenciador de placas:

    https://espressif.github.io/arduino-esp32/package_esp32_index.json

3. Em Boards Manager, instale esp32 by Espressif Systems.
4. Em Library Manager, instale FirebaseClient e ArduinoJson.
5. Abra [esp32/esp32.ino](esp32/esp32.ino).
6. Copie esp32/secrets.example.h para esp32/secrets.h e preencha Wi-Fi e credenciais técnicas Firebase.
7. Selecione Tools, Board, ESP32 Arduino e ESP32 Dev Module.
8. Conecte cabo USB de dados, escolha a porta e clique Upload.
9. Se aparecer Connecting, mantenha BOOT pressionado até a gravação começar.
10. Abra o Serial Monitor em 115200 baud.

Mensagens esperadas:

    Wi-Fi conectado: <ip>
    ESP32 preparado.
    Firebase conectado.
    Monitoramento de pedidos ativado.

O LED vermelho de algumas placas é apenas de alimentação. Se a placa não possuir LED azul programável no GPIO 2, use LED externo com resistor de 220 a 330 ohms entre GPIO 2 e GND.

## Relé e fechadura eletromagnética

> Segurança: nunca conecte rede 127/220 V ao ESP32. Use fonte AC/DC certificada, desligue tudo antes de mexer em fios e peça apoio de técnico/eletricista se não tiver experiência.

### Peças necessárias

| Peça | Especificação recomendada | Função |
| --- | --- | --- |
| ESP32 | ESP32 Dev Module ou DevKit | Wi-Fi e lógica de controle. |
| Relé | 1 canal, bobina 5 V, entrada compatível com 3,3 V, COM/NC/NO | Comuta a alimentação da trava. |
| Fechadura | Eletroímã 12 Vcc fail-safe, com força e suporte compatíveis com porta | Trava enquanto recebe energia. |
| Fonte da trava | 12 Vcc certificada, corrente igual ou maior que a exigida | Alimenta a fechadura. |
| Conversor buck | LM2596 ou equivalente, 12 V para 5 V, mínimo 2 A | Alimenta ESP32 e relé. |
| Fusível | Porta-fusível e fusível conforme corrente da trava | Protege a linha de 12 V. |
| Instalação | Bornes, caixa isolante, prensa-cabos e cabo 0,5 a 0,75 mm² | Segurança e organização. |

### Conceitos

- COM: contato comum do relé.
- NC: normalmente fechado; ligado ao COM quando relé está inativo.
- NO: normalmente aberto; ligado ao COM quando relé está acionado.
- Trava fail-safe: energizada, fica travada; sem energia, libera.
- O projeto usa NC: em repouso a trava recebe 12 V e fica fechada. Ao acionar relé, 12 V é interrompido e a porta abre.

### Conexões de controle em 5 V

    ESP32 GPIO 26 ───── IN do módulo relé
    ESP32 GND ───────── GND do módulo relé
    Buck OUT+ 5 V ───── VCC do módulo relé

    Fonte 12 V + ────── entrada + do buck LM2596
    Fonte 12 V - ────── entrada - do buck LM2596
    Buck OUT+ 5 V ───── pino 5V/VIN do ESP32
    Buck OUT- GND ───── GND do ESP32

O GND do ESP32 e do módulo relé deve ser comum quando o relé usar entrada convencional. Escolha módulo cujo pino IN reconheça 3,3 V; alguns módulos de 5 V exigem conversor de nível ou transistor.

### Conexões de potência da trava em 12 V

    Fonte 12 V + ── fusível ── COM do relé
                                  │
                                  └── NC do relé ─── + da trava eletromagnética

    Fonte 12 V - ─────────────────────────────────── - da trava eletromagnética

Deixe NO sem uso neste esquema. O GPIO 26 vai somente para IN do módulo relé; os 12 V nunca devem ir a pinos do ESP32.

### Estados usados pelo firmware

O firmware assume módulo de relé ativo em nível baixo:

| Situação | GPIO 26 | Relé | Trava |
| --- | ---: | --- | --- |
| Normal/trancada | HIGH | Inativo | Recebe 12 V pelo NC |
| Abertura | LOW | Ativo | NC abre, corta 12 V e libera |
| Depois de 10 segundos | HIGH | Inativo | NC volta e trava |

Antes de conectar fechadura, teste COM/NC com multímetro. Se o módulo tiver lógica invertida, ajuste RELE_TRAVADO e RELE_DESTRAVADO em esp32/esp32.ino.

### Ordem segura de instalação

1. Grave firmware e teste somente ESP32: 3 piscas de Wi-Fi e 5 de Firebase.
2. Conecte buck e relé, mas não conecte a fechadura.
3. Faça pedido novo e confirme que relé muda de estado por 10 segundos.
4. Com multímetro, confirme continuidade COM/NC normalmente e interrupção somente na abertura.
5. Desligue tudo; instale fonte 12 V, fusível e fechadura conforme o diagrama.
6. Teste primeiro com porta aberta.
7. Coloque fonte, buck, relé e bornes em caixa isolante, longe de umidade e partes móveis.

### Nunca faça

- Não ligue 12 V da trava em GPIO, 3V3, 5V ou GND do ESP32.
- Não alimente fechadura pelo USB do computador.
- Não instale sem fusível e sem conferir corrente nominal.
- Não deixe conexões expostas dentro da geladeira.
- Não use sistema sem rota de abertura de emergência.

## Diagnóstico

| Sintoma | Verificação |
| --- | --- |
| Não pisca 3 vezes | Rede/senha incorreta ou rede não é 2,4 GHz. |
| Pisca 3, mas não 5 | Firebase não autenticou; confira credenciais e regras. |
| Não há Monitoramento de pedidos ativado | Reinicie e confira Serial Monitor. |
| Site confirma, LED não pisca | Faça pedido novo depois do ESP32 pronto; confira Wi-Fi e monitor serial. |
| Relé invertido | Teste COM/NC e ajuste níveis no firmware. |
| Porta não abre | Meça os 12 V na trava durante os 10 segundos. |
| Pedido não entra no Sheets | Confira URL /exec, propriedades e autorização do Apps Script. |
| Admin não salva catálogo | Confira permissão administrativa e regras Firebase. |

## Publicação e segurança

O site é publicado pela branch main no GitHub Pages. Mudanças no site exigem commit e alguns minutos de publicação. Mudanças no ESP32 exigem nova gravação por Arduino IDE.

Nunca publique esp32/secrets.h, senhas Wi-Fi, senhas de usuários, tokens Cloudflare ou segredos do Apps Script. Mantenha regras Firebase restritivas e faça backup antes de excluir dados relevantes.
