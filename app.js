import { initializeApp } from 'https://www.gstatic.com/firebasejs/12.17.0/firebase-app.js';
import { getAuth, createUserWithEmailAndPassword, signInWithEmailAndPassword, signOut } from 'https://www.gstatic.com/firebasejs/12.17.0/firebase-auth.js';
import { getDatabase, ref, set, get, push, serverTimestamp } from 'https://www.gstatic.com/firebasejs/12.17.0/firebase-database.js';
import { firebaseConfig, loginDomain } from './firebase-config.js';

const categorias = {
  'Águas': { classe: 'agua', emoji: '💧', bebidas: ['Água Natural', 'Água com gás'] },
  'Refrigerantes': { classe: 'refrigerante', emoji: '🥤', bebidas: ['Coca Cola', 'Coca Cola Zero', 'Guaraná Antártica', 'Guaraná Poty', 'Guaraná Cotuba', 'Fanta', 'Pepsi', 'Sprite', 'Schweppes', 'H20'] },
  'Cervejas': { classe: 'cerveja', emoji: '🍺', bebidas: ['Cerveja Skol', 'Cerveja Brahma', 'Cerveja Heineken', 'Cerveja Amstel', 'Cerveja Budweiser', 'Cerveja Stella Artois', 'Cerveja Itaipava', 'Cerveja Antarctica', 'Cerveja Corona', 'Cerveja Eisenbahn', 'Cerveja Petra', 'Cerveja Kaiser', 'Cerveja Schin', 'Cerveja Devassa', 'Cerveja Bohemia', 'Cerveja Spaten', "Cerveja Beck's", 'Cerveja Baden Baden', 'Cerveja Colorado', 'Cerveja Crystal'] },
  'Sucos': { classe: 'suco', emoji: '🧃', bebidas: ['Suco Laranja', 'Suco Del Valle', 'Suco Natural One', 'Suco Tang', 'Suco AdeS'] }
};

const $ = (seletor) => document.querySelector(seletor);
const telas = document.querySelectorAll('.tela');
const erroLogin = $('#erro-login'); const erroCadastro = $('#erro-cadastro');
const aviso = $('#aviso'); const concluir = $('#botao-concluir');
let usuarioAtual = null; let perfilAtual = null; let bebidaSelecionada = null; let retornoLogin;

function validarConfiguracao() {
  if (firebaseConfig.apiKey === 'COLE_AQUI' || firebaseConfig.databaseURL.includes('COLE_AQUI')) {
    throw new Error('Configure o arquivo firebase-config.js antes de publicar o site.');
  }
}

function normalizarUsuario(valor) {
  const usuario = valor.trim().toLowerCase();
  if (!/^[a-z0-9._-]{3,24}$/.test(usuario)) throw new Error('Use 3 a 24 caracteres: letras, números, ponto, hífen ou _.');
  return usuario;
}
function emailInterno(usuario) { return `${normalizarUsuario(usuario)}@${loginDomain}`; }
function mostrarTela(id) { telas.forEach((tela) => tela.classList.toggle('ativa', tela.id === id)); }
function mensagemErro(erro) {
  const mapa = { 'auth/invalid-credential': 'Usuário ou senha incorretos.', 'auth/email-already-in-use': 'Este nome de usuário já existe.', 'auth/weak-password': 'Use uma senha com pelo menos 6 caracteres.', 'auth/network-request-failed': 'Verifique sua conexão com a internet.' };
  return mapa[erro.code] || erro.message || 'Não foi possível concluir a operação.';
}
function exibirBebidas() {
  $('#lista-bebidas').innerHTML = Object.entries(categorias).map(([categoria, dados]) => `<section class="categoria"><h2>${categoria}</h2><div class="bebidas">${dados.bebidas.map((bebida) => `<button class="bebida ${dados.classe}" type="button" data-bebida="${bebida}"><span class="icone-bebida">${dados.emoji}</span>${bebida}</button>`).join('')}</div></section>`).join('');
  bebidaSelecionada = null; concluir.disabled = true; mostrarTela('tela-bebidas');
}
function voltarAoLogin() { clearTimeout(retornoLogin); aviso.classList.remove('visivel'); bebidaSelecionada = null; concluir.disabled = true; mostrarTela('tela-login'); }
async function carregarPerfil(uid) {
  const snapshot = await get(ref(db, `users/${uid}`));
  if (!snapshot.exists()) throw new Error('Cadastro não encontrado. Peça ao administrador para recriar sua conta.');
  return snapshot.val();
}

let app; let auth; let db;
try { validarConfiguracao(); app = initializeApp(firebaseConfig); auth = getAuth(app); db = getDatabase(app); } catch (erro) { erroLogin.textContent = erro.message; }

$('#form-login').addEventListener('submit', async (evento) => {
  evento.preventDefault(); erroLogin.textContent = '';
  try { const credencial = await signInWithEmailAndPassword(auth, emailInterno($('#usuario').value), $('#senha').value); usuarioAtual = credencial.user; perfilAtual = await carregarPerfil(usuarioAtual.uid); evento.currentTarget.reset(); exibirBebidas(); }
  catch (erro) { erroLogin.textContent = mensagemErro(erro); }
});
$('#abrir-cadastro').addEventListener('click', () => { erroCadastro.textContent = ''; mostrarTela('tela-cadastro'); });
$('#voltar-login').addEventListener('click', () => mostrarTela('tela-login'));
$('#form-cadastro').addEventListener('submit', async (evento) => {
  evento.preventDefault(); erroCadastro.textContent = '';
  try {
    const nome = $('#cad-nome').value.trim(); const usuario = normalizarUsuario($('#cad-usuario').value); const senha = $('#cad-senha').value;
    if (nome.length < 3) throw new Error('Informe o nome completo.');
    const credencial = await createUserWithEmailAndPassword(auth, `${usuario}@${loginDomain}`, senha);
    await set(ref(db, `users/${credencial.user.uid}`), { username: usuario, fullName: nome, createdAt: serverTimestamp() });
    usuarioAtual = credencial.user; perfilAtual = { username: usuario, fullName: nome }; evento.currentTarget.reset(); exibirBebidas();
  } catch (erro) { erroCadastro.textContent = mensagemErro(erro); }
});
$('#lista-bebidas').addEventListener('click', (evento) => {
  const botao = evento.target.closest('.bebida'); if (!botao) return;
  document.querySelectorAll('.bebida').forEach((item) => item.classList.remove('selecionada'));
  botao.classList.add('selecionada'); bebidaSelecionada = botao.dataset.bebida; concluir.disabled = false;
});
concluir.addEventListener('click', async () => {
  if (!usuarioAtual || !bebidaSelecionada) return;
  concluir.disabled = true;
  try {
    const pedido = push(ref(db, 'orders'));
    await set(pedido, { uid: usuarioAtual.uid, username: perfilAtual.username, fullName: perfilAtual.fullName, drink: bebidaSelecionada, status: 'pending', createdAt: serverTimestamp() });
    aviso.classList.add('visivel'); retornoLogin = setTimeout(async () => { await signOut(auth); voltarAoLogin(); }, 3000);
  } catch (erro) { erroLogin.textContent = mensagemErro(erro); mostrarTela('tela-login'); concluir.disabled = false; }
});
$('#fechar-aviso').addEventListener('click', async () => { if (auth) await signOut(auth); voltarAoLogin(); });
$('#botao-sair').addEventListener('click', async () => { if (auth) await signOut(auth); voltarAoLogin(); });
