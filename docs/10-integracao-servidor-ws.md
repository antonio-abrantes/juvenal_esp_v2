# 10 — Integração Juvenal Server / WS v1

O usuário forneceu boot e três sessões completas no principal em13/11/2026: VAD/STT/IA, PCM de resposta, playback confirmado e nova wake. A primeira entrada durou6,46s, acima do antigo limite indevido. As novidades posteriores desta revisão (boot LED, volume, boca por amplitude e cleanup WS) ainda não foram compiladas/gravadas/validadas no hardware. A tentativa anterior de build pelo agente foi bloqueada pelo ambiente; os logs do usuário comprovam a versão anterior destas novidades.

## Configurar o primeiro modo

O servidor usa .env próprio: no ambiente atual, AUDIO_OUTPUT_MODE=esp32, LOCAL_VOICE_ENABLED=false, STT_ENABLED=true e ESP_DEVICE_ID/TOKEN. Endpoint continua sendo uma alternativa explícita de saída textual. Preserve a URL de entrega de texto e o provedor de IA. O header privado deste firmware usa a mesma identidade/token, Wi-Fi e URL ws://IP_DO_SERVIDOR:8000/ws/esp. JUVENAL_AUTO_TRANSPORT_WS=true. Reinicie o servidor e compile/grave o firmware manualmente.

Neste workspace foram preparados os headers privados com os valores reutilizáveis de Wi-Fi/painel/diagnóstico e o token do servidor. O endereço LAN do Windows foi detectado no momento da preparação; se mudar por DHCP, atualize JUVENAL_WS_URL. Nada disso é publicado nos exemplos. Não mostrar tokens nos logs, URLs ou capturas de tela.

LOCAL_VOICE_ENABLED no servidor pode continuar true para diagnóstico no computador; ao validar a entrada ESP, false evita o microfone do host. Essa alteração não é automática. O painel Python continua na porta configurada (8000 por padrão); o painel local do ESP usa HTTP na porta 80 e suas próprias credenciais.

## Captura, estados e limites

O capturador I2S do MicroWakeWord é único. subscribe encaminha PCM original ao buffer durante a sessão; rede e processamento de saída ficam em task separada. Não há segundo RX nem alteração nos GPIO10/11/12. HPF 80 Hz + ganho 1,6× são aplicados no worker, preservando continuidade do filtro e o PCM original do detector.

A captura WS usa a área PSRAM do diagnóstico manual como ring, com exclusão por Phase. O produtor escreve em posições circulares e o worker libera cada bloco após copiá-lo para envio, sem acumular toda a fala. Há pré-captura durante conexão; o servidor determina fim por VAD/input.stop e recebe input.end com amostras efetivamente enviadas. A capacidade do ring limita backlog de rede, não a duração da fala. Somente o WAV manual/legado continua limitado a6s. Overflow real de PCM pendente, falha de rede/formato ou prazo de proteção aborta sem executar comando cortado.

Amarelo em espera, três verdes na wake, azul em transmissão/processamento. Face, sono visual e despertar visual por ruído foram reaproveitados; despertar visual não abre WS. O modo endpoint não anuncia que o alto-falante externo terminou de falar. Sucesso/falha retorna à espera via evento local.

## Protocolo

/ws/esp usa protocol_version=1 e Bearer no header. session.start anuncia device_id, playback e janela de 16384 bytes. session.ready cria session_id/turn_id; ambos acompanham os controles seguintes. input.start declara s16le mono 16000 Hz; binários de entrada são PCM bruto, nominalmente 640 bytes, sem WAV.

O servidor manda input.stop ao detectar silêncio. input.end traz reason e total_samples efetivamente enviados. Eventos transcript/assistant.response são apenas prévias para visualização, sem segunda ação ou reprodução.

A entrega de cada turno aguarda HTTP em endpoint, player em local ou output.end/playback.finished em esp32. Em sucesso, outra session.ready abre a próxima escuta na mesma conexão; session.end só por inatividade do servidor, despedida ou falha. Erros e quedas cancelam buffers/socket e não repetem ação/resposta. Nova wake cria nova conexão; contexto no servidor é associado à identidade autenticada, respeitando TTL e limites.

## Diagnóstico WAV preservado

O painel mantém gravação manual, stop, ouvir/baixar WAV, descartar e POST por worker com resposta 202. O WAV é retido em falha e expira após 120 s sem acesso. Wake fica suspensa enquanto o modo manual ocupa o buffer; libere/descarte a gravação antes de retornar.

A URL editável do painel é somente webhook WAV de diagnóstico, persistida no mesmo namespace/chave NVS. Não aponta para /ws/esp, não recebe o texto final da IA e não altera JUVENAL_WS_URL. O transporte automático pode ser revertido explicitamente por JUVENAL_AUTO_TRANSPORT_WS=false e novo build/flash; nunca há WS + POST automático simultâneo. Isso requer que o webhook WAV/API Key estejam configurados para aquele contrato.

As condições de recuperação e concorrência precisam ser avaliadas novamente no principal. Os HTTP 200 relatados no experimental não foram produzidos por este build.

## Áudio de retorno, volume e boca

