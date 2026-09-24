function doPost(e) {
  const props = PropertiesService.getScriptProperties();
  const segredo = props.getProperty("DEVICE_SECRET");
  const planilhaId = props.getProperty("SPREADSHEET_ID");

  try {
    if (!e || !e.postData || !e.postData.contents)
      return resposta({ok:false,error:"requisição vazia"});

    const dado = JSON.parse(e.postData.contents);

    const enviadoPeloEsp = segredo && dado.secret === segredo;
    const uidFirebase = enviadoPeloEsp ? null : validarTokenFirebase(dado.idToken, props);
    if (!enviadoPeloEsp && (!uidFirebase || uidFirebase !== dado.uid))
      return resposta({ok:false,error:"não autorizado"});

    if (!dado.orderId || !dado.fullName || !Array.isArray(dado.items))
      return resposta({ok:false,error:"dados inválidos"});

    if (!planilhaId)
      return resposta({ok:false,error:"SPREADSHEET_ID não configurado"});

    const planilha = SpreadsheetApp.openById(planilhaId);
    const aba = obterAbaPedidos(planilha);
    const bebidas = dado.items.map(item => `${textoSeguro(item.drink,80)} x${Math.max(1,Number(item.quantity)||1)}`).join(", ");
    const totalCentavos = Math.max(0, Number(dado.totalCents) || 0);
    const agora = new Date();
    const fuso = Session.getScriptTimeZone() || "America/Sao_Paulo";

    // A linha 3 é sempre reservada ao pedido mais recente. Os anteriores
    // descem, preservando o título (linha 1) e os cabeçalhos (linha 2).
    aba.insertRowBefore(3);
    const destino = aba.getRange(3, 1, 1, 5);
    const linhaModelo = aba.getLastRow() >= 4 ? 4 : 0;
    // Copia somente a aparência do pedido que acabou de descer para a linha 4.
    // Assim, toda solicitação nova mantém as mesmas cores, bordas, fonte,
    // alinhamento e altura sem copiar os valores do pedido anterior.
    if (linhaModelo) {
      aba.getRange(linhaModelo, 1, 1, 5).copyTo(
        destino,
        SpreadsheetApp.CopyPasteType.PASTE_FORMAT,
        false
      );
      aba.setRowHeight(3, aba.getRowHeight(linhaModelo));
    } else {
      destino.setBackground("#ffffff").setFontColor("#1d1d1f")
        .setVerticalAlignment("middle").setWrap(true);
      aba.setRowHeight(3, 26);
    }
    destino.setValues([[
      Utilities.formatDate(agora,fuso,"dd/MM/yyyy"),
      Utilities.formatDate(agora,fuso,"HH:mm:ss"),
      textoSeguro(dado.fullName,80),
      bebidas,
      totalCentavos / 100
    ]]);
    return resposta({ok:true});
  } catch (erro) {
    console.error(erro);
    return resposta({ok:false,error:String(erro)});
  }
}

// O site manda o ID token do Firebase, nunca o segredo do equipamento.
// O token é validado nos servidores do Google antes de a linha ser gravada.
function validarTokenFirebase(token, props) {
  const apiKey = props.getProperty("FIREBASE_API_KEY") || "AIzaSyCdEUfUn9inSrri42DXKgmh27d9eT7Yd0Q";
  if (!token) return null;
  try {
    // O próprio Firebase valida a assinatura, expiração e projeto do ID token.
    const url = "https://identitytoolkit.googleapis.com/v1/accounts:lookup?key=" + apiKey;
    const retorno = UrlFetchApp.fetch(url, {
      method:"post",
      contentType:"application/json",
      payload:JSON.stringify({idToken:token}),
      muteHttpExceptions:true
    });
    if (retorno.getResponseCode() !== 200) {
      console.log("Firebase Auth rejeitou o token: " + retorno.getResponseCode() + " " + retorno.getContentText());
      return null;
    }
    const dados = JSON.parse(retorno.getContentText());
    return dados.users && dados.users[0] ? dados.users[0].localId : null;
  } catch (erro) {
    console.error(erro);
    return null;
  }
}

// Retenção segura no Realtime Database -------------------------------------
// Mantém somente os últimos 95 dias no Firebase. A planilha continua sendo
// o arquivo histórico permanente. Esta rotina só remove pedidos que a própria
// ESP32 marcou como "locked"; pedidos pendentes ou com estado desconhecido
// nunca são apagados automaticamente.
const RETENCAO_DIAS = 95;
const RETENCAO_LOTE_MAXIMO = 100;

function simularLimpezaPedidosExpirados() {
  return executarLimpezaPedidosExpirados_(true);
}

