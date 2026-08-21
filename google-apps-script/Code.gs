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
    const agora = new Date();
    const fuso = Session.getScriptTimeZone() || "America/Sao_Paulo";

    aba.appendRow([
      Utilities.formatDate(agora,fuso,"dd/MM/yyyy"),
      Utilities.formatDate(agora,fuso,"HH:mm:ss"),
      textoSeguro(dado.fullName,80),
      bebidas,
      String(dado.orderId)
    ]);

    return resposta({ok:true});
  } catch (erro) {
    console.error(erro);
    return resposta({ok:false,error:String(erro)});
  }
}

// O site manda o ID token do Firebase, nunca o segredo do equipamento.
// O token é validado nos servidores do Google antes de a linha ser gravada.
function validarTokenFirebase(token, props) {
  const projectId = props.getProperty("FIREBASE_PROJECT_ID") || "geladeira-14-bis";
  if (!token) return null;
  try {
    const url = "https://oauth2.googleapis.com/tokeninfo?id_token=" + encodeURIComponent(token);
    const retorno = UrlFetchApp.fetch(url, {muteHttpExceptions:true});
    if (retorno.getResponseCode() !== 200) return null;
    const dados = JSON.parse(retorno.getContentText());
    if (dados.aud !== projectId) return null;
    if (dados.iss !== "https://securetoken.google.com/" + projectId) return null;
    // tokeninfo segue o padrão OpenID e devolve o UID em "sub".
    // Alguns retornos do Firebase também usam "user_id".
    return dados.user_id || dados.sub || null;
  } catch (erro) {
    console.error(erro);
    return null;
  }
}

function configurarPlanilha() {
  const planilhaId = PropertiesService.getScriptProperties().getProperty("SPREADSHEET_ID");
  if (!planilhaId) throw new Error("SPREADSHEET_ID não configurado");
  obterAbaPedidos(SpreadsheetApp.openById(planilhaId));
}

function obterAbaPedidos(planilha) {
  let aba = planilha.getSheetByName("Pedidos");
  if (!aba) {
    const primeiraAba = planilha.getSheets()[0];
    aba = primeiraAba.getLastRow() === 0 ? primeiraAba.setName("Pedidos") : planilha.insertSheet("Pedidos");
  }
  if (aba.getLastRow() === 0) {
    aba.getRange(1,1,1,6).setValues([["Data","Hora","Nome","Bebidas","Pedido ID","Pago"]]);
    aba.getRange("A1:F1").setFontWeight("bold").setBackground("#1f4e78").setFontColor("#ffffff");
    aba.setFrozenRows(1);
    aba.autoResizeColumns(1,6);
  }
  return aba;
}

function textoSeguro(valor, limite) {
  const texto = String(valor || "").trim().slice(0,limite);
  return /^[=+\-@]/.test(texto) ? "'" + texto : texto;
}

function resposta(objeto) {
  return ContentService.createTextOutput(JSON.stringify(objeto)).setMimeType(ContentService.MimeType.JSON);
}