Montagem confirmada e validada pelo usuário: BCLK15 (roxo), WS16 (verde), DIN17 (cinza), VIN pela USB5V e terra comum, GAIN/SD desconectados. Não repetir a investigação 5VIN/Arduino. Veja [o inventário e a fiação](02-hardware-e-ligacoes.md). A rádio comprovou o hardware, sem comprovar a resposta do assistente.

O header privado deste workspace já define JUVENAL_SPEAKER_ENABLED=true e JUVENAL_TX_BCLK_GPIO=15, WS_GPIO=16, DOUT_GPIO=17. O exemplo público mantém ativação false e pinos validados, para habilitação explícita em outra montagem. O driver recusa sobreposição com microfone/tela/LED, Flash/PSRAM e UART reservados. Não remapear o INMP441.

Para ouvir no ESP, altere no servidor: AUDIO_OUTPUT_MODE=esp32, TTS_BACKEND=piper, modelo Piper configurado e perfil esp32 instalado pelo usuário. O usuário já selecionou esp32 e LOCAL_VOICE_ENABLED=false no ambiente atual; não reverter essas opções. Endpoint continua disponível quando desejado. PIPER_VOLUME controla a amplitude sem remapear GAIN/SD. Reinicie e grave o firmware atualizado. Para voltar ao texto, endpoint + restart no servidor; speaker pode ficar configurado, sem uso nesse modo. Não há fallback para saída local/HTTP.

Recepção usa PCM16 mono 16 kHz em blocos de 640 bytes e último par menor. TX I2S_NUM_1 duplica mono nos dois slots. Ring interno de 16 KiB, início após seis blocos/EOF, créditos cumulativos para backpressure. O ACK exige os totais e esvaziamento efetivo dos descritores DMA após output.end, não só recebimento dos bytes. Erro/underrun não confirma playback.

Half-duplex: wake/inferência permanecem suspensas durante a interação e a captura de fala termina antes do playback. Ainda não há AEC, full-duplex ou OTA.

A boca por amplitude estava ausente e foi implementada nesta revisão: nível médio absoluto do PCM após o volume, associado a cada descritor e publicado somente ao concluir sua saída pelo DMA. O timer LVGL existente (50ms) usa esse nível com suavização; silêncio/mute fecha e fim/cancelamento restaura a boca da pose. Sem animação fixa baseada em speaking, chamadas ao LLM ou APIs LVGL no callback I2S. Sensibilidade/atraso e estabilidade com áudio real ainda precisam de avaliação.

### Painel autenticado e volume

No endereço recebido no log: [http://192.168.100.50/](http://192.168.100.50/), HTTP porta80. Use as credenciais existentes em main/juvenal_web_credentials.h, arquivo privado ignorado; não foram alteradas nem copiadas para arquivos públicos. Mudança de DHCP exige conferir novo IP no monitor.

Volume do alto-falante:0–100%, inteiro;0=mudo,100=nível original. Default100 preserva comportamento anterior. O botão aplica em RAM, inclusive durante playback, e o status indica salvamento pendente/salvo. Persistência em juvenal_web/volume ocorre apenas quando o estado pode ser reservado entre interações/capturas; não há escrita NVS na tarefa I2S ou durante WS ativo. Aguarde Salvo no ESP32 antes de reiniciar. O ajuste afeta somente TX; volume do microfone/limiares permanecem.

GET/POST /api/volume exigem Basic Auth; POST também exige X-Juvenal-UI:1 e corpo JSON com único inteiro volume. Não há credenciais no JavaScript. O HTTP sem TLS permanece restrito à LAN confiável.

### LED de inicialização

O módulo juvenal_led é o único dono do GPIO48; a face publica a cor normal. Tarefa própria mantém amarelo piscando durante startup/Wi-Fi e vermelho piscando nas falhas detectadas, mesmo sem tela. Prontidão exige recursos locais, IP, microfone/wake e primeiro PCM (mais relógio válido no WSS), sem depender de servidor/LLM online. Verde por2s antecede as cores normais. Nova wake/manual só é admitida após essa confirmação.

Se a prontidão não chegar em30s, sinaliza vermelho e continua aguardando rede/relógio. Desconexão também sinaliza vermelho; recuperação repete verde2s. Erro de microfone/wake permanece sinalizado; pausa intencional durante conversa não é erro. NVS nunca é apagada automaticamente. Falha do próprio LED, falta de energia ou CPU paralisada não pode garantir pisca.

## Build e flash deste ciclo

Use terminal ESP-IDF5.4.4. COM5 é a porta fornecida pelo usuário; confira se reconectar:

~~~powershell
Set-Location -LiteralPath 'C:\Projects\Juvenal_v2\projects\juvenal_esp'
idf.py build
idf.py -p COM5 flash monitor
~~~

Ctrl+] encerra o monitor. O agente tentou build ESP-IDF5.4.4, sem conseguir concluir a resolução das dependências. Nenhum flash foi executado.

O manifest agora inclui tela/LVGL/WS. O primeiro build resolve dependências e atualiza dependencies.lock; revise e versione o lock resultante junto ao código. led_strip permanece 3.1.1, com API de cor adaptada. Os quatro patches MicroWakeWord, o modelo e sua licença continuam originais.

