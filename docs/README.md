# Documentação — Juvenal ESP32-S3 v0.1.0

Esta documentação descreve **o primeiro marco realmente testado na placa**: escuta contínua, wake word “Juvenal” reconhecida localmente e LED sinalizando a detecção com três piscadas verdes.

## Roteiro de leitura

1. [Visão geral, arquitetura e escopo](01-visao-geral.md) — capacidades comprovadas e limites.
2. [Hardware, ligação dos seis fios e LED](02-hardware-e-ligacoes.md) — fiação precisa e cuidados elétricos.
3. [Instalação do ambiente e compilação](03-preparacao-do-ambiente.md) — Windows, ESP-IDF, PSRAM, Flash e porta serial.
4. [Histórico dos testes do microfone e do LED](04-testes-do-microfone-e-led.md) — como foram isolados e validados.
5. [Integração microWakeWord e modelo](05-integracao-do-micro-wake-word.md) — bibliotecas, dependências, patches, callback.
6. [Problemas encontrados e soluções](06-problemas-e-solucoes.md) — registro de erros reais e alertas não bloqueantes.
7. [Checklist e evidências da v0.1.0](07-checklist-de-validacao.md) — definição de pronto e exemplos de logs.
8. [Versionamento, GitHub e segurança](08-versionamento-e-publicacao.md) — o que versionar, como publicar e marcar a versão.
9. [Dependências, referências e licenças](09-dependencias-e-licencas.md) — origem do código e obrigações de distribuição.

**Compatibilidade documentada:** ESP-IDF 5.4.4, ESP32-S3 N16R8, MicroWakeWord-IDF obtido de `bibidentuhanoi/MicroWakeWord-IDF` e adaptado localmente, `esp-tflite-micro` 1.4.3, `led_strip` 3.1.1, `esp-nn` 1.4.2. Alterar versões sem testes pode invalidar os patches.

## Sobre a versão

A marcação **v0.1.0** é a sugestão de release para o estado confirmado em 10/11/2026. O tag Git só existirá quando for criado e enviado pelo responsável do repositório. Não confundir a versão de release com o texto `App "hello_world" version: 1` ainda exibido pelo firmware de referência.
