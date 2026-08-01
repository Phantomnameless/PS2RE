## `README.md` — Completo

```markdown
# ps2re

Primeiramente e um re-branch do sistema para funcionar em arm64 (utilizei o xiaomi studioo) então podem haver
inumeros bugs e falhas presentes. o foco e para EMU então por favor quaisquer desenvolvedor que queira ajudar,
compartilhar a construir eu ficarei imensamente grato
pois entendo que o hardware do ps2 foi e continua sendo super-estimado para baixo.

**PS2 → ARM64 Re-architecture: Zero-waste async pipeline reimplementation**

> Extrair 100% da potência do hardware PS2 — EE, VU0, VU1, GS — remapeado
> para ARM64 com task graph assíncrono, Vulkan TBDR, e NEON compute.
> sem stalls de pipeline, sem overhead de DMA.

---

## Índice

- [O que é isso](#o-que-isso)
- [O que NÃO é isso](#o-que-não-isso)
- [Arquitetura](#arquitetura)
- [Mapeamento PS2 → ARM64](#mapeamento-ps2--arm64)
- [Sistema Assíncrono — O Coração](#sistema-assíncrono--o-coração)
- [Roadmap de Conclusão](#roadmap-de-conclusão)
- [Estado Atual — O que está feito](#estado-atual--o-que-está-feito)
- [Estado Atual — O que falta](#estado-atual--o-que-falta)
- [Performance Targets](#performance-targets)
- [Build](#build)
- [Plataformas](#plataformas)
- [Estrutura do Projeto](#estrutura-do-projeto)
- [Decisões de Arquitetura](#decisões-de-arquitetura)
- [Debugging e Profiling](#debugging-e-profiling)
- [Contribuição](#contribuição)
- [Roadmap de Features por Fase](#roadmap-de-features-por-fase)
- [Benchmarks](#benchmarks)
- [Known Issues](#known-issues)
- [Referências Técnicas](#referências-técnicas)

---

## O que é isso

O PS2 tinha um pipeline gráfico brutal:

```
EE (CPU) → VIF → VU0 (physics/anim) → VU1 (vertex) → GIF → GS (raster)
```

Cada estágio tinha gargalos específicos:

| Componente | Clock | Gargalo Principal |
|------------|-------|-------------------|
| **EE** (Emotion Engine) | 294 MHz | 16KB I$ + 8KB D$, branch predictor fraco |
| **VU0** (Vector Unit 0) | 294 MHz | 16KB InstrMem, pipeline stalls em dependências |
| **VU1** (Vector Unit 1) | 294 MHz | 16KB DataMem (~256 verts/batch), DMA stalls |
| **GS** (Graphics Synthesizer) | 147 MHz | 4MB eDRAM compartilhado (FB+Z+tex), sem shaders programáveis |
| **GIF** (Graphics Interface) | — | 128-bit tags com 12-20 ciclos overhead cada |
| **DMA** | — | Transferência manual de QWORD, tag chaining |

Jogos como Shadow of the Colossus, God of War II, e Gran Turismo 4
espremiam cada ciclo desses componentes. Faziam coisas que "não deveriam
ser possíveis" naquele hardware.

**Este projeto pega essa filosofia de "espremer tudo" e aplica ao ARM64.**

A ideia: se você tivesse o PS2, mas com:
- Cache de 64KB+ em vez de 8-16KB
- Branch predictor de 95%+ em vez de ~70%
- FMA nativo em vez de MUL+ADD separados
- TBDR (Tile-Based Deferred Rendering) em vez de immediate-mode
- Memória unificada em vez de 32MB RDRAM + 4MB eDRAM separados
- 50-100× mais largura de banda

...quanto mais longe você poderia ir?

---

## O que NÃO é isso

- **Não é um emulador.** Não roda ISOs de PS2 diretamente.
  Não interpreta MIPS, não emula o IOP, não roda firmware.

- **Não é um recompilador.** Não traduz binários MIPS→ARM64.

- **É um re-architecture.** Os subsistemas do PS2 (EE, VU0, VU1, GS)
  são re-escritos do zero em C11/ARM64, mantendo a mesma filosofia
  de operação mas eliminando cada gargalo conhecido.

- **É um framework.** Jogos precisam ser portados para cima deste
  engine, usando as APIs expostas (task graph, render pipeline,
  texture manager). Os tools incluídos ajudam na conversão de assets.

---

## Arquitetura

```
┌─────────────────────────────────────────────────────────────────────┐
│                         FRAME N+1 (CPU)                             │
│                                                                     │
│  arena_reset() → task_graph_reset() → build_frame_task_graph()     │
│                                                                     │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────────┐    │
│  │ t_logic  │──→│ t_physics│──→│ t_vu1    │──→│ t_build_draw │    │
│  │ (EE)     │   │ (NEON)   │   │ (NEON)   │   │ (CPU→GPU)    │    │
│  └──────────┘   └──────────┘   └──────────┘   └──────────────┘    │
│       │              │                                               │
│       ├──→ t_particles (NEON) ──────────────→ t_build_draw         │
│       ├──→ t_skinning (NEON)  ──────────────→ t_build_draw         │
│       └──→ t_audio (NEON, LITTLE core)                             │
│                                                                     │
│  Work-stealing scheduler distribui tasks entre 4+ cores ARM64      │
│  Arena allocator: zero malloc no hot path, reset em 1 instrução    │
│  Ring buffers lock-free: SPSC para audio, MPSC para upload         │
├─────────────────────────────────────────────────────────────────────┤
│                         FRAME N (GPU)                               │
│                                                                     │
│  Vulkan command buffer (pré-gravado):                              │
│    1. Depth pre-pass (TBDR: preenche HiZ, zero overdraw depois)    │
│    2. Shadow map (render pass separado, transient attachment)       │
│    3. Main opaque (TBDR: hidden surface removal em tile memory)    │
│    4. Transparent (sorted per-tile)                                │
│    5. Post-process (compute shader)                                │
│    6. UI overlay                                                    │
│                                                                     │
│  CPU e GPU nunca se esperam (exceto no turnover de frame slot)     │
├─────────────────────────────────────────────────────────────────────┤
│                     FRAME N-1 (Display)                             │
│                                                                     │
│  Swapchain image apresentada na tela                               │
└─────────────────────────────────────────────────────────────────────┘
```

**Triple buffering de frame slots:**
- Frame N-1: na tela (GPU terminou)
- Frame N: GPU renderizando
- Frame N+1: CPU construindo task graph + executando tasks

CPU nunca bloqueia esperando GPU. GPU nunca espera CPU.
O único ponto de sincronização é o turnover do frame slot —
e mesmo isso é feito com fence assíncrono (poll, não wait busy).

---

## Mapeamento PS2 → ARM64

### EE (Emotion Engine) → ARM64 CPU

```
PS2 EE                          ARM64 Equivalent
──────────────────────────────────────────────────────
MIPS R5900 @ 294MHz            ARM Cortex-X3/X4 @ 3GHz
16KB I$ + 8KB D$               64KB L1I + 64KB L1D
Out-of-order 6-stage            Out-of-order 200+ reorder buffer
128-bit "SIMD" (2×64 fake)     128-bit NEON (real 4-wide float)
Branch predictor ~70%           Branch predictor >95%
Division ~36 cycles             Division ~12 cycles
No FMA                          FMA nativo (fused multiply-add)
Scratchpad 16KB                 L2 256KB-1MB (transparent)
```

O EE fazia: game logic, AI, scripting, setup de DMA, setup de VU programs.
Tudo isso roda no thread principal (ou workers delegados pelo task graph).

### VU0 → ARM64 NEON Compute

```
PS2 VU0                         ARM64 Equivalent
──────────────────────────────────────────────────────
4 FMAC + 1 FDIV @ 294MHz       NEON FMA @ 3GHz
16KB InstrMem (~256 instr)     Programa ilimitado
16KB DataMem (~1024 floats)    Stream inteiro de vértices
Pipeline stall em deps          Zero stalls (OoO execution)
COP2 interface (manual)         Função C direta com inline NEON
```

VU0 fazia: skinning de ossos, simulação de partículas, física simples.
Cada um vira um task no graph, executado em batch com NEON 4-wide.

**Throughput estimado:**
- Skinning: ~100× mais rápido que VU0 (4-wide × 10× clock × FMA)
- Partículas: ~200× mais rápido (operação simples, ideal para SIMD)
- Física: ~80× mais rápido

### VU1 → ARM64 NEON Batch + GPU Vertex Shaders

```
PS2 VU1                         ARM64 Equivalent
──────────────────────────────────────────────────────
4 FMAC + 1 FDIV + 1 EFU        NEON FMA batch (CPU) or GPU VS
16KB InstrMem → 256 instr      Shader ilimitado (GPU) or loop (CPU)
16KB DataMem → ~256 verts      Batch de 65K+ vértices (CPU) or unlimited (GPU)
Manual clipping (hardwired)     Flexible clipping (CPU) or hardware clip (GPU)
GIF tag → 12-20 cyc overhead   Vulkan draw call → ~0 overhead
PATH3 stall on GS busy         Async command buffer → zero stalls
```

VU1 fazia: transformação MVP, clipping, viewport transform.
Em ARM64: batch NEON com SoA layout (4 verts por operação) para CPU-side,
ou vertex shader na GPU para alta geometria.

### GS → Vulkan TBDR

```
PS2 GS                          ARM64 GPU (Mali/Adreno/Apple)
──────────────────────────────────────────────────────
147 MHz, immediate mode         GHz-range, Tile-Based Deferred
4MB eDRAM (FB+Z+tex)           Memória virtual ilimitada
8KB texture cache               4-64KB por shader core + compression
Sem pixel shaders               Fragment shaders programáveis
Multi-pass = ×fill rate         Single-pass deferred, overdraw ≈ 0
Alpha test = bandwidth          Early-Z + TBDR = zero cost
Fog = hardware linear           Fog = shader (flexible)
CLUT lookup = hardware          Texture format nativo (ASTC/ETC2)
Framebuffer readback = caro     Compute shader post-process (free)
```

**O salto mais transformador: TBDR.**

O GS rasterizava triângulos imediatamente no eDRAM.
Cada overdraw = ler/escrever eDRAM = fill rate desperdiçado.
Multi-pass para efeitos = multiplicar esse desperdício.

TBDR rasteriza em tiles de 16×16 pixels em SRAM on-chip.
Z-test acontece ANTES do fragment shader (early fragment test).
Overdraw = custo zero (pixel descartado antes de calcular cor).

Isso é como BotW rodava no Wii U/Switch: zero desperdício de fill rate.

---

## Sistema Assíncrono — O Coração

### Por quê assíncrono?

O PS2 era síncrono por necessidade: programador controlava tudo manualmente.
O EE montava dados, disparava DMA para VU1, VU1 processava, mandava para GS.
Se qualquer estágio ficasse ocioso esperando, ciclos eram desperdiçados.

**Exemplo real de desperdício no PS2:**

```
EE monta batch A ─────[3ms]────→ DMA para VU1 ──[0.5ms]──→
VU1 processa A ────[4ms]────→ GIF stall ──[1ms]──→ GS rasteriza ──[2ms]──→
↑
EE OCIOSO por 7.5ms aqui
```

Em ARM64 com task graph:

```
Worker 0: [logic A] [logic B] [logic C] [build draw] [submit]
Worker 1: [physics A] [physics B] [skinning A] [skinning B]
Worker 2: [particles A] [particles B] [culling A] [culling B]
Worker 3: [audio mix] [upload tex] [animate A] [animate B]
GPU:      [render frame N-1] (async, sem relação com CPU)
```

Nenhum core fica ocioso. O work-stealing scheduler garante isso:
quando um worker termina suas tasks, ele "rouba" tasks de outros workers
que ainda estão ocupados.

### Componentes do sistema assíncrono

| Componente | Arquivo | O que faz |
|-----------|---------|-----------|
| **Arena Allocator** | `memory/arena.h/c` | Bump allocator por frame. Reset = 1 ADD instruction. Zero fragmentation. |
| **Object Pool** | `memory/pool.h/c` | Fixed-size free list. O(1) alloc/free. Para draw commands, closures. |
| **SPSC Ring Buffer** | `async/ring_buffer.h/c` | Lock-free single-producer/single-consumer. Cache-line padded. Para audio. |
| **MPSC Ring Buffer** | `async/ring_buffer.h/c` | CAS-based multi-producer. Para texture upload commands. |
| **Task Graph** | `async/task_graph.h/c` | DAG com dependency counters. Cascade automático. |
| **Work-Stealing Queue** | `async/work_queue.h/c` | Chase-Lev deque. Owner pop (LIFO), stealer steal (FIFO). |
| **CPU Fence** | `async/fence.h/c` | Mutex+condvar para frame slot synchronization. |
| **Platform Layer** | `platform.h` | Barrier emulation (Android), CPU affinity, spin hints. |

### Flow de uma task

```
1. task_graph_add()        → Task criada na arena, deps_remaining=0
2. task_graph_depend()     → deps_remaining++, predecessor registra dependent
3. work_queue_submit_graph() → Tasks com deps=0 vão para work deques
4. Worker thread:
   task = deque_pop()    → Tenta própria deque (LIFO, cache-friendly)
   if (!task) task = deque_steal() → Rouba de outra deque (FIFO)
   task->func(task->data) → Executa
   task_graph_complete()  → state=DONE, decrementa deps de dependentes
   → Se dependent chegou a 0 → pronto para executar
