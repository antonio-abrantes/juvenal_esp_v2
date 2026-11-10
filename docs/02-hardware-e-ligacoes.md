# 02 — Hardware, conexões e cores dos fios

## Componentes

- **ESP32-S3 N16R8:** Flash **16 MB**, PSRAM **8 MB** (modo octal), CPU de referência 160 MHz, LED RGB onboard associado ao **GPIO48**.
- **INMP441:** microfone digital mono compatível com I2S. Alimentação **3,3 V**.
- Cabo USB de dados para programação e monitor serial. A placa de referência enumerou como **CH343 USB-Enhanced-SERIAL**.

Não há alto-falante ou amplificador documentado como conectado nesta primeira fase.

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

```text
INMP441                          ESP32-S3 N16R8
------                           ----------------
VDD    ---- (vermelho) --------> 3V3
GND    ---- (preto 1) ---------> GND
L/R    ---- (preto 2) ---------> GND  [LEFT]
SCK    ---- (laranja) ---------> GPIO12 [I2S BCLK]
SD     ---- (verde) -----------> GPIO10 [I2S DIN]
WS     ---- (amarelo) ---------> GPIO11 [I2S WS]

LED RGB integrado (WS2812 compatível): GPIO48
```

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
