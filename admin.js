import { initializeApp } from "https://www.gstatic.com/firebasejs/12.17.0/firebase-app.js";
import {
  getAuth,
  signInWithEmailAndPassword,
  signOut,
} from "https://www.gstatic.com/firebasejs/12.17.0/firebase-auth.js";
import {
  getDatabase,
  ref,
  get,
  update,
  set,
} from "https://www.gstatic.com/firebasejs/12.17.0/firebase-database.js";
import { firebaseConfig, loginDomain } from "./firebase-config.js?v=1.2.1";
import {
  produtosIniciais,
  ordemCategorias,
  imagemProduto,
  slugProduto,
} from "./catalogo-base.js?v=1.2.1";

const $ = (s) => document.querySelector(s);
const ADMIN_UID = "IxZWx1TMk8VsTdEdCVUltFd96a93";
let auth,
  db,
  catalogo = {};
function email(usuario) {
  return `${usuario.trim().toLowerCase()}@${loginDomain}`;
}
function mostrarPainel(ativo) {
  $("#login-admin").hidden = ativo;
  $("#painel-admin").hidden = !ativo;
}
function render() {
  const grupos = Object.values(catalogo).reduce(
    (acc, p) => ((acc[p.category] ??= []).push(p), acc),
    {},
  );
  $("#produtos-admin").innerHTML = ordemCategorias
    .map((cat) =>
      !grupos[cat]
        ? ""
        : `<section class="grupo-admin"><h2>${cat}</h2>${grupos[cat].map((p) => `<article class="linha-produto" data-id="${p.id}"><img src="${encodeURI(imagemProduto(p.image))}" alt=""><div class="nome-produto">${p.name}</div><label class="chave"><input class="ativo" type="checkbox" ${p.enabled ? "checked" : ""}><span>EXIBIR</span></label><label class="preco-campo">PREÇO<input class="preco" inputmode="decimal" value="${((p.priceCents || 0) / 100).toFixed(2).replace(".", ",")}" aria-label="PREÇO ${p.name}"></label><button class="remover-produto" type="button" data-remover="${p.id}" aria-label="REMOVER ${p.name}">×</button></article>`).join("")}</section>`,
    )
    .join("");
}
async function carregar() {
  let atual = (await get(ref(db, "catalog"))).val();
  const base = produtosIniciais();
  if (!atual) {
    await update(ref(db, "catalog"), base);
    atual = base;
  }
  catalogo = Object.fromEntries(
    Object.entries({ ...base, ...atual }).map(([id, p]) => [
      id,
      { ...base[id], ...p, id },
    ]),
  );
  render();
}
async function salvar() {
  const atualizacoes = {};
  document.querySelectorAll(".linha-produto").forEach((l) => {
    const id = l.dataset.id;
    const preco = l.querySelector(".preco").value.replace(",", ".");
    const cents = Math.max(0, Math.round(Number(preco) * 100) || 0);
    catalogo[id] = {
      ...catalogo[id],
      enabled: l.querySelector(".ativo").checked,
      priceCents: cents,
    };
    const { id: campoTecnico, ...produtoParaSalvar } = catalogo[id];
    atualizacoes[`catalog/${id}`] = produtoParaSalvar;
  });
  $("#salvar").disabled = true;
  try {
    await update(ref(db), atualizacoes);
    $("#mensagem-admin").textContent =
      "CATÁLOGO SALVO. A PÁGINA DE BEBIDAS JÁ USARÁ ESTES DADOS.";
    render();
  } catch (e) {
    $("#mensagem-admin").textContent = "NÃO FOI POSSÍVEL SALVAR: " + e.message;
  } finally {
    $("#salvar").disabled = false;
  }
}
const app = initializeApp(firebaseConfig);
auth = getAuth(app);
db = getDatabase(app);
$("#form-admin").addEventListener("submit", async (e) => {
  e.preventDefault();
  $("#erro-admin").textContent = "";
  try {
    const c = await signInWithEmailAndPassword(
      auth,
      email($("#admin-usuario").value),
      $("#admin-senha").value,
    );
    let acesso = await get(ref(db, `admins/${c.user.uid}`));
    if (!acesso.exists() && c.user.uid === ADMIN_UID) {
      await set(ref(db, `admins/${c.user.uid}`), true);
      acesso = await get(ref(db, `admins/${c.user.uid}`));
    }
    if (acesso.val() !== true) {
      await signOut(auth);
      throw new Error("ESTA CONTA NÃO TEM ACESSO ADMINISTRATIVO.");
    }
    mostrarPainel(true);
    await carregar();
  } catch (err) {
    $("#erro-admin").textContent =
      err.code === "auth/invalid-credential"
        ? "USUÁRIO OU SENHA INCORRETOS."
        : err.message;
  }
});
$("#salvar").addEventListener("click", salvar);
$("#produtos-admin").addEventListener("click", async (e) => {
  const botao = e.target.closest("[data-remover]");
  if (!botao) return;
  const id = botao.dataset.remover;
  const produto = catalogo[id];
  if (!produto || !confirm(`REMOVER ${produto.name}?`)) return;
  try {
    await update(ref(db), { [`catalog/${id}`]: null });
    delete catalogo[id];
    render();
    $("#mensagem-admin").textContent = "PRODUTO REMOVIDO DO CATÁLOGO.";
  } catch (e) {
    $("#mensagem-admin").textContent = "NÃO FOI POSSÍVEL REMOVER: " + e.message;
  }
});
$("#sair-admin").addEventListener("click", async () => {
  await signOut(auth);
  mostrarPainel(false);
});
$("#adicionar-produto").addEventListener("click", () => {
  const name = $("#novo-nome").value.trim().toUpperCase(),
    category = $("#novo-categoria").value,
    image = $("#nova-imagem").value.trim() || "agua.png",
    priceCents = Math.max(
      0,
      Math.round(Number($("#novo-preco").value.replace(",", ".")) * 100) || 0,
    ),
    id = slugProduto(name);
  if (!name) {
    $("#mensagem-admin").textContent = "INFORME O NOME DO NOVO PRODUTO.";
    return;
  }
  if (catalogo[id]) {
    $("#mensagem-admin").textContent = "JÁ EXISTE UM PRODUTO COM ESTE NOME.";
    return;
  }
  catalogo[id] = {
    id,
    name,
    category,
    image,
    enabled: true,
    priceCents,
    stock: 999,
  };
  ["#novo-nome", "#nova-imagem", "#novo-preco"].forEach(
    (s) => ($(s).value = ""),
  );
  $("#mensagem-admin").textContent =
    "PRODUTO ADICIONADO. CLIQUE EM SALVAR ALTERAÇÕES PARA GRAVAR.";
  render();
});
