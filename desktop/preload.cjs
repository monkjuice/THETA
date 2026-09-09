const { contextBridge, ipcRenderer } = require('electron');
contextBridge.exposeInMainWorld('desktop', {
  saveProject: (name, content) => ipcRenderer.invoke('project:save', name, content),
  openProject: () => ipcRenderer.invoke('project:open'),
  exportAudio: (name, bytes) => ipcRenderer.invoke('audio:export', name, bytes)
});
