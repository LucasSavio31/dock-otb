const path = require('path');
const { pathToFileURL } = require('url');
const { app, BrowserWindow, dialog, protocol, session, net } = require('electron');

protocol.registerSchemesAsPrivileged([
  {
    scheme: 'app',
    privileges: {
      standard: true,
      secure: true,
      supportFetchAPI: true,
      stream: true,
      allowServiceWorkers: true,
      corsEnabled: true
    }
  }
]);

let mainWindow = null;
let pendingSerialRequest = null;

function registerAppProtocol() {
  protocol.handle('app', request => {
    const url = new URL(request.url);
    const pathname = decodeURIComponent(url.pathname === '/' ? '/index.html' : url.pathname);
    const filePath = path.join(app.getAppPath(), 'dashboard', pathname.replace(/^\/+/, ''));
    return net.fetch(pathToFileURL(filePath).toString());
  });
}

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1440,
    height: 960,
    minWidth: 1100,
    minHeight: 720,
    backgroundColor: '#080b10',
    title: 'OTB Dock Maintenance',
    autoHideMenuBar: true,
    webPreferences: {
      contextIsolation: true,
      sandbox: false,
      nodeIntegration: false
    }
  });

    mainWindow.loadURL('app://local/index.html');
}

function formatPortLabel(port) {
  const vendor = port.vendorId ? `VID:${String(port.vendorId).toUpperCase()}` : 'VID:----';
  const product = port.productId ? `PID:${String(port.productId).toUpperCase()}` : 'PID:----';
  return `${port.displayName || 'Porta serial'} (${vendor} ${product})`;
}

async function chooseSerialPort(webContents, portList, callback) {
  if (!portList.length) {
    dialog.showMessageBox({
      type: 'warning',
      title: 'Nenhuma porta encontrada',
      message: 'Nenhuma porta serial disponível foi encontrada para conectar à dock.'
    }).catch(() => {});
    callback('');
    return;
  }

  if (portList.length === 1) {
    callback(portList[0].portId);
    return;
  }

  const buttons = portList.slice(0, 8).map(formatPortLabel);
  buttons.push('Cancelar');
  const { response } = await dialog.showMessageBox(BrowserWindow.fromWebContents(webContents), {
    type: 'question',
    title: 'Selecionar porta serial',
    message: 'Escolha a porta serial da dock para manutenção.',
    buttons,
    cancelId: buttons.length - 1,
    noLink: true
  });

  if (response >= 0 && response < portList.length) callback(portList[response].portId);
  else callback('');
}

function configureSerialPermissions() {
  const ses = session.defaultSession;

  ses.setPermissionCheckHandler((_wc, permission, _requestingOrigin, details) => {
    if (permission === 'serial') return details.securityOrigin === 'app://local';
    return false;
  });

  ses.setPermissionRequestHandler((_wc, permission, callback, details) => {
    if (permission === 'serial') {
      callback(details.securityOrigin === 'app://local');
      return;
    }
    callback(false);
  });

  ses.setDevicePermissionHandler(details => {
    return details.deviceType === 'serial' && details.origin === 'app://local';
  });

  app.on('select-serial-port', (event, webContents, portList, _serialPortRequestedOrigin, callback) => {
    event.preventDefault();
    pendingSerialRequest = { callback };
    chooseSerialPort(webContents, portList, portId => {
      if (pendingSerialRequest?.callback === callback) pendingSerialRequest = null;
      callback(portId);
    }).catch(() => {
      if (pendingSerialRequest?.callback === callback) pendingSerialRequest = null;
      callback('');
    });
  });

  app.on('serial-port-added', () => {
    if (!pendingSerialRequest?.callback || !mainWindow) return;
    mainWindow.webContents.executeJavaScript('typeof renderPortList === "function" && renderPortList();').catch(() => {});
  });

  app.on('serial-port-removed', () => {
    if (!mainWindow) return;
    mainWindow.webContents.executeJavaScript('typeof renderPortList === "function" && renderPortList();').catch(() => {});
  });
}

app.whenReady().then(() => {
  registerAppProtocol();
  configureSerialPermissions();
  createWindow();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit();
});