```

**A cascata é automática.** Você não precisa gerenciar quando uma task
dependente pode rodar — o sistema faz isso.

### Frame Pipeline (triple-buffered)

```
Frame N-1: [GPU rendering] ──────────────────────→ [Present]
Frame N:   [CPU: all tasks] ──→ [GPU submit] ────→ [GPU rendering]
Frame N+1: [arena_reset] ───→ [build tasks] ──→ [start executing]

GPU fence: signaled quando frame slot está livre
CPU espera fence APENAS no início do frame (nunca no meio)
```

---

## Roadmap de Conclusão

### Fase 0 — Fundação [██████████] 100% COMPLETA

```
[x] Types, config, platform abstraction
[x] Arena allocator (bump, reset, page management)
[x] Object pool (fixed-size free list)
[x] SPSC ring buffer (lock-free, cache-padded)
[x] MPSC ring buffer (CAS-based)
[x] Task graph (DAG, dependency cascade)
[x] Work-stealing scheduler (Chase-Lev deque)
[x] CPU fence (frame synchronization)
[x] NEON vec4/matrix math (4-wide float, batch SoA transform)
[x] All unit tests passing
[x] CMakeLists.txt (Linux ARM64, Android NDK, x86_64 dev)
```

**Status: DONE. Fundação sólida, testada, sem known bugs.**

### Fase 1 — PS2 Subsystems [████████░░] ~80% COMPLETA

```
[x] EE context + game logic task
[x] VU0 pipeline (skinning, particles, physics — NEON)
[x] VU1 pipeline (vertex transform, clip, viewport — NEON batch)
[x] GS state machine (register decode, pipeline hash)
[x] GIF channel (tag parsing, vertex accumulation)
[x] DMA engine (streaming memcpy, prefetch)
[x] VIF unpack (V4-32, V4-16, V4-8, V2-32)
[ ] VU1 microprogram interpreter (para microcode original de jogos)
[ ] GS feedback readback (FB→CPU para efeitos como motion blur)
[ ] GS local-to-local transfer (eDRAM copy emulation)
[ ] IPU (Image Processing Unit — MPEG decode)
[ ] SIF (Subsystem Interface — IOP communication)
[ ] IOP emulation (SPU2, CDVD, PAD, SIO)
```

**O que falta na Fase 1 e POR QUE:**

- **VU1 microprogram interpreter**: Alguns jogos usam microprogramas VU1 customizados
  que não podem ser substituídos por shaders fixos. Precisamos de um interpretador
  que execute o microcódigo original em NEON. Prioridade: MÉDIA (jogos que usam
  microcode genérico já funcionam sem isso).

- **IOP emulation**: O IOP (I/O Processor) era um MIPS R3000 separado que controlava
  SPU2 (áudio), CDVD (leitor), e PAD (controle). Para rodar jogos originais, precisamos
  emular isso. Para jogos portados, não. Prioridade: BAIXA (framework-first approach).

### Fase 2 — Vulkan Renderer [██████░░░░] ~60% COMPLETA

```
[x] Renderer init (instance, device, swapchain, render pass)
[x] Per-frame resources (command pools, semaphores, fences)
[x] GS emulation (blend factors, compare ops, topology mapping)
[x] Pipeline cache (hash table, LRU eviction, disk persistence)
[x] Frame graph (render pass ordering, barriers, transient attachments)
[x] Texture manager (PS2 format decode, deswizzle, CLUT, streaming)
[x] Shaders (vertex: basic/skinned/particle/depth, fragment: basic/texture/alpha/depth)
[x] Compute shaders (physics, skinning, particles, tile cull)
[ ] Swapchain creation (platform-specific: Wayland/XCB/Android)
[ ] Depth buffer + transient attachments (TBDR optimization)
[ ] Descriptor set allocation pool
[ ] Vertex/index buffer management (staging, device-local)
[ ] Indirect draw (GPU-driven culling output)
[ ] Multi-subpass deferred rendering
[ ] Texture compression (ASTC 4×4 encode)
[ ] Mipmap generation (compute shader)
[ ] Shader hot-reload (development mode)
```

**O que falta na Fase 2 e POR QUE:**

- **Swapchain**: Depende da plataforma. Wayland para Linux desktop,
  XCB como fallback, ANativeWindow para Android. É código chato mas
  não complexo. Prioridade: ALTA (sem isso não desenha nada).

- **Descriptor sets**: O pipeline cache cria layouts, mas falta o pool
  de descriptors e a lógica de bind por draw call. Prioridade: ALTA.

- **Vertex buffers**: Falta o gerenciamento de staging→device-local copy
  e a lógica de sub-allocation dentro de buffers grandes. Prioridade: ALTA.

- **Indirect draw**: Para GPU-driven culling (tile_cull.comp output →
  vkCmdDrawIndexedIndirect). Prioridade: MÉDIA (otimização, não bloqueante).

- **ASTC encode**: Compressão de texturas em tempo de carga. Prioridade: MÉDIA.

### Fase 3 — Game Integration [██░░░░░░░░] ~20% COMPLETA

```
[x] Frame scheduler (triple buffer, fixed timestep, cascade)
[x] Task builder (dependency graph construction per frame)
[x] Audio mixer (NEON, 48 voices, ADSR)
[x] Audio output (ALSA/AAudio, dedicated thread, ring buffer)
[x] Input system (pad state, evdev-ready)
[ ] Scene graph (entity system, transform hierarchy)
[ ] Animation system (keyframe, skeletal, blend trees)
[ ] Collision detection (AABB, sphere, spatial hash)
[ ] Scripting bridge (Lua/WASM for game logic)
[ ] PS2 ISO loader (parse EE/IOP sections, extract assets)
[ ] PS2 memory card emulation
[ ] Save state system
[ ] Debug overlay (frame time, task timeline, draw call stats)
```

**O que falta na Fase 3 e POR QUE:**

- **Scene graph**: Necessário para qualquer jogo real. Prioridade: ALTA.

- **Animation system**: Essencial para jogos 3D. Prioridade: ALTA.

- **PS2 ISO loader**: Para rodar jogos originais. Prioridade: MÉDIA
  (framework approach: jogos são portados, não emulados).

- **Scripting bridge**: Para que designers possam criar lógica de jogo
  sem recompilar. Prioridade: MÉDIA.

### Fase 4 — Otimização e Polimento [░░░░░░░░░░] 0%

```
[ ] Profiling integration (perfetto, ARM Streamline)
[ ] CPU cache optimization (prefetch tuning, data layout)
[ ] GPU bandwidth optimization (AFBC, compression)
[ ] Memory budget tracking (per-subsystem reporting)
[ ] Frame pacing (adaptive vsync, tear-free)
[ ] Resolution scaling (dynamic, based on GPU load)
[ ] LOD system (automatic mesh simplification)
[ ] Occlusion culling (software or GPU-based)
[ ] Texture streaming (distance-based quality)
[ ] Audio spatialization (3D positional audio)
[ ] Battery-aware throttling (mobile)
```

**Nenhuma dessas é bloqueante.** São otimizações que fazem a diferença
entre "roda" e "roda perfeitamente".

---

## Estado Atual — O que está feito

### 52 arquivos implementados

```
ps2re/
├── CMakeLists.txt                    ✅ Completo (Linux/Android/x86_64)
├── build.sh                          ✅
├── include/ps2re/
│   ├── types.h                       ✅ Tipos, atomics, prefetch, align
│   ├── config.h                      ✅ Todas as constantes tunáveis
│   ├── platform.h                    ✅ Barrier emulation, CPU affinity
│   ├── async/
│   │   ├── ring_buffer.h             ✅ SPSC + MPSC lock-free
│   │   ├── task_graph.h              ✅ DAG com dependency cascade
│   │   ├── work_queue.h              ✅ Chase-Lev work-stealing
│   │   └── fence.h                   ✅ CPU fence com mutex+condvar
│   ├── memory/
│   │   ├── arena.h                   ✅ Bump allocator por frame
│   │   ├── pool.h                    ✅ Fixed-size free list
│   │   └── unified_mem.h             ✅ Vulkan sub-allocator
│   ├── math/
│   │   ├── vec4_neon.h               ✅ 4-wide float, dot, cross, normalize
│   │   └── mat4_neon.h               ✅ 4×4 matrix, batch SoA transform
│   ├── ps2/
│   │   ├── ee.h                      ✅ Game logic context
│   │   ├── vu0_pipeline.h            ✅ Skinning, particles, physics
│   │   ├── vu1_pipeline.h            ✅ Vertex transform, clip, viewport
│   │   ├── gs_state.h                ✅ Register decode + pipeline hash
│   │   ├── gif_channel.h             ✅ GIF tag parsing + vertex accumulation
│   │   ├── dma_engine.h              ✅ Streaming memcpy com prefetch
│   │   └── vif_unpack.h              ✅ V4-32/16/8, V2-32 unpack
│   ├── render/
│   │   ├── renderer.h                ✅ Vulkan core (init, frame, submit)
│   │   ├── pipeline_cache.h          ✅ Hash table + LRU + disk persistence
│   │   ├── frame_graph.h             ✅ Render pass ordering + barriers
│   │   ├── texture_manager.h         ✅ PS2 decode, deswizzle, CLUT, streaming
│   │   └── gs_emulation.h            ✅ GS state → Vulkan state mapping
│   └── sched/
│       ├── frame_scheduler.h         ✅ Triple buffer, fixed timestep
│       └── task_builder.h            ✅ Dependency graph construction
│
├── src/  (todos os .c correspondentes) ✅
│
├── shaders/
│   ├── vertex/
│   │   ├── gs_passthrough.vert       ✅ Basic pos+color+uv+fog
│   │   ├── gs_skinned.vert           ✅ Bone matrix blend + transform
│   │   ├── gs_particle.vert          ✅ Billboard quad
│   │   └── depth_only.vert           ✅ Depth pre-pass
│   ├── fragment/
│   │   ├── gs_basic.frag             ✅ Vertex color + fog
│   │   ├── gs_texture.frag           ✅ Texture × color modulation (tfx)
│   │   ├── gs_alpha.frag             ✅ Alpha test (GS TEST register)
│   │   └── depth_only.frag           ✅ Empty (depth written by rasterizer)
│   ├── compute/
│   │   ├── vu0_physics.comp          ✅ GPU physics (async compute)
│   │   ├── vu0_skinning.comp         ✅ GPU skinning (async compute)
│   │   ├── vu0_particles.comp        ✅ GPU particles (async compute)
│   │   └── tile_cull.comp            ✅ Frustum culling (compute)
│   └── compile_all.sh                ✅
│
├── tools/
│   ├── texconv/main.c                ✅ PS2 texture format converter
│   └── modelconv/main.c              ✅ PS2 model converter + normals
│
├── test/
│   ├── test_task_graph.c             ✅ (basic, deps, diamond)
│   ├── test_ring_buffer.c            ✅ (24 tests: SPSC, MPSC, stress, perf)
│   ├── test_arena.c                  ✅ (basic, reset, overflow, alignment)
│   └── test_neon_math.c              ✅ (mat mul, dot, cross, clip)
│
└── assets/
└── config.json                   ✅ Runtime configuration
```

### Linhas de código (estimativa)

```
include/    ~2,500 lines  (headers com documentação)
src/        ~4,000 lines  (implementação)
shaders/      ~500 lines  (GLSL)
tools/        ~400 lines  (conversores)
test/         ~800 lines  (testes)
CMakeLists    ~250 lines  (build system)
────────────────────────
Total:      ~8,450 lines
```

---

## Estado Atual — O que falta

Priorizado por impacto (o que bloqueia mais coisas):

### P0 — Sem isso não roda (bloqueante)

| Item | Arquivo(s) | Esforço | Impacto |
|------|-----------|---------|---------|
| Swapchain creation | `renderer.c` | 2-3 dias | Sem tela |
| Descriptor set pool | `renderer.c` | 1-2 dias | Sem bind de resources |
| Vertex buffer management | NOVO: `render/buffer_manager.h/c` | 2-3 dias | Sem geometria |
| Framebuffer + depth image | `renderer.c`, `frame_graph.c` | 1-2 dias | Sem render pass funcional |
| Draw call recording | `renderer.c`, `gs_emulation.c` | 3-4 dias | Sem draw calls |

**Total estimado para P0: ~2 semanas de trabalho focado.**

### P1 — Sem isso é limitado

| Item | Arquivo(s) | Esforço | Impacto |
|------|-----------|---------|---------|
| Scene graph | NOVO: `scene/` | 1 semana | Sem hierarquia de objetos |
| Model loading (.psm format) | `tools/modelconv` + loader | 3-4 dias | Sem carregar modelos |
| Texture loading (.astc/raw) | `texture_manager.c` | 2-3 dias | Sem texturas |
| Basic lighting (per-vertex) | NOVO shader | 2 dias | Visual muito básico |
| Camera system | NOVO: `scene/camera.h` | 1-2 dias | Sem controle de câmera |

### P2 — Sem isso não é um jogo

| Item | Esforço |
|------|---------|
| Animation system (skeletal) | 1 semana |
| Collision detection | 1 semana |
| Audio 3D positioning | 3-4 dias |
| PS2 ISO loader | 2 semanas |
| Scripting (Lua bridge) | 1 semana |

### P3 — Polish

| Item | Esforço |
|------|---------|
| Debug overlay + profiling | 1 semana |
| Dynamic resolution scaling | 3-4 dias |
| Texture streaming | 1 semana |
| Battery optimization | 2-3 dias |
| Save state system | 3-4 dias |

---

## Performance Targets

### CPU Budget (por frame a 60fps = 16.67ms)

```
Componente              Budget    Método
──────────────────────────────────────────────────
Game logic (EE)         2.0ms     Main thread or worker
Physics (VU0)           1.0ms     NEON batch (4-wide)
Particles (VU0)         0.5ms     NEON batch (4-wide)
Animation/skinning      1.5ms     NEON batch or GPU compute
Vertex transform (VU1)  1.0ms     NEON SoA batch
Audio mixing            0.5ms     NEON, dedicated LITTLE core thread
Task graph overhead     0.2ms     Lock-free, ~100ns per task complete
Frame build + submit    0.5ms     Vulkan command recording
──────────────────────────────────────────────────
Total CPU               ~7.2ms    Leaves ~9.5ms headroom
```

### GPU Budget (por frame a 60fps)

```
Componente              Budget    TBDR Optimization
──────────────────────────────────────────────────
Depth pre-pass          0.5ms     Fills HiZ (enables early-Z)
Shadow map              1.0ms     Low-res, transient attachment
Main opaque             4.0ms     TBDR HSR = zero overdraw
Transparent             1.5ms     Per-tile sorted (TBDR native)
Post-process            1.0ms     Compute shader (async queue)
UI overlay              0.3ms     Simple overlay pass
──────────────────────────────────────────────────
Total GPU               ~8.3ms    Leaves ~8.3ms headroom
```

### Memory Budget

```
Subsystem               Budget    Notes
──────────────────────────────────────────────────
Frame arenas (×3)       48MB      16MB per frame slot, bump alloc
Vertex buffers          32MB      Staging + device-local
Texture cache           64MB      Streaming, ASTC compressed
Pipeline cache          4MB       Vulkan binary cache on disk
Audio buffers           2MB       Ring buffer + voice data
Task graph + pools      4MB       Per-frame task closures
Ring buffers            1MB       SPSC + MPSC command rings
──────────────────────────────────────────────────
Total                   ~155MB    Fits in 256MB with headroom
```

### Medição

```bash
# Build com profiling
cmake -B build/release -DCMAKE_BUILD_TYPE=RelWithDebInfo .

