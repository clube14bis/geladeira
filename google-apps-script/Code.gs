/**
 * Registra somente pedidos confirmados pelo ESP32.
 * Configure SPREADSHEET_ID e DEVICE_SECRET em Project Settings > Script properties.
 */
function doPost(e) {
  const props = PropertiesService.getScriptProperties();
  const segredo = props.getProperty('DEVICE_SECRET');
  const planilhaId = props.getProperty('SPREADSHEET_ID');
  try {
    const dado = JSON.parse(e.postData.contents);
    if (!segredo || !planilhaId || dado.secret !== segredo) return resposta({ ok: false, error: 'não autorizado' });
    if (!dado.orderId || !dado.fullName || !dado.drink) return resposta({ ok: false, error: 'dados inválidos' });

    const aba = SpreadsheetApp.openById(planilhaId).getSheets()[0];
    const ultimaLinha = aba.getLastRow();
    if (ultimaLinha > 1 && aba.getRange(2, 5, ultimaLinha - 1, 1).createTextFinder(dado.orderId).matchEntireCell(true).findNext()) {
      return resposta({ ok: true, duplicate: true });
    }

    const agora = new Date();
    const fuso = Session.getScriptTimeZone() || 'America/Sao_Paulo';
    aba.appendRow([
      Utilities.formatDate(agora, fuso, 'dd/MM/yyyy'),
      Utilities.formatDate(agora, fuso, 'HH:mm:ss'),
      String(dado.fullName).slice(0, 80),
      String(dado.drink).slice(0, 80),
      String(dado.orderId)
    ]);
    return resposta({ ok: true });
  } catch (erro) {
    return resposta({ ok: false, error: String(erro) });
  }
}

function resposta(objeto) {
  return ContentService.createTextOutput(JSON.stringify(objeto)).setMimeType(ContentService.MimeType.JSON);
}
