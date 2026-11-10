# 03 — Preparação do ambiente e build do zero

Este guia descreve o processo feito no Windows 11 com **ESP-IDF v5.4.4**. Em Linux/macOS, use o procedimento oficial de instalação do ESP-IDF para o sistema, preservando a mesma versão e o target `esp32s3`.

## 1. Ferramentas

1. Instale **Visual Studio Code** e a extensão oficial **Espressif IDF**.
2. Utilize o **Espressif Installation Manager (EIM)**, ou instalador oficial equivalente, para instalar **ESP-IDF 5.4.4** e toolchain Xtensa ESP32-S3.
3. Abra o terminal integrado com o **ambiente do ESP-IDF ativado**.
4. Confira:

```powershell
idf.py --version
python --version
```

Na máquina usada na validação, `idf.py --version` retornou `ESP-IDF v5.4.4`. Não é necessário replicar a localização exata da instalação de ferramentas de outra máquina.

## 2. Obter o repositório

```powershell
git clone https://github.com/antonio-abrantes/juvenal_esp_v2.git
cd juvenal_esp_v2
```

Enquanto o repositório remoto ainda não tiver sido publicado, utilize a pasta de código-fonte já existente. **Não sobreponha uma cópia limpa ao firmware validado para tentar reconstruir o repositório.**

Confira se estão presentes `CMakeLists.txt` (raiz), `main/juvenal_main.cpp`, `main/juvenal_led.c`, `main/juvenal_led.h`, `main/juvehnal.tflite`, `main/idf_component.yml`, `components/micro_wake_word_idf/` **com patches** e `sdkconfig.defaults`.

## 3. Selecionar ESP32-S3

```powershell
idf.py set-target esp32s3
```

Se houver variável de ambiente `IDF_TARGET` fixada em `esp32` em algum perfil, remova-a **apenas da sessão** e repita:

```powershell
Remove-Item Env:IDF_TARGET -ErrorAction SilentlyContinue
idf.py set-target esp32s3
```

**Atenção:** `set-target` pode limpar a pasta de build e recriar `sdkconfig`. Faça backup das configurações locais antes de executar em projetos modificados.

## 4. Flash e PSRAM — configurações de referência

A placa tem **16 MB de Flash física**, mas o firmware usa **partição factory de 1 MB** nesta fase. O arquivo `sdkconfig.defaults` registra as opções básicas:

```ini
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_40M=y
CONFIG_SPIRAM_BOOT_INIT=y
CONFIG_SPIRAM_MEMTEST=y
CONFIG_SPIRAM_USE_MALLOC=y
```

Para verificar na interface:

```powershell
idf.py menuconfig
```

- `Serial flasher config`: **Flash size 16 MB**, **DIO**, **80 MHz**.
- `Component config` → `ESP PSRAM`: suporte habilitado, **Octal Mode PSRAM**, **40 MHz**, inicialização e teste de memória no boot habilitados.
- **Não selecionar Octal Flash.** A configuração octal é da PSRAM.

Um `sdkconfig` local preexistente pode ter precedência sobre os defaults. Em tal caso, revise no `menuconfig` ou gere um resumo do que realmente está sendo utilizado antes de confiar nas opções novas.

## 5. Componentes e resolução de dependências

A pasta `components/micro_wake_word_idf/` é **vendorizada** no repositório porque a cópia original precisou de correções. O gerenciador resolve as dependências do componente local via `idf_component.yml` e as do componente `main/`:

- `espressif/led_strip`: versão **3.1.1** resolvida no ambiente de referência (`^3.0.0` declarado).
- `espressif/esp-tflite-micro`: versão **1.4.3** resolvida.
- `espressif/esp-nn`: versão **1.4.2** resolvida.
- `ESPMicroSpeechFeatures`: commit **`c2a077454c24d71fc84be377bb6e28725f05c333`** resolvido.

Versione **`dependencies.lock`** quando presente; ignore a pasta gerada **`managed_components/`**. O arquivo lock evita resolver silenciosamente versões diferentes da referência validada.

## 6. Compilar, detectar porta e gravar

```powershell
idf.py reconfigure
idf.py build
python -m serial.tools.list_ports -v
idf.py -p COM5 flash monitor
```

Substitua `COM5` pela porta encontrada no seu computador. A interface usada na validação era **USB-Enhanced-SERIAL CH343** (VID:PID `1A86:55D3`), mas o número da COM pode mudar.

Com o monitor aberto, pronuncie **“Juvenal”**. A mensagem `WAKE WORD DETECTADA: juvehnal` deve aparecer; o LED deve piscar verde 3 vezes e retornar a amarelo. Para sair do monitor serial: **Ctrl+]**.

### Logs mínimos esperados

```text
I (...) esp_psram: Found 8MB PSRAM device
I (...) esp_psram: SPI SRAM memory test OK
I (...) micro_wake_word: Micro Wake Word initialized
I (...) micro_wake_word: Actual tensor arena size is 28736
I (...) i2s_audio.microphone: Microphone started
I (...) JUVENAL: Juvenal aguardando palavra-chave
```

O build final desta fase foi de aproximadamente **0x8DA00 bytes** antes da mudança de LED e continuava cabendo na partição de 1 MB; a compilação exata pode variar com código e flags. A alteração posterior do callback do LED foi **gravada e validada na placa**, mas não há tamanho final registrado para esse último build.

## 7. Histórico do protótipo

O projeto foi inicialmente criado a partir do exemplo oficial **ESP-IDF `hello_world`**. Por isso o `project()` e a saída `hello_world.bin` ainda podem usar esse nome. Essa nomenclatura não altera os GPIOs, a memória ou o comportamento da wake word; renomear o projeto pode ser uma tarefa posterior, com compilação e teste próprios.

A pasta antiga `main/hello_world_main.c` e a primeira versão `main/juvenal_main.c` não participam do build da v0.1.0 (o CMake usa `juvenal_main.cpp`). Elas foram excluídas do versionamento pelo `.gitignore` para manter o repositório mínimo.
