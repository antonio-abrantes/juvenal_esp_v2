# 08 — Versionamento, publicação e segurança

**Repositório GitHub pretendido:** `juvenal_esp_v2` (URL ilustrada: `https://github.com/antonio-abrantes/juvenal_esp_v2`). A URL só funcionará após criar/publicar o repositório na conta apropriada. O **marco v0.1.0** já está validado no hardware; os comandos Git e a tag são passos **a executar pelo responsável**, não ações realizadas por este kit.

## 1. O que deve ir para o Git

| Versionar | Motivo |
|---|---|
| `CMakeLists.txt` (raiz) | Define o projeto ESP-IDF |
| `main/CMakeLists.txt` | Compila o firmware correto e embarca modelo |
| `main/juvenal_main.cpp` | Código final de wake word |
| `main/juvenal_led.c` e `.h` | LED integrado à detecção |
| `main/juvehnal.tflite` | **Modelo obrigatório para compilar**, 63.520 B |
| `main/idf_component.yml` | Declara `led_strip` |
| `components/micro_wake_word_idf/` | Biblioteca de terceiros **com patches**; preservar `LICENSE.md` |
| `dependencies.lock` (se presente) | Congela versões resolvidas |
| `sdkconfig.defaults` | Parâmetros públicos de hardware |
| `models/juvehnal.json` (se presente) | Metadados de configuração do modelo |
| `README.md`, `AGENTS.md`, `docs/`, `.gitignore`, `.gitattributes` | Documentação, onboarding, higienização |

## 2. O que NÃO deve ir para o Git

`build/`, `managed_components/`, `sdkconfig` local, `sdkconfig.old`, caches, arquivos `.bak*`, `.env*`, credenciais, tokens, certificados/chaves privadas, configurações pessoais de editor, logs serial, imagens `.bin` de firmware geradas, duplicata `models/juvehnal.tflite`, os testes antigos `main/juvenal_main.c` e `main/hello_world_main.c`.

**Não excluir** `main/juvehnal.tflite` — é uma entrada de build, não um binário gerado. **Não excluir** `components/micro_wake_word_idf/` — sem a cópia corrigida, a compilação clonada pode falhar. O JSON do modelo pode ser versionado, mas evite manter duas cópias do TFLite.

## 3. Auditar antes do primeiro commit

Na **raiz do firmware**, conferir o conteúdo e `.gitignore`:

```powershell
Get-ChildItem -Force
Get-Item main/juvehnal.tflite | Select-Object Name,Length
Get-ChildItem components/micro_wake_word_idf | Select-Object Name
```

Revisar arquivos de texto, inclusive `sdkconfig.defaults`, por segredos; não confiar **apenas** no `.gitignore`, pois arquivos rastreados anteriormente podem continuar no índice. **Não publicar logs de terminal completos** sem revisar caminhos, IPs, credenciais e nomes de máquinas.

## 4. Criar Git SOMENTE nesta pasta do projeto

```powershell
git init -b main
git status --short
git add .gitignore .gitattributes README.md AGENTS.md sdkconfig.defaults docs/
git add CMakeLists.txt main/CMakeLists.txt main/idf_component.yml
git add main/juvenal_main.cpp main/juvenal_led.c main/juvenal_led.h main/juvehnal.tflite
git add components/micro_wake_word_idf/
git add dependencies.lock    # somente se existir
# Opcional: git add models/juvehnal.json

git diff --cached --stat
git diff --cached --name-only
```

> A linha `git add dependencies.lock` deve ser executada somente se o arquivo existir. O trecho acima é um **roteiro**, não um script único para colar sem conferência. Avalie todos os arquivos staged antes de prosseguir.

Depois de revisar o índice e garantir que não haja segredos:

```powershell
git commit -m "feat: wake word local e LED RGB funcional (v0.1.0)"
git remote add origin https://github.com/antonio-abrantes/juvenal_esp_v2.git
git push -u origin main
git tag -a v0.1.0 -m "Juvenal ESP32-S3: wake word local funcional"
git push origin v0.1.0
```

Primeiro crie um repositório **vazio** no GitHub, sem README nem licença automática, se deseja publicar a árvore local diretamente. Ajuste o usuário/organização da URL caso a conta de destino seja outra. Verifique se Git está instalado e configurado (`git config user.name`, `git config user.email`) antes do commit; não inclua esses valores na documentação pública.

## 5. Checklist final pré-publicação

- [ ] Repositório Git iniciado **somente na raiz do firmware**.
- [ ] `git status --short` revisado.
- [ ] Modelo TFLite incluído uma única vez (em `main/`).
- [ ] `components/micro_wake_word_idf/` contém as quatro correções e `LICENSE.md` original.
- [ ] `managed_components/`, `build/`, `sdkconfig`, backups e segredos não estão staged.
- [ ] `dependencies.lock` considerado e incluído, se existente.
- [ ] `README.md` e docs descrevem exatamente a versão gravada e testada.
- [ ] Licenciamento do projeto revisado por causa da dependência GPLv3.
- [ ] Novo clone limpo foi testado (build e flash) antes de anunciar portabilidade completa.
- [ ] Tag `v0.1.0` criada **somente após** o primeiro commit validado.

## 6. Escopo do ZIP de documentação

O ZIP de documentação é um **overlay não destrutivo** para uma cópia de firmware já existente. Não contém o `main/` real nem a pasta `components/` real do computador do desenvolvedor. **Não substitua esses arquivos por exemplos** nem use o kit como se fosse o firmware completo. Extraia na raiz do projeto, confira diferenças e versiona-se o código existente após auditoria.