# Run com perf (Linux ARM64)
perf record -g ./build/release/ps2re
perf report

# Run com ARM Streamline (se disponível)
streamline --capture ./build/release/ps2re

# Vulkan validation layers
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./build/release/ps2re

# Memory tracking (debug build)
ASAN_OPTIONS=detect_leaks=1 ./build/debug/ps2re
```

---

## Build

### Pré-requisitos

```bash
# Linux ARM64 (Raspberry Pi 5, Ampere, Apple Silicon via Asahi)
sudo apt install build-essential cmake pkg-config libvulkan-dev libasound2-dev

# Android NDK
# Download: https://developer.android.com/ndk/downloads
export ANDROID_NDK=/path/to/ndk
export ANDROID_ABI=arm64-v8a

# Vulkan SDK (para glslc — shader compiler)
# Option 1: Package manager
sudo apt install glslang-tools
# Option 2: LunarG SDK
wget -qO- https://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo tee /etc/apt/trusted.gpg.d/lunarg.asc
sudo apt install vulkan-sdk
```

### Linux ARM64 (native)

```bash
# Debug (com sanitizers)
./build.sh Debug
# ou manualmente:
cmake -B build/debug -DCMAKE_BUILD_TYPE=Debug .
cmake --build build/debug --parallel $(nproc)

