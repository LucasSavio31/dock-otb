const fs = require('fs');
let html = fs.readFileSync('c:/Users/sn1098525/Desktop/Firmwares OBT/dock-otb-main/dashboard/index.html', 'utf8');

const dict = {
  'pt': { 'lang_name': 'PT-BR' },
  'en': {
    'lang_name': 'EN',
    'Conectar': 'Connect',
    'Desconectar': 'Disconnect',
    'Desconectado': 'Disconnected',
    'VISÃO GERAL': 'OVERVIEW',
    'Leitor NFC': 'NFC Reader',
    'Atuadores': 'Actuators',
    'Erros': 'Errors',
    'Sistema Dock': 'Dock System',
    'Terminal Serial': 'Serial Terminal',
    'DIAGNÓSTICO': 'DIAGNOSTIC',
    'Erros Ativos': 'Active Errors',
    'Sensores de Nível': 'Level Sensors',
    'Válvulas Solenóides': 'Solenoid Valves',
    'Bomba Peristáltica': 'Peristaltic Pump',
    'Busca por Dispositivos I2C': 'I2C Device Search',
    'Códigos de Erro — OTB DockStation': 'Error Codes — OTB DockStation',
    'Informações do Sistema Dockstation': 'Dockstation System Info',
    'Selecionar Leitor': 'Select Reader',
    'Ações': 'Actions',
    'Gravar Tag': 'Write Tag',
    'Dados da Tag': 'Tag Data',
    'Nenhum erro ativo': 'No active errors',
    'Uso de Memória': 'Memory Usage',
    'Memória Livre': 'Free Memory',
    'Tasks Ativas': 'Active Tasks',
    'Aguardando': 'Waiting',
    'Aguardando scan...': 'Waiting for scan...',
    'Estado: Aberta': 'Status: Open',
    'Estado: Fechada': 'Status: Closed',
    'Identificar UID': 'Identify UID',
    'Ler Tag': 'Read Tag',
    'Resetar Tag': "Reset Tag",
    "Gravar na Tag": "Write to Tag",
    "Iniciar Purga": "Start Purge",
    "Limpar": "Clear",
    "Executar Scan": "Execute Scan"
  },
  'es': {
    'lang_name': 'ES',
    'Conectar': 'Conectar',
    'Desconectar': 'Desconectar',
    'Desconectado': 'Desconectado',
    'VISÃO GERAL': 'RESUMEN',
    'Leitor NFC': 'Lector NFC',
    'Atuadores': 'Actuadores',
    'Erros': 'Errores',
    'Sistema Dock': 'Sistema Dock',
    'Terminal Serial': 'Terminal Serial',
    'DIAGNÓSTICO': 'DIAGNÓSTICO',
    'Erros Ativos': 'Errores Activos',
    'Sensores de Nível': 'Sensores Nivel',
    'Válvulas Solenóides': 'Válvulas Solenoides',
    'Bomba Peristáltica': 'Bomba Peristáltica',
    'Busca por Dispositivos I2C': 'Búsqueda Disp. I2C',
    'Códigos de Erro — OTB DockStation': 'Códigos de Error — OTB DockStation',
    'Informações do Sistema Dockstation': 'Info Sistema Dockstation',
    'Selecionar Leitor': 'Seleccionar Lector',
    'Ações': 'Acciones',
    'Gravar Tag': 'Grabar Tag',
    'Dados da Tag': 'Datos del Tag',
    'Nenhum erro ativo': 'Sin errores activos',
    'Uso de Memória': 'Uso Memoria',
    'Memória Livre': 'Memoria Libre',
    'Tasks Ativas': 'Tasks Activas',
    'Aguardando': 'Esperando',
    'Aguardando scan...': 'Esperando scan...',
    'Estado: Aberta': 'Estado: Abierta',
    'Estado: Fechada': 'Estado: Cerrada',
    'Identificar UID': 'Identificar UID',
    'Ler Tag': 'Leer Tag',
    'Resetar Tag': "Resetear Tag",
    "Gravar na Tag": "Grabar en Tag",
    "Iniciar Purga": "Iniciar Purga",
    "Limpar": "Limpiar",
    "Executar Scan": "Ejecutar Scan"
  }
};

fs.writeFileSync('c:/Users/sn1098525/Desktop/Firmwares OBT/dock-otb-main/dashboard/i18n.js', `
const I18N_DICT = ${JSON.stringify(dict, null, 2)};

let currentLang = localStorage.getItem('otb-lang') || 'pt';

function setLanguage(lang) {
  if (!I18N_DICT[lang]) lang = 'pt';
  currentLang = lang;
  localStorage.setItem('otb-lang', lang);
  
  // Update static text marked with data-i18n-str
  document.querySelectorAll('[data-i18n-str]').forEach(el => {
    const orig = el.dataset.i18nStr;
    if (lang === 'pt') {
      el.textContent = orig;
    } else {
      el.textContent = I18N_DICT[lang][orig] || orig;
    }
  });

  // Specifically update the language UI selector button if any
  const selTitle = document.getElementById('btnLang');
  if (selTitle) selTitle.textContent = I18N_DICT[lang]['lang_name'];
}

function t(str) {
  if (currentLang === 'pt') return str;
  return I18N_DICT[currentLang][str] || str;
}
`);

