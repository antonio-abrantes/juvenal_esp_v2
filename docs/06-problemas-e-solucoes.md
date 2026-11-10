# 06 — Problemas reais encontrados e soluções

Esta seção registra erros de implantação ocorridos até a **v0.1.0**, separando falhas efetivas de avisos benignos. Mudanças de versão podem alterar as causas; investigue o erro atual antes de aplicar o mesmo patch.

## 1. `IDF_TARGET=esp32` conflitava com `set-target esp32s3`

**Sintoma:** falha ao configurar o target ESP32-S3, causada por variável de ambiente pré-existente definida como `esp32`.

**Solução no terminal PowerShell (somente sessão):**

```powershell
Remove-Item Env:IDF_TARGET -ErrorAction SilentlyContinue
idf.py set-target esp32s3
```

`set-target` pode recriar `sdkconfig` e invalidar build antigo. Não rodar sem conferir as configurações locais.

## 2. Perfil PowerShell do ESP-IDF não carregava

**Sintoma:** scripts de ativação da toolchain não inicializavam. A causa observada foi uma **aspa dupla extra** no final de uma entrada do `PATH` do Windows; depois de corrigir essa entrada e reabrir o editor, `idf.py --version` executou.

**Orientação:** verificar `PATH` nos escopos Máquina/Usuário/Processo; criar backup antes de editar qualquer variável de ambiente. Não copiar perfis ou caminhos de outra máquina.

## 3. `git rev-parse ... not a git repository`

**Sintoma:** mensagem durante o CMake: `Could not use 'git describe' to determine PROJECT_VER`.

**Causa:** a pasta do firmware ainda não estava inicializada como repositório Git. **Não impediu** a compilação. Depois de inicializar o Git na pasta certa (não em uma pasta superior), a identificação da versão pode evoluir; a mensagem não é falha de código.

## 4. Build aparentemente pequeno após adicionar `components/`

**Sintoma:** `Project build complete` após poucas etapas, ainda gerando binário do programa anterior (~248 KB).

**Causa:** configuração de componentes não havia sido regenerada e, sobretudo, ainda não havia código do `main/` utilizando o detector. Bibliotecas não usadas podem ser descartadas pelo linker.

**Ação que funcionou:**

```powershell
idf.py reconfigure
idf.py build
```

O build cresceu quando `juvenal_main.cpp` passou a referenciar o detector e o modelo.

## 5. `tensorflow/lite/core/c/common.h: No such file or directory`

**Erro real:**

```text
fatal error: tensorflow/lite/core/c/common.h: No such file or directory
```

**Diagnóstico:** a versão resolvida `espressif/esp-tflite-micro` **1.4.3** contém o arquivo em `tensorflow/lite/micro/c/common.h`, não no caminho antigo `core/c` esperado por uma cópia do MicroWakeWord-IDF.

**Correção aplicada nos três arquivos locais:**

```diff
- #include <tensorflow/lite/core/c/common.h>
+ #include <tensorflow/lite/micro/c/common.h>
```

Arquivos: `include/esphome/components/micro_wake_word/streaming_model.h`, `include/esphome/components/micro_wake_word/micro_wake_word.h`, `esphome/components/micro_wake_word/micro_wake_word.cpp` **sob `components/micro_wake_word_idf/`**.

Aplique no código local do componente, **não em `managed_components/`**.

## 6. `frontend_util.h: No such file or directory`

**Erro real:**

```text
fatal error: frontend_util.h: No such file or directory
```

**Diagnóstico emitido pelo ESP-IDF:** o cabeçalho público da biblioteca dependia de `ESPMicroSpeechFeatures`, mas essa dependência não era exportada para o `main/` (situação equivalente a `PRIV_REQUIRES` quando deveriam ser requisitos públicos).

**Correção:** editar `components/micro_wake_word_idf/CMakeLists.txt` para incluir em **`REQUIRES`**:

```cmake
"ESPMicroSpeechFeatures"
"espressif__esp-tflite-micro"
```

O build completo passou após essa correção.

## 7. `warning: "USE_ESP32" redefined`

**Aviso recorrente:** `USE_ESP32` foi passado mais de uma vez nas flags do compilador. Na versão testada, a duplicação **não impediu a compilação nem a detecção**. Não vale forçar a alteração do build agora sem verificar se há impacto em outros componentes.

## 8. `warning: ++ expression of volatile-qualified type is deprecated`

**Aviso em `micro_wake_word.cpp` (contador `current_epoch_`):** incremento de `volatile` está obsoleto em C++ moderno. Não bloqueou o build nem o funcionamento observado. **Não conclua** que `volatile` garante sincronização entre tarefas; futuras alterações devem avaliar o contexto concorrente antes de substituir o tipo.

## 9. Build reporta Flash 16 MB, mas partição de app 1 MB

**Não é um erro:** Flash física e tamanho da **partição factory** são grandezas diferentes. A v0.1.0 ainda cabe na partição factory padrão de **1 MB**, apesar da Flash de 16 MB. Para OTA e recursos de rede futuros, criar tabela de partições apropriada em uma nova etapa, com validação e cuidado para não sobrescrever partições/firmware inadvertidamente.

## 10. Detecções repetidas no log

Durante o teste da wake word houve sequências de detecções em intervalos relativamente curtos. O firmware final introduziu **cooldown de quatro segundos para o LED**, mas **não para a mensagem `WAKE WORD DETECTADA`**. Antes de ajustar `probability_cutoff`, medir falsos positivos em silêncio, fala comum e repetições intencionais.

## 11. Modelo/Flash carregados, mas o LED não pisca

Verifique se a build contém o callback chamando `juvenal_led_wake_detected()`, se o LED é o endereçável do `GPIO48`, se a tarefa FreeRTOS foi criada, e se o log traz `Wake word: 3 piscadas verdes`. O exemplo inicial de LED executava uma **sequência na inicialização**, independente de wake word. Não confunda os dois testes.

## 12. Sem erros, mas a palavra não é detectada

Revisar `L/R -> GND`, BCLK 12 / DIN 10 / WS 11, 3V3, canal esquerdo, 16 kHz, tamanho dos buffers, tamanho de arena, faixa de ganho, distância/ruído ambiente e versão exata do `.tflite`. Não assumir defeito do modelo sem primeiro medir entrada de áudio.
