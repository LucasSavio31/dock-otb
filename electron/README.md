# Electron Wrapper

Arquivos do aplicativo desktop que empacota o dashboard web da DockStation.

Scripts úteis:

- `npm.cmd install`
- `npm.cmd start`
- `npm.cmd run dist`

O app carrega `dashboard/index.html` via protocolo seguro `app://` para manter compatibilidade com Web Serial dentro do Electron.