function limparPedidosExpirados() {
  const props = PropertiesService.getScriptProperties();
  if (props.getProperty("RETENTION_ENABLED") !== "true") {
    const resultado = {
      ok: false,
      mensagem: "RETENTION_ENABLED não está definido como true; nenhuma exclusão foi feita."
    };
    console.log(JSON.stringify(resultado));
    return resultado;
  }
  return executarLimpezaPedidosExpirados_(false);
}

function executarLimpezaPedidosExpirados_(simulacao) {
  const bloqueio = LockService.getScriptLock();
  if (!bloqueio.tryLock(10000))
    throw new Error("Outra limpeza de retenção já está em andamento. Tente novamente em alguns minutos.");

  try {
    const props = PropertiesService.getScriptProperties();
    const configuracao = obterConfiguracaoRetencao_(props);
    const token = autenticarContaRetencao_(configuracao);
    const limite = Date.now() - RETENCAO_DIAS * 24 * 60 * 60 * 1000;
    const pedidos = lerPedidosExpirados_(configuracao, token, limite);
    const ids = Object.keys(pedidos || {});
    const exclusoes = {};
    const ignorados = [];

    ids.forEach(id => {
      const pedido = pedidos[id] || {};
      const criadoEm = Number(pedido.createdAt) || 0;
      const fechado = pedido.execution && pedido.execution.state === "locked";
      if (!pedido.uid || !criadoEm || criadoEm > limite || !fechado) {
        ignorados.push(id);
        return;
      }
      // Patch multi-local: pedido e histórico somem juntos, sem estado parcial.
      exclusoes[`orders/${id}`] = null;
      exclusoes[`userOrders/${pedido.uid}/${id}`] = null;
    });

    const resultado = {
      ok: true,
      simulacao: simulacao,
      diasRetencao: RETENCAO_DIAS,
      limiteIso: new Date(limite).toISOString(),
      candidatosLidos: ids.length,
      pedidosElegiveis: Object.keys(exclusoes).length / 2,
      pedidosIgnorados: ignorados.length,
      idsIgnorados: ignorados.slice(0, 20)
    };

    if (!simulacao && Object.keys(exclusoes).length) {
      aplicarExclusoesRetencao_(configuracao, token, exclusoes);
      resultado.excluidos = Object.keys(exclusoes).length / 2;
    } else {
      resultado.excluidos = 0;
    }
    console.log(JSON.stringify(resultado));
    return resultado;
  } finally {
    bloqueio.releaseLock();
  }
}

function obterConfiguracaoRetencao_(props) {
  const banco = String(props.getProperty("FIREBASE_DATABASE_URL") || "").replace(/\/$/, "");
  const apiKey = props.getProperty("FIREBASE_API_KEY");
  const email = props.getProperty("FIREBASE_RETENTION_EMAIL");
  const senha = props.getProperty("FIREBASE_RETENTION_PASSWORD");
  if (!banco || !apiKey || !email || !senha)
    throw new Error("Configure FIREBASE_DATABASE_URL, FIREBASE_API_KEY, FIREBASE_RETENTION_EMAIL e FIREBASE_RETENTION_PASSWORD nas Propriedades do script.");
  return { banco, apiKey, email, senha };
}

function autenticarContaRetencao_(configuracao) {
  const resposta = UrlFetchApp.fetch(
    "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" + encodeURIComponent(configuracao.apiKey),
    {
      method: "post",
      contentType: "application/json",
      payload: JSON.stringify({ email: configuracao.email, password: configuracao.senha, returnSecureToken: true }),
      muteHttpExceptions: true
    }
  );
  if (resposta.getResponseCode() !== 200)
    throw new Error("A conta de retenção não pôde autenticar: " + resposta.getContentText());
  const dados = JSON.parse(resposta.getContentText());
  if (!dados.idToken) throw new Error("Firebase não devolveu um token para a conta de retenção.");
  return dados.idToken;
}

function lerPedidosExpirados_(configuracao, token, limite) {
  const consulta = [
    "orderBy=" + encodeURIComponent(JSON.stringify("createdAt")),
    "endAt=" + encodeURIComponent(limite),
    "limitToFirst=" + RETENCAO_LOTE_MAXIMO,
    "auth=" + encodeURIComponent(token)
  ].join("&");
  const resposta = UrlFetchApp.fetch(configuracao.banco + "/orders.json?" + consulta + "&timeout=20s", {
    method: "get",
    muteHttpExceptions: true
  });
  if (resposta.getResponseCode() !== 200)
    throw new Error("Não foi possível consultar pedidos expirados: " + resposta.getContentText());
  return JSON.parse(resposta.getContentText()) || {};
}

