import { initializeApp } from "https://www.gstatic.com/firebasejs/12.17.0/firebase-app.js";
import { getAuth, createUserWithEmailAndPassword, signInWithEmailAndPassword, signOut } from "https://www.gstatic.com/firebasejs/12.17.0/firebase-auth.js";
import { getDatabase, ref, set, get, push, serverTimestamp } from "https://www.gstatic.com/firebasejs/12.17.0/firebase-database.js";
import { firebaseConfig, loginDomain } from "./firebase-config.js";

const categorias = {
  "Águas": {emoji:"💧", bebidas:["Água Natural","Água com gás"]},
  "Refrigerantes": {emoji:"🥤", bebidas:["Coca Cola","Coca Cola Zero","Guaraná Antártica","Guaraná Poty","Guaraná Cotuba","Fanta","Pepsi","Sprite","Schweppes","H20"]},
  "Cervejas": {emoji:"🍺", bebidas:["Cerveja Skol","Cerveja Brahma","Cerveja Heineken","Cerveja Amstel","Cerveja Budweiser","Cerveja Stella Artois","Cerveja Itaipava","Cerveja Antarctica","Cerveja Corona","Cerveja Eisenbahn","Cerveja Petra","Cerveja Kaiser","Cerveja Schin","Cerveja Devassa","Cerveja Bohemia","Cerveja Spaten","Cerveja Beck's","Cerveja Baden Baden","Cerveja Colorado","Cerveja Crystal"]},
  "Sucos": {emoji:"🧃", bebidas:["Suco Laranja","Suco Del Valle","Suco Natural One","Suco Tang","Suco AdeS"]}
};

const $ = s => document.querySelector(s);
const telas = document.querySelectorAll(".tela");
const erroLogin = $("#erro-login");
const erroCadastro = $("#erro-cadastro");
const aviso = $("#aviso");
const carrinhoModal = $("#carrinho");
const listaCarrinho = $("#lista-carrinho");
const botaoCarrinho = $("#botao-concluir");
const resumoCarrinho = $("#resumo-carrinho");

let usuarioAtual = null;
let perfilAtual = null;
let carrinho = {};
let retornoLogin = null;
let app, auth, db;

function validarConfiguracao(){
  if(!firebaseConfig || !firebaseConfig.apiKey || !firebaseConfig.databaseURL){
    throw new Error("Configure o arquivo firebase-config.js.");
  }
}
function normalizarUsuario(valor){
  const usuario = valor.trim().toLowerCase();
  if(!/^[a-z0-9._-]{3,24}$/.test(usuario)) throw new Error("Use 3 a 24 caracteres: letras, números, ponto, hífen ou _.");
  return usuario;
}
function emailInterno(usuario){ return `${normalizarUsuario(usuario)}@${loginDomain}`; }
function mostrarTela(id){ telas.forEach(t => t.classList.toggle("ativa", t.id === id)); }
function mensagemErro(erro){
  const mapa={"auth/invalid-credential":"Usuário ou senha incorretos.","auth/user-not-found":"Usuário ou senha incorretos.","auth/wrong-password":"Usuário ou senha incorretos.","auth/email-already-in-use":"Este nome de usuário já existe.","auth/weak-password":"Use uma senha com pelo menos 6 caracteres.","auth/network-request-failed":"Verifique sua conexão com a internet."};
  return mapa[erro.code] || erro.message || "Não foi possível concluir a operação.";
}
async function carregarPerfil(uid){
  const snapshot = await get(ref(db,`users/${uid}`));
  if(!snapshot.exists()) throw new Error("Cadastro não encontrado.");
  return snapshot.val();
}

