# README_AI.md

## SmartPen DockStation OTB

Este arquivo serve como ponto de entrada rapido para agentes de IA trabalhando neste repositório.

Para contexto aprofundado, arquitetura real do firmware, divergencias entre especificacao e implementacao, riscos e perguntas em aberto, ler tambem:

- `PROJECT_CONTEXT_DOCK_SMARTPEN.md`

## Resumo do projeto

Firmware para uma DockStation inteligente baseada em ESP32, voltada para SmartPen, com os seguintes subsistemas:

- NFC com PN532 para identificacao de canetas e cartuchos;
- sensores capacitivos de nivel;
- controle de bomba e valvulas;
- display Nextion;
- multitarefa com FreeRTOS.

## Objetivo funcional

O sistema deve ser capaz de:

- identificar automaticamente canetas e cartuchos;
- monitorar nivel de tinta;
- controlar recarga;
- exibir status em tempo real;
- operar continuamente com estabilidade.

## Regras obrigatorias para manutencao do codigo

- Nao quebrar a arquitetura multitarefa existente.
- Nao remover filas, mutexes ou sincronizacao sem necessidade real.
- Nao usar `delay()`.
- Usar `vTaskDelay()` e abordagens cooperativas.
- Manter compatibilidade com ESP32 e PlatformIO.
- Priorizar estabilidade sobre complexidade.

## Componentes principais esperados

- ESP32 WROOM
- 6 leitores PN532 via SPI
- 3 sensores capacitivos via I2C
- 1 TCA9548A
- 1 ULN2003
- 1 display Nextion

## Tasks esperadas

- `taskNFC`
- `taskSensor`
- logica de controle/recarga
- `taskNextion`
- `taskSerial`
- `taskLED`

Observacao:

- O codigo atual inspecionado nao possui uma `task_controle` explicita separada.
- A base de verdade da implementacao atual esta detalhada em `PROJECT_CONTEXT_DOCK_SMARTPEN.md`.

## Comunicacao entre tasks

Estruturas de sincronizacao esperadas:

- `QueueHandle_t`
- `SemaphoreHandle_t`

Filas relevantes no firmware atual:

- `qTagData`
- `qNextionData`
- `qSerialCmd`
- `qSerialResp`
- `qControleCmd`
- `qActCmd`

## NFC

- Interface SPI
- 6 leitores com CS dedicado
- necessidade de evitar leitura duplicada
- tags armazenam dados como identificacao e ciclo de vida

Atencao:

- O runtime atual parece operar principalmente sobre o leitor principal, apesar de validar 6 leitores no boot.

## Sensores de nivel

- Interface I2C via TCA9548A
- leituras de capacitancia
- idealmente com media, mitigacao de ruido e calibracao

Atencao:

- O firmware atual le valores `raw` e `pF`, mas nem toda a estrategia de calibracao/media descrita em especificacoes aparece implementada.

## Recarga

Existe intencao de produto para recarga automatica com histerese.

Atencao:

- Ha divergencia entre documentos sobre a regra de disparo:
  - um material descreve inicio em `< 40%`;
  - outro descreve inicio em `< 70%` e parada em `>= 80%`.
- Confirmar a regra correta antes de implementar ou ajustar automacao.

## Nextion

- usar `Serial2`
- manter atualizacao nao bloqueante
- evitar travar o loop das tasks

## Diretriz para futuros agentes

Antes de alterar comportamento, verificar primeiro:

1. o que o codigo atual realmente faz;
2. o que a especificacao deseja;
3. onde ha divergencia entre hardware, README e firmware.

Se houver conflito, registrar a diferenca explicitamente em vez de assumir.
