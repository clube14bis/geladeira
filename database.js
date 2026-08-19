// Banco de dados local de acesso. Em produção, use um servidor seguro.
const usuarios = [
  { nome: 'Adauto', senha: '1234' },
  { nome: 'Mateus', senha: '4659' }
];

function validarLogin(nome, senha) {
  return usuarios.some(
    (usuario) => usuario.nome.toLowerCase() === nome.trim().toLowerCase() && usuario.senha === senha
  );
}

// Disponibiliza a validação para a página, inclusive quando aberta por duplo clique.
window.validarLogin = validarLogin;