# Release (full optimization)
./build.sh Release
# ou:
cmake -B build/release -DCMAKE_BUILD_TYPE=Release .
cmake --build build/release --parallel $(nproc)

# Run tests
cd build/debug && ctest --output-on-failure

# Run specific test
./build/debug/test_ring_buffer
./build/debug/test_task_graph
./build/debug/test_arena
./build/debug/test_neon_math

# Run main
./build/release/ps2re
```

### Android (cross-compilation)

```bash
cmake -B build/android \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-28 \
    -DCMAKE_BUILD_TYPE=Release \
    .

cmake --build build/android --parallel $(nproc)

# Deploy
adb push build/android/ps2re /data/local/tmp/
adb shell "cd /data/local/tmp && ./ps2re"
```

### Linux x86_64 (development — no NEON hardware)

```bash
# Compila mas NEON intrinsics são stubs (no-op ou scalar fallback)
# Útil para CI, debugging de lógica, e testes que não dependem de SIMD
cmake -B build/x86dev -DCMAKE_BUILD_TYPE=Debug .
cmake --build build/x86dev --parallel $(nproc)
```

### Build targets

```bash
# Tudo
cmake --build build/release

# Apenas shaders
cmake --build build/release --target shaders

# Apenas testes
cmake --build build/release --target test_ring_buffer

