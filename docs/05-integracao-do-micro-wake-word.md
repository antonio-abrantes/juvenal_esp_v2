# 05 — Integração do MicroWakeWord-IDF e do modelo

## Visão geral das dependências

Esta fase utiliza `MicroWakeWord-IDF`, componente C++ adaptado da implementação de wake word usada no ecossistema ESPHome, **sem requerer o ambiente ESPHome nem Home Assistant**. O projeto usa o componente local em `components/micro_wake_word_idf/` e o gerenciador de componentes ESP-IDF para resolver as dependências:

| Pacote | Referência validada | Finalidade |
|---|---|---|
| MicroWakeWord-IDF | Código local obtido de `bibidentuhanoi/MicroWakeWord-IDF`, com patches | I2S, features, inferência e callback |
| `espressif/esp-tflite-micro` | **1.4.3** | Inferência TFLite Micro |
| `ESPMicroSpeechFeatures` | commit `c2a077454c24d71fc84be377bb6e28725f05c333` | Extração das características do áudio |
| `espressif/esp-nn` | **1.4.2** | Otimizações de operações |
| `espressif/led_strip` | **3.1.1** | LED WS2812 via RMT |
| ESP-IDF | **5.4.4** | Sistema operacional, compilador, drivers |

O `MicroWakeWord-IDF` foi originalmente obtido do ZIP do ramo `main`. **Para releases reproduzíveis, a cópia local modificada precisa ser incluída no Git; não depender de baixar `main.zip` no build.** A versão/fonte *exata* da cópia original não foi registrada como SHA nesta fase. Consulte o histórico de commits ao evoluir a biblioteca.

## Passo a passo da integração que foi realizada

### 1. Biblioteca do LED

No diretório raiz do projeto ESP-IDF:

```powershell
idf.py add-dependency "espressif/led_strip^3.0.0"
```

O comando criou `main/idf_component.yml` e resolveu `led_strip` 3.1.1 no ambiente de referência.

### 2. Adição do componente MicroWakeWord-IDF