function exibirBebidas(){
  const container = $("#lista-bebidas");
  container.innerHTML = Object.entries(categorias).map(([categoria,dados])=>`
    <section class="categoria"><h2>${categoria}</h2><div class="bebidas">
      ${dados.bebidas.map(bebida=>`
        <button class="bebida" type="button" data-bebida="${bebida}">
          <span class="icone-bebida">${dados.emoji}</span>
          <span class="bebida-nome">${bebida}</span>
          <span class="bebida-add" data-add="${bebida}">+</span>
        </button>`).join("")}
    </div></section>`).join("");
  carrinho={}; atualizarCarrinho(); mostrarTela("tela-bebidas");
}
function totalItens(){ return Object.values(carrinho).reduce((t,q)=>t+q,0); }
function adicionarBebida(bebida){ carrinho[bebida]=(carrinho[bebida]||0)+1; atualizarInterfaceBebidas(); atualizarCarrinho(); }
function removerBebida(bebida){ if(!carrinho[bebida]) return; carrinho[bebida]--; if(carrinho[bebida]<=0) delete carrinho[bebida]; atualizarInterfaceBebidas(); atualizarCarrinho(); }

function atualizarInterfaceBebidas(){
  document.querySelectorAll(".bebida").forEach(botao=>{
    const bebida=botao.dataset.bebida, quantidade=carrinho[bebida]||0, add=botao.querySelector(".bebida-add");
    if(quantidade>0){
      botao.classList.add("selecionada");
      add.innerHTML=`<div class="quantidade"><button type="button" data-minus="${bebida}">−</button><span>${quantidade}</span><button type="button" data-plus="${bebida}">+</button></div>`;
    } else { botao.classList.remove("selecionada"); add.innerHTML="+"; }
  });
}
function atualizarCarrinho(){
  const total=totalItens();
  resumoCarrinho.textContent=total===0?"Carrinho vazio":`${total} item${total>1?"s":""} no carrinho`;
  botaoCarrinho.disabled=total===0;
  if(total===0){listaCarrinho.innerHTML=`<div class="carrinho-vazio">Nenhuma bebida adicionada.</div>`;return;}
  listaCarrinho.innerHTML=Object.entries(carrinho).map(([bebida,quantidade])=>`
    <div class="item-carrinho"><strong>${bebida}</strong><div class="quantidade">
      <button type="button" data-cart-minus="${bebida}">−</button><span>${quantidade}</span><button type="button" data-cart-plus="${bebida}">+</button>
    </div></div>`).join("");
}
function abrirCarrinho(){ atualizarCarrinho(); carrinhoModal.classList.add("visivel"); carrinhoModal.setAttribute("aria-hidden","false"); }
function fecharCarrinho(){ carrinhoModal.classList.remove("visivel"); carrinhoModal.setAttribute("aria-hidden","true"); }

$("#form-login").addEventListener("submit",async e=>{
  e.preventDefault(); erroLogin.textContent="";
  try{
    const cred=await signInWithEmailAndPassword(auth,emailInterno($("#usuario").value),$("#senha").value);
    usuarioAtual=cred.user; perfilAtual=await carregarPerfil(usuarioAtual.uid); e.currentTarget.reset(); exibirBebidas();
  }catch(err){erroLogin.textContent=mensagemErro(err);}
});
$("#abrir-cadastro").addEventListener("click",()=>{erroCadastro.textContent="";mostrarTela("tela-cadastro");});
$("#voltar-login").addEventListener("click",()=>mostrarTela("tela-login"));

$("#form-cadastro").addEventListener("submit",async e=>{
  e.preventDefault(); erroCadastro.textContent="";
  try{
    const nome=$("#cad-nome").value.trim(), usuario=normalizarUsuario($("#cad-usuario").value), senha=$("#cad-senha").value;
    if(nome.length<3) throw new Error("Informe o nome completo.");
    const cred=await createUserWithEmailAndPassword(auth,`${usuario}@${loginDomain}`,senha);
    await set(ref(db,`users/${cred.user.uid}`),{username:usuario,fullName:nome,createdAt:serverTimestamp()});
    usuarioAtual=cred.user; perfilAtual={username:usuario,fullName:nome}; e.currentTarget.reset(); exibirBebidas();
  }catch(err){erroCadastro.textContent=mensagemErro(err);}
});

