const assert = require("node:assert/strict");
const { test } = require("node:test");
const { readFileSync } = require("node:fs");
const vm = require("node:vm");

const source = readFileSync(require("node:path").join(__dirname, "../app.js"), "utf8");
const screenSource = source.slice(source.indexOf("function obrigadoTela("), source.indexOf("async function sheets("));
const sheetsSource = source.slice(source.indexOf("async function sheets("), source.indexOf("async function reduzirEstoque("));
const sendSource = source.slice(source.indexOf("async function enviar("), source.indexOf('$("#form-login").addEventListener'));

function harness() {
  const elements = new Map();
  function $(selector) {
    if (!elements.has(selector)) {
      const classes = new Set();
      elements.set(selector, {
        textContent: "",
        classList: {
          add: (...names) => names.forEach((name) => classes.add(name)),
          remove: (...names) => names.forEach((name) => classes.delete(name)),
          contains: (name) => classes.has(name),
        },
      });
    }
    return elements.get(selector);
  }
  const timers = new Map();
  const sounds = [];
  let serial = 0;
  let listener, listenerError, stops = 0;
  const requests = [];
  const context = vm.createContext({
    $, obrigado: $("#obrigado"), demo: false, db: {}, retornoLogin: null,
    intervaloObrigado: null, cancelarConfirmacaoAbertura: null,
    setTimeout: (fn, ms) => { timers.set(++serial, { fn, ms }); return serial; },
    clearTimeout: (id) => timers.delete(id),
    setInterval: (fn, ms) => { timers.set(++serial, { fn, ms, interval: true }); return serial; },
    clearInterval: (id) => timers.delete(id),
    fmt: (value) => String(value),
    ref: (_db, path) => path,
    onValue: (path, callback, error) => {
      assert.equal(path, "orders/pedido-1/execution/state");
      listener = callback; listenerError = error;
      return () => { stops++; };
    },
    tocarAudioGeladeira: (name) => sounds.push(name),
    audioGeladeiraAberta: {}, audioGeladeiraFechada: {},
    usuarioAtual: { uid: "cliente-1", getIdToken: async () => "token-teste" },
    perfilAtual: { fullName: "Cliente Teste", username: "cliente" },
    carrinho: { agua: 1 }, produtos: { agua: { name: "Água", priceCents: 500 } },
    qtd: () => 1, total: () => 500, prepararAudioGeladeira: async () => {},
    load: () => {}, fechar: () => {}, erro: String,
    alert: () => { throw Error("Um pedido salvo não deve voltar ao erro de envio"); },
    push: () => ({ key: "pedido-1" }), update: async () => {},
    serverTimestamp: () => 123, reduzirEstoque: async () => {},
    set: async () => {}, sheetsEndpoint: "https://example.test/sheets",
    fetch: async (_url, options) => { requests.push(JSON.parse(options.body)); },
    console: { warn: () => {} },
  });
  vm.runInContext(screenSource + sheetsSource + sendSource, context);
  return {
    context, $, sounds, timers, requests,
    emit: (state) => listener({ val: () => state }),
    fail: () => listenerError(new Error("permission denied")),
    stops: () => stops,
    advance: (ms) => {
      const entry = [...timers].find(([, timer]) => timer.ms === ms);
      assert.ok(entry, `Expected a ${ms}ms timer`);
      if (!entry[1].interval) timers.delete(entry[0]);
      entry[1].fn();
    },
  };
}

test("only opened on this order enables the opening sound and countdown", () => {
  const h = harness();
  h.context.obrigadoTela(500, "pedido-1");
  h.emit(null);
  assert.deepEqual(h.sounds, []);
  assert.ok(h.$("#contador").classList.contains("oculto"));
  h.emit("opened");
  h.emit("opened");
  assert.deepEqual(h.sounds, ["aberta"]);
  assert.ok(h.$("#obrigado").classList.contains("fase-aberta"));
  assert.equal(h.stops(), 1);
  assert.equal([...h.timers.values()].some((t) => t.ms === 15000), false);
  for (let i = 0; i < 20; i++) h.advance(1000);
  assert.deepEqual(h.sounds, ["aberta", "fechada"]);
});

test("missing confirmation shows the exact notice without audio or late playback", () => {
  const h = harness();
  h.context.obrigadoTela(500, "pedido-1");
  h.advance(15000);
  assert.equal(h.$("#mensagem-obrigado").textContent,
    "Seu pedido foi registrado, mas não recebemos a confirmação da geladeira");
  assert.ok(h.$("#obrigado").classList.contains("fase-aviso"));
  h.emit("opened");
  assert.deepEqual(h.sounds, []);
  assert.equal(h.stops(), 1);
});

test("listener errors show the notice; an already locked order never announces opening", () => {
  for (const state of ["error", "locked"]) {
    const h = harness();
    h.context.obrigadoTela(500, "pedido-1");
    if (state === "error") h.fail(); else h.emit(state);
    assert.deepEqual(h.sounds, []);
    assert.equal(h.stops(), 1);
    assert.equal(h.timers.size, 0);
  }
});

test("cancelled screens ignore callbacks and remove the confirmation timeout", () => {
  const h = harness();
  h.context.obrigadoTela(500, "pedido-1");
  h.context.cancelarConfirmacaoAbertura();
  h.emit("opened");
  assert.deepEqual(h.sounds, []);
  assert.equal(h.timers.size, 0);
  assert.equal(h.stops(), 1);
});

test("Sheets receives the saved order even if command delivery fails or stays pending", async () => {
  for (const fails of [false, true]) {
    const h = harness();
    let commands = 0;
    h.context.set = (path, value) => {
      assert.equal(path, "commands/geladeira/pedido-1");
      assert.equal(value, true);
      commands++;
      return fails ? Promise.reject(Error("offline")) : new Promise(() => {});
    };
    await h.context.enviar({ disabled: false });
    await new Promise(setImmediate);
    assert.equal(h.requests.length, 1);
    assert.equal(h.requests[0].orderId, "pedido-1");
    assert.equal(h.requests[0].totalCents, 500);
    h.advance(15000);
    assert.equal(h.requests.length, 1);
    assert.equal(commands, 1);
    assert.deepEqual(h.sounds, []);
  }
});

test("an unsaved order is never sent to Sheets or the lock", async () => {
  const h = harness();
  h.context.update = async () => { throw Error("save failed"); };
  let alerts = 0, commands = 0;
  h.context.alert = () => alerts++;
  h.context.set = async () => { commands++; };
  await h.context.enviar({ disabled: false });
  assert.equal(alerts, 1);
  assert.equal(commands, 0);
  assert.equal(h.requests.length, 0);
});

test("pending Sheets authentication retains customer details after sign out", async () => {
  const h = harness();
  let resolveToken;
  h.context.usuarioAtual.getIdToken = () => new Promise((resolve) => { resolveToken = resolve; });
  const sending = h.context.sheets("pedido-1", [], 500);
  h.context.usuarioAtual = h.context.perfilAtual = null;
  resolveToken("token-teste");
  await sending;
  assert.equal(h.requests[0].uid, "cliente-1");
  assert.equal(h.requests[0].fullName, "Cliente Teste");
});
