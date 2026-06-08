const puppeteer = require('puppeteer');
const fs = require('fs');
const path = require('path');

(async () => {
  const screenshotsDir = path.join(__dirname, 'screenshots');
  if (!fs.existsSync(screenshotsDir)){
    fs.mkdirSync(screenshotsDir);
  }

  const browser = await puppeteer.launch({
    headless: 'new',
    defaultViewport: { width: 1280, height: 800 }
  });
  
  const page = await browser.newPage();
  
  // Navigate to local dashboard
  await page.goto(`file:///${__dirname.replace(/\\/g, '/')}/dashboard/index.html`, { waitUntil: 'networkidle0' });

  // 1. Login Screen
  await page.waitForSelector('#loginBtn', { visible: true });
  await page.screenshot({ path: path.join(screenshotsDir, 'login_screen.png') });
  console.log('Took login_screen.png');

  // Click Login with credentials
  await page.type('#loginUser', 'otb');
  await page.type('#loginPass', '1234');
  await page.click('#loginBtn');
  await page.waitForSelector('#page-overview.active', { visible: true });
  await new Promise(r => setTimeout(r, 1000)); // wait for animations
  await page.screenshot({ path: path.join(screenshotsDir, 'overview_screen.png') });
  console.log('Took overview_screen.png');

  // 2. Erros
  await page.evaluate(() => { document.querySelector('.nav-item[data-page="erros"]').click(); });
  await page.waitForSelector('#page-erros.active', { visible: true });
  await new Promise(r => setTimeout(r, 500));
  await page.screenshot({ path: path.join(screenshotsDir, 'erros_screen.png') });
  console.log('Took erros_screen.png');

  // 3. NFC
  await page.evaluate(() => { document.querySelector('.nav-item[data-page="nfc"]').click(); });
  await page.waitForSelector('#page-nfc.active', { visible: true });
  await new Promise(r => setTimeout(r, 500));
  await page.screenshot({ path: path.join(screenshotsDir, 'nfc_screen.png') });
  console.log('Took nfc_screen.png');

  // 4. Atuadores
  await page.evaluate(() => { document.querySelector('.nav-item[data-page="atuadores"]').click(); });
  await page.waitForSelector('#page-atuadores.active', { visible: true });
  await new Promise(r => setTimeout(r, 500));
  await page.screenshot({ path: path.join(screenshotsDir, 'atuadores_screen.png') });
  console.log('Took atuadores_screen.png');

  // 5. Sensor
  await page.evaluate(() => { document.querySelector('.nav-item[data-page="sensor"]').click(); });
  await page.waitForSelector('#page-sensor.active', { visible: true });
  await new Promise(r => setTimeout(r, 500));
  await page.screenshot({ path: path.join(screenshotsDir, 'sensores_screen.png') });
  console.log('Took sensores_screen.png');

  // 6. Recarga
  await page.evaluate(() => { document.querySelector('.nav-item[data-page="recarga"]').click(); });
  await page.waitForSelector('#page-recarga.active', { visible: true });
  await new Promise(r => setTimeout(r, 500));
  await page.screenshot({ path: path.join(screenshotsDir, 'recarga_screen.png') });
  console.log('Took recarga_screen.png');
  
  // 7. Calibracao
  await page.evaluate(() => { document.querySelector('.nav-item[data-page="calibracao"]').click(); });
  await page.waitForSelector('#page-calibracao.active', { visible: true });
  await new Promise(r => setTimeout(r, 500));
  await page.screenshot({ path: path.join(screenshotsDir, 'calibracao_screen.png') });
  console.log('Took calibracao_screen.png');

  // 8. Dock
  await page.evaluate(() => { document.querySelector('.nav-item[data-page="dock"]').click(); });
  await page.waitForSelector('#page-dock.active', { visible: true });
  await new Promise(r => setTimeout(r, 500));
  await page.screenshot({ path: path.join(screenshotsDir, 'dock_screen.png') });
  console.log('Took dock_screen.png');

  // 9. Diag
  await page.evaluate(() => { document.querySelector('.nav-item[data-page="diag"]').click(); });
  await page.waitForSelector('#page-diag.active', { visible: true });
  await new Promise(r => setTimeout(r, 500));
  await page.screenshot({ path: path.join(screenshotsDir, 'diag_screen.png') });
  console.log('Took diag_screen.png');

  // 10. I2C
  await page.evaluate(() => { document.querySelector('.nav-item[data-page="i2c"]').click(); });
  await page.waitForSelector('#page-i2c.active', { visible: true });
  await new Promise(r => setTimeout(r, 500));
  await page.screenshot({ path: path.join(screenshotsDir, 'i2c_screen.png') });
  console.log('Took i2c_screen.png');

  // 11. Ativacao
  await page.evaluate(() => { document.querySelector('.nav-item[data-page="ativacao"]').click(); });
  await page.waitForSelector('#page-ativacao.active', { visible: true });
  await new Promise(r => setTimeout(r, 500));
  await page.screenshot({ path: path.join(screenshotsDir, 'ativacao_screen.png') });
  console.log('Took ativacao_screen.png');

  // 12. Historico
  await page.evaluate(() => { document.querySelector('.nav-item[data-page="historico"]').click(); });
  await page.waitForSelector('#page-historico.active', { visible: true });
  await new Promise(r => setTimeout(r, 500));
  await page.screenshot({ path: path.join(screenshotsDir, 'historico_screen.png') });
  console.log('Took historico_screen.png');

  // 13. Console
  await page.evaluate(() => { document.querySelector('.nav-item[data-page="console"]').click(); });
  await page.waitForSelector('#page-console.active', { visible: true });
  await new Promise(r => setTimeout(r, 500));
  await page.screenshot({ path: path.join(screenshotsDir, 'console_screen.png') });
  console.log('Took console_screen.png');

  await browser.close();
  console.log('All screenshots taken successfully.');
})();
