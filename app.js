import { initializeApp } from "https://www.gstatic.com/firebasejs/12.17.0/firebase-app.js";
import {
  getAuth,
  createUserWithEmailAndPassword,
  signInWithEmailAndPassword,
  signOut,
} from "https://www.gstatic.com/firebasejs/12.17.0/firebase-auth.js";
import {
  getDatabase,
  ref,
  set,
  get,
  push,
  serverTimestamp,
} from "https://www.gstatic.com/firebasejs/12.17.0/firebase-database.js";
import {
  firebaseConfig,
  loginDomain,
  sheetsEndpoint,
} from "./firebase-config.js?v=1.2.2";
import {
  ordemCategorias,
  imagemProduto,
  produtosIniciais,
} from "./catalogo-base.js?v=1.2.2";
const $ = (s) => document.querySelector(s),
  telas = document.querySelectorAll(".tela"),
  VERSAO_APP = "V1.2.2",
  PIX = "c9cb7e85-240b-46e5-b500-327844209247",
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
  intervaloObrigado;
const erroLogin = $("#erro-login"),
  erroCadastro = $("#erro-cadastro"),
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
function tela(id) {
  telas.forEach((x) => x.classList.toggle("ativa", x.id === id));
}
function load(a, t = "CARREGANDO...") {
  textoCarregando.textContent = t;
  carregando.classList.toggle("visivel", a);
}
function erro(e) {
  return (
    {
      "auth/invalid-credential": "USUÁRIO OU SENHA INCORRETOS.",
      "auth/email-already-in-use": "ESTE NOME DE USUÁRIO JÁ EXISTE.",
      "auth/weak-password": "A SENHA PRECISA TER AO MENOS 6 CARACTERES.",
      "auth/network-request-failed": "VERIFIQUE SUA CONEXÃO COM A INTERNET.",
    }[e.code] ||
    e.message ||
    "NÃO FOI POSSÍVEL CONCLUIR A OPERAÇÃO."
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
  load(true, "CARREGANDO BEBIDAS...");
  try {
    await catalogo();
    let out = ordemCategorias
      .map((cat) => {
        let ps = itens(cat);
        return !ps.length
          ? ""
          : `<section class="categoria"><h2>${cat}</h2><div class="produtos">${ps.map((p) => `<button class="produto" type="button" data-bebida="${p.id}"><img class="produto-imagem" src="${encodeURI(imagemProduto(p.image))}" alt="${p.name}"><span class="produto-corpo"><span><span class="produto-nome">${p.name}</span><small class="produto-preco">${fmt(p.priceCents)}</small></span><span class="produto-add">+</span></span></button>`).join("")}</div></section>`;
      })
      .join("");
    $("#lista-bebidas").innerHTML =
      out ||
      "<p class='carrinho-vazio'>NENHUM PRODUTO DISPONÍVEL NO MOMENTO.</p>";
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
    a.innerHTML = q
      ? `<span class="quantidade"><button type="button" data-minus="${id}">−</button><span>${q}</span><button type="button" data-plus="${id}">+</button></span>`
      : "+";
  });
}
function atualizar() {
  let n = qtd(),
    v = total();
  barraCarrinho.classList.toggle("tem-itens", n > 0);
  resumoCarrinho.textContent = n
    ? `${n} ITEN${n > 1 ? "S" : ""} • ${fmt(v)}`
    : "CARRINHO VAZIO";
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
}
function fechar() {
  carrinhoModal.classList.remove("visivel");
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
  let s = 5,
    p = obrigado.querySelector(".obrigado-conteudo p");
  p.textContent =
    "SUA ESCOLHA FOI REGISTRADA. A GELADEIRA SERÁ ABERTA EM INSTANTES.";
  $("#valor-final").textContent = fmt(v);
  $("#contador").textContent = s;
  obrigado.classList.add("visivel");
  intervaloObrigado = setInterval(() => {
    $("#contador").textContent = --s;
    if (s <= 0) clearInterval(intervaloObrigado);
  }, 1000);
  retornoLogin = setTimeout(() => {
    p.textContent =
      "GELADEIRA ABERTA.";
    $("#contador").textContent = "✓";
  }, 5000);
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
  load(true, "REGISTRANDO PEDIDO...");
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
      await set(pedido, {
        uid: usuarioAtual.uid,
        username: perfilAtual.username,
        fullName: perfilAtual.fullName,
        items,
        totalItems: qtd(),
        totalCents: v,
        status: "pending",
        createdAt: serverTimestamp(),
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
  load(true, "ENTRANDO...");
  try {
    let c = await signInWithEmailAndPassword(auth, email(u), s);
    usuarioAtual = c.user;
    perfilAtual = await perfil(c.user.uid);
    $("#form-login").reset();
    await bebidas();
  } catch (e) {
    erroLogin.textContent = erro(e);
  } finally {
    load(false);
  }
});
$("#mostrar-senha").onclick = () => {
  let c = $("#senha");
  c.type = c.type === "text" ? "password" : "text";
  c.focus();
};
$("#abrir-cadastro").onclick = () => tela("tela-cadastro");
$("#voltar-login").onclick = () => tela("tela-login");
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
      senha = $("#cad-senha").value;
    if (fullName.length < 3) throw Error("INFORME O NOME COMPLETO.");
    if (phone.length < 10) throw Error("INFORME O CELULAR COM DDD.");
    if (!cpfOk(cpf)) throw Error("INFORME UM CPF VÁLIDO.");
    if (senha.length < 6)
      throw Error("A SENHA PRECISA TER AO MENOS 6 CARACTERES.");
    let c = await createUserWithEmailAndPassword(auth, email(username), senha);
    await set(ref(db, `users/${c.user.uid}`), {
      username,
      fullName,
      phone,
      cpf,
      createdAt: serverTimestamp(),
    });
    usuarioAtual = c.user;
    perfilAtual = { username, fullName };
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
$("#copiar-pix").onclick = async () => {
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
