// Catálogo inicial. O painel administrativo grava estas configurações no Firebase.
export const pastaImagens = "Imagens Bebidas/ ";
export const ordemCategorias = ["ÁGUA", "REFRIGERANTES E OUTROS", "CERVEJAS", "SUCOS"];
export const catalogoBase = {
  "ÁGUA":[
    ["ÁGUA MINERAL LEVITY 510ML S/GÁS","agua.png",350,12],
    ["ÁGUA MINERAL LEVITY 510ML C/GÁS","agua com gas.png",350,12]
  ],
  "CERVEJAS":[
    ["CERVEJA HEINEKEN 269ML (ZERO)","cerveja-heineken-zero-lata-269ml-1.png",600,12],
    ["CERVEJA HEINEKEN 330ML LAGER","heineken-garrafa.png",800,36],
    ["CERVEJA ORIGINAL 350ML","original-lata.png",600,36]
  ],
  "REFRIGERANTES E OUTROS":[
    ["MONSTER 473ML ENERGY","energetico-monster-energy-lata-473ml-1.png",1000,6],
    ["MONSTER 473ML ENERGY (ZERO)","energetico-monster-energy-lata-473ml-1.png",1000,6],
    ["REFRIG ANTARCTICA 350ML GUARANÁ","refrigerante-guarana-antarctica-350ml-1.png",500,12],
    ["REFRIG ANTARCTICA 350ML (ZERO)","refrigerante-guarana-antarctica-350ml-1.png",500,0],
    ["REFRIG COCA COLA 350ML","coca-cola-lata-350-ml-1.png",550,12],
    ["REFRIG COCA COLA 350ML (ZERO)","cocazero-lata.png",550,12],
    ["REFRIG FANTA 350ML LARANJA","fanta-laranja-220ml-1.png",500,12],
    ["REFRIG FANTA 350ML LARANJA (ZERO)","fanta-laranja-220ml-1.png",500,0],
    ["REFRIG SPRITE 350ML ORIGINAL","sprite.png",500,0],
    ["REFRIG SPRITE 350ML S/ AÇÚCAR","sprite.png",500,12]
  ],
  "SUCOS":[]
};
export const slugProduto = nome => nome.normalize("NFD").replace(/[\u0300-\u036f]/g,"").toLowerCase().replace(/[^a-z0-9]+/g,"-").replace(/(^-|-$)/g,"");
export function produtosIniciais(){return Object.fromEntries(Object.entries(catalogoBase).flatMap(([category,items])=>items.map(([name,image,priceCents,stock])=>{const id=slugProduto(name);return [id,{name,category,image,enabled:stock>0,priceCents,stock}];})));}
