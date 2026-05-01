# Project Context: Dock OTB / SmartPen

Este arquivo existe para dar contexto rápido e confiável a agentes como ChatGPT/Codex e Claude quando estiverem trabalhando neste repositório.

## Escopo e fonte da informação

- Fonte primária: arquivos locais deste repositório em `dock-otb`.
- Este documento foi consolidado a partir de `README.md`, `platformio.ini` e dos headers em `src/`.
- Fonte complementar adicionada pelo usuario: especificacao textual intitulada `README_AI.md — SmartPen DockStation OTB`, colada em chat em 2026-04-21.
- Nao ha acesso automatico ao historico de chats da conta do usuario fora deste workspace.
- Se houver decisoes, requisitos ou conversas importantes sobre Dock ou SmartPen em outro lugar, elas precisam ser trazidas para este repositório para entrarem no contexto persistente.

## Objetivo do projeto

Firmware embarcado para uma DockStation OTB baseada em ESP32, responsavel por:

- detectar canetas e cartuchos via NFC;
- ler e gravar dados em tags NTAG;
- monitorar sensores capacitivos de nivel;
- controlar bomba e valvulas;
- atualizar um display Nextion;
- expor diagnostico e operacao manual via serial.

O `README.md` ainda fala bastante em "Firmware V4", mas o codigo atual esta rotulado como `V5` em multiplos arquivos e nos logs de boot. Em caso de conflito, considerar o codigo fonte como verdade principal.

## Contexto adicional vindo do usuario

O usuario forneceu uma especificacao adicional de alto nivel chamada `README_AI.md — SmartPen DockStation OTB`. Esse material parece representar intencao arquitetural e regras de negocio desejadas para o sistema, mesmo quando nem tudo aparece implementado no codigo atual.

Pontos relevantes dessa especificacao:

- ESP32 WROOM como controlador principal;
- 6 leitores PN532 via SPI, sendo 3 de canetas e 3 de cartuchos;
- 3 sensores FDC1004 via I2C;
- 1 TCA9548A;
- 1 ULN2003 para bomba e valvulas;
- 1 display Nextion via UART;
- uso obrigatorio de FreeRTOS;
- evitar `delay()` e preferir `vTaskDelay()`;
- prioridade para estabilidade sobre complexidade;
- manutencao de filas e mutex existentes;
- objetivo final de recarga automatica robusta e operacao continua.

Esse material tambem define algumas regras de negocio que merecem ser tratadas como "alvo de produto", nao como "estado garantido do firmware atual":

- iniciar recarga com nivel abaixo de 70%;
- parar recarga com nivel maior ou igual a 80%;
- aplicar histerese;
- usar media de leitura nos sensores;
- calibrar cheio e vazio em janelas de 5 segundos.

## Stack e ambiente

- MCU: ESP32 (`board = esp32dev`)
- Framework: Arduino via PlatformIO
- Biblioteca externa declarada: `adafruit/Adafruit PN532@^1.3.4`
- Serial monitor/upload: `115200`

Arquivo de referencia: `platformio.ini`

## Estrutura real do firmware

Arquivos mais relevantes:

- `src/main.cpp`
- `src/shared.h`
- `src/task_nfc.h`
- `src/task_nextion.h`
- `src/task_serial.h`
- `src/task_sensor.h`
- `src/task_atuadores.h`
- `src/task_erros.h`
- `src/task_i2c_scan.h`
- `src/task_led.h`

Pastas `.pio/` contem artefatos de build e bibliotecas baixadas. Nao sao a fonte principal de verdade do comportamento do sistema.

## Arquitetura de runtime

O firmware usa FreeRTOS com tasks pinadas por core.

Tasks criadas em `main.cpp`:

- `taskNFC` no Core 0, prioridade 3
- `taskSensor` no Core 0, prioridade 2
- `taskAtuadores` no Core 0, prioridade 2
- `taskNextion` no Core 1, prioridade 2
- `taskSerial` no Core 1, prioridade 2
- `taskErros` no Core 1, prioridade 1
- `taskLED` no Core 1, prioridade 1
- `taskI2CScan` no Core 1, prioridade 1

Filas globais:

