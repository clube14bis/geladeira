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

    aba.appendRow([
      Utilities.formatDate(agora,fuso,"dd/MM/yyyy"),
      Utilities.formatDate(agora,fuso,"HH:mm:ss"),
      textoSeguro(dado.fullName,80),
      bebidas,
      String(dado.orderId),
      totalCentavos / 100
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

function configurarPlanilha() {
  const planilhaId = PropertiesService.getScriptProperties().getProperty("SPREADSHEET_ID");
  if (!planilhaId) throw new Error("SPREADSHEET_ID não configurado");
  obterAbaPedidos(SpreadsheetApp.openById(planilhaId));
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
  if (aba.getLastRow() === 0) {
    aba.getRange(1,1,1,7).setValues([["Data","Hora","Nome","Bebidas","Pedido ID","Pago","Valor total"]]);
    aba.getRange("A1:G1").setFontWeight("bold").setBackground("#1f4e78").setFontColor("#ffffff");
    aba.setFrozenRows(1);
    aba.autoResizeColumns(1,7);
  }
  if (aba.getRange("G1").getValue() !== "Valor total") {
    aba.getRange("G1").setValue("Valor total").setFontWeight("bold").setBackground("#1f4e78").setFontColor("#ffffff");
    aba.autoResizeColumn(7);
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
