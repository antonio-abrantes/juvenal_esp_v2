# 09 — Dependências, procedência e licenças

Este firmware depende de componentes terceiros e de um modelo de wake word previamente publicado. Antes de distribuir o projeto completo, é necessário respeitar suas licenças e manter as atribuições.

## Componentes utilizados

| Item | Fonte | Registro de referência |
|---|---|---|
| ESP-IDF | https://github.com/espressif/esp-idf | Versão 5.4.4 |
| MicroWakeWord-IDF | https://github.com/bibidentuhanoi/MicroWakeWord-IDF | Cópia local alterada; `LICENSE.md` = **GPLv3** |
| TensorFlow Lite Micro para Espressif | https://components.espressif.com/components/espressif/esp-tflite-micro | 1.4.3 |
| ESP-NN | https://components.espressif.com/components/espressif/esp-nn | 1.4.2 |
| LED Strip | https://components.espressif.com/components/espressif/led_strip | 3.1.1 |
| ESPMicroSpeechFeatures | https://github.com/0xD34D/ESPMicroSpeechFeatures | Commit resolvido `c2a077454c24d71fc84be377bb6e28725f05c333` |
| Arquivos da wake word | https://github.com/antonio-abrantes/microWakeWords | `juvehnal.tflite` e `juvehnal.json` |

## Atenção — GPLv3 no componente de reconhecimento

O repositório `bibidentuhanoi/MicroWakeWord-IDF` **declara GPLv3** e contém `LICENSE.md` com a licença completa. O firmware integra esse código diretamente no executável e mantém cópia adaptada no diretório `components/`.

**Consequência prática:** não excluir o `LICENSE.md`, não apresentar o código de terceiros como autoria própria e, **antes de distribuir firmware e fonte como um projeto único**, escolher um esquema de licenciamento compatível e atender às obrigações da GPLv3, incluindo disponibilização do código-fonte correspondente quando aplicável. A obrigação exata pode depender de como o software é combinado e distribuído; em caso de dúvida, obter análise jurídica. Não assumir que “sem arquivo LICENSE na raiz” significa “licença livre de obrigações”.

Este kit **não gera automaticamente um arquivo `LICENSE` na raiz**, pois a decisão sobre a licença do código próprio cabe ao titular. A existência de componentes GPLv3 precisa ser considerada nessa decisão. Inspecione também as licenças dos demais componentes e do modelo antes da primeira publicação.

## Registro de mudanças no código de terceiros

Foram feitos patches locais para compatibilidade com `esp-tflite-micro` 1.4.3 e exportação de dependências públicas:

1. `streaming_model.h`: include antigo `core/c/common.h` -> `micro/c/common.h`.
2. `micro_wake_word.h`: mesma correção.
3. `micro_wake_word.cpp`: mesma correção.
4. `CMakeLists.txt` local: `REQUIRES` exporta `ESPMicroSpeechFeatures` e `espressif__esp-tflite-micro`.

Consulte [integração detalhada](05-integracao-do-micro-wake-word.md) e preserve esse registro junto com o código alterado.

## Versões e manutenção

O `dependencies.lock` documenta as versões efetivamente resolvidas no ambiente de referência. Se a dependência ou o IDF mudar, rode build e teste funcional na placa novamente, revise licenças, atualize a documentação de compatibilidade e faça um novo release **em vez de alterar a base v0.1.0 retroativamente**.