# Run all tests
cmake --build build/release --target check

# Run benchmarks
cmake --build build/release --target bench

# Install
cmake --install build/release --prefix /usr/local
```

---

## Plataformas

| Plataforma | Status | Notes |
|-----------|--------|-------|
| Linux ARM64 (native) | ✅ Primary | Raspberry Pi 5, Ampere Altra, Rockchip RK3588 |
| Linux ARM64 (Asahi) | ✅ Should work | Apple Silicon Mac via Asahi Linux |
| Android ARM64 | ✅ Supported | NDK build, AAudio for audio |
| Linux x86_64 | ⚠️ Dev only | Compiles, NEON stubs, no display |
| macOS ARM64 | ❌ Not yet | Needs Metal renderer (future) |
| Windows | ❌ Not yet | Needs Win32 surface + MSVC compat |

---

## Estrutura do Projeto

```
ps2re/
│
├── CMakeLists.txt              Build system (Linux/Android/x86_64)
├── build.sh                    Convenience build script
├── README.md                   Este arquivo
│
├── include/ps2re/              Headers públicos
│   ├── types.h                 Tipos fundamentais (u8-u64, f32, Result)
│   ├── config.h                Constantes tunáveis (frame count, buffer sizes)
│   ├── platform.h              Cross-platform abstraction (barriers, affinity)
│   │
│   ├── async/                  Sistema assíncrono
│   │   ├── ring_buffer.h       Lock-free SPSC + MPSC ring buffers
│   │   ├── task_graph.h        DAG task scheduler com dependency cascade
│   │   ├── work_queue.h        Chase-Lev work-stealing deques
│   │   └── fence.h             CPU fence (mutex+condvar, timeline value)
│   │
│   ├── memory/                 Gerenciamento de memória
│   │   ├── arena.h             Bump/linear allocator (per-frame)
│   │   ├── pool.h              Fixed-size object pool (O(1) alloc/free)
│   │   └── unified_mem.h       Vulkan memory sub-allocator
│   │
│   ├── math/                   Matemática NEON
│   │   ├── vec4_neon.h         4-wide float: add, mul, FMA, dot, cross, normalize
│   │   └── mat4_neon.h         4×4 matrix: mul, MVP, perspective, batch SoA
│   │
│   ├── ps2/                    Subsistemas PS2 re-escritos
│   │   ├── ee.h                Emotion Engine: game logic context
│   │   ├── vu0_pipeline.h      VU0: skinning, particles, physics (NEON)
│   │   ├── vu1_pipeline.h      VU1: vertex transform, clip, viewport (NEON batch)
│   │   ├── gs_state.h          GS: register state machine + pipeline hash
│   │   ├── gif_channel.h       GIF: tag parsing + vertex accumulation
│   │   ├── dma_engine.h        DMA: streaming memcpy com prefetch
│   │   └── vif_unpack.h        VIF: format decode (V4-32/16/8, V2-32)
│   │
│   ├── render/                 Renderização Vulkan
│   │   ├── renderer.h          Vulkan core: instance, device, swapchain, frames
│   │   ├── pipeline_cache.h    GS state → VkPipeline hash table (LRU + disk)
│   │   ├── frame_graph.h       Render pass ordering + barriers + aliasing
│   │   ├── texture_manager.h   PS2 texture decode, CLUT, streaming, LRU eviction
│   │   └── gs_emulation.h      GS blend/test/fog → Vulkan state mapping
│   │
│   └── sched/                  Scheduling
│       ├── frame_scheduler.h   Triple-buffered frame pipeline orchestrator
│       └── task_builder.h      Per-frame task DAG construction
│
├── src/                        Implementações (.c)
│   ├── async/                  (1:1 com headers)
│   ├── memory/
│   ├── math/
│   ├── ps2/
│   ├── render/
│   ├── sched/
│   ├── audio/
│   │   ├── mixer.c             Software mixer: 48 voices, NEON float→s16
│   │   └── output.c            Platform output: ALSA / AAudio / stub
│   ├── input/
│   │   └── pad.c               Controller input: state, pressure, analog
│   └── main.c                  Entry point: init → loop → cleanup
│
├── shaders/                    Vulkan GLSL shaders
│   ├── vertex/                 4 shaders (basic, skinned, particle, depth)
│   ├── fragment/               4 shaders (basic, texture, alpha, depth)
│   ├── compute/                4 shaders (physics, skinning, particles, cull)
│   └── compile_all.sh          Batch SPIR-V compilation
│
├── tools/                      Ferramentas offline
│   ├── texconv/main.c          PS2 texture → ASTC/raw (deswizzle + decode)
│   └── modelconv/main.c        Raw model → .psm (normals + bbox)
│
├── test/                       Testes unitários
│   ├── test_task_graph.c       3 tests (basic, deps, diamond)
│   ├── test_ring_buffer.c      24 tests (SPSC, MPSC, stress, perf, edge)
│   ├── test_arena.c            4 tests (basic, reset, overflow, alignment)
│   └── test_neon_math.c        4 tests (matmul, dot, cross, clip)
│
└── assets/
    └── config.json             Runtime config (display, render, audio, ps2)
