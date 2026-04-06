# ARCHITECTURE_GUIDE.md - Dining Philosophers

Guia de arquitetura completo do projeto `philosophers`, baseado na analise estrutural do repositorio. Este documento cobre o problema classico dos Filosofos Jantando (Dining Philosophers), a implementacao em C com POSIX threads e mutexes, e todos os detalhes de concorrencia, sincronizacao e ciclo de vida das threads.

---

## Sumario

1. [Visao Geral do Projeto](#1-visao-geral-do-projeto)
2. [O Problema dos Filosofos Jantando](#2-o-problema-dos-filosofos-jantando)
3. [Arquitetura do Sistema](#3-arquitetura-do-sistema)
4. [Estruturas de Dados e Estado Compartilhado](#4-estruturas-de-dados-e-estado-compartilhado)
5. [Threads - Ciclo de Vida e Loop de Simulacao](#5-threads---ciclo-de-vida-e-loop-de-simulacao)
6. [Mutexes - Sincronizacao e Protecao de Recursos](#6-mutexes---sincronizacao-e-protecao-de-recursos)
7. [Prevencao de Deadlock](#7-prevencao-de-deadlock)
8. [Prevencao de Starvation e Precisao de Timing](#8-prevencao-de-starvation-e-precisao-de-timing)
9. [O Monitor (The Reaper)](#9-o-monitor-the-reaper)
10. [Build, Uso e Argumentos CLI](#10-build-uso-e-argumentos-cli)
11. [Glossario Tecnico](#11-glossario-tecnico)

---

## 1. Visao Geral do Projeto

O projeto `philosophers` e um exercicio de programacao concorrente baseado no classico problema dos Filosofos Jantando (Dining Philosophers Problem). Ele simula um cenario onde multiplos filosofos sentam em uma mesa redonda com um grande prato de espaguete. Eles alternam entre tres estados: **comer**, **dormir** e **pensar**.

A simulacao requer sincronizacao cuidadosa porque ha tantos garfos quanto filosofos, e um filosofo precisa de **dois garfos** para comer.

O projeto e implementado em **C** usando:
- **POSIX threads** (`pthread_t`) para representar cada filosofo como uma thread independente
- **Mutexes** (`pthread_mutex_t`) para gerenciar a contencao de recursos e prevenir race conditions

### Mapeamento Conceitual: Problema para Codigo

| Entidade Fisica | Entidade no Codigo | Estrutura de Dados |
| :--- | :--- | :--- |
| **Filosofo** | Thread | `t_philo` |
| **Garfo** | Mutex | `pthread_mutex_t` |
| **Mesa/Ambiente** | Estado Global | `t_data` |
| **Timer de Morte** | Monitor (Reaper) | `monitor_routine` |

### Componentes Principais

O codigo e estruturado em tres areas funcionais primarias:

1. **Inicializacao**: Parsing dos argumentos de linha de comando e alocacao da memoria necessaria para filosofos e seus respectivos mutexes.
2. **O Loop de Simulacao**: O ciclo de vida de uma thread filosofo, que alterna entre `philo_eat`, `philo_sleep` e `philo_think`.
3. **O Monitor (Reaper)**: Uma rotina de background que garante que a simulacao termina se um filosofo exceder o `time_to_die` ou se todos ja comeram o numero requerido de vezes.

### Diagrama de Arquitetura Geral

```
+--------------------------------------------------+
|          Simulation Environment (t_data)          |
|                                                  |
|  [pthread_mutex_t* forks]  [write_lock mutex]    |
|  [dead_flag]               [dead_lock mutex]     |
+--------------------------------------------------+
        |               |               |
        v               v               v
  +-----------+   +-----------+   +-----------+
  | t_philo 1 |   | t_philo 2 |   | t_philo N |
  | Thread 1  |   | Thread 2  |   | Thread N  |
  | L-Fork -->|   | L-Fork -->|   | L-Fork -->|
  | R-Fork -->|   | R-Fork -->|   | R-Fork -->|
  +-----------+   +-----------+   +-----------+
        |               |               |
        +-------+-------+-------+-------+
                |                |
                v                v
         [Log via write_lock]  [Monitor Thread (Reaper)]
                                  Polls all philosophers
```

### Fluxo de Execucao

```
main()
  |
  +--> init_data()       -> Parse ARGV & Setup Mutexes
  |       |
  |       +--> Retorna t_data preenchida
  |
  +--> pthread_create()  -> Lanca Philosopher Threads
  |
  +--> monitor_routine() -> Lanca Monitor Thread
  |
  |    [Simulacao Rodando...]
  |
  +--> Monitor sinaliza fim da simulacao
  |
  +--> pthread_join()    -> Join em todas as threads
  |
  +--> Cleanup & Exit
```

---

## 2. O Problema dos Filosofos Jantando

O problema descreve um conjunto de filosofos sentados em uma mesa redonda. Entre cada par de filosofos adjacentes ha um unico garfo. Para comer, um filosofo deve possuir **tanto o garfo a sua esquerda quanto o garfo a sua direita**.

Este problema foi originalmente formulado por **Edsger Dijkstra em 1965** e e um dos problemas classicos de sincronizacao em ciencia da computacao.

### Restricoes do Problema

1. **Deadlock-Freedom (Livre de Deadlock)**: Um estado onde todo filosofo segura um garfo e espera indefinidamente pelo segundo deve ser impossivel.
2. **Starvation-Freedom (Livre de Starvation)**: Todo filosofo deve eventualmente conseguir comer. Se um filosofo nao come dentro de `time_to_die`, ele morre e a simulacao termina.
3. **Exclusao Mutua**: Dois filosofos nao podem segurar o mesmo garfo simultaneamente.
4. **Timing**: Filosofos devem gastar quantidades especificas de tempo nos estados `EATING`, `SLEEPING` e `THINKING`.

### Mapeamento Problema -> Codigo

| Entidade Conceitual | Entidade de Implementacao | Tipo / Estrutura |
| :--- | :--- | :--- |
| Filosofo | Thread | `pthread_t` |
| Garfo | Mutex | `pthread_mutex_t` |
| Monitor de Starvation | Reaper Thread | `monitor_routine` |
| Relogio de Morte | Verificacao de Timestamp | `get_time_ms` / `last_meal_time` |

---

## 3. Arquitetura do Sistema

O sistema `philosophers` e projetado como uma simulacao multi-threaded onde cada filosofo e representado por uma thread dedicada. A arquitetura segue um modelo de **memoria compartilhada**, utilizando POSIX threads (`pthread_t`) e mutexes (`pthread_mutex_t`) para gerenciar o acesso concorrente a recursos (garfos) e estado compartilhado (status da simulacao).

### Dois Subsistemas Primarios

1. **O Motor de Simulacao**: Gerencia o ciclo de vida e as transicoes de estado das threads dos filosofos.
2. **O Monitor (The Reaper)**: Uma rotina de alta prioridade que garante que as restricoes da simulacao sao respeitadas e trata a logica de terminacao.

### Mapeamento de Conceitos para Codigo

```
+-----------------------+         +------------------------+
| Espaco de Linguagem   |         | Espaco de Codigo       |
| Natural               |         |                        |
+-----------------------+         +------------------------+
| Filosofo              | ------> | t_philo struct         |
|                       | ------> | philosopher_routine()  |
| Garfo                 | ------> | pthread_mutex_t (forks)|
| Mesa/Ambiente         | ------> | t_data struct          |
+-----------------------+         +------------------------+

t_philo --Refers to--> t_data
philosopher_routine() --Uses--> pthread_mutex_t (forks)
```

### Fluxo de Inicializacao da Simulacao

```
[Initialization - main.c]
    main() --> init_simulation_data()
                  |
                  +--> init_forks_mutexes()
                  +--> init_philosophers_structs()

[Execution - simulation.c]
    start_simulation() --> pthread_create(Philosopher_Threads)
                       --> pthread_create(Monitor_Thread)

[Thread Routines]
    Philosopher_Threads --> philosopher_routine()
    Monitor_Thread      --> monitor_routine()

[Shared State Interactions]
    philosopher_routine() --Update last_meal--> t_philo (Shared State)
    monitor_routine()     --Read last_meal---> t_philo (Shared State)
    monitor_routine()     --Set stop_flag----> t_data  (Shared State)
    philosopher_routine() --Check stop_flag--> t_data  (Shared State)
```

---

## 4. Estruturas de Dados e Estado Compartilhado

A implementacao usa um modelo de dados hierarquico em dois niveis para separar parametros globais da simulacao do estado individual de cada filosofo.

### 4.1. Estado Global: `t_data`

A struct `t_data` serve como o repositorio central para constantes de toda a simulacao e primitivas de sincronizacao compartilhadas. E inicializada uma vez no inicio do programa e persiste ate que todas as threads tenham feito join.

#### Definicao da Estrutura

A struct `t_data` contem:
- **Parametros da Simulacao**: Valores read-only parseados dos argumentos de linha de comando (e.g., `time_to_die`, `time_to_eat`).
- **Primitivas de Sincronizacao**: Um array de `pthread_mutex_t` representando os garfos, e um mutex dedicado para logging thread-safe.
- **Controle de Terminacao**: Um `dead_flag` e um mutex `dead_lock` associado para sinalizar todas as threads a pararem quando um filosofo morre ou a cota de refeicoes e atingida.

#### Layout de Memoria

| Campo | Tipo | Proposito |
| :--- | :--- | :--- |
| `nb_philos` | `long` | Numero total de filosofos/threads. |
| `time_die` | `long` | Tempo (ms) antes de um filosofo morrer sem comer. |
| `time_eat` | `long` | Tempo (ms) para terminar uma refeicao. |
| `time_sleep` | `long` | Tempo (ms) gasto dormindo apos comer. |
| `nb_meals` | `long` | Limite opcional de refeicoes por filosofo. |
| `dead_flag` | `int` | Flag booleana (0/1) para parar a simulacao. |
| `forks` | `pthread_mutex_t*` | Array de mutexes representando garfos fisicos. |
| `write_lock` | `pthread_mutex_t` | Mutex para prevenir mensagens de log intercaladas. |
| `dead_lock` | `pthread_mutex_t` | Mutex protegendo o `dead_flag`. |

#### Propriedade de Memoria

A struct `t_data` e tipicamente alocada na stack na funcao `main` e passada por referencia para todas as funcoes de inicializacao e threads dos filosofos.

### 4.2. Estado por Filosofo: `t_philo`

Cada filosofo e representado por uma struct `t_philo`. Esta estrutura contem o estado especifico do filosofo, seu handle de thread, e ponteiros de volta para o `t_data` compartilhado e seus garfos atribuidos.

#### Definicao da Estrutura

A struct `t_philo` rastreia:
- **Identidade**: Um ID inteiro unico (1 ate `nb_philos`).
- **Recursos**: Ponteiros para os mutexes dos garfos esquerdo e direito.
- **Sinais Vitais**: O timestamp `last_meal` e o contador `meals_eaten`, que sao monitorados pela thread "Reaper".
- **Sincronizacao**: Um mutex local `meal_lock` para prevenir race conditions quando a thread monitor le `last_meal` enquanto a thread do filosofo o atualiza.

#### Mapeamento de Garfos

Para evitar duplicacao de recursos, a struct `t_philo` nao contem mutexes, mas sim **ponteiros** (`pthread_mutex_t *`) para elementos especificos dentro do array `t_data->forks`.

### 4.3. Diagrama de Fluxo de Dados

```
+--- Shared Global State: t_data -------------------+
|                                                    |
|   data->forks[0...N]  --> pthread_mutex_t Array    |
|   data->dead_flag      --> int (Termination Signal)|
|   data->write_lock     --> pthread_mutex_t (Stdout)|
|                                                    |
+----------------------------------------------------+
         ^                    ^
         |                    |
         | References         | References
         |                    |
+--- Philosopher Entity: t_philo ---+
|                                   |
|   philo[i]                        |
|     |-- philo->l_fork  --> &data->forks[i]          |
|     |-- philo->r_fork  --> &data->forks[(i+1)%N]    |
|     |-- philo->meal_lock                             |
|     |-- philo->last_meal                             |
|                                                      |
+------------------------------------------------------+
```

### 4.4. Propriedade de Memoria e Threading

```
[Main Thread Context]
    main() --allocates--> t_data struct
    main() --allocates--> t_philo array

[Philosopher Thread Context]
    pthread_create --arg: &philo[i]--> routine(void *ptr)
    routine() --casts ptr to--> t_philo*
    t_philo* --accesses--> t_data

[Monitor Thread Context]
    monitor_routine() --polls--> t_philo*
    monitor_routine() --sets--> data->dead_flag
```

### 4.5. Estrategia de Protecao de Recursos

O acesso a variaveis compartilhadas e estritamente controlado atraves de mutexes especificos para prevenir data races.

| Variavel Compartilhada | Mutex Protetor | Descricao |
| :--- | :--- | :--- |
| `t_data->dead_flag` | `t_data->dead_lock` | Flag global de terminacao |
| `t_philo->last_meal` | `t_philo->meal_lock` | Timestamp da ultima refeicao |
| `t_philo->meals_eaten` | `t_philo->meal_lock` | Contador de refeicoes |
| `STDOUT` (Logging) | `t_data->write_lock` | Protecao da saida de terminal |

### 4.6. Logica de Atribuicao de Garfos

Os garfos sao atribuidos baseados no indice `i` do filosofo:
- **Garfo Esquerdo**: `&data->forks[i]`
- **Garfo Direito**: `&data->forks[(i + 1) % data->nb_philos]`

Esta atribuicao circular garante que o ultimo filosofo compartilha seu garfo direito com o garfo esquerdo do primeiro filosofo, completando a mesa.

### Diagrama de Relacionamento de Entidades

```
+---------------------+          +---------------------+
|    DATA_STRUCT       |          |    PHILO_STRUCT      |
|---------------------|          |---------------------|
| long nb_philos      |  1----*  | int id              |
| long time_to_die    | contains | pthread_t thread    |
| pthread_mutex_t     |          | long last_meal_time |
|   write_lock        |          | int meals_eaten     |
| int stop_simulation |          +---------------------+
+---------------------+                 |         |
                                   left_fork  right_fork
                                         |         |
                                         v         v
                                  +---------------------+
                                  |      MUTEX          |
                                  |---------------------|
                                  | pthread_mutex_t     |
                                  |   fork_lock         |
                                  +---------------------+
```

---

## 5. Threads - Ciclo de Vida e Loop de Simulacao

Esta secao faz um deep dive tecnico na rotina das threads dos filosofos, cobrindo as transicoes de maquina de estados, a logica de sincronizacao usada para prevenir deadlocks, e os mecanismos precisos de timing requeridos para a simulacao.

### 5.1. Inicializacao e Entrada da Thread

Cada thread de filosofo e criada dentro de um loop na funcao `main` usando `pthread_create`. O ponto de entrada para cada thread e a funcao `philo_routine`.

Antes de entrar no loop principal da simulacao, cada filosofo recebe um ponteiro para uma struct `t_philo` contendo:
- Seu ID unico
- Mutexes dos garfos
- Uma referencia ao objeto compartilhado `t_data`

#### Staggered Start (Inicio Escalonado)

Para prevenir um efeito "thundering herd" onde todos os filosofos imediatamente pegam garfos no exato mesmo microsegundo, a simulacao introduz um **pequeno delay de inicio escalonado** para filosofos de ID impar. Isso e crucial para evitar contencao desnecessaria no comeco.

### 5.2. A Maquina de Estados (EATING -> SLEEPING -> THINKING)

O ciclo de vida do filosofo e um loop continuo que executa ate que uma flag global de parada seja detectada. Cada ciclo consiste em tres estados primarios:

```
                   +----------+
                   | THINKING |<---------+
                   +----------+          |
                        |                |
                        v                |
                  +-------------+        |
                  | TAKING_FORKS|        |
                  +-------------+        |
                        |                |
               pthread_mutex_lock()      |
                        |                |
                        v                |
                   +--------+            |
                   | EATING |            |
                   +--------+            |
                        |                |
              pthread_mutex_unlock()     |
                        |                |
                        v                |
                   +----------+          |
                   | SLEEPING |----------+
                   +----------+
                     usleep()

         Se simulation_end == true --> [EXIT THREAD]
```

#### Detalhamento dos Estados

1. **Eating (Comendo)**: O filosofo tenta adquirir dois garfos. Uma vez adquiridos, atualiza seu `last_meal_time`, incrementa seu contador `meals_eaten`, e aguarda por `time_to_eat` milissegundos.

2. **Sleeping (Dormindo)**: Apos comer, o filosofo libera ambos os garfos e aguarda por `time_to_sleep` milissegundos.

3. **Thinking (Pensando)**: O filosofo entra em estado de pensamento (sem delay significativo) antes de tentar pegar garfos novamente.

### 5.3. Fluxo da Rotina de Simulacao

```
philo_routine()
    |
    +--> id % 2 == 0?
    |       |
    |    Yes: usleep(100)  [Staggered start]
    |    No:  continua
    |
    +--> While !get_stop_flag()
            |
            +--> take_forks()       --> pthread_mutex_lock(fork)
            |
            +--> philo_eat()        --> update_last_meal()
            |
            +--> philo_sleep()
            |
            +--> philo_think()
            |
            +--> [volta ao inicio do loop]
    
    get_stop_flag() == true --> Exit Thread
```

### 5.4. Sequencia de Terminacao

O loop da simulacao em `philo_routine` verifica continuamente a flag global `stop_flag` usando um getter thread-safe `get_stop_flag()`. Esta flag e setada pela rotina Monitor (The Reaper) se:

1. Um filosofo excede o `time_to_die`.
2. O requisito opcional `number_of_times_each_philosopher_must_eat` e atingido por todas as threads.

Uma vez que a flag e `true`, o filosofo finaliza sua acao atomica atual (se houver), sai do loop `while`, e retorna `NULL`, permitindo que a thread principal faca `pthread_join`.

### 5.5. Timing e Logging

#### Formato de Log

Toda mudanca de estado e logada usando uma funcao thread-safe `print_status`, que protege o `stdout` com um mutex para prevenir intercalacao de mensagens.

**Formato:** `[timestamp_ms] [id] [action]`

| String da Acao | Significado |
| :--- | :--- |
| `has taken a fork` | Mutex de um garfo travado com sucesso |
| `is eating` | Ambos garfos travados; `last_meal_time` atualizado |
| `is sleeping` | Ambos garfos destravados; entrando em `usleep` |
| `is thinking` | Terminou de dormir; pronto para competir por garfos |
| `died` | Thread monitor detectou `time_since_last_meal > time_to_die` |

#### Implementacao de Timing

A funcao `ft_usleep` e usada em vez do `usleep` padrao para fornecer maior precisao. Ela faz polling do tempo atual em um loop para compensar o fato de que o `usleep` padrao pode dormir significativamente mais do que o solicitado.

```
get_time_ms()      --> Relogio da Simulacao (gettimeofday)
ft_usleep()        --> Delay de Precisao (spin-check loop)
print_status()     --> Logging Thread-Safe (protegido por write_lock)
```

#### Fluxo de Logging

```
Philosopher Thread
    |
    +--> print_status(ID, State)
            |
            +--> pthread_mutex_lock(write_lock)
            |       [Previne outras threads de imprimir]
            |
            +--> printf("[timestamp] [id] [state]")
            |
            +--> pthread_mutex_unlock(write_lock)
```

---

## 6. Mutexes - Sincronizacao e Protecao de Recursos

A simulacao depende de POSIX threads (pthreads) para representar filosofos e mutexes para proteger dados compartilhados e representar garfos fisicos.

### 6.1. O que e um Mutex?

Um **mutex** (mutual exclusion) e uma primitiva de sincronizacao que previne multiplas threads de acessar o mesmo recurso simultaneamente. No contexto deste projeto:

- **`pthread_mutex_lock()`**: Trava o mutex. Se ja estiver travado por outra thread, a thread chamadora **bloqueia** ate que o mutex seja liberado.
- **`pthread_mutex_unlock()`**: Destrava o mutex, permitindo que outras threads o adquiram.
- **`pthread_mutex_init()`**: Inicializa um novo mutex.
- **`pthread_mutex_destroy()`**: Destroi um mutex apos uso.

### 6.2. Tipos de Mutexes no Projeto

O projeto emprega varios tipos de mutexes para diferentes propositos:

#### 1. Fork Mutexes (Mutexes de Garfo)

Um array de mutexes onde cada elemento `pthread_mutex_t` garante que apenas um filosofo segura um garfo por vez.

```c
// Conceptualmente:
pthread_mutex_t *forks;  // Array de nb_philos mutexes
// forks[0], forks[1], ..., forks[N-1]
```

Cada filosofo recebe ponteiros para dois garfos:
- `philo->l_fork = &data->forks[i]`
- `philo->r_fork = &data->forks[(i + 1) % nb_philos]`

#### 2. Data Mutexes (Mutexes de Dados)

Mutexes especializados usados para proteger variaveis compartilhadas:

- **`meal_lock`**: Protege `last_meal_time` e `meals_eaten` de race conditions entre a thread do filosofo e a thread monitor.
- **`dead_lock` / `stop_lock`**: Protege o `dead_flag` / `stop_sim`, a flag global de terminacao.

#### 3. Write Lock (Mutex de Escrita)

- **`write_lock`**: Previne saida garbled (intercalada) no terminal. Toda operacao de `printf` e envolvida em lock/unlock deste mutex.

### 6.3. Gerenciamento de Recursos e Threading

```
[Mapeamento de Conceitos para Codigo]

Garfo Fisico       --> t_fork.fork_m (pthread_mutex_t)
Timer de Morte     --> t_philo.last_meal (long long)
Parada da Simulacao --> t_data.keep_iterating (bool)
Acesso a Recurso   --> pthread_mutex_lock()
```

### 6.4. Estrategias de Sincronizacao

1. **Fork Mutexes**: Array de mutexes onde cada struct `t_fork` contem um `pthread_mutex_t` para garantir que apenas um filosofo segure um garfo por vez.

2. **Data Mutexes**: Mutexes especializados para proteger variaveis compartilhadas como o timestamp `last_meal` e a flag de status `keep_iterating`, prevenindo data races durante o monitoramento.

3. **A Thread Monitor**: Uma thread "reaper" separada que roda `monitor_routine` para supervisionar o estado de todos os filosofos e garantir terminacao oportuna se uma condicao de morte for detectada.

### 6.5. Fluxo de Interacao entre Threads

```
Philosopher Thread (routine)          Monitor Thread (monitor_routine)
         |                                      |
         |--Update last_meal (meal_lock)-->      |
         |                              Shared   |
         |                              State    |
         |                              (t_data) |
         |                                      |
         |      <--Check last_meal vs time_to_die-|
         |                                      |
         |      <--Set keep_iterating = false-----|
         |         (lock_stop)                   |
         |                                      |
         |--Read keep_iterating-->               |
         |                                      |
         |--Exit Loop-->                         |
```

---

## 7. Prevencao de Deadlock

Deadlock e prevenido neste projeto atraves de uma estrategia de **aquisicao assimetrica de recursos**. Em vez de todos os filosofos alcancarem o mesmo lado primeiro, a ordem e determinada pelo ID do filosofo. Isso quebra a **condicao de espera circular** necessaria para um deadlock ocorrer.

### 7.1. A Estrategia de Pegar Garfos Assimetricamente

Para quebrar a condicao de espera circular (uma das quatro condicoes de Coffman para deadlock), a simulacao impoe uma ordem especifica em como os filosofos adquirem seus mutexes (garfos). A logica e ramificada baseada em se o ID do filosofo e par ou impar.

#### Logica de Aquisicao

- **Filosofos com ID Par (0, 2, 4...)**: Tentam travar o `left_fork` primeiro, depois o `right_fork`.
- **Filosofos com ID Impar (1, 3, 5...)**: Tentam travar o `right_fork` primeiro, depois o `left_fork`.

#### Tabela de Prioridades

| ID do Filosofo | Primeira Prioridade | Segunda Prioridade |
| :--- | :--- | :--- |
| **Par (0, 2, 4...)** | `left_fork` | `right_fork` |
| **Impar (1, 3, 5...)** | `right_fork` | `left_fork` |

### 7.2. Diagrama de Branching Assimetrico

```
philo_routine()
    |
    +--> philo_eat()
            |
            +--> philo->id % 2 == 0?
                    |
                 True (Par):
                    |   pthread_mutex_lock(philo->left_fork)
                    |   pthread_mutex_lock(philo->right_fork)
                    |
                 False (Impar):
                    |   pthread_mutex_lock(philo->right_fork)
                    |   pthread_mutex_lock(philo->left_fork)
                    |
                    v
              [Ambos garfos adquiridos - EATING]
```

### 7.3. Quebrando a Espera Circular

Um deadlock requer um ciclo no grafo de alocacao de recursos. Ao forcar filosofos pares e impares a pegar garfos em ordens diferentes, a implementacao garante que pelo menos um filosofo em qualquer cadeia circular sera incapaz de pegar seu **primeiro** garfo porque ele e o **segundo** garfo do seu vizinho.

#### Exemplo com 2 Filosofos

1. Philo 1 (Impar) tenta pegar `right_fork` (que e o `left_fork` de Philo 2).
2. Philo 2 (Par) tenta pegar `left_fork` (que tambem e o `left_fork` de Philo 2).
3. Eles competem pelo **mesmo** mutex imediatamente. Um ganha, o outro espera.
4. O vencedor entao pega o segundo garfo (que nao tem contencao) e come.
5. Eventualmente libera ambos, permitindo que o outro prossiga.

### 7.4. Fluxo de Dados dos Ponteiros de Garfos

```
+--- t_data (Estado Global) ---+
|                               |
|   forks[0]      forks[1]     |
|      ^              ^        |
+------|--------------|--------+
       |              |
+--- t_philo (ID: 1 - Impar) --------+
|   left_fork  --> forks[0]           |
|   right_fork --> forks[1]           |
|   Ordem: right_fork DEPOIS left_fork|
+-----------------------------------------+

+--- t_philo (ID: 2 - Par) ----------+
|   left_fork  --> forks[1]           |
|   right_fork --> forks[0]           |
|   Ordem: left_fork DEPOIS right_fork|
+-----------------------------------------+
```

### 7.5. Secoes Criticas e Saida Segura

Quando um filosofo termina de comer, ele deve liberar os garfos na ordem reversa de aquisicao ou simplesmente garantir que ambos estao destravados antes de transicionar para o estado `SLEEPING`.

Se a simulacao termina (detectada via `check_stop_flag`), o filosofo deve sair da rotina sem ficar preso em um lock.

### 7.6. Abordagens Alternativas e Trade-offs

Embora a estrategia assimetrica seja usada aqui, outros metodos foram considerados:

1. **Ordenacao Global de Garfos**: Sempre pegar o garfo de indice menor primeiro (e.g., `if (left < right)`). Esta implementacao alcanca um resultado similar usando a paridade do ID.

2. **Arbitro/Garcom**: Um mutex central que um filosofo deve adquirir antes de poder pegar *qualquer* garfo. Rejeitado pois limita a concorrencia (apenas um filosofo pode tentar pegar garfos por vez).

3. **Hierarquia de Recursos**: Atribuir uma ordem parcial estrita a todos os recursos. A paridade baseada em ID e uma implementacao funcional de uma hierarquia de recursos que previne ciclos no grafo de espera.

---

## 8. Prevencao de Starvation e Precisao de Timing

### 8.1. Rastreamento de Tempo de Alta Resolucao

A simulacao mede todos os intervalos em milissegundos. Para alcacar a precisao necessaria para os parametros `time_to_die`, `time_to_eat` e `time_to_sleep`, o projeto utiliza `gettimeofday` de `<sys/time.h>`.

#### Geracao de Timestamps

A funcao `get_time_in_ms` serve como a utilidade central para recuperar o tempo atual do sistema convertido em um inteiro de milissegundos de 64 bits. Esta funcao e usada para:

1. Calcular o timestamp relativo para saidas de log.
2. Atualizar o `last_meal_time` de cada filosofo.
3. Calcular o tempo decorrido desde a ultima refeicao durante o polling do monitor.

#### Implementacao de Sleep Customizado

O `usleep` padrao e frequentemente impreciso porque so garante uma duracao *minima* de sleep; o processo pode permanecer nao-escalonado por mais tempo do que solicitado.

Para mitigar isso, o projeto implementa `precise_usleep` (ou `ft_usleep`). Esta funcao usa um loop de "spin-check" que repetidamente chama `usleep` em pequenos incrementos (500 microsegundos) enquanto verifica o tempo real decorrido contra o `time_to_wait` alvo.

```
precise_usleep(time_to_wait):
    start = get_time_in_ms()
    while (get_time_in_ms() - start < time_to_wait):
        usleep(500)
    return
```

### 8.2. Deteccao de Starvation: A Thread Monitor

Starvation e definida como a condicao onde o tempo decorrido desde a ultima refeicao de um filosofo (ou desde o inicio da simulacao) excede o parametro `time_to_die`.

#### A Rotina Reaper

A simulacao cria uma thread monitor dedicada rodando `monitor_routine`. Esta thread itera continuamente pelo array de structs `t_philo` para verificar se algum filosofo morreu ou se o requisito opcional "must eat" foi atingido.

1. **Verificacao de Starvation**: Para cada filosofo, o monitor calcula `time_since_last_meal = current_time - philo->last_meal_time`.
2. **Acesso Atomico**: O acesso a `philo->last_meal_time` e protegido por `philo->meal_lock` para prevenir race conditions com a thread do filosofo atualizando seu tempo de refeicao.
3. **Sinal de Terminacao**: Se starvation e detectada, o monitor seta a flag `stop_sim` dentro da struct `t_data`, protegida por `data->stop_lock`.

### 8.3. Polling vs. Uso de CPU

O thread monitor emprega uma estrategia de polling de alta frequencia.

| Feature | Implementacao | Trade-off |
| :--- | :--- | :--- |
| **Intervalo de Polling** | `usleep(1000)` em `monitor_routine` | Baixa latencia para deteccao de morte vs. maior uso de CPU |
| **Sincronizacao de Dados** | `pthread_mutex_t meal_lock` | Garante que o monitor nao le um `last_meal_time` obsoleto |
| **Propagacao do Stop** | utilidade `check_stop_flag` | Filosofos verificam frequentemente a flag `stop_sim` para sair dos loops |

### 8.4. Propagacao da Flag de Stop

Para garantir que a simulacao para imediatamente apos starvation, a funcao `check_stop_flag` e chamada pelas threads dos filosofos antes de iniciar qualquer nova acao (comer, dormir, pensar). Isso previne que um filosofo entre em um longo `precise_usleep` apos outro filosofo ja ter morrido.

---

## 9. O Monitor (The Reaper)

A Rotina de Monitoramento, coloquialmente chamada de **"The Reaper"**, e uma thread dedicada responsavel por supervisionar o estado da simulacao. Sua funcao primaria e garantir que a simulacao termine imediatamente se um filosofo morre de starvation ou se todos os filosofos atingiram a contagem requerida de refeicoes.

### 9.1. Ciclo de Vida e Responsabilidade do Monitor

O monitor e lancado apos todas as threads dos filosofos terem sido criadas. Ele roda em um loop continuo, fazendo polling do status de cada filosofo ate que uma condicao de terminacao seja atingida.

### 9.2. Logica de Deteccao de Starvation

O nucleo da logica do Reaper e a deteccao de starvation. Para cada filosofo, o monitor compara o tempo atual contra o `last_meal_time` armazenado na struct `t_philo`.

```
monitor_routine loop:
    |
    +--> check_death(philo)
            |
            +--> pthread_mutex_lock(&philo->meal_lock)
            |
            +--> get_time_ms() - philo->last_meal_time
            |
            +--> Resultado > time_to_die?
                    |
                 Sim:
                    +--> set_sim_stop_flag(data, true)
                    +--> print_status(philo, 'died')
                    |
                 Nao:
                    +--> pthread_mutex_unlock(&philo->meal_lock)
                    +--> continua verificando proximo filosofo
```

### 9.3. Terminacao por Saciedade (Fullness - Opcional)

Se o usuario fornece o argumento opcional `number_of_times_each_philosopher_must_eat`, o monitor tambem rastreia se todos os filosofos atingiram esse threshold.

- **Rastreamento de Contador**: Dentro da logica `check_fullness`, o monitor itera por todos os filosofos.
- **Thread Safety**: Trava `philo->meal_lock` para ler `philo->meals_eaten`.
- **Estado Global**: Se o `meals_eaten` de cada filosofo e maior ou igual ao requerido, o monitor seta a flag global `stop_sim`.

### 9.4. Diagrama de Resolucao de Condicao de Terminacao

```
+--- t_data (Estado Global) ---+       +--- t_philo (Estado Local) ---+
|                               |       |                              |
|   stop_sim flag               |       |   last_meal_time             |
|                               |       |   meals_eaten                |
+------^-----------^------------+       +------^-----------^-----------+
       |           |                           |           |
    Writes      Reads                       Reads       Reads
       |           |                           |           |
  [monitor_    [philosopher_              [monitor_    [monitor_
   routine]     routine]                   routine]    routine]
                   |
                   v
            Exit if stop_sim == true
                   |
                   v
            [Thread Termination]
```

### 9.5. Resposta da Thread ao Sinal de Stop

As threads dos filosofos **nao terminam instantaneamente** via sinais externos (como `pthread_cancel`). Em vez disso, elas realizam "cooperative multitasking" verificando frequentemente a flag `stop_sim`.

1. **Rotina de Verificacao**: No inicio de cada acao (comer, dormir, pensar) e durante a fase de aquisicao de garfos, o filosofo chama `sim_should_stop()`.
2. **Saida Segura**: Se `sim_should_stop()` retorna `true`, a rotina do filosofo quebra seu loop, permitindo que a thread retorne e seja joinada pela thread principal.
3. **Integridade de Lock**: Esta abordagem garante que um filosofo **nunca termina enquanto segura um mutex** (garfo), o que de outra forma causaria deadlock para threads restantes durante a sequencia de shutdown.

### 9.6. Tabela de Funcoes do Monitor

| Funcao | Arquivo | Proposito |
| :--- | :--- | :--- |
| `monitor_routine` | `src/monitor.c` | Ponto de entrada para a thread reaper |
| `check_death` | `src/monitor.c` | Verifica se um filosofo excedeu `time_to_die` |
| `check_fullness` | `src/monitor.c` | Verifica se a simulacao deve terminar por contagem de refeicoes |
| `sim_should_stop` | `src/utils.c` | Getter thread-safe para a flag global de terminacao |
| `set_sim_stop_flag` | `src/utils.c` | Setter thread-safe para a flag global de terminacao |

---

## 10. Build, Uso e Argumentos CLI

### 10.1. Sistema de Build

O projeto utiliza um `Makefile` para gerenciar a compilacao. O processo de build gera um unico executavel chamado `philo`.

#### Flags do Compilador

- `-Wall -Wextra -Werror`: Habilita todos os warnings padrao e os trata como erros.
- `-pthread`: Linka a biblioteca POSIX threads requerida para `pthread_create`, `pthread_mutex_init` e outras primitivas de sincronizacao.

#### Targets do Makefile

| Target | Descricao |
| :--- | :--- |
| `all` | Compila os arquivos fonte e gera o binario `philo`. |
| `clean` | Remove arquivos objeto (`.o`). |
| `fclean` | Remove arquivos objeto e o executavel `philo`. |
| `re` | Faz uma re-compilacao completa (`fclean` seguido de `all`). |

### 10.2. Execucao e Argumentos CLI

A simulacao e invocada pela linha de comando e requer 4 a 5 argumentos inteiros. O programa valida que todas as entradas sao inteiros positivos e que a contagem de filosofos esta dentro de um range razoavel (tipicamente 1 a 200).

#### Sintaxe do Comando

```bash
./philo <number_of_philosophers> <time_to_die> <time_to_eat> <time_to_sleep> [number_of_times_each_philosopher_must_eat]
```

#### Definicoes dos Argumentos

1. **`number_of_philosophers`**: O numero total de threads de filosofos e mutexes de garfos a criar.
2. **`time_to_die` (ms)**: Se um filosofo nao comecar a comer dentro deste numero de milissegundos desde sua ultima refeicao (ou inicio da simulacao), ele morre.
3. **`time_to_eat` (ms)**: A duracao que um filosofo segura dois garfos para realizar a acao de "comer".
4. **`time_to_sleep` (ms)**: A duracao que um filosofo passa dormindo apos comer.
5. **`number_of_times_each_philosopher_must_eat` (opcional)**: Se especificado, a simulacao termina quando todos os filosofos comeram pelo menos este numero de vezes.

### 10.3. Fluxo de Dados de Inicializacao

```
argv[1..5]  (strings)
    |
    v
  main()
    |
    +--> ft_atoi() --> init_data()
                          |
                          +--> Armazena valores em t_data struct
                          |
                          +--> init_philos()
                                  |
                                  +--> Inicializa t_philo array
```

### 10.4. Interpretando a Saida da Simulacao

#### Formato de Log

`[timestamp_in_ms] [philosopher_id] [action]`

- **timestamp_in_ms**: O tempo atual relativo ao inicio da simulacao (`current_time - start_time`).
- **philosopher_id**: O indice do filosofo (base 1).
- **action**: Uma das acoes listadas na secao de Timing e Logging.

#### Tratamento de Morte

Quando um filosofo morre, a mensagem "died" e impressa, e a simulacao para imediatamente. A implementacao garante que nenhum log adicional e impresso apos uma mensagem de morte ser exibida.

### 10.5. Tratamento de Erros

- **Argumentos nao numericos**: Se um argumento contem nao-digitos, o programa imprime uma mensagem de erro e sai.
- **Ranges invalidos**: Se `number_of_philosophers` e menor que 1, o programa termina.
- **Falhas de Memoria/Sistema**: Se `malloc` falha ou `pthread_create` nao consegue spawnar uma thread, o programa limpa recursos alocados e sai com status nao-zero.

---

## 11. Glossario Tecnico

### 11.1. Termos Especificos do Dominio

| Termo | Definicao |
| :--- | :--- |
| **Starvation** | Condicao onde um filosofo excede o threshold `time_to_die` sem iniciar uma refeicao. |
| **Deadlock** | Falha de concorrencia onde threads esperam indefinidamente por recursos segurados umas pelas outras. Prevenido aqui via aquisicao assimetrica de garfos. |
| **The Reaper** | A logica de monitoramento que verifica se algum filosofo morreu. |
| **Simulation End Flag** | Variavel compartilhada tipo-atomica (`stop_sim`) protegida por mutex que sinaliza todas as threads a terminarem. |
| **Fork (Garfo)** | Recurso compartilhado (mutex) requerido em pares para um filosofo entrar no estado `EATING`. |

### 11.2. Identificadores da API POSIX

#### `pthread_t`
Tipo de dado usado para identificar uma thread. Cada instancia de filosofo e associada a um handle `pthread_t`.
- **Uso**: Definido dentro da struct `t_philo`.
- **Ciclo de Vida**: Criado via `pthread_create` e sincronizado via `pthread_join`.

#### `pthread_mutex_t`
Primitiva de exclusao mutua usada para proteger recursos compartilhados (garfos) e dados sensiveis (tempo de ultima refeicao, flag de stop).
- **Garfos**: Array de mutexes representando os garfos fisicos.
- **Protecao de Dados**: Mutexes como `meal_lock` e `write_lock` previnem race conditions durante atualizacoes de estado e logging.

#### `gettimeofday`
Funcao usada para recuperar o tempo atual do sistema com precisao de microsegundos.
- **Implementacao**: Envolvida em `get_time_ms` para fornecer timestamps em milissegundos relativos ao inicio da simulacao.

### 11.3. Estruturas de Dados do Projeto

```
[Mapeamento: Linguagem Natural para Espaco de Codigo]

A Mesa          --> struct t_data   (Estado do Mundo)
Filosofo        --> struct t_philo  (Ator Individual)
Talheres        --> pthread_mutex_t *forks
O Relogio       --> get_time_ms()

t_data -->|contains array of|--> t_philo
t_data -->|manages|-----------> pthread_mutex_t *forks
t_philo -->|points to L/R|---> pthread_mutex_t *forks
```

### 11.4. Funcoes Chave do Ciclo de Vida

| Nome da Funcao | Descricao | Localizacao |
| :--- | :--- | :--- |
| `init_data` | Parseia argumentos CLI e inicializa a struct `t_data` compartilhada. | `src/init.c` |
| `init_philos` | Aloca memoria para filosofos e atribui ponteiros de garfos esquerdo/direito. | `src/init.c` |
| `philo_routine` | Ponto de entrada para cada thread de filosofo; contem o loop Eat-Sleep-Think. | `src/routine.c` |
| `check_death` | Logica central do monitor; itera pelos filosofos para verificar `time_to_die`. | `src/monitor.c` |
| `safe_print` | Wrapper thread-safe para `printf` que usa `write_lock` para prevenir saida garbled. | `src/utils.c` |

### 11.5. Constantes e Variaveis Tecnicas

- **`time_to_die`**: Tempo maximo (ms) permitido entre o inicio da ultima refeicao de um filosofo e sua proxima refeicao.
- **`nb_meals`**: Contagem opcional de refeicoes que um filosofo deve terminar antes da simulacao poder terminar.
- **`last_meal`**: Timestamp (ms) armazenado por filosofo, atualizado no inicio da acao de comer.
- **`fork_id`**: O indice no array `forks`. Um filosofo com indice `i` tipicamente usa `forks[i]` e `forks[(i + 1) % total]`.

### 11.6. Fluxo Logico Completo

```
[Estado: THINKING]
    |
    +--> pthread_mutex_lock(left_fork)
    +--> pthread_mutex_lock(right_fork)
    |
[Estado: EATING]
    |
    +--> pthread_mutex_lock(write_lock)
    |       printf("[timestamp] id is eating")
    +--> pthread_mutex_unlock(write_lock)
    |
    +--> usleep(time_to_eat)
    |
    +--> pthread_mutex_unlock(right_fork)
    +--> pthread_mutex_unlock(left_fork)
    |
[Estado: SLEEPING]
    |
    +--> usleep(time_to_sleep)
    |
    +--> [Volta para THINKING]
```

---

**Documento gerado a partir da analise estrutural do repositorio `VictorValente99/philosophers` via DeepWiki.**