- `qTagData`: eventos NFC para serial
- `qNextionData`: eventos NFC para display
- `qSerialCmd`: comandos serial para NFC
- `qSerialResp`: resposta do NFC para serial
- `qControleCmd`: comandos explicitamente enviados para atuadores
- `qActCmd`: comandos vindos da UI Nextion para atuadores

Mutexes/semaforos principais:

- `mutexTag`
- `mutexErros`
- `mutexI2C`
- `mutexSPI`
- `mutexNivel`
- `semI2CScanDone`

Estados globais importantes:

- `gTag`: estado compartilhado da tag atual
- `gNivel[3]`: estado dos tres canais de sensor
- `gSerialDock[32]`: serial textual do dock, padrao `OTB202601`
- `nfcReinitPending`: usado apos diagnostico SPI
- `nfcCanalAtivo`: declarado, mas nao parece ter uso funcional relevante no codigo atual

## Mapa funcional de hardware

### NFC / SPI

Barramento SPI:

- SCK `18`
- MISO `19`
- MOSI `23`

CS dos 6 leitores PN532:

- `13`: NFC #1, Caneta 1, leitor principal
- `14`: NFC #2, Caneta 2
- `4`: NFC #3, Caneta 3
- `5`: NFC #4, Cartucho 1
- `15`: NFC #5, Cartucho 2
- `2`: NFC #6, Cartucho 3 e tambem LED onboard

Observacao importante:

- O codigo inicializa e valida os 6 leitores no boot.
- O leitor principal e o de `CS=D13`.
- Se o leitor principal nao responder, `taskNFC` entra em loop infinito piscando LED. Sem ele, o sistema nao opera.

### I2C

Pinos:

- SDA `21`
- SCL `22`

Enderecos definidos:

- `0x70`: TCA9548A
- `0x50`: FDC1004
- `0x48`: AD7747

Comportamento atual:

- Existem 3 canais de sensor usados via TCA.
- Cada canal tenta detectar automaticamente `FDC1004` ou `AD7747`.
- O sistema tenta re-detectar chip quando perde leitura repetidamente.

### Nextion / UART2

- RX do ESP32: `16`
- TX do ESP32: `17`
- Baud atual no codigo: `9600`

Responsabilidades:

- mostrar status da pen/tag;
- mostrar dados do dock;
- listar erros ativos;
- receber toques da pagina de configuracoes para bomba/valvulas.

### Atuadores

- Bomba PWM: `25`
- Valvula 1: `26`
- Valvula 2: `27`
- Valvula 3: `33`

PWM:

- LEDC high speed
- 1 kHz
- resolucao de 8 bits

### Divergencia importante de pinagem

O material enviado pelo usuario (`README_AI.md`) descreve esta pinagem de atuadores:

- Bomba: `26` PWM
- Valvula 1: `27`
- Valvula 2: `25`
- Valvula 3: `33`

Mas o codigo atual em `src/shared.h` implementa:

- Bomba: `25`
- Valvula 1: `26`
- Valvula 2: `27`
- Valvula 3: `33`

Para qualquer alteracao futura de hardware/firmware, essa divergencia precisa ser resolvida antes de assumir que a placa e o codigo estao alinhados.

## Modelo de dados NFC

`TagData`:

- `vida` (`uint16_t`)
- `ciclos` (`uint16_t`)
- `status` (`uint8_t`)
- `serial[17]`
- `id[17]`

Layout gravado na NTAG:

- pagina 4: `vida` + `ciclos`
- pagina 5: `status`
- paginas 6-9: `serial`
- paginas 10-13: `id`

Reset padrao de tag:

- `vida = 100`
- `ciclos = 0`
- `status = 1`
- `id = "OTB000"`
- `serial = "---"`

Status textual da tag:

- `0`: Vazio
- `1`: OK
- `2`: Em uso
- `3`: Bloqueado
- `4`: Erro

## Fluxos principais

### Boot

1. `setup()` cria filas, mutexes e tasks.
2. `taskNFC` sobe SPI e I2C.
3. `taskNFC` valida presenca do TCA.
4. `taskNFC` valida os 6 leitores PN532.
5. Se NFC principal falhar, sistema para em loop.
6. Leitor principal recebe `begin()` e `SAMConfig()`.
7. `taskNextion`, `taskSerial`, `taskSensor`, `taskAtuadores` completam inicializacao.

