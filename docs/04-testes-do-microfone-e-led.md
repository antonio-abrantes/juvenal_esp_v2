# 04 — Testes isolados antes da integração

O firmware foi construído incrementalmente para separar problemas de **fiação**, **driver I2S**, **LED endereçável** e **motor de inferência**. Estes testes históricos **já passaram**; não precisam ser repetidos na placa validada, a menos que a configuração seja alterada.

## 1. Teste inicial do microfone INMP441

O primeiro teste foi feito com Arduino (driver legado `driver/i2s.h`), seguido de teste nativo **ESP-IDF 5.4.4** com `driver/i2s_std.h`. Este segundo teste confirmou a **fiação final**, não apenas a montagem preliminar.

### Configuração da captura

```c
#define MIC_WS  GPIO_NUM_11
#define MIC_SD  GPIO_NUM_10
#define MIC_SCK GPIO_NUM_12
#define SAMPLE_RATE 16000

// I2S_NUM_0 em modo master, canal esquerdo, amostras de 32 bits.
i2s_chan_config_t chan_cfg =
    I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
chan_cfg.dma_desc_num = 8;
chan_cfg.dma_frame_num = 256;

i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = MIC_SCK,
        .ws = MIC_WS,
        .dout = I2S_GPIO_UNUSED,
        .din = MIC_SD
    }
};
std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
```

Inicializou o RX com `i2s_new_channel`, `i2s_channel_init_std_mode`, `i2s_channel_enable` e leu blocos de `int32_t samples[256]` usando `i2s_channel_read(...)`. Para medir o nível no teste, cada amostra era convertida aproximadamente a PCM16 por `samples[i] >> 16`; foi calculada a **média do valor absoluto** ao longo de um segundo.

O teste reportou valores típicos próximos de **5–7 em ambiente silencioso**, aumentando para dezenas/centenas ao falar. **Essa métrica não é dB nem fornece limiar de wake word.** O objetivo era comprovar que chegavam amostras válidas do INMP441.

> O firmware atual **não usa esse laço I2S de teste**, pois a biblioteca MicroWakeWord-IDF é a única responsável pelo microfone. Evite dois donos do periférico simultaneamente.

## 2. Teste isolado do LED RGB integrado

O pino `GPIO48` foi identificado no pinout da placa e testado com **`espressif/led_strip`** (WS2812, formato GRB, periférico RMT). Inicialmente, a sequência automática foi:

1. Amarelo de baixa intensidade: `(20, 14, 0)`.
2. **3 piscadas verdes**: `(0, 28, 0)` / apagado, cerca de 200 ms por etapa.
3. **Azul**: `(0, 0, 28)` por 2 segundos.
4. **Amarelo permanente**: `(20, 14, 0)`.

Todas as cores foram confirmadas **visualmente** no dispositivo. O microfone continuou reportando os níveis sem interrupção.

### API do LED usada no teste

```c
led_strip_config_t strip_cfg = {
    .strip_gpio_num = 48,
    .max_leds = 1,
    .led_model = LED_MODEL_WS2812,
    .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
};

led_strip_rmt_config_t rmt_cfg = {
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = 10000000,
    .mem_block_symbols = 64,
    .flags = {.with_dma = false},
};

// led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &led);
// led_strip_set_pixel(led, 0, r, g, b);
// led_strip_refresh(led);
```

A partir daí, o componente `juvenal_led.c/.h` foi integrado ao detector real.

## 3. Integração LED + callback da wake word

O `juvenal_main.cpp` registra um callback `add_detection_callback(...)`, que registra a detecção no monitor serial e chama `juvenal_led_wake_detected()`.

Na versão funcional:

- A função de callback **não espera 3 piscadas**.
- A chamada do callback notifica uma tarefa FreeRTOS com `xTaskNotifyGive`.
- Essa tarefa recebe a notificação com `ulTaskNotifyTake`, executa as três piscadas de **180 ms ligadas / 150 ms desligadas** e volta ao amarelo.
- `WAKE_COOLDOWN_MS = 4000` limita a frequência de **animações**, não a emissão de logs de detecção.

**Evidência final confirmada:**

```text
I (...) JUVENAL_LED: LED pronto: amarelo aguardando
I (...) micro_wake_word: Micro Wake Word initialized
I (...) i2s_audio.microphone: Microphone started
I (...) JUVENAL: Juvenal aguardando palavra-chave
W (...) JUVENAL: WAKE WORD DETECTADA: juvehnal
I (...) JUVENAL_LED: Wake word: 3 piscadas verdes
I (...) JUVENAL_LED: Aguardando: LED amarelo
```

O log comprova a execução dos caminhos de código; a validação física da placa foi reportada como satisfatória. Para teste de outros lotes de placa, conferir também visualmente a cor e a duração.