// Now modify HTML to have data-i18n-str on all these matches
const stringKeys = Object.keys(dict.en).filter(k => k !== 'lang_name').sort((a,b)=>b.length - a.length);

for (const key of stringKeys) {
  // Find EXACT text node content "key". 
  // Wait, replacing > key < is tricky. Let's do it using DOM or clever regex.
  // We'll replace it carefully.
  const escapedKey = key.replace(/[-\\/\\\\^$*+?.()|[\\]{}]/g, '\\\\$&');
  // Match >[spaces]key[spaces]<
  const regex = new RegExp('>(\\\\s*)(' + escapedKey + ')(\\\\s*)<', 'g');
  html = html.replace(regex, '>\\$1<span data-i18n-str=\\"$2\\">\\$2</span>\\$3<');
}

// Special case for buttons that might have emojis inside them:
// <button>🔎 Identificar UID</button> -> <button>🔎 <span data-i18n-str="Identificar UID">...</span></button>
const btnKeys = ['Identificar UID', 'Ler Tag', 'Resetar Tag', 'Gravar na Tag', 'Iniciar Purga', 'Executar Scan', 'Limpar'];
for (const key of btnKeys) {
  const escapedKey = key.replace(/[-\\/\\\\^$*+?.()|[\\]{}]/g, '\\\\$&');
  const regex2 = new RegExp('([^>]*?)(' + escapedKey + ')(\\\\s*)<', 'g');
  html = html.replace(regex2, (m, p1, p2, p3) => {
    if (m.includes('data-i18n-str')) return m; // Already wrapped
    return p1 + '<span data-i18n-str=\\"' + p2 + '\\">' + p2 + '</span>' + p3 + '<';
  });
}

// Also wait, I will just append the <script src="i18n.js"></script> before the main script tag
html = html.replace('<script>', '<script src=\\"i18n.js\\"></script>\\n<script>');

// And add a language button in conn-row
const themeBtnIdx = html.indexOf('id="btnTheme"');
if (themeBtnIdx > 0) {
  const langDropdownHtml = 
    '<div style="position:relative; display:inline-block">\\n' +
    '  <button class="btn btn-outline btn-sm" id="btnLang" style="font-size:12px; font-weight:700; width:60px; margin:0 4px">PT-BR</button>\\n' +
    '  <div id="langMenu" style="display:none; position:absolute; top:30px; left:0; background:var(--surface); border:1px solid var(--border); padding:5px; border-radius:5px; z-index:99; flex-direction:column; gap:5px; box-shadow:0 10px 20px rgba(0,0,0,.5)">\\n' +
    '    <a href="#" style="color:var(--text); text-decoration:none; padding:4px 8px; border-radius:4px" onclick="setLanguage(\\"pt\\"); document.getElementById(\\"langMenu\\").style.display=\\"none\\"; return false;">PT-BR</a>\\n' +
    '    <a href="#" style="color:var(--text); text-decoration:none; padding:4px 8px; border-radius:4px" onclick="setLanguage(\\"en\\"); document.getElementById(\\"langMenu\\").style.display=\\"none\\"; return false;">EN</a>\\n' +
    '    <a href="#" style="color:var(--text); text-decoration:none; padding:4px 8px; border-radius:4px" onclick="setLanguage(\\"es\\"); document.getElementById(\\"langMenu\\").style.display=\\"none\\"; return false;">ES</a>\\n' +
    '  </div>\\n' +
    '</div>\\n' +
    '<script>\\n' +
    '  setTimeout(()=>{ \\n' +
    '     document.getElementById("btnLang").onclick = function() {\\n' +
    '       const m = document.getElementById("langMenu");\\n' +
    '       m.style.display = m.style.display === "none" ? "flex" : "none";\\n' +
    '     };\\n' +
    '     if(typeof setLanguage === "function") setLanguage(localStorage.getItem("otb-lang") || "pt");\\n' +
    '  }, 500);\\n' +
    '</script>\\n';

  const insertPos = html.lastIndexOf('<button', themeBtnIdx); // before btnTheme
  html = html.substring(0, insertPos) + langDropdownHtml + html.substring(insertPos);
}

// Ensure JS code uses t() where needed:
// e.g. statusText.textContent = 'Desconectado' -> t('Desconectado')
html = html.replace(/textContent\\s*=\\s*'Desconectado'/g, "textContent = typeof t === 'function' ? t('Desconectado') : 'Desconectado'");
html = html.replace(/'Conectado'/g, "typeof t === 'function' ? t('Conectado') : 'Conectado'");

fs.writeFileSync('c:/Users/sn1098525/Desktop/Firmwares OBT/dock-otb-main/dashboard/index.html', html);
console.log('Successfully written index.html and i18n.js');
