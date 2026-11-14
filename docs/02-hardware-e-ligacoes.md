# 02 — Hardware, conexões e cores dos fios

## Componentes

- **ESP32-S3 N16R8:** Flash **16 MB**, PSRAM **8 MB** (modo octal), CPU de referência 160 MHz, LED RGB onboard associado ao **GPIO48**.
- **INMP441:** microfone digital mono compatível com I2S. Alimentação **3,3 V**.
- **GC9A01:** display SPI da face LVGL; pinagem preservada abaixo.
- **MAX98357A + alto-falante:** conectados e funcionais em teste isolado no ESP-IDF5.4.4, conforme evidência do usuário em 13/11/2026. Alimentação USB5V com GND comum; resposta do assistente ainda não validada.
- Cabo USB de dados para programação e monitor serial. A placa de referência enumerou como **CH343 USB-Enhanced-SERIAL**.

**Atualização da montagem:** MAX98357A validado com som real no projeto separado juvenal_audio_teste. Não repetir diagnóstico Arduino/5VIN nem confundir essa evidência com playback integrado por WS. O teste temporário substituiu o firmware na placa, não os arquivos deste projeto.

## Mapeamento físico confirmado

| Cor nesta montagem | Saída INMP441 | Entrada ESP32-S3 | Observação |
|---|---|---|---|
| **Vermelho** | `VDD` | `3V3` | **Somente 3,3 V**; nunca `5V` ou `VIN` |
| **Preto 1** | `GND` | `GND` | Terra elétrico |
| **Preto 2** | `L/R` | `GND` | Seleciona o **canal esquerdo** |
| **Laranja** | `SCK` / `BCLK` | `GPIO12` | Clock de bits do I2S |
| **Verde** | `SD` / `DOUT` | `GPIO10` | Fluxo digital de áudio |
| **Amarelo** | `WS` / `LRCLK` | `GPIO11` | Clock de quadro/canal I2S |

**Os dois fios pretos são distintos**: um parte do `GND` do microfone e o outro parte de `L/R`. Ambos terminam no `GND` do ESP32. As cores são as escolhidas para esta unidade e não devem ser aplicadas cegamente a outros conjuntos de fios.

## Inventário de GPIOs e coexistência

| Periférico | Sinais / GPIOs |
|---|---|
| INMP441, RX I2S_NUM_0 | BCLK12, WS11, DOUT10; VDD3V3, L/R aterrado |
| GC9A01, SPI | SCLK14, MOSI13, DC9, CS8, RST7 |
| LED WS2812 | GPIO48 |
| MAX98357A, TX I2S_NUM_1 | BCLK15, LRC/WS16, DIN17 |

A pinagem nova não conflita com os sinais anteriores. RX continua sob controle exclusivo do MicroWakeWord, compartilhado via subscribe; o driver TX separado não cria outro capturador nem usa GPIO18.

## MAX98357A — fiação física validada

| Cor | Pino do módulo | Destino / condição validada |
|---|---|---|
| Roxo | BCLK | GPIO15 |
| Verde | LRC / WS | GPIO16 |
| Cinza | DIN | GPIO17, dados TX do ESP |
| Vermelho | VIN | +5 V da alimentação USB externa ao pino 5VIN da placa |
| Preto | GND | Negativo da fonte USB e GND comum ao ESP32 |
| Sem fio | GAIN | Desconectado no teste funcional |
| Sem fio | SD | Desconectado no teste funcional; não presumir jumper para VIN/3V3 |
| — | SPK+ / SPK- | Terminais positivo/negativo do alto-falante; saída diferencial/BTL |

~~~mermaid
flowchart TB
    USB["Fonte USB do amplificador"]
    ESP["ESP32-S3 N16R8"]
    AMP["MAX98357A"]
    SPK["Alto-falante"]
    MIC["INMP441"]
    USB -->|"VIN: +5 V; GND: negativo"| AMP
    USB -->|"Somente GND comum"| ESP
    MIC -->|"BCLK12, WS11, DOUT10; VDD3V3/GND"| ESP
    ESP -->|"BCLK15, WS16, DOUT17"| AMP
    AMP -->|"SPK+ e SPK-; saída BTL"| SPK
~~~

O ESP mantém sua própria alimentação. Se forem usadas duas fontes USB, compartilhar somente os negativos, sem interligar os positivos. Não ligar SPK- ao GND nem +5 V externo em GPIO/3V3. Montar/desmontar com fontes desligadas.

### Alimentação e limite das medições

O pino 5VIN desta placa não forneceu 5 V no diagnóstico com alimentação USB; foi relatada leitura próxima de -0,25 V naquela medição. O jumper IN-OUT não foi soldado/modificado. A solução que produziu som foi VIN pela linha USB5V e terra comum ao ESP, sem depender do 5VIN da placa.

Foi relatada leitura de aproximadamente 5,20 V na fonte, mas o ponto da medição final não foi documentado; não tratar como medição certificada em VIN/GND do módulo. Potência/impedância do alto-falante e qualidade objetiva não foram informadas. Preservar a montagem funcional, sem inventar especificações.

### Evidência já concluída e integração pendente

No projeto juvenal_audio_teste, o usuário compilou/gravou ESP-IDF5.4.4 e monitorou COM5: rádio SomaFM Groove Salad com HTTP200, MP3 44.100Hz/estéreo/128kbps, mais de2000 frames processados e som confirmado depois de corrigir a alimentação. Isso comprova o caminho físico I2S/amplificador/alto-falante.

O assistente usa outro formato: WS v1 com PCM16 mono16k, sem MP3/rádio. O driver da fase03 usa I2S_NUM_1 TX e replica mono nos slots estéreo, mantendo half-duplex, buffer/flow control e confirmação depois do DMA. O usuário posteriormente mostrou três sessões completas desse caminho com playback confirmado no servidor e nova wake no ESP. Qualidade acústica e falhas ainda precisam de avaliação. PIPER_VOLUME controla a geração no servidor; o principal agora implementa atenuação local0–100% no painel autenticado, após receber PCM. Esse controle e a boca por envelope pós-volume ainda não foram validados na placa. GAIN/SD físicos permanecem como na montagem validada.


## Restrições elétricas e segurança

1. Desconecte a placa da alimentação antes de montar ou mover fios.
2. Verifique os nomes **impressos nos pinos do módulo INMP441**, não só a cor do cabo.
3. **Não aplique 5 V no VDD** do INMP441.
4. Evite conexões frouxas e mantenha o retorno de GND estável para diminuir ruído.
5. O GPIO48 aciona o LED **já integrado à placa**; não é necessário adicionar LED externo para reproduzir este teste.
6. Não altere os GPIOs de áudio ao copiar trechos de exemplos de outras placas.

## Configuração de áudio confirmada

| Propriedade | Valor de referência |
|---|---|
| Controlador I2S | `I2S_NUM_0` |
| Taxa de amostragem | **16.000 Hz** |
| Formato de leitura física | **32 bits** |
| Canal | **Esquerdo** (`L/R` aterrado) |
| PCM16 no teste inicial | Conversão a partir de `int32_t` usando deslocamento de 16 bits para medição de nível |
| Driver em produção v0.1.0 | `I2SAudioMicrophone` fornecido pela biblioteca MicroWakeWord-IDF |

A taxa de 16 kHz e a seleção do canal foram verificadas durante os testes independentes. O motor de wake word gerencia as características de áudio; não se deve alimentá-lo diretamente com PCM bruto no lugar das features.