A antiga factory de 1 MiB não comportava o binário experimental de referência (aprox. 1,68 MB). partitions.csv amplia a aplicação para 4 MiB, mantendo NVS/PHY nos mesmos offsets/tamanhos. Essa tabela será gravada pelo flash manual; confira o resumo do build antes de gravar. sdkconfig local anterior foi preservado em backup ignorado. Nenhuma NVS ou Flash foi apagada.

## Pontos de aceite manual

1. Build com ESP-IDF 5.4.4, resolução do lock e tamanho dentro de factory; conferir DIO80/16MB e PSRAMoctal40/8MB.
2. Boot, display, modelo, áudio e Wi-Fi sem regressão. Na espera WS fechado.
3. Wake e frase com VAD, transcrição e resposta somente no destino selecionado, fim e nova wake. Três sessões esp32 já foram registradas; endpoint é alternativa independente.
4. Falha/timeout/queda, ocupação concorrente e overflow sem comando cortado/replay; contexto entre conexões dentro do TTL.
5. Painel/manual WAV completo, POST assíncrono, descarte, retenção após erro e expiração, sem disputa com WS.
6. Com o speaker já montado: áudio de resposta audível com velocidade correta, totais e ACK após TX, cancelamento e nova interação.

Montagem/som isolado e três ciclos integrados de resposta constam no relato/logs do usuário. Ainda faltam os cenários negativos, contexto/TTL, regressão WAV e a revisão nova de boot/volume/boca. Não considerar esta lista inteira aprovada.

## Restaurar o firmware Visual v0.4

O pedido histórico após o teste de rádio era restaurar a Visualv0.4. O relato mais recente já mostra o principal com WS em execução; restaurar o experimental é agora uma opção manual, não o próximo passo obrigatório. Feche o monitor atual com Ctrl+] e libere COM5. No terminal ESP-IDF5.4.4:

~~~powershell
idf.py -C "C:\Projects\Juvenal_v2\projects\juvenal_http_teste" -p COM5 flash monitor
~~~

Comando manual: não foi executado pelo agente e não grava juvenal_esp. Não usar erase-flash. Confira Wi-Fi/painel/webhook ao restaurar: o teste temporário pode ter afetado NVS, sem garantia de preservação integral. Não precisa desmontar o amplificador; conservar alimentação/GND conforme o guia físico. Não atribuir playback WS à Visual v0.4.

O build/flash do principal continua na sequência da seção anterior. COM5 é a porta fornecida no teste recente; se reconectar a placa, confira a enumeração antes de gravar.

## Correção do streaming após uso real

O log do usuário comprova wake, conexão e sessão aceita com saída esp32. A falha capture_limit cerca de6s após wake vinha do buffer linear herdado do WAV, decisão indevida do agente. Esse caminho foi substituído pelo ring sem aumentar a alocação PSRAM. Logs PCM streaming mostram amostras capturadas, enviadas, pendentes e RMS bruto; o servidor informa início/fim VAD e RMS do PCM tratado. Não alterar ganho/limiares sem observar esses diagnósticos.

Sem fala no prazo configurado, o servidor pede parada antes de eventual IDLE_MESSAGE no destino selecionado. Sem TTS no modo endpoint. Depois de atualizar código, reinicie o servidor e compile/grave o principal manualmente. O usuário posteriormente comprovou entrada de6,46s, retorno PCM/ACK e mais duas interações completas. Os novos refinamentos de boot/volume/boca e fechamento ainda exigem novo build/flash e conferência.

## Continuidade conversacional — atualização de14/11/2026

Uma wake abre a conversa; não é necessário repeti-la após cada resposta. A extensão v1 exige conversation_events=true no session.start/session.ready. Em cada turno, o firmware prepara o ring, envia input.start e input.ready; primeiro bloco de fala elegível no limiar fornecido pelo servidor envia input.speech_started. O VAD de fim continua no Python. A boca por PCM, volume, pinagem, RX único e MAX98357A permanecem.

Somente o Python possui CONVERSATION_IDLE_TIMEOUT_SECONDS=10. Conta depois de input.ready, cancela com início de fala e permanece desarmado durante captura/STT/ferramentas/TTS. Outra resposta concluída gera session.ready; novo input.ready inicia prazo inteiro. O ESP permanece em escuta conversacional até input.stop por inatividade e session.end. Erro/queda inesperada preserva cleanup, sem reconexão/replay, e retorna à wake.

O antigo teto total de180s foi removido do firmware. Só restam prazos de abertura e mecanismos de falha de transporte, não um timeout local de conversa. Limites de captura/processamento continuam no servidor por turno ativo, sem contar a espera ociosa.

Este contrato requer uma atualização inicial do firmware e do servidor. Depois, trocar10 por20s exige apenas CONVERSATION_IDLE_TIMEOUT_SECONDS=20 no .env do Python e reinício do serviço. Não alterar header, recompilar ou regravar ESP para essa troca. A validação real dessa revisão na placa permanece pendente.
