import { initializeApp } from "https://www.gstatic.com/firebasejs/12.17.0/firebase-app.js";
import {
  getAuth,
  createUserWithEmailAndPassword,
  signInWithEmailAndPassword,
  sendPasswordResetEmail,
  sendEmailVerification,
  verifyBeforeUpdateEmail,
  signOut,
  deleteUser,
} from "https://www.gstatic.com/firebasejs/12.17.0/firebase-auth.js";
import {
  getDatabase,
  ref,
  set,
  remove,
  get,
  push,
  update,
  serverTimestamp,
} from "https://www.gstatic.com/firebasejs/12.17.0/firebase-database.js";
import {
  firebaseConfig,
  authServiceUrl,
  loginDomain,
  sheetsEndpoint,
} from "./firebase-config.js?v=1.5.4";
import {
  ordemCategorias,
  imagemProduto,
  produtosIniciais,
} from "./catalogo-base.js?v=1.2.4";
const $ = (s) => document.querySelector(s),
  telas = document.querySelectorAll(".tela"),
  VERSAO_APP = "V1.5.16",
  PIX = "00020126580014BR.GOV.BCB.PIX0136c9cb7e85-240b-46e5-b500-3278442092475204000053039865802BR5912Clube 14 Bis6011Mirassol SP62160512Bebidas14Bis63045F94",
  fmt = (c) =>
    new Intl.NumberFormat("pt-BR", {
      style: "currency",
      currency: "BRL",
    }).format((+c || 0) / 100),
  demo = ["localhost", "127.0.0.1"].includes(location.hostname);
let auth,
  db,
  usuarioAtual,
  perfilAtual,
  carrinho = {},
  produtos = {},
  catalogoConfigurado = false,
  retornoLogin,
  intervaloObrigado,
  ultimoTotalCarrinho = 0,
  historicoPedidos = [],
  mesHistorico = new Date(),
  diaHistorico = "";
const erroLogin = $("#erro-login"),
  erroCadastro = $("#erro-cadastro"),
  erroRecuperacao = $("#erro-recuperacao"),
  erroAtualizarEmail = $("#erro-atualizar-email"),
  carrinhoModal = $("#carrinho"),
  listaCarrinho = $("#lista-carrinho"),
  botaoCarrinho = $("#botao-concluir"),
  resumoCarrinho = $("#resumo-carrinho"),
  obrigado = $("#obrigado"),
  barraCarrinho = $(".carrinho-barra"),
  carregando = $("#carregando"),
  textoCarregando = $("#texto-carregando");