```

### Adicionando um novo subsistema

```bash
# 1. Crie o header
touch include/ps2re/new_system/foo.h

# 2. Crie a implementação
touch src/new_system/foo.c

# 3. Adicione ao CMakeLists.txt
# No bloco PS2RE_CORE (ou PS2RE_RENDER, etc):
#   src/new_system/foo.c

# 4. Crie testes
touch test/test_foo.c
# Adicione ao PS2RE_TESTS no CMakeLists.txt

# 5. Implemente usando o task graph para async
#    Use arena_alloc para per-frame data
#    Use pool_alloc para reusable objects
#    Use ring buffers para cross-thread communication
```

---

## Decisões de Arquitetura

### Por que C11 e não C++?

1. **Controle total sobre layout de memória.** Sem vtables, sem padding
   surpresa, sem hidden allocations. Cada byte está onde você quer.

2. **Atomics explícitos.** C11 `<stdatomic.h>` é mais previsível que
   `std::atomic<>` do C++. Você vê exatamente qual memory order está
   sendo usado.

3. **Performance de compilação.** Headers de C compilam muito mais rápido
   que templates de C++. Em um projeto com 50+ headers, isso importa.

4. **Portabilidade.** C é suportado por qualquer toolchain. NDK, GCC,
   Clang, até ICC. Sem worries sobre ABI de C++.

5. **Embedde-friendly.** Se um dia portar para bare-metal ARM64 (sem OS),
   C funciona com zero runtime.

### Por que arena allocator e não malloc?

```
malloc/free:
  - ~100-200ns per call (lock contention, fragmentation)
  - Thread safety via locks (global heap lock em glibc < 2.26)
  - Fragmentation cresce ao longo do tempo

Arena allocator:
  - ~1-2ns per allocation (single pointer bump)
  - Thread safety: arena por frame = zero contention
  - Fragmentation: impossível por design (bump only)
  - Reset: ~1ns (pointer reset to start)
```

No PS2, não existia malloc. Tudo era estático. O arena reproduz isso
mas com tamanho dinâmico. É o ponto intermediário perfeito:
flexibilidade de malloc com velocidade de alocação estática.

### Por que Chase-Lev deques para work-stealing?

```
Alternativas consideradas:
  - Global queue com lock:       ~500ns per push/pop (lock contention)
  - Lock-free MPMC queue:        ~100ns per op (CAS retry storms)
  - Per-thread LIFO stack:       ~10ns per pop, mas sem stealing
  - Chase-Lev deque:             ~5-10ns per pop (owner), ~30ns steal

Chase-Lev wins:
  - Owner push/pop: ~5-10ns (lock-free, no contention)
  - Steal: ~30ns (CAS, but rare — only when own deque empty)
  - Cache-friendly: owner processes LIFO (hot data in cache)
  - Load-balancing: stealer takes FIFO (cold data, different cache line)
```

### Por que Vulkan e não OpenGL ES?

1. **TBDR explicit control.** Vulkan lets you specify load/store ops,
   subpass dependencies, and transient attachments. This is how you
   tell the GPU "keep this in tile memory, don't write back".

2. **Async compute.** Separate queues for graphics and compute.
   Physics on compute queue while graphics renders previous frame.

3. **Pipeline cache.** Vulkan's binary pipeline cache persists across runs.
   First boot: slow. Subsequent boots: instant.

4. **Descriptor sets.** Batch resource binding. No per-draw-call overhead.

5. **ARM64 native.** Vulkan is the primary graphics API on ARM64 SoCs.
   Mali, Adreno, Apple (via MoltenVK) all have mature Vulkan drivers.

### Por que GLSL e não SPIR-V direto?

Para legibilidade e manutenção. Os shaders são compilados para SPIR-V
via `glslc` durante o build (CMake custom command). Em produção, o
pipeline cache Vulkan armazena os binários compilados em disco.

Para desenvolvimento, GLSL é muito mais fácil de debugar e iterar.
O `glslc -O` gera SPIR-V otimizado automaticamente.

---

## Debugging e Profiling

### Vulkan Validation Layers

```bash
# Instalação
sudo apt install vulkan-tools lunarg-vulkan-sdk

