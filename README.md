# Juvenal ESP32-S3 — interface de voz

Firmware do assistente Juvenal para ESP32-S3 N16R8, com INMP441, MicroWakeWord local, face GC9A01/LVGL, painel e integração com o Juvenal Server. Projeto ESP-IDF **5.4.4**, repositório juvenal_esp_v2. A palavra pronunciada é Juvenal; juvehnal identifica o modelo fonético.

## Servidor complementar — Juvenal Server

Este firmware se integra ao **[Juvenal Server — juvenal_esp_v2_server](https://github.com/antonio-abrantes/juvenal_esp_v2_server)**, o backend Python responsável pelo processamento da conversa.

O servidor recebe o áudio do ESP32 por WebSocket, transcreve a fala com faster-whisper, consulta o provedor de IA e mantém o contexto e a persona do Juvenal. Conforme a configuração, sintetiza a resposta com Piper e devolve PCM ao ESP32, reproduz no próprio servidor ou entrega texto a um endpoint HTTP. Também controla a continuidade da conversa e seu timeout de inatividade.

Para usar o modo conversacional WebSocket/TTS, execute o servidor e configure sua URL e a credencial do dispositivo no firmware. O ESP32 cuida da wake word local, captura do microfone, reprodução I2S, tela e LED; o servidor cuida de STT, IA e TTS. Os projetos têm repositórios independentes; a pasta local do backend neste workspace é projects/juvenal_server.

## Home Assistant

A integração opcional com o Home Assistant roda no [Juvenal Server](https://github.com/antonio-abrantes/juvenal_esp_v2_server#home-assistant). No modo padrão api, a IA do HA interpreta o comando e sua resposta é reproduzida neste ESP; no modo text_endpoint, o receptor assume a fala e o ESP não reproduz essa resposta. Não é necessário cadastrar ações no padrão atual.

Ativação, modo e credenciais HASS_* ficam no .env privado do Python; alternar exige apenas reiniciar o servidor, sem regravar este firmware. Etapa concluída por aceite do usuário em 14/11/2026, com funcionamento relatado; cenários complementares estão no guia do servidor.

## Estado atual e evidências

| Entrega | Situação |
|---|---|
| v0.1.0: boot, PSRAM, INMP441, modelo e wake/LED | Validação física histórica em 10/11/2026 |
| Visual v0.4 do projeto separado juvenal_http_teste | Instalada, compilada e parcialmente validada pelo usuário; WAV/HTTP POST, sem WS/TTS/MAX98357A |
| Integração da fase 03 neste principal | Boot e sessões PCM/playback comprovados; continuidade, LED de inicialização e boca aprovados pelo usuário em 14/11 |
| WS v1 sob demanda e PCM16 para o servidor | Três entradas por VAD/STT/IA e playback confirmado; primeira fala de6,46s demonstra reutilização do ring |
| MAX98357A + alto-falante, GPIO15/16/17 | Conectados e funcionais em teste isolado ESP-IDF5.4.4 com som real; USB5V e GND comum |
| Retorno PCM e driver MAX98357A | Três respostas com playback confirmado e retorno à espera; qualidade e cenários de falha ainda sem avaliação abrangente |
| Painel/manual WAV herdados seletivamente da v0.4 | Portados/adaptados; regressão manual completa pendente |
| Boot LED, volume e boca por PCM | LEDs e boca aprovados na placa; volume/persistência sem aceite específico |
| Home Assistant via servidor Python | Etapa concluída por aceite do usuário; sem alteração de firmware para ativar/alternar modos |

A etapa Home Assistant/persona foi concluída no servidor. A fase03 mantém pendências próprias de robustez/operação; os aceites relatados de áudio, continuidade, LED e boca estão separados de volume e cenários negativos ainda não detalhados. A Visualv0.4 experimental continua separada. AGENTS.md e os guias históricos da v0.1.0 registram a baseline; este README e o [guia de integração](docs/10-integracao-servidor-ws.md) descrevem o código corrente.

## Fluxo implementado

Na espera, MicroWakeWord reconhece Juvenal no próprio ESP e não há WS aberto. Wake reserva o estado, pausa inferência, sinaliza três verdes e abre uma conexão autenticada. PCM16 mono 16 kHz segue para STT/IA no servidor; azul durante transmissão/processamento. Após a resposta, a escuta reabre na mesma conexão; inatividade configurada exclusivamente no servidor, despedida ou erro encerra e retorna à wake. Queda não provoca replay/reconexão automática.

O servidor seleciona um único destino: endpoint textual, áudio local ou PCM de retorno ao ESP. A primeira configuração preservou endpoint; o usuário posteriormente selecionou esp32 e desligou a captura local do Windows. O firmware privado habilita TX para a montagem validada. Áudio retorna nos turnos destinados ao ESP, incluindo respostas da API HA; o modo textual HA delega a fala ao receptor. O painel do ESP mantém gravação, escuta/download, descarte, envio manual WAV/HTTP e diagnósticos. A URL desse formulário é do webhook WAV de diagnóstico, não configura WS nem a saída textual do servidor. Uma mesma wake não envia pelos dois transportes.

### Inicialização, painel e volume

LED amarelo piscando ao iniciar/conectar; verde por cerca de2s quando recursos locais, primeiro PCM e Wi-Fi estiverem prontos; depois a lógica normal. Falhas detectáveis ficam em vermelho piscando, inclusive Wi-Fi perdido. A recuperação da rede repete a confirmação verde. O prazo de30s sinaliza falha de inicialização pendente e não limita captura de voz. O controlador do LED é independente do LCD/LVGL.

No IP do último log, abra [o painel do ESP](http://192.168.100.50/), porta80, usando o usuário e a senha já definidos no header privado main/juvenal_web_credentials.h. Não colocar credenciais no HTML ou em documentos públicos. O painel do Python é outro serviço, na porta8000.

O controle Volume do alto-falante permite0–100% (0=mudo;100=PCM original). Aplica durante reprodução e salva na NVS entre interações; aguarde a indicação Salvo no ESP32 antes de reiniciar para preservar o último ajuste. Não muda ganho do microfone. A boca usa o nível do PCM após volume, publicado quando o descritor DMA termina; LVGL atualiza localmente a cada50ms, sem chamada ao LLM. Silêncio/mute fecha a boca; término/cancelamento restaura a pose. O usuário confirmou os LEDs de inicialização e a boca na placa; volume e persistência ainda não receberam aceite específico.

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

## Configuração privada

Copie main/juvenal_secrets.example.h para main/juvenal_secrets.h e main/juvenal_web_credentials.example.h para main/juvenal_web_credentials.h apenas se os arquivos locais ainda não existirem. Os dois arquivos reais são ignorados pelo repositório; nunca force sua inclusão. Exemplos públicos estão vazios/desativados.

- Wi-Fi: JUVENAL_WIFI_SSID e JUVENAL_WIFI_PASSWORD.
- Transporte: JUVENAL_AUTO_TRANSPORT_WS=true e JUVENAL_WS_URL=ws://IP_DO_SERVIDOR:8000/ws/esp.
- Identidade/token: JUVENAL_WS_DEVICE_ID e JUVENAL_WS_DEVICE_TOKEN iguais a ESP_DEVICE_ID/TOKEN no .env privado do servidor.
- Painel ESP: usuário/senha próprios; senha com pelo menos 12 caracteres. HTTP porta 80, somente rede privada protegida.
- Diagnóstico WAV: JUVENAL_API_KEY e JUVENAL_WEBHOOK_URL próprios; o painel mantém override em NVS.
- Speaker: GPIO15=BCLK, GPIO16=WS/LRC e GPIO17=DOUT/DIN. A montagem está validada e JUVENAL_SPEAKER_ENABLED=true no arquivo privado local; o exemplo público mantém ativação explícita false. MAX98357A recebe USB5V/GND comum, GAIN/SD soltos e SPK- nunca no GND.

Se os arquivos privados já foram preparados neste workspace, preserve-os; confira apenas IP/porta se a rede mudar. O servidor precisa ser alcançável pelo ESP, sem localhost. WS sem TLS exige LAN protegida. WSS usa bundle de certificados e sincronização de relógio.

## Build e flash pelo usuário

Abra terminal com ESP-IDF **5.4.4** ativado. A estrutura CMake e o target esp32s3 foram mantidos; não é necessário trocar target/SDK a cada ciclo.

~~~powershell
Set-Location -LiteralPath 'C:\Projects\Juvenal_v2\projects\juvenal_esp'
idf.py build
idf.py -p COM5 flash monitor
~~~

COM5 é a porta fornecida no teste recente; confira se reconectar a placa. Ctrl+] sai do monitor. Não executar erase-flash, fullclean ou set-target como rotina. O build prepara os artefatos; o flash substitui o firmware instalado e deve ser feito manualmente depois de revisar configuração e tabela de partições.

A Flash permanece DIO 80 MHz/16 MB e PSRAM octal 40 MHz/8 MB. A integração de rede/tela excede o espaço da antiga aplicação de 1 MiB: partitions.csv prepara factory de 4 MiB em 0x10000. NVS 0x9000/0x6000 e phy_init 0xf000/0x1000 mantêm offsets/tamanhos; nenhuma NVS é apagada automaticamente. O boot fornecido pelo usuário mostra aplicação carregada em factory4MiB; o tamanho da revisão nova precisa ser conferido no próximo build.

main/idf_component.yml fixa led_strip 3.1.1, GC9A01 2.0.0, esp_lvgl_port 2.9.0, LVGL 9.4.0 e esp_websocket_client 1.8.0. O código exige ESP-IDF exatamente 5.4.4. A tentativa anterior de build pelo agente não concluiu a resolução por restrições Git/rede; o usuário posteriormente forneceu execução do principal compilado no ESP-IDF5.4.4. Os refinamentos desta rodada não mudam manifest nem lock. Revise/inclua o lock gerado pelo seu build junto ao código; preserve as dependências e patches MicroWakeWord. Não copiar managed_components de experimentos.

## Estrutura e operação

| Arquivo/módulo | Responsabilidade |
|---|---|
| main/juvenal_main.cpp | Microfone/wake, estados atômicos, buffer compartilhado, Wi-Fi e diagnóstico WAV |
| main/juvenal_ws.cpp/.h | Cliente v1, autenticação, captura/flow control, conexão por interação |
| main/juvenal_speaker.cpp/.h | I2S_NUM_1 TX, ring de 16 KiB, créditos e confirmação depois do DMA |
| main/juvenal_face_neutral.c/.h | GC9A01/LVGL, poses e boca pelo nível do PCM reproduzido |
| main/juvenal_led.c/.h | Único proprietário do GPIO48/RMT; boot e cor normal publicada pela face |
| main/juvenal_web.cpp/.h/.html | Painel Basic, gravação manual, POST assíncrono e status |
| main/juvenal_config.h / exemplos | Configuração privada opcional e defaults sem segredos |
| main/juvehnal.tflite | Modelo original embarcado por EMBED_FILES, 63.520 bytes |
| components/micro_wake_word_idf | Captura I2S RX e modelo; quatro patches e licença preservados |
| main/CMakeLists.txt | Registra fontes, modelo, HTML e dependências do firmware |
| sdkconfig.defaults / partitions.csv | Referência N16R8 e tabela de aplicação ampliada |

juvenal_led.c/.h foi integrado ao build como único controlador físico do LED; não há duas tarefas disputando GPIO48. project() e binário podem continuar hello_world, nome histórico do exemplo ESP-IDF.

MicroWakeWord permanece dono exclusivo do I2S_NUM_0 RX; subscribe compartilha áudio sem segundo capturador. O callback não faz rede/LVGL. O worker aplica HPF 80 Hz e ganho 1,6× uma única vez. A memória já reservada ao WAV é reutilizada no WS como ring produtor/consumidor: cada bloco enviado libera espaço para o próximo. Não há gravação WS limitada a seis segundos. O servidor encerra fala pelo VAD; limites configuráveis de proteção e falha real de rede/fila abortam sem processar comando truncado. O WAV de diagnóstico/legado mantém seu limite próprio.

Consulte o [guia WS/servidor e aceite manual](docs/10-integracao-servidor-ws.md), o [ambiente ESP-IDF](docs/03-preparacao-do-ambiente.md) e o [índice histórico](docs/README.md). Reprodução integrada, continuidade, LEDs e boca têm confirmação do usuário; recuperação de falhas, volume/persistência e demais verificações específicas permanecem separadas. Veja [fiação completa](docs/02-hardware-e-ligacoes.md) e [restauração da v0.4](docs/10-integracao-servidor-ws.md#restaurar-o-firmware-visual-v04).

## Agradecimento pelo treinamento da wake word

O modelo personalizado **`juvehnal.tflite`**, utilizado para reconhecer a palavra de ativação “Juvenal”, foi obtido com a ajuda do projeto open source **[Tater Wake Words](https://github.com/TaterTotterson/Tater-Wake-Words)**, mantido por **[TaterTotterson](https://github.com/TaterTotterson)**.

O projeto disponibiliza modelos de wake word para a comunidade e permite solicitar o treinamento de novas palavras por meio de issues no GitHub, com um processo automatizado de geração e publicação dos artefatos. No caso do Juvenal, o modelo personalizado foi disponibilizado gratuitamente, sem exigir uma infraestrutura própria de treinamento.

**Muito obrigado ao TaterTotterson e aos colaboradores por disponibilizarem esse trabalho à comunidade!**

> **Distinção técnica:** o Tater Wake Words é creditado pelo processo de solicitação/treinamento e disponibilização do modelo personalizado. O reconhecimento em tempo de execução no ESP32-S3 utiliza o componente MicroWakeWord-IDF, listado abaixo. São contribuições diferentes e complementares.

## Links e atribuições

- [ESP-IDF](https://github.com/espressif/esp-idf) — framework de desenvolvimento.
- [MicroWakeWord-IDF](https://github.com/bibidentuhanoi/MicroWakeWord-IDF) — componente de terceiros (GPLv3, com alterações locais); **não omitir seu código-fonte nem a licença**.
- [Tater Wake Words — TaterTotterson](https://github.com/TaterTotterson/Tater-Wake-Words) — projeto open source utilizado para obter o modelo personalizado da palavra “Juvenal”; catálogo e solicitações de treinamento de wake words.
- [Modelo de referência `juvehnal`](https://github.com/antonio-abrantes/microWakeWords) — arquivos TFLite e metadados.

**Licenciamento:** este firmware inclui/integra código de terceiros sob GPLv3. Antes de publicar ou distribuir o firmware completo, consulte [dependências e licenças](docs/09-dependencias-e-licencas.md) e escolha uma licença de projeto compatível. O Tater Wake Words publica seu código original e os artefatos próprios sob MIT, ressalvando licenças de componentes e modelos de terceiros. O kit não atribui automaticamente uma licença ao código próprio.

## Continuidade e timeout do servidor

A extensão de14/11/2026 mantém a conexão entre perguntas sem nova wake. O ESP informa input.ready e input.speech_started; o Python controla CONVERSATION_IDLE_TIMEOUT_SECONDS=10, cancela ao começar fala e rearma só após resposta e nova prontidão. STT/ferramentas/TTS não consomem essa contagem. O antigo teto total de180s do firmware foi removido.

É necessária atualização inicial coordenada dos dois projetos. Depois dela, mudar de10 para20s exige somente editar o .env do Python e reiniciar o serviço: nenhum novo build/flash do ESP. O usuário informou que essa continuidade passou a funcionar após a atualização dos dois projetos; cenários negativos não relatados permanecem como verificações complementares.