$("#lista-bebidas").addEventListener("click",e=>{
  const menos=e.target.closest("[data-minus]"), mais=e.target.closest("[data-plus]"), botao=e.target.closest(".bebida");
  if(menos){e.stopPropagation();removerBebida(menos.dataset.minus);return;}
  if(mais){e.stopPropagation();adicionarBebida(mais.dataset.plus);return;}
  if(botao) adicionarBebida(botao.dataset.bebida);
});
botaoCarrinho.addEventListener("click",abrirCarrinho);
$("#fechar-carrinho").addEventListener("click",fecharCarrinho);
$("#limpar-carrinho").addEventListener("click",()=>{carrinho={};atualizarInterfaceBebidas();atualizarCarrinho();});
listaCarrinho.addEventListener("click",e=>{
  const menos=e.target.closest("[data-cart-minus]"), mais=e.target.closest("[data-cart-plus]");
  if(menos){removerBebida(menos.dataset.cartMinus);return;}
  if(mais) adicionarBebida(mais.dataset.cartPlus);
});

$("#confirmar-carrinho").addEventListener("click",async()=>{
  if(!usuarioAtual||totalItens()===0)return;
  const botao=$("#confirmar-carrinho"); botao.disabled=true;
  try{
    const items=Object.entries(carrinho).map(([drink,quantity])=>({drink,quantity}));
    const pedido=push(ref(db,"orders"));
    await set(pedido,{uid:usuarioAtual.uid,username:perfilAtual.username,fullName:perfilAtual.fullName,items,totalItems:totalItens(),status:"pending",createdAt:serverTimestamp()});
    fecharCarrinho(); aviso.classList.add("visivel","abrindo");
    $("#aviso-titulo").textContent="Pedido enviado";
    $("#aviso-texto").textContent="Aguarde a liberação da porta da geladeira.";
    retornoLogin=setTimeout(async()=>{await signOut(auth);aviso.classList.remove("visivel","abrindo");carrinho={};mostrarTela("tela-login");},7000);
  }catch(err){alert(mensagemErro(err));botao.disabled=false;}
});
$("#botao-sair").addEventListener("click",async()=>{if(auth)await signOut(auth);carrinho={};mostrarTela("tela-login");});
$("#fechar-aviso").addEventListener("click",async()=>{clearTimeout(retornoLogin);if(auth)await signOut(auth);aviso.classList.remove("visivel","abrindo");carrinho={};mostrarTela("tela-login");});

const botaoTema=$("#botao-tema"), iconeTema=$("#icone-tema");
function aplicarTema(tema){
  if(tema==="dark"){document.documentElement.classList.add("dark");iconeTema.innerHTML=`<path d="M21 12.8A8.5 8.5 0 1 1 11.2 3A6.7 6.7 0 0 0 21 12.8Z"/>`;}
  else{document.documentElement.classList.remove("dark");iconeTema.innerHTML=`<circle cx="12" cy="12" r="4"/><path d="M12 2v2"/><path d="M12 20v2"/><path d="m4.93 4.93 1.41 1.41"/><path d="m17.66 17.66 1.41 1.41"/><path d="M2 12h2"/><path d="M20 12h2"/><path d="m6.34 17.66-1.41 1.41"/><path d="m19.07 4.93-1.41 1.41"/>`;}
}
aplicarTema(localStorage.getItem("clube14bis-tema")==="dark"?"dark":"light");
botaoTema.addEventListener("click",()=>{const novo=document.documentElement.classList.contains("dark")?"light":"dark";aplicarTema(novo);localStorage.setItem("clube14bis-tema",novo);});