# Run com validation
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \
VK_LAYER_PATH=/usr/share/vulkan/explicit_layer.d \
./build/debug/ps2re
```

### Task Graph Visualization

O task graph loga a timeline de execução em debug builds:

```
Worker 0: [ee_logic: 0.3ms] ──── [vu1_transform: 0.8ms] ──── [build_draw: 0.2ms]
Worker 1: [physics: 0.4ms] ─────────── [skinning: 0.6ms]
Worker 2: [particles: 0.2ms] ────────────────── [audio: 0.1ms]
Worker 3: IDLE ░░░░░░░░░░░░░░░░░░░░░ [stolen work]
```

Para habilitar:

```c
// Em config.h:
#define PS2RE_TASK_GRAPH_LOG 1

// Output: build/debug/task_timeline.json (formato Chrome Trace)
// Abrir em chrome://tracing
```

### Memory Tracking

```bash
# Address Sanitizer (catches buffer overflows, use-after-free)
cmake -B build/asan -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_FLAGS="-fsanitize=address,undefined"
./build/asan/ps2re

# Leak detection
ASAN_OPTIONS=detect_leaks=1:leak_check_at_exit=1 ./build/asan/ps2re

# Arena stats (debug overlay)
# Mostra: total allocated, pages, utilization %
```

### GPU Profiling

```bash
# RenderDoc (frame capture)
renderdoccmd capture ./build/release/ps2re

# ARM Mali Offline Compiler (shader analysis)
malioc shaders/spirv/gs_texture.frag.spv

# Vulkan timestamps
# Em debug builds, vkCmdWriteTimestamp em cada render pass
# Resultados no debug overlay
```

### Testes

```bash
# Todos
cd build/debug && ctest --output-on-failure -j$(nproc)

# Específico
./build/debug/test_ring_buffer
./build/debug/test_task_graph
./build/debug/test_arena
./build/debug/test_neon_math

# Com verbose output
ctest --output-on-failure --verbose

# Com timeout
ctest --timeout 10

# Stress tests (demoram mais)
./build/debug/test_ring_buffer  # inclui 1M item stress test
```

---

## Contribuição

### Branch strategy

```
main           ← stable, always buildable
├── develop    ← integration branch
│   ├── feat/swapchain       ← new feature
│   ├── fix/arena-overflow   ← bug fix
│   ├── opt/neon-prefetch    ← optimization
│   └── test/stress-particles ← test addition
```

### Code style

```c
// 4 spaces indent (no tabs)
// snake_case para funções e variáveis
// PascalCase para tipos e structs
// UPPER_CASE para macros e defines
// Bloco de comentário por seção com ── borders
// Cada função documentada no header, não no .c
// No nested ternary operators
// No more than 3 levels of nesting

// Exemplo:
static Result process_batch(BatchContext* ctx)
{
    if (!ctx || ctx->count == 0) return ERR_INVALID;

    for (int i = 0; i < ctx->count; i++) {
        PREFETCH_R(ctx->data + (i + 4) * ctx->stride);
        process_single(ctx, i);
    }
    return OK;
}
```

### Commits

```
feat(swapchain): add Wayland surface creation
fix(arena): prevent overflow on page boundary
opt(neon): prefetch skinning input data 4 iterations ahead
test(ring_buffer): add MPSC multi-producer contention test
docs(readme): add Android build instructions
refactor(renderer): extract swapchain into separate module
```

### Pull request checklist

```
[ ] Compila sem warnings em Release (gcc -Wall -Wextra -Werror)
[ ] Todos os testes passam
[ ] Não introduz malloc no hot path (usar arena/pool)
[ ] Não introduz locks (usar lock-free structures)
[ ] Atomics com memory order explícito (não usar __atomic builtins)
[ ] Prefetch em loops quentes (>1000 iterations)
[ ] Alinhamento de dados para cache line (ALIGN_CACHE)
[ ] Documentação no header (não apenas no .c)
```

---

## Roadmap de Features por Fase

### Sprint 1 — Primeiro pixel na tela

```
Meta: Renderizar um triângulo colorido no screen via Vulkan.
Tempo estimado: 1 semana

Tasks:
  [ ] Swapchain creation (platform-specific: Wayland/XCB/Android)
  [ ] Depth image creation
  [ ] Framebuffer creation
  [ ] Descriptor pool + descriptor sets
  [ ] Vertex buffer: staging → device-local
  [ ] Record: bind pipeline → bind vertex buffer → draw(3)
  [ ] Submit + present

Critério de sucesso:
  Janela preta com triângulo colorido, rodando a 60fps.
```

### Sprint 2 — Primeiro modelo 3D

```
Meta: Carregar e renderizar um modelo .psm com textura.
Tempo estimado: 1 semana

Tasks:
  [ ] Model loader (parse .psm header + vertex/index data)
  [ ] Index buffer creation
  [ ] Texture loading (RGBA8 → Vulkan image)
  [ ] Uniform buffer (MVP matrix, fog params)
  [ ] GS state → pipeline lookup + bind
  [ ] Camera system (keyboard/mouse or gamepad)

Critério de sucesso:
  Modelo 3D texturizado com fog, rotacionável com input.
```

### Sprint 3 — Task graph integration

```
Meta: Pipeline de renderização usando task graph.
Tempo estimado: 1-2 semanas

Tasks:
  [ ] Connect frame_scheduler to renderer
  [ ] Task: build_draw_commands → outputs to Vulkan command buffer
  [ ] Task: vu0_physics_step → updates entity positions
  [ ] Task: vu0_skin_vertices → skeletal animation
  [ ] Task: vu1_transform_vertices → MVP + clip + viewport
  [ ] Frame fence → CPU-GPU sync
  [ ] Verify: 60fps with physics + animation + rendering

Critério de sucesso:
  Multiple animated models, physics running, 60fps locked.
```

### Sprint 4 — PS2 asset pipeline

```
Meta: Carregar assets de jogos PS2 reais.
Tempo estimado: 2 semanas

Tasks:
  [ ] texconv: decode all PS2 PSM formats (32/24/16/8/4-bit)
  [ ] texconv: ASTC 4×4 encode
  [ ] texconv: Mipmap generation
  [ ] modelconv: batch conversion (directory → .psm)
  [ ] texture_manager: full pipeline (decode → compress → upload → cache)
  [ ] CLUT support in shaders
  [ ] Test with real PS2 game assets (textures + models)

Critério de sucesso:
  Textures and models from a real PS2 game rendered correctly.
```

### Sprint 5 — Visual fidelity

```
Meta: Matching PS2 visual quality, then surpassing it.
Tempo estimado: 2-3 semanas

Tasks:
  [ ] Per-pixel lighting (normal mapping)
  [ ] Shadow mapping (compute-shader culling → shadow pass)
  [ ] Post-processing: bloom, tone mapping, FXAA
  [ ] Multi-pass effects via frame graph (reflection, refraction)
  [ ] GS alpha test emulation (correct in all edge cases)
  [ ] GS fog emulation (linear, per-pixel)
  [ ] GS sprite rendering (billboard particles)
  [ ] Dynamic resolution scaling (GPU load based)
  [ ] Debug overlay: frame time, task timeline, draw calls