### Deteccao e leitura de tag

1. `taskNFC` faz polling do PN532 principal.
2. Ao detectar nova tag, tenta ler `TagData` ate 3 vezes.
3. Publica `TagEvent` para serial e Nextion.
4. Ao remover a tag, publica `TAG_REMOVIDA` e limpa cache.

### Operacao via serial

Comandos de alto nivel do menu:

- `pen`
- `cart`
- `valv`
- `pump`
- `diag`
- `dock`
- `sensor`
- `erros`
- `i2cscan`

O material enviado pelo usuario tambem menciona uma task chamada `task_controle`, responsavel pela logica de recarga. Essa task nao aparece como task explicita no codigo atual inspecionado.

Observacao critica:

- O submenu `pen/cart` deixa o usuario escolher entre 3 leitores, mas o backend real de `taskNFC` opera apenas sobre o leitor principal ativo no objeto `nfc`.
- Hoje o menu parece conceitualmente preparado para 6 leitores, mas a operacao normal de leitura/escrita de tag nao muda explicitamente de leitor antes de executar.
- Isso sugere desalinhamento entre UX/diagnostico e implementacao real multi-leitor.

### Operacao via Nextion

Eventos tratados:

- refresh periodico de dados do dock e erros a cada 2s;
- eventos de touch da pagina `configs`;
- exibicao/limpeza dos dados da tag;
- comando textual `HOME:ABRIU`, que dispara verificacao de home.

Toques suportados na pagina `configs`:

- purga
- toggle valvula 1
- toggle valvula 2
- toggle valvula 3

### Sensores

`taskSensor`:

- percorre 3 canais;
- seleciona canal no TCA;
- detecta se o chip e FDC1004 ou AD7747;
- inicializa o chip;
- le periodicamente `raw` e `pF`;
- grava o resultado em `gNivel[ch]`.

Alvo de produto declarado pelo usuario para sensores:

- aplicar media de leitura;
- mitigar ruido;
- considerar shield;
- suportar calibracao de cheio e vazio.

Esses comportamentos nao estao claramente implementados de forma completa na arvore atual.

## Sistema de erros

Tabela central em `src/task_erros.h`.

Grupos definidos:

- `E001-E004`: inicializacao/sistema
- `E101-E112`: NFC/tag
- `E201-E222`: I2C/sensores
- `E301-E313`: recarga/atuacao
- `E401-E402`: Nextion

Comportamento real observado:

- heap baixo gera `E003`;
- ausencia de PN532 por leitor gera `E101-E106`;
- falha de leitura/escrita de tag gera `E110/E111`;
- ausencia de TCA gera `E201`.

Atencao:

- varios erros existem na tabela, mas nem todos estao sendo realmente setados/limpos no codigo atual.
- Em especial, ha codigos ligados a logica de recarga automatica que nao aparecem implementados de forma completa nesta versao.

## Divergencias e lacunas importantes

### V4 no README vs V5 no codigo

- O `README.md` parece atrasado em relacao ao codigo atual.
- O codigo menciona V5 em `main.cpp`, `shared.h`, `task_nfc.h`, `task_nextion.h`, `task_serial.h`, `task_atuadores.h` e `task_erros.h`.

### Recarga automatica ainda nao esta fechada no codigo

O README descreve logica de recarga automatica baseada em percentual:

- iniciar com nivel <= 40%;
- parar com nivel >= 80%;
- telas e mensagens de fluxo.

O material adicional enviado pelo usuario descreve outra regra:

- iniciar recarga com nivel < 70%;
- parar recarga com nivel >= 80%;
- usar histerese.

Mas no codigo atual:

- existe leitura de sensores;
- existem atuadores;
- existem codigos de erro de recarga;
- porem nao aparece uma maquina de estados explicita unindo sensor + NFC + atuadores + Nextion para executar esse fluxo automatico de ponta a ponta.

Conclusao pratica:

- o firmware atual parece ter a base de infraestrutura pronta, mas a automacao completa de recarga nao esta claramente implementada nesta arvore.
- existe tambem divergencia entre regras de negocio documentadas: `40/80` no README antigo versus `70/80 com histerese` no material mais recente enviado pelo usuario.