function aplicarExclusoesRetencao_(configuracao, token, exclusoes) {
  const resposta = UrlFetchApp.fetch(
    configuracao.banco + "/.json?auth=" + encodeURIComponent(token) + "&print=silent",
    {
      method: "patch",
      contentType: "application/json",
      payload: JSON.stringify(exclusoes),
      muteHttpExceptions: true
    }
  );
  if (resposta.getResponseCode() !== 200 && resposta.getResponseCode() !== 204)
    throw new Error("Não foi possível excluir pedidos expirados: " + resposta.getContentText());
}

function instalarLimpezaAutomatica() {
  ScriptApp.getProjectTriggers()
    .filter(gatilho => gatilho.getHandlerFunction() === "limparPedidosExpirados")
    .forEach(gatilho => ScriptApp.deleteTrigger(gatilho));
  ScriptApp.newTrigger("limparPedidosExpirados").timeBased().everyDays(1).atHour(3).create();
  console.log("Limpeza automática diária instalada. RETENTION_ENABLED precisa ser true para excluir dados.");
}

function removerLimpezaAutomatica() {
  ScriptApp.getProjectTriggers()
    .filter(gatilho => gatilho.getHandlerFunction() === "limparPedidosExpirados")
    .forEach(gatilho => ScriptApp.deleteTrigger(gatilho));
}

function configurarPlanilha() {
  const planilhaId = PropertiesService.getScriptProperties().getProperty("SPREADSHEET_ID");
  if (!planilhaId) throw new Error("SPREADSHEET_ID não configurado");
  const aba = obterAbaPedidos(SpreadsheetApp.openById(planilhaId));
  formatarEstruturaInicial(aba);
}

// Execute esta função uma única vez no editor caso o Google peça autorização
// para usar UrlFetchApp. Ela garante que a validação Firebase tenha permissão.
function autorizarIntegracaoFirebase() {
  UrlFetchApp.fetch("https://identitytoolkit.googleapis.com/", {muteHttpExceptions:true});
}

function obterAbaPedidos(planilha) {
  let aba = planilha.getSheetByName("Pedidos");
  if (!aba) {
    const primeiraAba = planilha.getSheets()[0];
    aba = primeiraAba.getLastRow() === 0 ? primeiraAba.setName("Pedidos") : planilha.insertSheet("Pedidos");
  }
  // A primeira linha é reservada ao título. Em planilhas já existentes,
  // insere uma linha sem alterar nem perder os pedidos anteriores.
  if (aba.getLastRow() === 0) {
    aba.insertRowsBefore(1, 2);
  }
  // Esta função é chamada a cada pedido. Não altere títulos, cores,
  // tamanhos, larguras ou alinhamentos aqui: o administrador os define
  // manualmente na planilha e os pedidos novos apenas os preservam.
  return aba;
}

// Use somente ao criar ou reorganizar a planilha. Após o administrador
// personalizar o visual, não é necessário executar esta função novamente.
function formatarEstruturaInicial(aba) {
  if (aba.getRange("A1").getValue() !== "Geladeira 14 BIS") {
    aba.insertRowBefore(1);
  }
  ["Pedido ID","Pago"].forEach(titulo => {
    const cabecalhos = aba.getRange(2,1,1,Math.max(1,aba.getLastColumn())).getValues()[0];
    const coluna = cabecalhos.indexOf(titulo) + 1;
    if (coluna) aba.deleteColumn(coluna);
  });
  aba.getRange("A1:E1").breakApart().merge().setValue("Geladeira 14 BIS")
    .setFontWeight("bold").setFontSize(27).setHorizontalAlignment("center")
    .setVerticalAlignment("middle").setBackground("#158557").setFontColor("#ffffff");
  aba.getRange(2,1,1,5).setValues([["Data","Hora","Nome do cliente","Bebidas","Valor total"]]);
  aba.getRange("A2:E2").setFontWeight("bold").setFontSize(16)
    .setBackground("#1f4e78").setFontColor("#ffffff").setVerticalAlignment("middle");
  aba.setRowHeight(1, 42);
  aba.setRowHeight(2, 30);
  aba.getRange("E:E").setNumberFormat('[$R$-pt-BR] #,##0.00');
  aba.setFrozenRows(2);
  aba.autoResizeColumns(1,5);
}

function textoSeguro(valor, limite) {
  const texto = String(valor || "").trim().slice(0,limite);
  return /^[=+\-@]/.test(texto) ? "'" + texto : texto;
}

function resposta(objeto) {
  return ContentService.createTextOutput(JSON.stringify(objeto)).setMimeType(ContentService.MimeType.JSON);
}