Critério de sucesso:
  Visual quality equivalent to best PS2 games (SotC, GoW2, GT4).
```

---

## Benchmarks

### Expected throughput (ARM Cortex-X3 class)

```
Operation                   Expected        Measurement Method
──────────────────────────────────────────────────────────────
NEON vec4 dot product       3.0 GHz / 2 =   test_neon_math
                            1.5 billion/s    (cycle counter)

NEON mat4 × vec4            4 FMA = 1 cyc    test_neon_math
                            3 billion/s

SoA batch transform         4 verts/cyc      test_neon_math
(4096 verts)                ~1M batches/s    (wall clock)

SPSC ring push+pop          ~5ns per op      test_ring_buffer
(single thread)             ~200M ops/s      (10M iteration benchmark)

MPSC ring push (CAS)        ~15ns per op     test_ring_buffer
(4 producers)               ~67M ops/s       (contention benchmark)

Task complete + cascade     ~50ns per task   test_task_graph
(incl atomic decrement)     ~20M tasks/s     (microbenchmark)

Arena alloc                 ~1ns per alloc   test_arena
(bump pointer)              ~1 billion/s     (100M alloc benchmark)

Arena reset (16MB)          ~1ns             test_arena
(pointer reset)             instant

Texture decode PS2→RGBA8    ~200MB/s         texconv tool
(PSMCT32 deswizzle)                         (10MB texture)
```

### Running benchmarks

```bash
cmake -B build/release -DCMAKE_BUILD_TYPE=Release .
cmake --build build/release --parallel $(nproc)

# Ring buffer throughput
./build/release/test_ring_buffer
# Expected output includes:
#   spsc: throughput benchmark (10M items, 4B) [200.0M ops/s, 0.050s] [PASS]
#   mpsc: throughput benchmark (1M items, single thread) [100.0M ops/s, 0.010s] [PASS]

# NEON math
./build/release/test_neon_math

# Task graph
./build/release/test_task_graph

# Arena
./build/release/test_arena
```

---

## Known Issues

### Clangd no Android Studio

O Android Studio usa clangd para analysis de C/C++. O projeto é
100% compatível após as correções de tipo (`_Atomic(int)` em vez
de `_Atomic(bool)`, includes explícitos de `<stdatomic.h>`, casts
explícitos para `void*` ↔ `u8*`).

Se clangd reclamar de algo, verifique:
1. `<stdatomic.h>` incluído?
2. `_Atomic(int)` e não `_Atomic(bool)`?
3. Casts explícitos para tipos Vulkan?
4. `struct` tag definido antes de usar como ponteiro opaco?

### NEON em x86_64

O projeto compila em x86_64 mas NEON intrinsics são stubs.
Para development em x86_64, as funções matemáticas usam fallback
scalar. Os testes de NEON math não verificam resultados exatos
em x86_64 (tolerância ampliada).

### Android Audio

AAudio pode não estar disponível em Android < 8.0 (API 26).
O build system detecta isso e usa stub silencioso.

### Vulkan em emuladores

O projeto não foi testado em emuladores Vulkan (SwiftShader, lavapipe).
Pode funcionar para debug mas performance será inaceitável para benchmarking.

---

## Referências Técnicas

### PS2 Hardware

- [PS2 Technical Reference (psx-spx)](https://problemkaputt.de/psx-spx.htm)
  A referência definitiva. Cada registrador, cada ciclo, cada edge case.

- [EE Users Manual (Sony)](https://archive.org/details/sony_ps2_tool)
  Documentação oficial da Sony para o Emotion Engine.

- [GS Users Manual (Sony)](https://archive.org/details/sony_ps2_tool)
  Documentação oficial do Graphics Synthesizer.

- [VU Users Manual (Sony)](https://archive.org/details/sony_ps2_tool)
  Microcódigo VU0/VU1, instruction set, pipeline hazards.

### ARM64 Architecture

- [ARM Architecture Reference Manual (ARM ARM)](https://developer.arm.com/documentation/ddi0487/latest)
  ISA reference completa. Seções relevantes: NEON (C7), Atomics (B2),
  Memory model (B3).

- [ARM NEON Programmer's Guide](https://developer.arm.com/documentation/den0018/latest)
  Guia prático de NEON intrinsics. SoA vs AoS, batch processing.

- [ARM Cortex-X3 TRM](https://developer.arm.com/documentation/102484/latest)
  Pipeline details, cache sizes, branch predictor behavior.

### Vulkan

- [Vulkan Specification 1.3](https://registry.khronos.org/vulkan/specs/1.3/html/)
  Referência oficial. Seções relevantes: Render Passes, Pipeline Cache,
  Synchronization.

- [Vulkan Guide (Khronos)](https://docs.vulkan.org/guide/latest/)
  Best practices, performance tips, platform-specific guidance.

- [ARM Best Practices for Mali GPU](https://developer.arm.com/documentation/102662/latest)
  TBDR optimization, bandwidth reduction, tile usage.

### Lock-Free Programming

- [Chase-Lev Deque paper (2005)](https://www.dre.vanderbilt.edu/~schmidt/PDF/work-stealing-dequeue.pdf)
  O paper original do deque usado no work-stealing scheduler.

- [Herlihy & Shavit: "The Art of Multiprocessor Programming"](https://booksite.elsevier.com/9780123705914/)
  Referência acadêmica para lock-free data structures.

- [Jeff Preshing's blog](https://preshing.com/)
  Tutoriais práticos de memory ordering, lock-free queues, atomics.

### Tile-Based Deferred Rendering

- [A look at the PowerVR Graphics Architecture](https://blog.imgtec.com/powervr/a-look-at-the-powervr-graphics-architecture-tile-based-rendering)
  Conceitos de TBDR aplicáveis a Mali e Adreno.

- [Bandwidth in Tile-Based Rendering (ARM)](https://community.arm.com/arm-community-blogs/b/graphics-gaming-and-vr-blog/posts/the-mali-gpu-an-abstract-machine-part-3-the-tiler)
  Como minimizar bandwidth em GPUs ARM.

---

## Licença

MIT

---

## Créditos

Desenvolvido com a filosofia de que hardware limitado produz engenharia
criativa. O PS2 é a prova disso — e ARM64 é o próximo capítulo.

"Não é sobre o hardware que você tem. É sobre como você o usa."
```

---

O README está completo. Ele serve como:

1. **Onboarding** para qualquer senior que entre no projeto
2. **Roadmap** com prioridades claras (P0-P3) e esforço estimado
3. **Referência técnica** com decisões de arquitetura justificadas
4. **Manual de build** para todas as plataformas
5. **Guia de debugging** com ferramentas específicas
6. **Checklist de contribuição** para manter qualidade
7. **Benchmarks** com valores esperados e como medir
8. **Sprint planning** com critérios de sucesso concretos
