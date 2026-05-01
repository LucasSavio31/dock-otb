# Nextion Dashboard Layout

Este arquivo documenta a nova dashboard para o projeto `IHM_OTB.HMI`, mantendo as telas atuais e adicionando uma pagina resumida inspirada na `overview` da dashboard web.

## Paginas existentes encontradas no `.HMI`

- `home`
- `dock_status`
- `status_pen`
- `erros`
- `configs`
- `bloqueio`
- `recarga`
- `status`
- `status_cart`
- `status_dock`
- `senha_popup`

## Nova pagina sugerida

- Nome da pagina: `dashboard`
- Objetivo: entregar uma visao rapida do dock logo ao abrir a interface
- Navegacao sugerida:
  - `home` -> botao/icone para `dashboard`
  - `dashboard` -> atalhos para `status_pen`, `erros`, `dock_status` e `configs`

## Componentes que o firmware passou a atualizar

Crie estes objetos na pagina `dashboard` com exatamente estes nomes:

- `dbStatus` — texto com status principal (`Pronto`, `E401`, etc.)
- `dbErrCount` — quantidade de erros ativos
- `dbSerial` — numero de serie da dock
- `dbHeap` — heap livre
- `dbTasks` — numero de tasks
- `dbUptime` — uptime em `HH:MM:SS`
- `dbPenId` — ID da tag presente
- `dbPenStatus` — status da caneta
- `dbPenCycles` — ciclos
- `dbPenLife` — vida util
- `dbPenSerial` — serial da caneta
- `dbCh0` — nivel do canal 1 em texto
- `dbCh1` — nivel do canal 2 em texto
- `dbCh2` — nivel do canal 3 em texto
- `dbRechargeState` — estado da recarga
- `dbRechargeChannel` — canal ativo da recarga
- `dbRechargeLevel` — nivel atual da recarga
- `dbRechargeDuty` — duty atual da bomba

## Barras opcionais

Se quiser uma dashboard mais visual, crie tambem progress bars com estes nomes:

- `jCh0`
- `jCh1`
- `jCh2`
- `jRecharge`

O firmware envia `.val` de `0..100` para elas.

## Layout sugerido para 480x272

```text
+--------------------------------------------------------------+
| OTB DOCK | dbStatus | Erros: dbErrCount | Uptime: dbUptime   |
| Serie: dbSerial                             Heap: dbHeap      |
+---------------------------+----------------+------------------+
| CANETA                    | SENSORES       | RECARGA          |
| ID: dbPenId               | CH1 dbCh0 jCh0 | Estado db...     |
| Status: dbPenStatus       | CH2 dbCh1 jCh1 | Canal  db...     |
| Ciclos: dbPenCycles       | CH3 dbCh2 jCh2 | Nivel  db...     |
| Vida: dbPenLife           |                | Duty   db...     |
| Serie: dbPenSerial        |                | jRecharge        |
+---------------------------+----------------+------------------+
| [Canetas] [Erros] [Dock] [Configs] [Home]                     |
+--------------------------------------------------------------+
```

## Estilo visual sugerido

- fundo escuro grafite
- faixa superior em azul/ciano
- cards com cantos retos ou raio baixo
- textos principais em branco
- status OK em verde
- erro em vermelho/laranja
- barras de nivel em azul ou verde

## Eventos sugeridos na propria pagina

- no `Preinitialize Event` da pagina `dashboard`: nada obrigatorio
- no `Postinitialize Event` da pagina `dashboard`: opcionalmente `sendme`
- botao Home: `page home`
- botao Dock: `page dock_status`
- botao Erros: `page erros`
- botao Configs: `page configs`
- botao Canetas: `page status_pen`

## Observacoes

- O firmware atual ja continua atualizando as telas antigas.
- Esta dashboard e uma pagina adicional, nao substitui `dock_status`.
- Como o ambiente aqui nao tem o Nextion Editor, eu preparei o contrato de nomes e o suporte no firmware. Ao abrir o `IHM_OTB.HMI` no editor, basta criar a pagina `dashboard` com esses componentes para ela funcionar.
