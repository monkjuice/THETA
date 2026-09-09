const { app, BrowserWindow, ipcMain, dialog, session } = require('electron');
const path = require('node:path');
const fs = require('node:fs/promises');

app.setName('Theda');
if (process.env.THEDA_TEST_DATA) app.setPath('userData', process.env.THEDA_TEST_DATA);
let window;
function createWindow() {
  window = new BrowserWindow({
    width: 1540, height: 980, minWidth: 1060, minHeight: 720,
    backgroundColor: '#181a1b', title: 'Theda — Music Studio',
    autoHideMenuBar: true,
    show: process.env.THEDA_TEST !== '1',
    webPreferences: {
      preload: path.join(__dirname, 'preload.cjs'),
      contextIsolation: true, nodeIntegration: false, sandbox: true,
      backgroundThrottling: false
    }
  });
  window.loadFile(path.join(__dirname, '../index.html'));
  window.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  window.webContents.on('will-navigate', event => event.preventDefault());
  window.webContents.on('will-prevent-unload', async event => {
    const result = dialog.showMessageBoxSync(window, { type: 'question',
      buttons: ['Keep working', 'Close without saving'], defaultId: 0, cancelId: 0,
      message: 'Close this project?', detail: 'There are changes that have not been saved to a project file. A recovery copy is stored on this computer.' });
    if (result === 1) event.preventDefault();
  });
}

app.whenReady().then(() => {
  session.defaultSession.setPermissionRequestHandler((contents, permission, callback, details) => {
    callback(contents === window?.webContents && permission === 'media' &&
      details.mediaTypes?.every(type => type === 'audio'));
  });
  ipcMain.handle('project:save', async (_event, name, content) => {
    if (typeof content !== 'string' || content.length > 150_000_000) throw new Error('Project is too large to save (150 MB limit).');
    const result = await dialog.showSaveDialog(window, { defaultPath: `${safeName(name)}.theda`, filters: [{ name: 'Theda project', extensions: ['theda'] }] });
    if (result.canceled) return null;
    await fs.writeFile(result.filePath, content, 'utf8');
    return path.basename(result.filePath);
  });
  ipcMain.handle('project:open', async () => {
    const result = await dialog.showOpenDialog(window, { properties: ['openFile'], filters: [{ name: 'Theda project', extensions: ['theda', 'json'] }] });
    if (result.canceled) return null;
    const stat = await fs.stat(result.filePaths[0]);
    if (stat.size > 150_000_000) throw new Error('Project exceeds the 150 MB limit.');
    return fs.readFile(result.filePaths[0], 'utf8');
  });
  ipcMain.handle('audio:export', async (_event, name, bytes) => {
    if (!(bytes instanceof Uint8Array) || bytes.byteLength > 500_000_000) throw new Error('Invalid audio export.');
    const result = await dialog.showSaveDialog(window, { defaultPath: `${safeName(name)}.wav`, filters: [{ name: 'Wave audio', extensions: ['wav'] }] });
    if (result.canceled) return null;
    await fs.writeFile(result.filePath, Buffer.from(bytes));
    return path.basename(result.filePath);
  });
  createWindow();
  app.on('activate', () => { if (!BrowserWindow.getAllWindows().length) createWindow(); });
});
function safeName(name) { return String(name || 'Untitled').replace(/[<>:"/\\|?*\x00-\x1f]/g, '_').slice(0, 100); }
app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit(); });
