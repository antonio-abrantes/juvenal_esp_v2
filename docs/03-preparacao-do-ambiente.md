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

## 3. Selecionar ESP32-S3 — somente instalação nova

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

A placa tem **16 MB de Flash física**. A baseline v0.1.0 usava factory de 1 MiB; o código integrado da fase 03 usa partitions.csv com factory de **4 MiB**, preservando NVS 0x9000/0x6000 e PHY 0xf000/0x1000. A tabela nova não foi gravada pelo agente. Confira o resumo de partições antes do flash manual. O arquivo `sdkconfig.defaults` registra as opções básicas:

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

- `espressif/led_strip`: **3.1.1**, agora fixada no manifest; API de cor da face adaptada a essa versão.
- `espressif/esp-tflite-micro`: versão **1.4.3** resolvida.
- `espressif/esp-nn`: versão **1.4.2** resolvida.
- `ESPMicroSpeechFeatures`: commit **`c2a077454c24d71fc84be377bb6e28725f05c333`** resolvido.

A fase 03 adiciona GC9A01 2.0.0, esp_lvgl_port 2.9.0, LVGL 9.4.0 e esp_websocket_client 1.8.0. O lock existente registra a baseline e ainda não foi regenerado; o próximo build do usuário resolverá o manifest alterado. Revise e versione **`dependencies.lock`** resultante; ignore a pasta gerada **`managed_components/`**. O arquivo lock evita resolver silenciosamente versões diferentes da referência validada.

## 6. Compilar, detectar porta e gravar

```powershell
Set-Location -LiteralPath 'C:\Projects\Juvenal_v2\projects\juvenal_esp'
idf.py build
idf.py -p PORTA_SERIAL flash monitor
```

Substitua `PORTA_SERIAL` pela porta encontrada no seu computador. A consulta CIM desta entrega não permitiu confirmar a porta atual; não presumir COM5 histórica. A interface usada na validação era **USB-Enhanced-SERIAL CH343** (VID:PID `1A86:55D3`), mas o número da COM pode mudar.

Com o monitor aberto, pronuncie **“Juvenal”**. A mensagem `WAKE WORD DETECTADA: juvehnal` deve aparecer; o LED deve piscar verde 3 vezes e retornar a amarelo. Para sair do monitor serial: **Ctrl+]**.

### Logs históricos da baseline v0.1.0

```text
I (...) esp_psram: Found 8MB PSRAM device
I (...) esp_psram: SPI SRAM memory test OK
I (...) micro_wake_word: Micro Wake Word initialized
I (...) micro_wake_word: Actual tensor arena size is 28736
I (...) i2s_audio.microphone: Microphone started
I (...) JUVENAL: Juvenal aguardando palavra-chave
```

O build da baseline v0.1.0 foi de aproximadamente **0x8DA00 bytes** antes da mudança de LED e continuava cabendo na partição de 1 MB; a compilação exata pode variar com código e flags. A alteração posterior do callback do LED foi **gravada e validada na placa**, mas não há tamanho final registrado para esse último build.

## 7. Histórico do protótipo

O projeto foi inicialmente criado a partir do exemplo oficial **ESP-IDF `hello_world`**. Por isso o `project()` e a saída `hello_world.bin` ainda podem usar esse nome. Essa nomenclatura não altera os GPIOs, a memória ou o comportamento da wake word; renomear o projeto pode ser uma tarefa posterior, com compilação e teste próprios.

A pasta antiga `main/hello_world_main.c` e a primeira versão `main/juvenal_main.c` não participam do build da v0.1.0 (o CMake usa `juvenal_main.cpp`). Elas foram excluídas do versionamento pelo `.gitignore` para manter o repositório mínimo.

## 8. Integração atual

Antes de build/flash, configure os headers privados e consulte [integração WS](10-integracao-servidor-ws.md). A fase 03 preserva ESP-IDF 5.4.4 e a configuração física, acrescenta Wi-Fi/face/painel e WS. Foi tentado build após o pedido de conclusão/MAX98357A, mas a resolução de dependências parou por restrições Git/rede do ambiente do agente. Sem compilação aprovada ou flash; tamanho, resolução do lock e operação na placa ainda aguardam o usuário. Não usar erase-flash, fullclean ou set-target como rotina de atualização.
