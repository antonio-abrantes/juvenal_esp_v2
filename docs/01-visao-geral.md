# 01 — Visão geral e arquitetura

**Referência histórica:** as seções abaixo descrevem a v0.1.0 comprovada. O código corrente recebeu a integração WS v1/face/painel e TX opcional na fase 03, ainda sem build ou validação integrada. Veja [o guia atual](10-integracao-servidor-ws.md).

## Objetivo

Criar um assistente físico autônomo, **sem exigir Home Assistant ou ESPHome**, capaz de ouvir continuamente sua palavra-chave com inferência local no ESP32-S3 e, em etapas posteriores, conectar-se a um servidor próprio de voz.

## O que a versão 0.1.0 realmente faz

```text
INMP441 (3V3, I2S 16 kHz, dados de 32 bits)
       |
       v
MicroWakeWord-IDF / captador I2S (um único dono do periférico)
       |
       v
ESPMicroSpeechFeatures (extração dos recursos de áudio)
       |
       v
TensorFlow Lite Micro -> juvehnal.tflite (modelo local na Flash)
       |
       v
Callback de detecção -> log + juvenal_led_wake_detected()
       |
       v
Tarefa FreeRTOS do LED: amarelo -> 3 verdes -> amarelo
```

Todos os blocos acima foram exercitados no dispositivo real durante o teste funcional. Na inicialização, a memória externa foi detectada (8 MB PSRAM octal a 40 MHz) e passou no autoteste. O motor reportou uma arena de tensores real de **28.736 bytes**, com limite configurado de **30.000**.

## Estados visuais

| Estado | RGB aproximado | Funcionamento |
|---|---|---|
| Escuta/ocioso | `(20, 14, 0)` | Amarelo fraco contínuo |
| Detecção | `(0, 28, 0)` | 3 piscadas, 180 ms ligadas + 150 ms desligadas |
| Após detecção | `(20, 14, 0)` | Retorno automático ao amarelo |
| Comunicação / processamento | Azul, a definir | **Planejado, não implementado** |

O callback registra **todas** as detecções no log. Um intervalo de **4 segundos** limita **apenas o acionamento visual**. Portanto, é normal ver um aviso `WAKE WORD DETECTADA` durante o cooldown sem uma nova animação.

## Fora do escopo nesta versão

Não há conexão Wi-Fi, WebSocket, envio de áudio, autenticação de servidor, STT, agente conversacional, TTS, alto-falante, captura da fala após ativação nem OTA. Um servidor de voz existente poderá ser integrado futuramente, mas seu protocolo não foi definido neste repositório. Não confundir a primeira wake word funcional com um assistente conversacional completo.

## Organização do firmware

- `main/juvenal_main.cpp`: cria os objetos `I2SAudioMicrophone` e `MicroWakeWord`, configura GPIOs, taxa, arena, modelo, callback e laço de status.
- `main/juvenal_led.c/.h`: inicializa LED endereçável via RMT e recebe notificações sem bloquear a tarefa de detecção.
- `main/juvehnal.tflite`: conteúdo do modelo (63.520 bytes) incorporado no link via `EMBED_FILES`.
- `main/CMakeLists.txt`: registra o firmware C++ e C, inclui o modelo e requer `micro_wake_word_idf` e `led_strip`.
- `main/idf_component.yml`: declara `espressif/led_strip` (range `^3.0.0` na montagem validada).
- `components/micro_wake_word_idf`: cópia local de componente de terceiros, **com quatro arquivos corrigidos para compilação**.

## Critério de pronto da v0.1.0

O boot não reinicia nem apresenta erros fatais; microfone inicializa; modelo é carregado; a palavra pronunciada em voz alta gera mensagem de detecção; o LED pisca três vezes em verde e volta a amarelo. Esse resultado foi confirmado no ESP32-S3 real em **10/11/2026**.
