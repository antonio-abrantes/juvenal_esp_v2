# 07 — Checklist de validação e evidências — v0.1.0

**Data da validação:** 10/11/2026. **Hardware:** ESP32-S3 N16R8, INMP441 I2S, LED RGB no GPIO48. **ESP-IDF:** 5.4.4.

## Validações confirmadas

- [x] ESP32-S3 inicializa sem reinícios de falha no teste.
- [x] Flash física de **16 MB** detectada/configurada; Flash em **DIO**, 80 MHz.
- [x] PSRAM **octal 8 MB, 40 MHz** detectada e teste de memória aprovado.
- [x] Leitura de áudio no INMP441 testada isoladamente em 16 kHz.
- [x] LED WS2812 GPIO48 acende amarelo, verde e azul no teste isolado.
- [x] Dependências TFLite Micro / MicroSpeechFeatures / LED compilam com patches locais.
- [x] Modelo `juvehnal.tflite` embarcado e inicializado no ESP32-S3.
- [x] Captura I2S iniciada via MicroWakeWord-IDF, sem segundo driver simultâneo.
- [x] Palavra de ativação **“Juvenal”** detectada localmente, com log.
- [x] Callback aciona tarefa FreeRTOS do LED.
- [x] LED executa 3 piscadas verdes na detecção e retorna ao amarelo.
- [x] Build e gravação na placa reportados como funcionais.

## Evidências representativas de boot

```text
I (...) esp_psram: Found 8MB PSRAM device
I (...) esp_psram: Speed: 40MHz
I (...) esp_psram: SPI SRAM memory test OK
I (...) JUVENAL: Iniciando Juvenal com MicroWakeWord
I (...) JUVENAL_LED: LED pronto: amarelo aguardando
I (...) JUVENAL: Configurando INMP441
I (...) JUVENAL: Configurando modelo juvehnal
I (...) micro_wake_word: Auto-setting model 'juvehnal' to sequential mode.
I (...) micro_wake_word: Setting up microWakeWord...
I (...) micro_wake_word: Micro Wake Word initialized
I (...) micro_wake_word: Actual tensor arena size is 28736
I (...) micro_wake_word: Preprocessor buffer allocated in internal RAM
I (...) i2s_audio.microphone: Microphone started
I (...) micro_wake_word: Model 'juvehnal' will run sequentially in producer task.
I (...) micro_wake_word: Configured 0 parallel models out of 1 total
I (...) JUVENAL: Juvenal aguardando palavra-chave
```

## Evidências representativas de ativação real

```text
W (...) JUVENAL: WAKE WORD DETECTADA: juvehnal
I (...) JUVENAL_LED: Wake word: 3 piscadas verdes
I (...) JUVENAL_LED: Aguardando: LED amarelo
```

O log de referência incluiu vários ciclos de detecção e animação no mesmo boot; em outros episódios, um log de detecção apareceu sem animação porque houve intervalo inferior ao cooldown.

## Testes ainda recomendados (não declarar concluídos)

- [ ] Quantificar falso positivo em ambiente silencioso e conversas sem a wake word.
- [ ] Testar detecção em diferentes distâncias/níveis de ruído.
- [ ] Medir consumo de CPU e memória em execução por período prolongado.
- [ ] Testar recuperação de falhas de I2S e perdas de alimentação.
- [ ] Implementar e validar rede/WebSocket; design do protocolo ainda pendente.
- [ ] Implementar áudio de saída e LED azul conforme estado real do servidor.
- [ ] Definir partições para OTA se essa função for adicionada.

## Como reproduzir a validação

Com o firmware compilado e gravado, abrir o monitor serial, verificar inicialização da PSRAM e do detector, pronunciar “Juvenal” e observar no mesmo ciclo: **mensagem de detecção -> três piscadas verdes -> amarelo permanente**. Registrar se a palavra foi pronunciada intencionalmente, para diferenciar detecções legítimas de falsos positivos.
