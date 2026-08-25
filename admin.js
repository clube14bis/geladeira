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
  remove,
} from "https://www.gstatic.com/firebasejs/12.17.0/firebase-database.js";
import { firebaseConfig, loginDomain, authServiceUrl } from "./firebase-config.js?v=1.5.2";
import {
  produtosIniciais,
  ordemCategorias,
  imagemProduto,
  slugProduto,
} from "./catalogo-base.js?v=1.2.3";

const $ = (s) => document.querySelector(s);
const ADMIN_UID = "IxZWx1TMk8VsTdEdCVUltFd96a93";
let auth,
  db,
  catalogo = {},
  categoriasOrdenadas = [...ordemCategorias];
function email(usuario) {
  return `${usuario.trim().toLowerCase()}@${loginDomain}`;
}
function normalizarUsuario(usuario) {
  const nome = usuario.trim().toLowerCase();
  if (!/^[a-z0-9._-]{3,24}$/.test(nome)) throw Error("INFORME UM NOME DE USUÁRIO VÁLIDO.");
  return nome;
}
async function entrarComUsuario(usuario, senha) {
  const nome = normalizarUsuario(usuario);
  try {
    const resposta = await fetch(`${authServiceUrl}/login`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ username: nome, password: senha }),
    });
    const dados = await resposta.json().catch(() => ({}));
    if (!resposta.ok) throw Error(dados.error || "INVALID_CREDENTIALS");
    return await signInWithEmailAndPassword(auth, dados.email, senha);
  } catch {
    // Mantém compatibilidade até o Admin confirmar o novo e-mail.
    return await signInWithEmailAndPassword(auth, email(nome), senha);
  }
}
// Aceita valores brasileiros com separadores de milhar, por exemplo:
// 550.000.000.000,00. O valor em centavos permanece seguro no JavaScript.
function paraCentavos(valor) {
  let texto = String(valor ?? "").trim().replace(/\s|R\$/gi, "");
  if (!texto) return null;
  if (texto.includes(",")) texto = texto.replace(/\./g, "").replace(",", ".");
  else if ((texto.match(/\./g) || []).length > 1) texto = texto.replace(/\./g, "");
  const reais = Number(texto);
  if (!Number.isFinite(reais) || reais < 0) return null;
  const centavos = Math.round(reais * 100);
  return Number.isSafeInteger(centavos) ? centavos : null;
}
function paraEstoque(valor) {
  const estoque = Number(String(valor ?? "").trim());
  return Number.isSafeInteger(estoque) && estoque >= 0 && estoque <= 1000000
    ? estoque
    : null;
}
function mostrarPainel(ativo) {
  $("#login-admin").hidden = ativo;
  $("#painel-admin").hidden = !ativo;
}
function ordenarCategorias(ordenacao = []) {
  return [...new Set([...ordenacao, ...ordemCategorias, ...Object.values(catalogo).map((p) => p.category)])]
    .filter(Boolean);
}
function produtosDaCategoria(categoria) {
  return Object.values(catalogo)
    .filter((p) => p.category === categoria)
    .sort((a, b) => (+a.sortOrder || 0) - (+b.sortOrder || 0) || a.name.localeCompare(b.name));
}
function botoesOrdem(atributo, valor) {
  return `<span class="ordem-controles"><button type="button" data-${atributo}="${valor}" data-direcao="-1" aria-label="MOVER PARA CIMA">↑</button><button type="button" data-${atributo}="${valor}" data-direcao="1" aria-label="MOVER PARA BAIXO">↓</button></span>`;
}
function render() {
  $("#produtos-admin").innerHTML = categoriasOrdenadas
    .map((cat) =>
      !produtosDaCategoria(cat).length
        ? ""
        : `<section class="grupo-admin"><h2><span>${cat}</span>${botoesOrdem("mover-categoria", cat)}</h2>${produtosDaCategoria(cat).map((p) => `<article class="linha-produto" data-id="${p.id}"><img src="${encodeURI(imagemProduto(p.image))}" alt=""><div class="nome-produto"><span>${p.name}</span>${botoesOrdem("mover-produto", p.id)}</div><label class="chave"><input class="ativo" type="checkbox" ${p.enabled ? "checked" : ""}><span>EXIBIR</span></label><label class="preco-campo">PREÇO<input class="preco" inputmode="decimal" value="${((p.priceCents || 0) / 100).toFixed(2).replace(".", ",")}" aria-label="PREÇO ${p.name}"></label><label class="preco-campo">ESTOQUE<input class="estoque" type="number" min="0" step="1" inputmode="numeric" value="${Number.isSafeInteger(+p.stock) && +p.stock >= 0 ? +p.stock : 0}" aria-label="ESTOQUE ${p.name}"></label><button class="remover-produto" type="button" data-remover="${p.id}" aria-label="REMOVER ${p.name}">×</button></article>`).join("")}</section>`,
    )
    .join("");
}
async function carregar() {
  let [catalogoAtual, configuracao] = await Promise.all([
    get(ref(db, "catalog")),
    get(ref(db, "catalogConfig")),
  ]);
  let atual = catalogoAtual.val();
  const base = produtosIniciais();
  if (!atual) {
    await update(ref(db, "catalog"), base);
    atual = base;
  }
  catalogo = Object.fromEntries(
    Object.entries({ ...base, ...atual }).map(([id, p], indice) => [
      id,
      {
        ...base[id],
        ...p,
        id,
        sortOrder: Number.isFinite(+p.sortOrder) ? +p.sortOrder : indice,
      },
    ]),
  );
  categoriasOrdenadas = ordenarCategorias(configuracao.val()?.categoryOrder);
  categoriasOrdenadas.forEach((cat) =>
    produtosDaCategoria(cat).forEach((p, indice) => (p.sortOrder = indice)),
  );
  render();
}
async function salvar() {
  const atualizacoes = {};
  let erroPreco = "";
  document.querySelectorAll(".linha-produto").forEach((l) => {
    const id = l.dataset.id;
    const cents = paraCentavos(l.querySelector(".preco").value);
    const stock = paraEstoque(l.querySelector(".estoque").value);
    if (cents === null) {
      erroPreco = `PREÇO INVÁLIDO PARA ${catalogo[id].name}.`;
      return;
    }
    if (stock === null) {
      erroPreco = `ESTOQUE INVÁLIDO PARA ${catalogo[id].name}.`;
      return;
    }
    catalogo[id] = {
      ...catalogo[id],
      enabled: l.querySelector(".ativo").checked,
      priceCents: cents,
      stock,
    };
    const { id: campoTecnico, ...produtoParaSalvar } = catalogo[id];
    atualizacoes[`catalog/${id}`] = produtoParaSalvar;
  });
  if (erroPreco) {
    $("#mensagem-admin").textContent = erroPreco;
    return;
  }
  atualizacoes["catalogConfig/categoryOrder"] = categoriasOrdenadas;
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
    const c = await entrarComUsuario($("#admin-usuario").value, $("#admin-senha").value);
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
  const moverCategoria = e.target.closest("[data-mover-categoria]");
  if (moverCategoria) {
    const indice = categoriasOrdenadas.indexOf(moverCategoria.dataset.moverCategoria);
    const destino = indice + Number(moverCategoria.dataset.direcao);
    if (indice >= 0 && destino >= 0 && destino < categoriasOrdenadas.length) {
      [categoriasOrdenadas[indice], categoriasOrdenadas[destino]] = [categoriasOrdenadas[destino], categoriasOrdenadas[indice]];
      $("#mensagem-admin").textContent = "ORDEM ALTERADA. CLIQUE EM SALVAR ALTERAÇÕES.";
      render();
    }
    return;
  }
  const moverProduto = e.target.closest("[data-mover-produto]");
  if (moverProduto) {
    const produto = catalogo[moverProduto.dataset.moverProduto];
    const lista = produtosDaCategoria(produto?.category);
    const indice = lista.findIndex((p) => p.id === produto?.id);
    const destino = indice + Number(moverProduto.dataset.direcao);
    if (indice >= 0 && destino >= 0 && destino < lista.length) {
      [lista[indice], lista[destino]] = [lista[destino], lista[indice]];
      lista.forEach((p, posicao) => (catalogo[p.id].sortOrder = posicao));
      $("#mensagem-admin").textContent = "ORDEM ALTERADA. CLIQUE EM SALVAR ALTERAÇÕES.";
      render();
    }
    return;
  }
  const botao = e.target.closest("[data-remover]");
  if (!botao) return;
  const id = botao.dataset.remover;
  const produto = catalogo[id];
  if (!produto || !confirm(`REMOVER ${produto.name}?`)) return;
  try {
    await remove(ref(db, `catalog/${id}`));
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
    priceCents = paraCentavos($("#novo-preco").value),
    stock = paraEstoque($("#novo-estoque").value),
    id = slugProduto(name);
  if (!name) {
    $("#mensagem-admin").textContent = "INFORME O NOME DO NOVO PRODUTO.";
    return;
  }
  if (priceCents === null) {
    $("#mensagem-admin").textContent = "INFORME UM PREÇO VÁLIDO.";
    return;
  }
  if (stock === null) {
    $("#mensagem-admin").textContent = "INFORME UMA QUANTIDADE VÁLIDA.";
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
    stock,
    sortOrder: produtosDaCategoria(category).length,
  };
  ["#novo-nome", "#nova-imagem", "#novo-preco"].forEach(
    (s) => ($(s).value = ""),
  );
  $("#novo-estoque").value = "0";
  $("#mensagem-admin").textContent =
    "PRODUTO ADICIONADO. CLIQUE EM SALVAR ALTERAÇÕES PARA GRAVAR.";
  render();
});