Código-fonte: [bibidentuhanoi/MicroWakeWord-IDF](https://github.com/bibidentuhanoi/MicroWakeWord-IDF).

Foi extraído diretamente para `components/micro_wake_word_idf/`, mantendo os diretórios `esphome/`, `include/` e os arquivos `CMakeLists.txt`, `idf_component.yml`, `Kconfig`, `LICENSE.md` e `README.md`.

Em seguida:

```powershell
idf.py reconfigure
```

Essa reconfiguração resolveu também `esp-tflite-micro`, `esp-nn` e `ESPMicroSpeechFeatures`.

### 3. Quatro patches que permitiram compilar com TFLite Micro 1.4.3

**Três includes** esperavam um caminho antigo que não existia na versão instalada:

```diff
- #include <tensorflow/lite/core/c/common.h>
+ #include <tensorflow/lite/micro/c/common.h>
```

Essa troca foi aplicada aos arquivos **dentro do componente local**:

```text
components/micro_wake_word_idf/include/esphome/components/micro_wake_word/streaming_model.h
components/micro_wake_word_idf/include/esphome/components/micro_wake_word/micro_wake_word.h
components/micro_wake_word_idf/esphome/components/micro_wake_word/micro_wake_word.cpp
```

**Quarto patch:** no `components/micro_wake_word_idf/CMakeLists.txt`, `REQUIRES` passou a exportar `ESPMicroSpeechFeatures` e `espressif__esp-tflite-micro` para o `main/`, pois o cabeçalho público do componente inclui `frontend_util.h` e referências do TFLite.

```cmake
idf_component_register(
    # ... SRCS e INCLUDE_DIRS originais preservados ...
    REQUIRES "esp_common"
             "esp_driver_i2s"
             "esp_psram"
             "esp_driver_gpio"
             "esp_system"
             "esp_timer"
             "freertos"
             "log"
             "ESPMicroSpeechFeatures"
             "espressif__esp-tflite-micro"
)
```

**Importante:** esse bloco ilustra os `REQUIRES`, não substitui automaticamente o `CMakeLists.txt` real. No primeiro commit, **versione os quatro arquivos corrigidos** junto com o restante do componente e preserve seu `LICENSE.md` GPLv3. Não reedite o `managed_components/`: é gerado, e os ajustes seriam perdidos.

### 4. Modelo de wake word

Fonte pública de referência:

- [`juvehnal.tflite`](https://raw.githubusercontent.com/antonio-abrantes/microWakeWords/main/microWakeWords/juvehnal.tflite)
- [`juvehnal.json`](https://raw.githubusercontent.com/antonio-abrantes/microWakeWords/main/microWakeWords/juvehnal.json)

O modelo `.tflite` tem **63.520 bytes**, foi copiado para `main/juvehnal.tflite` e é **arquivo necessário ao build**, não um artefato temporário. O JSON de metadados tem **432 bytes** no arquivo de referência baixado; ele não é necessário à compilação, mas é útil para rastrear parâmetros.

Valores confirmados do JSON:

```json
{
  "type": "micro",
  "wake_word": "juvehnal",
  "model": "juvehnal.tflite",
  "trained_languages": ["en"],
  "version": 2,
  "micro": {
    "probability_cutoff": 0.97,
    "sliding_window_size": 5,
    "feature_step_size": 10,
    "tensor_arena_size": 30000,
    "minimum_esphome_version": "2024.7.0"
  }
}
```

Esse trecho mostra os **parâmetros operacionais**; os campos de autoria e website estão no arquivo JSON original. `trained_languages: ["en"]` é o metadado da configuração publicada, **não a afirmação de que o usuário deve pronunciar em inglês**. O nome de ativação esperado é “Juvenal”, em português.

### 5. Incorporar o `.tflite` na Flash

O `main/CMakeLists.txt` validado usa:

```cmake
idf_component_register(
    SRCS "juvenal_main.cpp" "juvenal_led.c"
    INCLUDE_DIRS "."
    EMBED_FILES "juvehnal.tflite"
    REQUIRES esp_driver_i2s led_strip micro_wake_word_idf
)
```

Em `juvenal_main.cpp`, os símbolos `asm("_binary_juvehnal_tflite_start")` e `asm("_binary_juvehnal_tflite_end")` dão acesso ao modelo incorporado no binário. Não duplique o arquivo TFLite em vários locais versionados sem necessidade.

### 6. Configuração do microfone e reconhecimento

A integração em C++ cria `esphome::i2s_audio::I2SAudioMicrophone` e `esphome::micro_wake_word::MicroWakeWord`.

```cpp
microphone.set_bclk_pin(GPIO_NUM_12);
microphone.set_lrclk_pin(GPIO_NUM_11);
microphone.set_din_pin(GPIO_NUM_10);
microphone.set_sample_rate(16000);
microphone.set_bits_per_sample(I2S_DATA_BIT_WIDTH_32BIT);

wake_word.set_microphone(&microphone);
wake_word.set_features_step_size(10);
wake_word.add_wake_word_model(
    juvenal_model_start,
    0.97f,
    5,
    "juvehnal",
    30000
);
```

O código também configura `I2S_NUM_0`, `dma_desc_num = 8`, `dma_frame_num = 256`, inicializa o microfone, registra callback e inicia o detector. **Somente o driver da biblioteca controla o microfone na versão final.** O antigo `main/juvenal_main.c` é um teste histórico e **não deve ser compilado junto**.

O callback usa `ESP_LOGW(TAG, "WAKE WORD DETECTADA: %s", word.c_str())` e `juvenal_led_wake_detected()`; o LED responde via uma tarefa FreeRTOS. O `while(true)` principal apenas monitora erros periodicamente, não bloqueia os dados de áudio.

## 7. O que foi confirmado em execução

```text
I (...) micro_wake_word: Auto-setting model 'juvehnal' to sequential mode.
I (...) micro_wake_word: Micro Wake Word initialized
I (...) micro_wake_word: Actual tensor arena size is 28736
I (...) i2s_audio.microphone: Microphone started
I (...) micro_wake_word: Model 'juvehnal' will run sequentially in producer task.
I (...) JUVENAL: Juvenal aguardando palavra-chave
W (...) JUVENAL: WAKE WORD DETECTADA: juvehnal
I (...) JUVENAL_LED: Wake word: 3 piscadas verdes
I (...) JUVENAL_LED: Aguardando: LED amarelo
```

**Atenção para evolução:** compatibilidade de versão do modelo e operadores, falso positivo e uso de CPU ainda exigem ensaios mais longos. O pacote atual está comprovadamente funcional para a detecção e sinalização, mas não se deve extrapolar isso para outros modelos/versões de biblioteca.
