const assert = require("node:assert/strict");
const { test } = require("node:test");
const { readFileSync } = require("node:fs");
const { join } = require("node:path");
const vm = require("node:vm");
const source = readFileSync(join(__dirname, "../admin.js"), "utf8");
const clock = 1800000000000;
const helpers = source.slice(source.indexOf("function limiteSemSinalESP("), source.indexOf("function atualizarStatusESP("));

function harness() {
  const elements = new Map();
  const context = vm.createContext({
    Date: { now: () => clock },
    $: (selector) => {
      if (!elements.has(selector)) elements.set(selector, {});
      return elements.get(selector);
    },
    estadoESP: null, reinicioRemotoEmTransito: false,
    confirm: () => true, db: {}, ref: (_, path) => path,
    set: async () => { context.envios++; }, envios: 0,
  });
  const restart = source.slice(source.indexOf("async function solicitarReinicioESP("), source.indexOf("function monitorarESP("));
  vm.runInContext(helpers + restart, context);
  return context;
}

test("firmware novo fica online entre mensagens de cinco minutos e expira após 15 min", () => {
  const h = harness();
  for (const age of [0, 180, 299, 300, 600, 900]) {
    assert.equal(h.espEstaOnline({ firmware: "2.8.3", online: true, lastSeen: clock - age * 1000 }), true);
  }
  assert.equal(h.espEstaOnline({ firmware: "2.8.3", online: true, lastSeen: clock - 901000 }), false);
});

test("firmwares antigos e dados ausentes conservam o limite de três minutos", () => {
  const h = harness();
  for (const firmware of ["2.8.1", "2.8.2", "2.7.9", "", "invalido"]) {
    assert.equal(h.limiteSemSinalESP({ firmware }), 180);
    assert.equal(h.espEstaOnline({ firmware, online: true, lastSeen: clock - 181000 }), false);
  }
  assert.equal(h.espEstaOnline(null), false);
  assert.equal(h.espEstaOnline({ firmware: "2.8.3", online: true }), false);
  assert.equal(h.espEstaOnline({ firmware: "2.8.3", online: false, lastSeen: clock }), false);
});

test("reinício remoto usa o mesmo limite do indicador online", async () => {
  const h = harness();
  h.estadoESP = { firmware: "2.8.3", online: true, lastSeen: clock - 300000 };
  await h.solicitarReinicioESP();
  assert.equal(h.envios, 1);
  h.estadoESP.lastSeen = clock - 901000;
  await h.solicitarReinicioESP();
  assert.equal(h.envios, 1);
});