### Multi-leitor NFC parcialmente implementado

- O boot e o diagnostico verificam 6 leitores.
- Os nomes de 3 canetas e 3 cartuchos existem.
- Mas a instancia operacional principal e unica de `Adafruit_PN532` esta fixa em `CS=13`.
- Portanto, leitura/escrita operacional parece estar efetivamente restrita ao leitor principal, salvo futuras mudancas.

### Texto e encoding

- O `README.md` e partes do output exibem sinais de encoding inconsistente.
- Se editar documentacao ou menus, manter um padrao consistente para evitar caracteres corrompidos.

### Estrutura de arquivos desejada vs implementada

O material do usuario descreve estrutura alvo com arquivos `.cpp` separados por task, por exemplo:

- `task_nfc.cpp`
- `task_sensor.cpp`
- `task_controle.cpp`
- `task_nextion.cpp`
- `task_serial.cpp`
- `task_led.cpp`

No estado atual do repositório, a implementacao esta concentrada em headers `.h` dentro de `src/`, incluidos diretamente por `main.cpp`.

Isso indica uma possivel refatoracao desejada para o futuro, mas ainda nao realizada.

## Convencoes uteis para futuros agentes

- Tratar o codigo em `src/` como fonte principal, nao o README.
- Quando houver conflito entre o codigo e a especificacao textual mais nova do usuario, registrar explicitamente a divergencia antes de alterar comportamento.
- Antes de alterar fluxo funcional, validar se o alvo e:
  - somente diagnostico/manual;
  - multi-leitor real;
  - automacao de recarga;
  - integracao com Nextion.
- Preservar mutexes de `I2C` e `SPI`; o firmware depende deles para evitar conflito entre tasks.
- Cuidado com pinos de boot do ESP32 usados como CS (`4`, `5`, `15`, `2`).
- `D2` acumula dupla funcao como CS do NFC #6 e LED onboard.
- `taskSerial` e `taskNextion` sao as melhores superfícies para diagnostico rapido.
- `taskI2CScan` ajuda muito a validar TCA e sensores sem instrumentacao extra.
- Nao introduzir `delay()`; manter `vTaskDelay()` e cooperacao entre tasks.
- Nao remover filas, mutexes ou sincronizacao existentes sem justificativa forte.
- Priorizar estabilidade operacional sobre arquitetura mais sofisticada.

## Perguntas em aberto

Estas respostas nao existem com seguranca neste workspace e precisariam vir do usuario ou de documentacao externa:

- Qual e o comportamento final esperado da recarga automatica?
- A regra correta de inicio de recarga e `<40%` ou `<70%`?
- Como o nivel em `pF` deve ser convertido para percentual real?
- A media de leitura e a calibracao de 5 segundos ja foram definidas formalmente ou ainda sao diretrizes?
- Existe um Nextion `.HMI` ou mapa completo de paginas/components fora deste repositório?
- A intencao e realmente operar 6 leitores NFC em runtime normal, ou apenas 1 leitor principal com 5 pontos reservados?
- Quais regras de negocio diferenciam caneta de cartucho no payload NFC?
- O serial do dock (`gSerialDock`) deve vir de configuracao persistente, NVS, etiqueta fisica ou build flag?
- A pinagem correta dos atuadores e a do codigo atual ou a da especificacao textual enviada pelo usuario?

## Arquivos para abrir primeiro em manutencao futura

- `src/main.cpp`
- `src/shared.h`
- `src/task_nfc.h`
- `src/task_sensor.h`
- `src/task_atuadores.h`
- `src/task_nextion.h`
- `src/task_serial.h`
- `src/task_erros.h`

## Resumo executivo

Este repositorio contem um firmware ESP32 relativamente bem estruturado para uma DockStation OTB associada a SmartPen, com:

- base multitask em FreeRTOS;
- infraestrutura de NFC, sensores, UI e atuadores;
- diagnostico serial e visual;
- tratamento basico de erros.

Os dois maiores pontos a verificar antes de qualquer evolucao relevante sao:

1. se a operacao multi-leitor NFC precisa ser real de ponta a ponta ou apenas diagnostica;
2. se a logica de recarga automatica descrita no README ainda precisa ser implementada ou apenas reintegrada.
