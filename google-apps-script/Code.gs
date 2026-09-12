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