function iconeClimaSVG(c){
  if(c===0)return `<svg class="weather-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M6.34 17.66l-1.41 1.41M19.07 4.93l-1.41 1.41"/></svg>`;
  if(c===1||c===2)return `<svg class="weather-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="9" cy="9" r="3.2"/><path d="M9 2v2M3.5 5.5L5 7"/><path d="M17 18H7a4 4 0 1 1 1-7.87A5 5 0 0 1 17 13.5A2.5 2.5 0 0 1 17 18Z"/></svg>`;
  if(c===3)return `<svg class="weather-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M18 18H7a4 4 0 1 1 1-7.87A5 5 0 0 1 18 13.5A2.5 2.5 0 0 1 18 18Z"/></svg>`;
  if(c>=51&&c<=82)return `<svg class="weather-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M18 14H7a4 4 0 1 1 1-7.87A5 5 0 0 1 18 9.5A2.5 2.5 0 0 1 18 14Z"/><path d="M8 17l-1 3M13 17l-1 3M18 17l-1 3"/></svg>`;
  if(c>=95)return `<svg class="weather-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><path d="M18 14H7a4 4 0 1 1 1-7.87A5 5 0 0 1 18 9.5A2.5 2.5 0 0 1 18 14Z"/><path d="M13 14l-3 5h3l-2 4 6-7h-3l2-2"/></svg>`;
  return `<svg class="weather-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor"><circle cx="12" cy="12" r="8"/></svg>`;
}
function direcaoVento(g){return ["N","NE","L","SE","S","SO","O","NO"][Math.round(g/45)%8];}
function bussolaSVG(g){return `<svg class="wind-compass" viewBox="0 0 50 50"><circle cx="25" cy="25" r="21" fill="none" stroke="currentColor" stroke-opacity=".18"/><circle cx="25" cy="25" r="16" fill="none" stroke="currentColor" stroke-opacity=".08"/><path d="M25 5v4M25 41v4M5 25h4M41 25h4" stroke="currentColor" stroke-opacity=".35"/><text x="25" y="8" text-anchor="middle" font-size="5" fill="currentColor">N</text><text x="25" y="47" text-anchor="middle" font-size="5" fill="currentColor">S</text><text x="43" y="27" text-anchor="middle" font-size="5" fill="currentColor">L</text><text x="7" y="27" text-anchor="middle" font-size="5" fill="currentColor">O</text><g class="wind-arrow" style="transform:rotate(${g}deg)"><path d="M25 10L29 25L25 22L21 25Z" fill="currentColor"/><circle cx="25" cy="25" r="2.5" fill="currentColor"/></g></svg>`;}

async function carregarClima(){
  const el=$("#weather");
  try{
    const url="https://api.open-meteo.com/v1/forecast?latitude=-20.817&longitude=-49.520&current=temperature_2m,weather_code,wind_speed_10m,wind_direction_10m&timezone=America%2FSao_Paulo";
    const r=await fetch(url); if(!r.ok)throw new Error("Falha ao consultar clima");
    const d=await r.json(), a=d.current, dir=direcaoVento(a.wind_direction_10m);
    const cond={0:"Céu limpo",1:"Principalmente limpo",2:"Parcialmente nublado",3:"Nublado",45:"Neblina",48:"Neblina",51:"Garoa",53:"Garoa",55:"Garoa",61:"Chuva",63:"Chuva",65:"Chuva forte",80:"Pancadas",81:"Pancadas",82:"Pancadas fortes",95:"Trovoada"};
    el.innerHTML=`<span class="weather-brand">Clube 14 Bis</span><span class="weather-separator">·</span><span>Mirassol</span><span class="weather-separator">·</span>${iconeClimaSVG(a.weather_code)}<span class="weather-temperature">${Math.round(a.temperature_2m)}°C</span><span class="weather-separator">·</span><span class="weather-wind">${bussolaSVG(a.wind_direction_10m)}vento ${Math.round(a.wind_speed_10m)} km/h ${dir}</span>`;
    el.title=cond[a.weather_code]||"Condição variável";
  }catch(e){console.error(e);el.innerHTML=`<span class="weather-brand">Clube 14 Bis</span><span>·</span>Mirassol`;}
}

try{
  validarConfiguracao();
  app=initializeApp(firebaseConfig);
  auth=getAuth(app);
  db=getDatabase(app);
}catch(e){erroLogin.textContent=e.message;}
carregarClima();
