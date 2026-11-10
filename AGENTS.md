# Orientações para agentes de IA — Juvenal ESP32-S3

**Leia este arquivo e `README.md` antes de modificar o firmware.** Este repositório corresponde exclusivamente ao firmware do assistente Juvenal; não contém servidor de voz.

## Estado de referência

- **v0.1.0 validada fisicamente** em ESP32-S3 N16R8 com ESP-IDF **5.4.4** em 10/11/2026.
- INMP441: `VDD->3V3`, `GND->GND`, `L/R->GND`, `BCLK->GPIO12`, `DOUT->GPIO10`, `WS->GPIO11`.
- LED WS2812 onboard: `GPIO48`.
- `main/juvenal_main.cpp`: inicialização do microfone e wake word; **somente o componente MicroWakeWord-IDF controla o I2S**.
- `main/juvenal_led.c/.h`: controle visual separado, tarefa FreeRTOS, resfriamento de 4 s para animação.
- `main/juvehnal.tflite`: modelo de **63.520 B**, embarcado via `EMBED_FILES`.
- Os quatro arquivos de `components/micro_wake_word_idf/` corrigidos localmente **precisam ser mantidos**; ver `docs/05-integracao-do-micro-wake-word.md`.
- v0.1.0 comprovada: wake word detectada -> 3 piscadas verdes -> amarelo de espera. Azul, transporte de áudio e speaker são **futuros**.

## Regras operacionais

1. Não mudar GPIOs, modo PSRAM, flash ou driver do microfone sem necessidade comprovada.
2. Não adicionar outro controlador I2S simultâneo ao MicroWakeWord-IDF.
3. Preservar o modelo, `main/idf_component.yml`, `dependencies.lock` e o componente de terceiros *corrigido*.
4. Não editar `managed_components/` como solução definitiva: é regenerado pelo gerenciador de componentes.
5. Não habilitar Octal Flash: a montagem usa Flash DIO e **PSRAM octal**.
6. Antes de gravar firmware ou alterar partições, avisar sobre substituição do firmware atual e validar build.
7. Não criar push, tags, commits ou reset automático sem autorização explícita.
8. Sem Wi-Fi/credenciais embutidas no código público. Nunca inserir segredos no Git.
9. Ao investigar falhas, diagnosticar uma causa por vez; ver `docs/06-problemas-e-solucoes.md`.
10. Manter documentação e código sincronizados quando evoluir o projeto. Distinguir o que foi testado na placa do que foi apenas compilado/proposto.
11. Observar as obrigações da licença GPLv3 da biblioteca de terceiros; preservar `components/micro_wake_word_idf/LICENSE.md`.

## Ordem sugerida de leitura

`README.md` -> `docs/01-visao-geral.md` -> `docs/02-hardware-e-ligacoes.md` -> `docs/03-preparacao-do-ambiente.md` -> `docs/05-integracao-do-micro-wake-word.md` -> `docs/07-checklist-de-validacao.md`.
