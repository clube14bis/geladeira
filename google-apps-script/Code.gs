function doPost(e) {
  const props = PropertiesService.getScriptProperties();
  const segredo = props.getProperty("DEVICE_SECRET");
  const planilhaId = props.getProperty("SPREADSHEET_ID");

  try {
    if (!e || !e.postData || !e.postData.contents)
      return resposta({ok:false,error:"requisição vazia"});

    const dado = JSON.parse(e.postData.contents);

    if (!segredo || dado.secret !== segredo)
      return resposta({ok:false,error:"não autorizado"});

    if (!dado.orderId || !dado.fullName || !Array.isArray(dado.items))
      return resposta({ok:false,error:"dados inválidos"});

    if (!planilhaId)
      return resposta({ok:false,error:"SPREADSHEET_ID não configurado"});

    const planilha = SpreadsheetApp.openById(planilhaId);
    const aba = planilha.getSheets()[0];
    const bebidas = dado.items.map(item => `${String(item.drink||"").trim().slice(0,80)} x${Math.max(1,Number(item.quantity)||1)}`).join(", ");
    const agora = new Date();
    const fuso = Session.getScriptTimeZone() || "America/Sao_Paulo";

    aba.appendRow([
      Utilities.formatDate(agora,fuso,"dd/MM/yyyy"),
      Utilities.formatDate(agora,fuso,"HH:mm:ss"),
      String(dado.fullName).trim().slice(0,80),
      bebidas,
      String(dado.orderId)
    ]);

    return resposta({ok:true});
  } catch (erro) {
    console.error(erro);
    return resposta({ok:false,error:String(erro)});
  }
}

function resposta(objeto) {
  return ContentService.createTextOutput(JSON.stringify(objeto)).setMimeType(ContentService.MimeType.JSON);
}