$("#cad-senha").minLength = 6;
[
  "#cad-nome",
  "#cad-usuario",
  "#cad-telefone",
  "#cad-cpf",
  "#cad-email",
  "#cad-senha",
].forEach((s) => ($(s).placeholder = ""));
function normalizarUsuario(v) {
  let u = v.trim().toLowerCase();
  if (!/^[a-z0-9._-]{3,24}$/.test(u))
    throw Error(
      "USE DE 3 A 24 CARACTERES: LETRAS, NÚMEROS, PONTO, HÍFEN OU _.",
    );
  return u;
}
function email(u) {
  return `${normalizarUsuario(u)}@${loginDomain}`;
}
function emailValido(valor) {
  let e = valor.trim().toLowerCase();
  if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(e))
    throw Error("INFORME UM E-MAIL VÁLIDO.");
  return e;
}
function identificadorLogin(valor) {
  let v = valor.trim().toLowerCase();
  return v.includes("@") ? emailValido(v) : email(v);
}
async function servicoAuth(caminho, dados) {
  if (!authServiceUrl) throw Error("SERVIÇO DE LOGIN NÃO CONFIGURADO.");
  const resposta = await fetch(`${authServiceUrl}${caminho}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(dados),
  });
  const corpo = await resposta.json().catch(() => ({}));
  if (!resposta.ok) {
    const falha = Error(corpo.error || "USUÁRIO OU SENHA INCORRETOS.");
    falha.code = corpo.error;
    throw falha;
  }
  return corpo;
}
async function registrarNomeUsuario(username, user) {
  await servicoAuth("/register", {
    username: normalizarUsuario(username),
    idToken: await user.getIdToken(),
  });
}
function tela(id) {
  telas.forEach((x) => x.classList.toggle("ativa", x.id === id));
}
function load(a, t = "Carregando...") {
  textoCarregando.textContent = t;
  carregando.classList.toggle("visivel", a);
}
function erro(e) {
  return (
    {
      "auth/invalid-credential": "Usuário ou senha incorretos.",
      "auth/email-already-in-use": "Este e-mail já está cadastrado.",
      "auth/weak-password": "A senha precisa ter ao menos 6 caracteres.",
      "auth/invalid-email": "Informe um e-mail válido.",
      "auth/user-not-found": "Não encontramos uma conta com este e-mail.",
      "auth/too-many-requests": "Muitas tentativas. Aguarde alguns minutos.",
      "auth/network-request-failed": "Verifique sua conexão com a internet.",
      USERNAME_TAKEN: "Este nome de usuário já está em uso.",
      FIREBASE_AUTH_FAILED: "Não foi possível validar o cadastro. Tente novamente.",
      INVALID_TOKEN: "A sessão expirou. Tente criar o cadastro novamente.",
    }[e.code] ||
    e.message ||
    "Não foi possível concluir a operação."
  );
}
async function clima() {
  try {
    let d = await (
      await fetch(
        "https://api.open-meteo.com/v1/forecast?latitude=-20.816&longitude=-49.52&current=temperature_2m&timezone=America%2FSao_Paulo",
      )
    ).json();
    $("#status-clube").innerHTML =
      `<span>CLUBE 14 BIS</span><span>MIRASSOL: ${Math.round(d.current.temperature_2m)}°C</span><span>${VERSAO_APP}</span>`;
  } catch {
    $("#status-clube").innerHTML =
      `<span>CLUBE 14 BIS</span><span>MIRASSOL</span><span>${VERSAO_APP}</span>`;
  }
}
async function perfil(uid) {
  let s = await get(ref(db, `users/${uid}`));
  if (!s.exists()) throw Error("CADASTRO NÃO ENCONTRADO.");
  return s.val();
}
async function prosseguirAposLogin() {
  await bebidas();
}
function cpfMask() {
  let d = $("#cad-cpf").value.replace(/\D/g, "").slice(0, 11);
  $("#cad-cpf").value = d
    .replace(/(\d{3})(\d)/, "$1.$2")
    .replace(/(\d{3})(\d)/, "$1.$2")
    .replace(/(\d{3})(\d{1,2})$/, "$1-$2");
}
function cpfOk(c) {
  c = c.replace(/\D/g, "");
  if (!/^\d{11}$/.test(c) || /^(\d)\1+$/.test(c)) return false;
  let s = 0;
  for (let i = 0; i < 9; i++) s += +c[i] * (10 - i);
  let d = 11 - (s % 11);
  if (d >= 10) d = 0;
  if (d !== +c[9]) return false;
  s = 0;
  for (let i = 0; i < 10; i++) s += +c[i] * (11 - i);
  d = 11 - (s % 11);
  return d >= 10 ? +c[10] === 0 : d === +c[10];
}
async function catalogo() {
  let salvo = (await get(ref(db, "catalog"))).val(),
    base = produtosIniciais();
  catalogoConfigurado = !!salvo;
  produtos = Object.fromEntries(
    Object.entries({ ...base, ...(salvo || {}) }).map(([id, p]) => [
      id,
      { ...base[id], ...p, id },
    ]),
  );
}
function itens(cat) {
  return Object.values(produtos).filter(
    (p) => p.category === cat && (!catalogoConfigurado || p.enabled),
  );
}
async function bebidas() {
  load(true, "Carregando bebidas...");
  try {
    await catalogo();
    let out = ordemCategorias
      .map((cat) => {
        let ps = itens(cat);
        return !ps.length
          ? ""
          : `<section class="categoria"><h2>${cat}</h2><div class="produtos">${ps.map((p) => `<button class="produto" type="button" data-bebida="${p.id}"><img class="produto-imagem" src="${encodeURI(imagemProduto(p.image))}" alt="${p.name}"><span class="produto-corpo"><span class="produto-nome">${p.name}</span><small class="produto-preco">${fmt(p.priceCents)}</small></span><span class="produto-add"><span class="icone-adicionar">+</span></span></button>`).join("")}</div></section>`;
      })
      .join("");
    $("#lista-bebidas").innerHTML =
      out ||
      "<p class='carrinho-vazio'>Nenhum produto disponível no momento.</p>";
    carrinho = {};
    atualizar();
    tela("tela-bebidas");
  } catch (e) {
    erroLogin.textContent = erro(e);
    tela("tela-login");
  } finally {
    load(false);
  }
}
const nomesMeses = [
  "JANEIRO", "FEVEREIRO", "MARÇO", "ABRIL", "MAIO", "JUNHO",
  "JULHO", "AGOSTO", "SETEMBRO", "OUTUBRO", "NOVEMBRO", "DEZEMBRO",
];
const nomesSemana = ["DOMINGO", "SEGUNDA-FEIRA", "TERÇA-FEIRA", "QUARTA-FEIRA", "QUINTA-FEIRA", "SEXTA-FEIRA", "SÁBADO"];
const escapar = (texto) => String(texto || "").replace(/[&<>"']/g, (c) => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"})[c]);
function chaveData(data) {
  return `${data.getFullYear()}-${String(data.getMonth() + 1).padStart(2, "0")}-${String(data.getDate()).padStart(2, "0")}`;
}
function inicioMes(data) { return new Date(data.getFullYear(), data.getMonth(), 1); }
function dentroDosTresMeses(data) {
  const limite = new Date();
  limite.setHours(0, 0, 0, 0);
  limite.setMonth(limite.getMonth() - 3);
  return data >= limite;
}
function renderCalendarioHistorico() {
  const mes = inicioMes(mesHistorico);
  const agora = inicioMes(new Date());
  const limite = inicioMes(new Date());
  limite.setMonth(limite.getMonth() - 2);
  const ano = mes.getFullYear(), numeroMes = mes.getMonth();
  $("#mes-historico").textContent = `${nomesMeses[numeroMes]} ${ano}`;
  $("#mes-anterior").disabled = mes <= limite;
  $("#mes-proximo").disabled = mes >= agora;
  const consumoPorDia = new Set(historicoPedidos.map((p) => chaveData(new Date(p.createdAt))));
  let html = ["DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SÁB"].map((d) => `<span class="semana-calendario">${d}</span>`).join("");
  for (let vazio = 0; vazio < new Date(ano, numeroMes, 1).getDay(); vazio++) html += '<span class="dia-vazio"></span>';
  const ultimoDia = new Date(ano, numeroMes + 1, 0).getDate();
  for (let dia = 1; dia <= ultimoDia; dia++) {
    const data = new Date(ano, numeroMes, dia), chave = chaveData(data), temConsumo = consumoPorDia.has(chave);
    html += `<button class="dia-calendario${temConsumo ? " tem-consumo" : ""}${chave === diaHistorico ? " selecionado" : ""}" type="button" data-dia="${chave}" aria-label="${dia} de ${nomesMeses[numeroMes]}">${dia}</button>`;
  }
  $("#calendario-historico").innerHTML = html;
}
function renderDetalhesHistorico() {
  const data = new Date(`${diaHistorico}T12:00:00`);
  const pedidosDia = historicoPedidos.filter((p) => chaveData(new Date(p.createdAt)) === diaHistorico).sort((a, b) => a.createdAt - b.createdAt);
  $("#titulo-dia-historico").textContent = `${String(data.getDate()).padStart(2, "0")}/${String(data.getMonth() + 1).padStart(2, "0")}/${data.getFullYear()} — ${nomesSemana[data.getDay()]}`;
  if (!pedidosDia.length) {
    $("#itens-historico").innerHTML = '<p class="historico-vazio">NENHUM CONSUMO REGISTRADO NESTE DIA.</p>';
    $("#total-dia-historico").textContent = fmt(0);
    return;
  }
  $("#itens-historico").innerHTML = pedidosDia.map((pedido) => {
    const hora = new Date(pedido.createdAt).toLocaleTimeString("pt-BR", { hour: "2-digit", minute: "2-digit" });
    const itens = Object.values(pedido.items || {}).map((item) => `<li>${Number(item.quantity) || 1}x ${escapar(item.drink)} <strong>${fmt(item.subtotalCents ?? ((item.priceCents || 0) * (item.quantity || 1)))}</strong></li>`).join("");
    return `<article class="consumo-historico"><time>${hora}</time><ul>${itens}</ul><b>${fmt(pedido.totalCents)}</b></article>`;
  }).join("");
  $("#total-dia-historico").textContent = fmt(pedidosDia.reduce((s, p) => s + (+p.totalCents || 0), 0));
}
async function abrirHistorico() {
  load(true, "Carregando histórico...");
  try {
    let dados = demo ? {} : (await get(ref(db, `userOrders/${usuarioAtual.uid}`))).val() || {};
    historicoPedidos = Object.values(dados).filter((p) => p && Number.isFinite(+p.createdAt) && dentroDosTresMeses(new Date(+p.createdAt)));
    mesHistorico = inicioMes(new Date());
    diaHistorico = chaveData(new Date());
    renderCalendarioHistorico();
    renderDetalhesHistorico();
    tela("tela-historico");
  } catch (e) {
    alert(erro(e));
  } finally {
    load(false);
  }
}
function qtd() {
  return Object.values(carrinho).reduce((a, b) => a + b, 0);
}
function total() {
  return Object.entries(carrinho).reduce(
    (s, [id, q]) => s + (produtos[id]?.priceCents || 0) * q,
    0,
  );
}
function add(id) {
  if (produtos[id]) {
    carrinho[id] = (carrinho[id] || 0) + 1;
    atualizarInterface();
    atualizar();
  }
}
function sub(id) {
  if (carrinho[id] && !--carrinho[id]) delete carrinho[id];
  atualizarInterface();
  atualizar();
}
function del(id) {
  delete carrinho[id];
  atualizarInterface();
  atualizar();
}
function atualizarInterface() {
  document.querySelectorAll(".produto").forEach((b) => {
    let id = b.dataset.bebida,
      q = carrinho[id] || 0,
      a = b.querySelector(".produto-add");
    b.classList.toggle("selecionada", q > 0);
    a.classList.toggle("tem-quantidade", q > 0);
    a.innerHTML = q
      ? `<span class="quantidade"><button type="button" data-minus="${id}">−</button><span>${q}</span><button type="button" data-plus="${id}">+</button></span>`
      : '<span class="icone-adicionar">+</span>';
  });
}
function atualizar() {
  let n = qtd(),
    v = total();
  barraCarrinho.classList.toggle("tem-itens", n > 0);
  resumoCarrinho.textContent = n
    ? `${n} ${n === 1 ? "Item" : "Itens"} • ${fmt(v)}`
    : "Carrinho vazio";
  if (n > 0 && ultimoTotalCarrinho === 0) {
    barraCarrinho.classList.remove("aparecer-carrinho");
    requestAnimationFrame(() => barraCarrinho.classList.add("aparecer-carrinho"));
  }
  ultimoTotalCarrinho = n;
  botaoCarrinho.disabled = !n;
  listaCarrinho.innerHTML = n
    ? Object.entries(carrinho)
        .map(([id, q]) => {
          let p = produtos[id];
          return `<div class="item-carrinho"><img src="${encodeURI(imagemProduto(p.image))}" alt=""><strong>${p.name}<small>${fmt(p.priceCents)} CADA</small></strong><span class="quantidade"><button type="button" data-cart-minus="${id}">−</button><span>${q}</span><button type="button" data-cart-plus="${id}">+</button></span><b>${fmt(p.priceCents * q)}</b><button class="remover-item" type="button" data-remove="${id}">REMOVER</button></div>`;
        })
        .join("")
    : "<div class='carrinho-vazio'>NENHUMA BEBIDA ADICIONADA.</div>";
  $("#total-carrinho").textContent = fmt(v);
}
function abrir() {
  atualizar();
  carrinhoModal.classList.add("visivel");
  document.body.classList.add("carrinho-aberto");
}
function fechar() {
  carrinhoModal.classList.remove("visivel");
  document.body.classList.remove("carrinho-aberto");
}
async function sair() {
  clearTimeout(retornoLogin);
  clearInterval(intervaloObrigado);
  carrinho = {};
  obrigado.classList.remove("visivel");
  fechar();
  $("#confirmar-carrinho").disabled = false;
  if (auth && !demo) await signOut(auth);
  usuarioAtual = perfilAtual = null;
  tela("tela-login");
}
function obrigadoTela(v) {
  const p = obrigado.querySelector(".obrigado-conteudo p");
  p.textContent =
    "Sua escolha foi registrada. A geladeira será aberta em instantes.";
  p.classList.remove("geladeira-aberta", "geladeira-trancada");
  $("#valor-final").textContent = fmt(v);
  $("#contador").classList.add("oculto");
  obrigado.classList.add("visivel");
  retornoLogin = setTimeout(() => {
    let restante = 10;
    p.textContent = "Geladeira aberta. Destravada por 10 segundos.";
    p.classList.add("geladeira-aberta");
    $("#contador").textContent = restante;
    $("#contador").classList.remove("oculto");
    intervaloObrigado = setInterval(() => {
      restante -= 1;
      if (restante <= 0) {
        clearInterval(intervaloObrigado);
        $("#contador").classList.add("oculto");
        p.textContent = "Geladeira trancada.";
        p.classList.remove("geladeira-aberta");
        p.classList.add("geladeira-trancada");
        return;
      }
      $("#contador").textContent = restante;
    }, 1000);
  }, 6000);
}
async function sheets(orderId, items, totalCents) {
  let idToken = await usuarioAtual.getIdToken();
  await fetch(sheetsEndpoint, {
    method: "POST",
    mode: "no-cors",
    headers: { "Content-Type": "text/plain;charset=UTF-8" },
    body: JSON.stringify({
      orderId,
      uid: usuarioAtual.uid,
      idToken,
      fullName: perfilAtual.fullName,
      items,
      totalCents,
    }),
  });
}
async function enviar(b) {
  if (!usuarioAtual || !qtd()) return;
  b.disabled = true;
  load(true, "Registrando pedido...");
  try {
    let items = Object.entries(carrinho).map(([id, quantity]) => ({
        id,
        drink: produtos[id].name,
        quantity,
        priceCents: produtos[id].priceCents,
        subtotalCents: produtos[id].priceCents * quantity,
      })),
      v = total();
    if (!demo) {
      let pedido = push(ref(db, "orders"));
      await sheets(pedido.key, items, v);
      const dadosPedido = {
        uid: usuarioAtual.uid,
        username: perfilAtual.username,
        fullName: perfilAtual.fullName,
        items,
        totalItems: qtd(),
        totalCents: v,
        status: "pending",
        // O ESP32 usa este horário para iniciar a abertura junto com a tela.
        unlockAt: Date.now() + 6000,
        createdAt: serverTimestamp(),
      };
      await update(ref(db), {
        [`orders/${pedido.key}`]: dadosPedido,
        [`userOrders/${usuarioAtual.uid}/${pedido.key}`]: {
          uid: usuarioAtual.uid,
          items,
          totalCents: v,
          createdAt: serverTimestamp(),
        },
      });
    }
    fechar();
    obrigadoTela(v);
  } catch (e) {
    alert(erro(e));
    b.disabled = false;
  } finally {
    load(false);
  }
}
$("#form-login").addEventListener("submit", async (e) => {
  e.preventDefault();
  erroLogin.textContent = "";
  let u = $("#usuario").value,
    s = $("#senha").value;
  if (demo && u.trim().toLowerCase() === "admin" && s === "adauto26@") {
    usuarioAtual = { uid: "demo" };
    perfilAtual = { username: "admin", fullName: "ADMIN" };
    bebidas();
    return;
  }
  load(true, "Entrando...");
  try {
    let c;
    try {
      const conta = await servicoAuth("/login", {
        username: normalizarUsuario(u),
        password: s,
      });
      c = await signInWithEmailAndPassword(auth, conta.email, s);
    } catch (falhaServico) {
      // Compatibilidade temporária com contas antigas, que ainda usavam e-mail interno.
      c = await signInWithEmailAndPassword(auth, identificadorLogin(u), s);
    }
    usuarioAtual = c.user;
    perfilAtual = await perfil(c.user.uid);
    $("#form-login").reset();
    await prosseguirAposLogin();
  } catch (e) {
    erroLogin.textContent = erro(e);
  } finally {
    load(false);
  }
});
$("#mostrar-senha").onclick = () => {
  let c = $("#senha");
  c.type = c.type === "text" ? "password" : "text";
  const visivel = c.type === "text";
  $("#mostrar-senha").setAttribute("aria-pressed", String(visivel));
  $("#mostrar-senha").setAttribute(
    "aria-label",
    visivel ? "OCULTAR SENHA" : "MOSTRAR SENHA",
  );
  c.focus();
};
$("#abrir-cadastro").onclick = () => tela("tela-cadastro");
$("#voltar-login").onclick = () => tela("tela-login");
$("#abrir-recuperacao").onclick = () => {
  erroRecuperacao.textContent = "";
  erroRecuperacao.classList.remove("sucesso");
  $("#form-recuperacao").reset();
  tela("tela-recuperacao");
};
$("#voltar-recuperacao").onclick = () => tela("tela-login");
$("#sair-atualizar-email").onclick = async () => {
  await signOut(auth);
  tela("tela-login");
};
$("#form-atualizar-email").addEventListener("submit", async (e) => {
  e.preventDefault();
  erroAtualizarEmail.textContent = "";
  try {
    const endereco = emailValido($("#atualizar-email").value);
    load(true, "Atualizando e-mail...");
    auth.languageCode = "pt-BR";
    await verifyBeforeUpdateEmail(usuarioAtual, endereco, {
      url: `${location.origin}${location.pathname.replace(/index\\.html$/, "")}reset.html`,
      handleCodeInApp: true,
    });
    erroAtualizarEmail.classList.add("sucesso");
    erroAtualizarEmail.textContent = "Enviamos um link para confirmar o e-mail. Abra-o para concluir.";
    tela("tela-login");
  } catch (e) {
    erroAtualizarEmail.classList.remove("sucesso");
    erroAtualizarEmail.textContent = erro(e);
  } finally {
    load(false);
  }
});
$("#form-recuperacao").addEventListener("submit", async (e) => {
  e.preventDefault();
  erroRecuperacao.textContent = "";
  try {
    const endereco = emailValido($("#rec-email").value);
    load(true, "Enviando e-mail...");
    auth.languageCode = "pt-BR";
    await sendPasswordResetEmail(auth, endereco);
    erroRecuperacao.classList.add("sucesso");
    erroRecuperacao.textContent = "Se o e-mail estiver cadastrado, o link de recuperação foi enviado.";
  } catch (e) {
    erroRecuperacao.classList.remove("sucesso");
    erroRecuperacao.textContent = erro(e);
  } finally {
    load(false);
  }
});
$("#cad-telefone").oninput = (e) =>
  (e.target.value = e.target.value.replace(/\D/g, "").slice(0, 11));
$("#cad-cpf").oninput = cpfMask;
$("#form-cadastro").addEventListener("submit", async (e) => {
  e.preventDefault();
  erroCadastro.textContent = "";
  try {
    let fullName = $("#cad-nome").value.trim(),
      username = normalizarUsuario($("#cad-usuario").value),
      phone = $("#cad-telefone").value.replace(/\D/g, ""),
      cpf = $("#cad-cpf").value.replace(/\D/g, ""),
      contactEmail = emailValido($("#cad-email").value),
      senha = $("#cad-senha").value;
    if (fullName.length < 3) throw Error("Informe o nome completo.");
    if (phone.length < 10) throw Error("Informe o celular com DDD.");
    if (!cpfOk(cpf)) throw Error("Informe um CPF válido.");
    if (senha.length < 6)
      throw Error("A senha precisa ter ao menos 6 caracteres.");
    let c,
      perfilGravado = false,
      nomeRegistrado = false;
    try {
      c = await createUserWithEmailAndPassword(auth, contactEmail, senha);
      await set(ref(db, `users/${c.user.uid}`), {
        username,
        fullName,
        phone,
        cpf,
        email: contactEmail,
        createdAt: serverTimestamp(),
      });
      perfilGravado = true;
      await registrarNomeUsuario(username, c.user);
      nomeRegistrado = true;
    } catch (falha) {
      if (c?.user) {
        const idToken = await c.user.getIdToken().catch(() => null);
        if (nomeRegistrado && idToken)
          await servicoAuth("/unregister", { username, idToken }).catch(() => {});
        if (perfilGravado)
          await remove(ref(db, `users/${c.user.uid}`)).catch(() => {});
        await deleteUser(c.user).catch(() => {});
      }
      throw falha;
    }
    usuarioAtual = c.user;
    perfilAtual = { username, fullName, email: contactEmail };
    $("#form-cadastro").reset();
    await bebidas();
  } catch (e) {
    erroCadastro.textContent = erro(e);
  }
});
$("#lista-bebidas").onclick = (e) => {
  let m = e.target.closest("[data-minus]"),
    p = e.target.closest("[data-plus]"),
    b = e.target.closest(".produto");
  if (m) {
    e.stopPropagation();
    sub(m.dataset.minus);
  } else if (p) {
    e.stopPropagation();
    add(p.dataset.plus);
  } else if (b) add(b.dataset.bebida);
};
$("#fechar-carrinho").onclick = fechar;
$("#limpar-carrinho").onclick = () => {
  carrinho = {};
  atualizarInterface();
  atualizar();
};
listaCarrinho.onclick = (e) => {
  let m = e.target.closest("[data-cart-minus]"),
    p = e.target.closest("[data-cart-plus]"),
    r = e.target.closest("[data-remove]");
  if (m) sub(m.dataset.cartMinus);
  if (p) add(p.dataset.cartPlus);
  if (r) del(r.dataset.remove);
};
botaoCarrinho.onclick = abrir;
$("#confirmar-carrinho").onclick = () => enviar($("#confirmar-carrinho"));
$("#botao-sair").onclick = sair;
$("#botao-historico").onclick = abrirHistorico;
$("#voltar-historico").onclick = () => tela("tela-bebidas");
$("#mes-anterior").onclick = () => { mesHistorico.setMonth(mesHistorico.getMonth() - 1); renderCalendarioHistorico(); };
$("#mes-proximo").onclick = () => { mesHistorico.setMonth(mesHistorico.getMonth() + 1); renderCalendarioHistorico(); };
$("#calendario-historico").onclick = (e) => {
  const botao = e.target.closest("[data-dia]");
  if (!botao) return;
  diaHistorico = botao.dataset.dia;
  renderCalendarioHistorico();
  renderDetalhesHistorico();
};
$("#copiar-pix").onclick = async () => {
  try {
    await navigator.clipboard.writeText(PIX);
  } catch {}
};
$("#copiar-pix-historico").onclick = async () => {
  try {
    await navigator.clipboard.writeText(PIX);
  } catch {}
};
let f = document.createElement("button");
f.id = "fechar-obrigado";
f.textContent = "×";
obrigado.prepend(f);
f.onclick = sair;
try {
  let app = initializeApp(firebaseConfig);
  auth = getAuth(app);
  db = getDatabase(app);
} catch (e) {
  erroLogin.textContent = e.message;
}
clima();
setInterval(clima, 600000);
