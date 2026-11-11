# dayz-log-analyzer

Lê os logs de um servidor de DayZ (`.ADM` e `.RPT`) e mostra o que interessa: do que o pessoal está morrendo, quem mata quem e com qual arma, quanto tempo cada um jogou e quais erros de script mais se repetem.

Os logs do DayZ são texto puro e crescem rápido. No meu servidor, o DIA-Z HARDCORE, são uns 50 MB de log por dia, e achar alguma coisa ali era na base do Ctrl+F. Escrevi isso em C++ pra passar a pasta `profiles` inteira e ter um resumo.

```
Logs de administração (.ADM): 1 arquivo, de 2026-11-08 a 2026-11-09
  51 linhas · 5 jogadores · pico de 3 online · 3h 10min jogadas no total
  7 mortes de jogadores (2 PvP, 1 pela IA) · 1 mensagem no chat

  Causas de morte
    jogador         2
    IA              1
    animais         1
    infectados      1
    queda           1
    suicídio        1

  Quem mais morreu
    Corvo                3
    Lobo                 1
    Survivor (Zr4t)      1
    Survivor (aX9k)      1
    Raposa               1

  Armas que mais mataram jogadores (PvP e IA)
    AKM            1
    M4-A1          1
    Mosin9130      1

  O que mais acertou os jogadores
    jogadores       4 acertos      239 de dano
    IA              2 acertos      159 de dano
    infectados      2 acertos       18 de dano
    Urso-pardo      1 acertos       30 de dano
    queda           1 acertos        0 de dano
    veículos        1 acertos        0 de dano

  Quem mais matou (PvP)
          kills  mortes   K/D  acertos  na cabeça
    Lobo      2       1   2.0        4          2

  Tiro mais longo (PvP): Lobo matou Survivor (Zr4t) com Mosin9130 a 462.3 m

  Mais tempo online
    Lobo               1h 33min
    Corvo                 33min
    Survivor (aX9k)       31min
    Survivor (Zr4t)       19min
    Raposa                12min

  Também no log: 2 linhas de IA, 1 de mods, 0 não reconhecidas

Logs de script e engine (.RPT): 1 arquivo
  46 linhas · 7 erros · 3 avisos
  Erros por sistema: SCRIPT 3 · PHYSICS 2 · ANIMATION 1 · outros 1

  Erros que mais se repetem
         2x  SCRIPT (E): NULL pointer to instance  [MeuMod/scripts/4_World/missao_manager.c:216]
             primeira vez: DayZServer_x64_exemplo.RPT, linha 15
         2x  PHYSICS (E): LODShape: 'dz\structures\walls\wall_indcnc_#.p3d' was instanced with interaction layer 'CHARACTER'
             primeira vez: DayZServer_x64_exemplo.RPT, linha 42
         ...
```

(os logs da pasta `examples` seguem o formato real, mas os nomes e ids são inventados)

## Uso

```
dayzlog [opções] <arquivo ou pasta>...
```

Passando a pasta `profiles` do servidor, ele junta todos os `.ADM` e `.RPT` num relatório só e conta os `.mdmp` (dumps de crash) que tiver lá.

- `--json` → saída em JSON
- `--top N` → tamanho dos rankings (padrão 10)

Os ids dos jogadores não aparecem no relatório. Só quando dois jogadores têm o mesmo nome (acontece muito com "Survivor") entram os 4 primeiros caracteres do id pra diferenciar.

## Compilando

Com CMake:

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

No Windows sem CMake (MinGW, ou o TDM-GCC que vem com o Dev-C++), roda o `build.bat`. Ele gera `build\dayzlog.exe` e já roda os testes.

Precisa de compilador com C++17. Sem dependências.

## O que deu trabalho

A primeira versão eu fiz em cima de um log de exemplo e quando rodei nos logs de verdade do servidor quase nada batia. Tinha IA matando gente, mod escrevendo no ADM, linha de IA com "Player" no meio, e dois jogadores diferentes chamados "Survivor". Por isso tudo é contado pelo id do jogador e não pelo nome (o mesmo cara às vezes aparece como "Nome (2)" quando reconecta rápido).

Outra coisa chata: o jogo escreve a mesma morte em duas ou três linhas (`hit by ... [HP: 0]`, `killed by ...`, `died. Stats>`) e não é sempre na mesma ordem. Depois da primeira linha o jogador fica marcado como morto até conectar de novo, e as outras só completam a causa. Se a causa veio do dano ela pode ser trocada pela do `killed by`, porque o dano vem com nome traduzido ("Urso-pardo") e o killed by vem certinho (`Animal_UrsusArctos`).

O ADM só tem a hora em cada linha, a data fica no cabeçalho `AdminLog started on ...`. Quando a hora volta pra trás (23:59 -> 00:01) eu somo um dia, e se aparece outro cabeçalho no meio é porque o servidor reiniciou, então fecho a sessão de quem tava online.

No RPT, um erro de script (`Virtual Machine Exception`) ocupa várias linhas e o que importa mesmo é o `Reason:` e a primeira linha do stack trace, que mostra qual mod tá quebrando. Pra agrupar mensagens parecidas troco os números por `#`, menos os grudados em letra tipo `p3d`. Também tive que tratar hora com espaço na frente (` 6:03:12`), que eu não sabia que existia até ver no log.

Não usei regex, é tudo `find` e `string_view`. 5 dias de log do meu servidor (52 arquivos, 255 MB) rodam em uns 3,7 s.

## Estrutura

```
src/adm.cpp      parser do .ADM (mortes, kills, sessões, dano)
src/rpt.cpp      parser do .RPT (agrupa erros e avisos)
src/report.cpp   saída em texto e JSON
src/main.cpp     linha de comando
tests/tests.cpp  testes (sem framework, só um CHECK simples)
```

Os testes rodam no GitHub Actions em Linux (GCC) e Windows (MSVC) a cada push.

## Limitações

O formato do ADM muda um pouco entre versões do DayZ e com mods. Linhas que o analisador não entende entram na contagem de "não reconhecidas" no fim do relatório, e não quebram nada.

Se rodar no seu servidor e aparecer muita linha não reconhecida, abre uma issue com um trecho do log (sem os ids) que eu dou uma olhada.
