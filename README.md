# Juvenal ESP32-S3 — Wake Word Local

**Versão de referência: v0.1.0 (marco funcional validado em hardware em 10/11/2026).**

Firmware independente para **ESP32-S3 N16R8** (16 MB Flash, 8 MB PSRAM), com microfone **INMP441 (I2S)**, detecção **local** da palavra de ativação **“Juvenal”** por **microWakeWord / TensorFlow Lite Micro** e LED RGB integrado **GPIO48**.

> A palavra pronunciada é **Juvenal**. `juvehnal` é o identificador fonético usado nos arquivos do modelo e nos logs. Esta fase não depende de Home Assistant, ESPHome, nuvem nem conexão Wi-Fi.

## O que funciona na v0.1.0

| Recurso | Estado |
|---|---|
| ESP32-S3 com Flash 16 MB e PSRAM octal 8 MB | **Validado na placa** |
| Microfone INMP441 em 16 kHz, I2S | **Validado** |
| Modelo `juvehnal.tflite` embarcado na Flash | **Validado** |
| Detecção da wake word no próprio ESP32-S3 | **Validado** |
| LED amarelo enquanto aguarda | **Validado** |
| Três piscadas verdes na detecção; retorno ao amarelo | **Validado** |
| Intervalo visual entre ativações: 4 s | **Implementado** |
| LED azul na transmissão/processamento | **Reservado para fase futura** |
| Wi-Fi, WebSocket, servidor, STT, TTS, alto-falante | **Ainda não implementados neste firmware** |

### Hardware e conexão rápida

| INMP441 | ESP32-S3 | Cor do fio desta montagem |
|---|---|---|
| `VDD` | `3V3` | Vermelho |
| `GND` | `GND` | Preto 1 |
| `L/R` | `GND` (canal esquerdo) | Preto 2 |
| `SCK` / `BCLK` | `GPIO12` | Laranja |
| `SD` / `DOUT` | `GPIO10` | Verde |
| `WS` / `LRCLK` | `GPIO11` | Amarelo |

**LED integrado:** WS2812/compatível endereçável, `GPIO48`. **Nunca alimentar o INMP441 em 5 V.** As cores identificam os fios *desta montagem*, não são um padrão de mercado. Veja [hardware e montagem](docs/02-hardware-e-ligacoes.md).

## Clonar, compilar e testar

**Pré-requisitos:** ESP-IDF **v5.4.4**, ferramentas de compilação para ESP32-S3, Python e Git instalados pelo instalador oficial da Espressif (no Windows, VS Code + extensão ESP-IDF / Espressif Installation Manager).

> O repositório remoto precisa ter sido criado e publicado para que o comando `git clone` funcione. A URL abaixo presume publicação no perfil `antonio-abrantes`.

```powershell
git clone https://github.com/antonio-abrantes/juvenal_esp_v2.git
cd juvenal_esp_v2
idf.py --version
idf.py set-target esp32s3
idf.py build
python -m serial.tools.list_ports -v
idf.py -p COM5 flash monitor
```

**Troque `COM5` pela porta real da sua placa.** O exemplo `COM5` foi o dispositivo CH343 usado na validação de referência. Para encerrar o monitor serial: `Ctrl+]`.

O projeto traz **`sdkconfig.defaults`** com a configuração-base pública: Flash **DIO / 80 MHz / 16 MB** e PSRAM **octal / 40 MHz**. Em computadores novos, o `idf.py set-target esp32s3` gera um `sdkconfig` local; confira em `idf.py menuconfig` antes de gravar. **Não habilite modo octal para a Flash**: octal nesta montagem se refere à **PSRAM**, não à Flash.

**Saída esperada de reconhecimento:**

```text
I (...) micro_wake_word: Micro Wake Word initialized
I (...) i2s_audio.microphone: Microphone started
I (...) JUVENAL: Juvenal aguardando palavra-chave
W (...) JUVENAL: WAKE WORD DETECTADA: juvehnal
I (...) JUVENAL_LED: Wake word: 3 piscadas verdes
I (...) JUVENAL_LED: Aguardando: LED amarelo
```

Os `(...)` substituem tempos do monitor, que variam. A inicialização da biblioteca e a compilação foram confirmadas; **a taxa de falsos positivos ainda não foi medida sistematicamente**.

## Estrutura essencial do repositório

```text
juvenal_esp_v2/
├── README.md
├── AGENTS.md                    # Orientação para agentes e desenvolvedores
├── .gitignore
├── .gitattributes
├── sdkconfig.defaults            # Configuração-base pública
├── CMakeLists.txt                # Projeto ESP-IDF existente
├── dependencies.lock             # Versões resolvidas (versionar se presente)
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   ├── juvenal_main.cpp          # Microfone + detector + callback
│   ├── juvenal_led.h
│   ├── juvenal_led.c              # Estados do LED e tarefa FreeRTOS
│   └── juvehnal.tflite           # Modelo necessário ao build (63.520 B)
├── components/
│   └── micro_wake_word_idf/      # Componente local COM correções de compatibilidade
│       └── LICENSE.md            # Licença original GPLv3 — preservar
├── models/
│   └── juvehnal.json             # Metadados opcionais, manter se presente
└── docs/
    ├── README.md
    ├── 01-visao-geral.md
    ├── 02-hardware-e-ligacoes.md
    ├── 03-preparacao-do-ambiente.md
    ├── 04-testes-do-microfone-e-led.md
    ├── 05-integracao-do-micro-wake-word.md
    ├── 06-problemas-e-solucoes.md
    ├── 07-checklist-de-validacao.md
    ├── 08-versionamento-e-publicacao.md
    └── 09-dependencias-e-licencas.md
```

**Importante:** a documentação foi entregue separadamente como **kit complementar** para ser copiado à raiz do projeto **já existente e funcional**. O kit **não substitui** o código `main/`, o modelo `.tflite` nem o componente local corrigido. Eles devem permanecer na pasta do projeto e ser incluídos no primeiro commit.

O binário ainda pode aparecer como `hello_world.bin` e o projeto como `hello_world` porque o projeto nasceu do exemplo oficial da Espressif. Isso é apenas nomenclatura histórica, **não impede o funcionamento da v0.1.0**.

## Documentação detalhada

Comece pelo [índice de documentação](docs/README.md): ambiente do zero, GPIOs, cores dos fios, procedimento de testes, configuração do TensorFlow, compatibilidade entre versões, solução dos erros reais e publicação no GitHub.

## Links e atribuições

- [ESP-IDF](https://github.com/espressif/esp-idf) — framework de desenvolvimento.
- [MicroWakeWord-IDF](https://github.com/bibidentuhanoi/MicroWakeWord-IDF) — componente de terceiros (GPLv3, com alterações locais); **não omitir seu código-fonte nem a licença**.
- [Modelo de referência `juvehnal`](https://github.com/antonio-abrantes/microWakeWords) — arquivos TFLite e metadados.

**Licenciamento:** este firmware inclui/integra código de terceiros sob GPLv3. Antes de publicar ou distribuir o firmware completo, consulte [dependências e licenças](docs/09-dependencias-e-licencas.md) e escolha uma licença de projeto compatível. O kit não atribui automaticamente uma licença ao código próprio.
