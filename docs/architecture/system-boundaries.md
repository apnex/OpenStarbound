# The system's boundaries, and the evidence for each

**Scope:** the whole of OpenStarbound, not the render subsystem. `docs/render/` describes one tier of one
of the six parts named below; this document is the map that tier sits inside.

**Freshness basis:** nine of the ten diagrams are **generated** by `scripts/arch-graph.py --inject` and
gated by the `arch_graph_fresh` ctest and the `Gates` workflow. The tenth — the determination method —
is hand-authored, contains no measurable fact, and deliberately has no marker.

This follows the rule `docs/render/README.md` states and the render campaign learned the hard way:

> No document may state a current-state number that an instrument can measure.

A Mermaid edge labelled with an include count is such a number. So are node sizes, tier memberships,
binary compositions and singleton counts. Everything numeric below comes out of the tree.

---

## 1. How a boundary is determined

Four tests, in descending order of evidential strength. The order matters: a boundary that passes test 1
needs no argument, and a boundary that only passes test 4 is a proposal rather than a fact.

```mermaid
flowchart TD
  Q(["Is this a primary boundary<br/>of the system?"]) --> T1

  T1{"<b>Test 1 — Refusal</b><br/>Does the build refuse to let<br/>one side name the other?"}
  T2{"<b>Test 2 — Severability</b><br/>Does some artifact ship with<br/>one side and not the other?"}
  T3{"<b>Test 3 — Vocabulary</b><br/>Does either side have to know<br/>the other's concepts?"}
  T4{"<b>Test 4 — Duty</b><br/>Do the two sides answer<br/>to different duties?"}

  T1 -->|yes| E["<b>ENFORCED</b><br/>the compiler is the gate.<br/>No further argument needed."]
  T1 -->|no| T2
  T2 -->|yes| P["<b>PROVEN</b><br/>an artifact is the evidence.<br/>Real, but says nothing about<br/>boundaries <i>within</i> either side."]
  T2 -->|no| T3
  T3 -->|"no — neither<br/>names the other"| L["<b>LATENT</b><br/>already separate in practice.<br/>Cheap to make enforced."]
  T3 -->|yes| T4
  T4 -->|yes| A["<b>ASPIRATIONAL</b><br/>a duty boundary with no mechanism.<br/>Needs a hand-built instrument —<br/>or a directory of its own."]
  T4 -->|no| N["<b>NOT A BOUNDARY</b><br/>one thing you hoped was two."]

  E --> R1["ratchet: keep it"]
  P --> R2["ratchet: keep the artifact building"]
  L --> R3["<b>action:</b> revoke the unused grant"]
  A --> R4["<b>action:</b> give it a directory and a grant list.<br/>That converts a lint you maintain<br/>into a compile error you don't."]

  classDef verdict fill:#1b3a4b,stroke:#2c6e8f,color:#e0f2f9
  classDef action fill:#3a2d5c,stroke:#7b5ea7,color:#e8e0f5
  class E,P,L,A,N verdict
  class R1,R2,R3,R4 action
```

**Why this order.** Test 1 is authoritative because it is not an opinion: each directory's
`INCLUDE_DIRECTORIES` block names the layers it may see, and a `#include` outside that set is a hard
compile error. Test 2 is next because an artifact that builds is a fact about the world. Tests 3 and 4
are measurements of *pressure* rather than of structure — useful for deciding what to do next, useless
for settling whether a boundary exists.

The most important consequence sits between tests 1 and 4: **the compiler enforces boundaries between
directories and enforces nothing within one.** Every instrument in `scripts/` is a hand-built substitute
for include visibility, applied below directory granularity, in the two directories where the render
decomposition invented layers the build does not know about.

---

## 2. The system at the top — six parts, one of which is code

<!-- BEGIN GENERATED: scripts/arch-graph.py#taxonomy -->
```mermaid
mindmap
  root((OpenStarbound))
    Engine
      source/
      823 files
      220061 lines
      6 tiers
    Content
      assets/
      222 files in tree
      31 lua
      vanilla pak is external
    Protocol
      net + save
      175 files name it
      spans 6 directories
      no directory of its own
    Toolchain
      cmake/vcpkg
      36 build files
      12 declared binaries
    Instruments
      scripts/ + tests
      16 scripts
      11 ctest gates
      8 CI gates
    Governance
      docs/
      34 markdown documents
```

A mindmap because this genuinely is a tree: six independent children of one root with no cross-links. Five of the six are invisible to any tool that only reads `source/`.
<!-- END GENERATED: taxonomy -->

Only the first is what people mean by "the codebase". The other five are versioned separately, fail
separately, and are invisible to any tool that reads only `source/`. The **protocol** in particular has
no directory anywhere and no owner — see §8.

---

## 3. The grant lattice — what the compiler permits

This is test 1, drawn. Arrows are granted visibility, transitively reduced.

<!-- BEGIN GENERATED: scripts/arch-graph.py#lattice -->
```mermaid
flowchart TD
  subgraph T0["T0 vendored"]
    direction LR
    extern["extern<br/><small>18 files · 19,243 lines · Root×0</small>"]
  end
  subgraph T1["T1 language"]
    direction LR
    core["core<br/><small>214 files · 55,891 lines · Root×0</small>"]
  end
  subgraph T2["T2 services"]
    direction LR
    base["base<br/><small>27 files · 7,325 lines · Root×0</small>"]
    platform["platform<br/><small>4 files · 142 lines · Root×0</small>"]
    application["application<br/><small>25 files · 7,372 lines · Root×0</small>"]
  end
  subgraph T3["T3 simulation"]
    direction LR
    game["game<br/><small>338 files · 96,002 lines · Root×521</small>"]
  end
  subgraph T4["T4 presentation"]
    direction LR
    rendering["rendering<br/><small>23 files · 4,413 lines · Root×17</small>"]
    windowing["windowing<br/><small>61 files · 9,646 lines · Root×41</small>"]
    frontend["frontend<br/><small>102 files · 16,861 lines · Root×200</small>"]
  end
  subgraph T5["T5 shells"]
    direction LR
    client["client<br/><small>4 files · 2,383 lines · Root×2</small>"]
    server["server<br/><small>7 files · 783 lines · Root×4</small>"]
  end
  core --> extern
  base --> core
  platform --> core
  application --> platform
  game --> base
  game --> platform
  rendering --> application
  rendering --> game
  windowing --> rendering
  frontend --> windowing
  client --> frontend
  server --> game
  classDef clean fill:#1b4332,stroke:#2d6a4f,color:#d8f3dc
  classDef warm  fill:#5c4d1e,stroke:#8a7420,color:#fff3bf
  classDef hot   fill:#7f3e12,stroke:#b5561b,color:#ffe8d6
  classDef blaze fill:#7a1420,stroke:#c1121f,color:#ffe5e5
  class extern,core,base,platform,application clean
  class rendering,windowing,client,server warm
  class frontend hot
  class game blaze
```

Arrows point from consumer to provider and are **transitively reduced** -- `game` is granted `core` directly, but the edge is implied through `base` and drawing it adds no information. Node fill is `Root::singleton()` density: green is zero, red is over 250.
<!-- END GENERATED: lattice -->

Two things to read off it.

**The graph is acyclic, and that is not discipline.** A reciprocal pair would be uncompilable, so the
absence of cycles across the whole tree is a property of the build, not of anyone's restraint.

**`application` is not a presentation library.** It is granted `core` and `platform` and nothing else —
it cannot see `base`, cannot see `game`. It is a *sibling of `base`*, and the only reason it appears at
the top of every hand-drawn layer diagram in this repository is that nothing except the client needs a
window. The lattice has two incomparable branches at the services tier that only join at `rendering`;
that shape is invisible in a ladder drawing and it is the correct one.

---

## 4. Severability — what ships without what

Test 2. Each shell is a distinct set of object libraries linked by at least one declared executable.

<!-- BEGIN GENERATED: scripts/arch-graph.py#shells -->
```mermaid
flowchart TD
  subgraph S0["shell 0 — core + extern"]
    direction LR
    core_tests(["core_tests"])
    json_tool(["json_tool"])
  end
  subgraph S1["shell 1 — base + core + extern"]
    direction LR
    asset_packer(["asset_packer"])
    asset_unpacker(["asset_unpacker"])
    btree_repacker(["btree_repacker"])
    mod_uploader(["mod_uploader"])
    render_surface_tests(["render_surface_tests"])
  end
  S0 -.->|adds base| S1
  subgraph S2["shell 2 — base + core + extern + game"]
    direction LR
    dump_versioned_json(["dump_versioned_json"])
    game_tests(["game_tests"])
    make_versioned_json(["make_versioned_json"])
    starbound_server(["starbound_server"])
  end
  S1 -.->|adds game| S2
  subgraph S3["shell 3 — application + base + core + extern + frontend + game + rendering + windowing"]
    direction LR
    starbound(["starbound"])
  end
  S2 -.->|adds application, frontend, rendering, windowing| S3
```

12 declared executables fall into **4 distinct link sets**, and they are **strictly nested**. Each shell is what ships without everything below it: the existence of `starbound_server` is the proof that game↔presentation is a primary boundary, and nothing in this tree proves any boundary *within* presentation, because those four libraries appear together in exactly one binary.
<!-- END GENERATED: shells -->

`starbound_server` is the whole argument for game↔presentation being a primary boundary: the simulation
runs to completion with no render code linked at all. Note carefully what the shells do **not** prove.
The presentation libraries appear together in exactly one binary, so severability is silent on every
boundary inside them — which is precisely why the render decomposition had to build its own instruments.

`render_surface_tests` is the deliberate exception: it links the services shell and then splices two
`application` translation units in directly, so that L1 can be exercised without dragging in the
simulation. It is the only artifact in the tree that treats an intra-directory layer as a real boundary.

---

## 5. Granted versus spent

The sharpest diagram here, and the one to act on. Three edge states, three native Mermaid arrow weights.

<!-- BEGIN GENERATED: scripts/arch-graph.py#grantuse -->
```mermaid
flowchart LR
  base ==>|88 in 24| core
  platform -->|5 in 2| core
  application ==>|49 in 16| core
  application -->|8 in 2| platform
  game ==>|132 in 113| base
  game ==>|675 in 286| core
  game -->|3 in 3| platform
  rendering -->|9 in 9| application
  rendering -->|7 in 5| base
  rendering ==>|45 in 18| core
  rendering ==>|24 in 12| game
  rendering -.->|0| platform
  windowing -->|2 in 1| application
  windowing ==>|19 in 17| base
  windowing ==>|38 in 24| core
  windowing ==>|39 in 25| game
  windowing -.->|0| platform
  windowing -->|3 in 1| rendering
  frontend -->|4 in 4| application
  frontend ==>|54 in 45| base
  frontend ==>|96 in 54| core
  frontend ==>|226 in 75| game
  frontend -.->|0| platform
  frontend -->|9 in 9| rendering
  frontend ==>|217 in 67| windowing
  client -->|2 in 2| application
  client -->|2 in 1| base
  client -->|14 in 3| core
  client -->|11 in 2| frontend
  client -->|13 in 2| game
  client -.->|0| platform
  client -->|1 in 1| rendering
  client -->|1 in 1| windowing
  server -->|4 in 4| base
  server -->|25 in 7| core
  server -->|8 in 4| game
  server -.->|0| platform
```

Edge labels are `includes in files`. **Dotted** is a granted permission spent zero times -- 5 of them, free to revoke. **Solid** is thin: used in 9 files or fewer, a bounded cut. **Thick** is load-bearing. Counts: 5 unused, 19 thin, 13 load-bearing. Grants of `extern` are omitted -- all are unused, because `extern` is reached through `core`.

| edge | includes | files | state |
|:-----|---------:|------:|:------|
| `client → platform` | 0 | 0 | UNUSED -- revocable |
| `frontend → platform` | 0 | 0 | UNUSED -- revocable |
| `rendering → platform` | 0 | 0 | UNUSED -- revocable |
| `server → platform` | 0 | 0 | UNUSED -- revocable |
| `windowing → platform` | 0 | 0 | UNUSED -- revocable |
| `client → rendering` | 1 | 1 | thin |
| `client → windowing` | 1 | 1 | thin |
| `client → base` | 2 | 1 | thin |
| `windowing → application` | 2 | 1 | thin |
| `windowing → rendering` | 3 | 1 | thin |
| `client → application` | 2 | 2 | thin |
| `platform → core` | 5 | 2 | thin |
| `application → platform` | 8 | 2 | thin |
| `client → frontend` | 11 | 2 | thin |
| `client → game` | 13 | 2 | thin |
| `game → platform` | 3 | 3 | thin |
| `client → core` | 14 | 3 | thin |
| `frontend → application` | 4 | 4 | thin |
| `server → base` | 4 | 4 | thin |
| `server → game` | 8 | 4 | thin |
| `rendering → base` | 7 | 5 | thin |
| `server → core` | 25 | 7 | thin |
| `frontend → rendering` | 9 | 9 | thin |
| `rendering → application` | 9 | 9 | thin |
| `rendering → game` | 24 | 12 | load-bearing |
| `application → core` | 49 | 16 | load-bearing |
| `windowing → base` | 19 | 17 | load-bearing |
| `rendering → core` | 45 | 18 | load-bearing |
| `windowing → core` | 38 | 24 | load-bearing |
| `base → core` | 88 | 24 | load-bearing |
| `windowing → game` | 39 | 25 | load-bearing |
| `frontend → base` | 54 | 45 | load-bearing |
| `frontend → core` | 96 | 54 | load-bearing |
| `frontend → windowing` | 217 | 67 | load-bearing |
| `frontend → game` | 226 | 75 | load-bearing |
| `game → base` | 132 | 113 | load-bearing |
| `game → core` | 675 | 286 | load-bearing |
<!-- END GENERATED: grantuse -->

**Dotted edges are free money.** A granted permission spent zero times costs nothing to revoke and
ratchets the boundary in the one place the compiler will hold it. This is the cheapest enforcement
available anywhere in the system.

**Thin edges are the design questions.** Each is a boundary that is nearly severable already. The
threshold is on *files touched* rather than include count, because files predict the work of cutting an
edge and include counts do not.

---

## 6. Weighted coupling

The same edges, with magnitude instead of buckets.

<!-- BEGIN GENERATED: scripts/arch-graph.py#sankey -->
```mermaid
sankey-beta

game,core,675
frontend,game,226
frontend,windowing,217
game,base,132
frontend,core,96
base,core,88
frontend,base,54
application,core,49
rendering,core,45
windowing,game,39
windowing,core,38
server,core,25
rendering,game,24
windowing,base,19
client,core,14
client,game,13
client,frontend,11
rendering,application,9
frontend,rendering,9
application,platform,8
server,game,8
rendering,base,7
core,extern,6
platform,core,5
frontend,application,4
server,base,4
game,platform,3
windowing,rendering,3
windowing,application,2
client,base,2
client,application,2
client,rendering,1
client,windowing,1
```

**Magnitude only -- this is not a flow.** Sankey implies conservation and include counts do not conserve: `game → core` at 675 and `base → core` at 88 do not "arrive at" core in any meaningful sense. It is here because it is the only form that shows the dynamic range the three-state diagram above deliberately flattens.
<!-- END GENERATED: sankey -->

---

## 7. Mass, and the one directory that is a continent

<!-- BEGIN GENERATED: scripts/arch-graph.py#mass -->
```mermaid
treemap-beta
"source/"
    "T0 vendored"
        "extern": 19243
    "T1 language"
        "core": 55891
    "T2 services"
        "application": 7372
        "base": 7325
        "platform": 142
    "T3 simulation"
        "game": 96002
    "T4 presentation"
        "frontend": 16861
        "windowing": 9646
        "rendering": 4413
    "T5 shells"
        "client": 2383
        "server": 783
```

`treemap-beta` is a beta diagram type; the table is the drift-proof fallback and carries the same numbers.

| tier | directory | files | lines | share |
|:-----|:----------|------:|------:|------:|
| T0 vendored | `extern` | 18 | 19,243 | 8.7% |
| T1 language | `core` | 214 | 55,891 | 25.4% |
| T2 services | `base` | 27 | 7,325 | 3.3% |
| T2 services | `platform` | 4 | 142 | 0.1% |
| T2 services | `application` | 25 | 7,372 | 3.3% |
| T3 simulation | `game` | 338 | 96,002 | 43.6% |
| T4 presentation | `rendering` | 23 | 4,413 | 2.0% |
| T4 presentation | `windowing` | 61 | 9,646 | 4.4% |
| T4 presentation | `frontend` | 102 | 16,861 | 7.7% |
| T5 shells | `client` | 4 | 2,383 | 1.1% |
| T5 shells | `server` | 7 | 783 | 0.4% |

`game` is **44% of the engine in one directory** -- one grant list, no sub-`CMakeLists.txt`, and therefore no internal boundary the compiler can enforce.
<!-- END GENERATED: mass -->

`game` is the structural problem this document exists to name. It is a single directory with a single
grant list and no sub-`CMakeLists.txt`, which means **no boundary inside it is enforceable by test 1**.
Its natural clusters are legible in the filenames — entities and items, world simulation, universe and
networking — and nothing whatsoever holds them apart. It is the same situation as the render painters,
at roughly twenty times the scale.

---

## 8. Reach — where the god-object is visible

<!-- BEGIN GENERATED: scripts/arch-graph.py#reach -->
```mermaid
xychart-beta
    title "Root::singleton() reads per directory"
    x-axis [core, base, platform, application, game, rendering, windowing, frontend, client, server]
    y-axis "references" 0 --> 600
    bar [0, 0, 0, 0, 521, 17, 41, 200, 2, 4]
```

**Read the zeros carefully.** `Root` lives in `source/game`. `core`, `base`, `platform`, `application` read zero because they are not granted `game` and therefore *cannot see it* -- that is a consequence of the grant list, not a property anyone earned. The render campaign's L1 sovereignty is a different and narrower claim: an *internal* split of `source/application` that no compiler checks and `layering-lint.py` does.

| directory | files reading Root | references |
|:----------|-------------------:|-----------:|
| `game` | 111 | 521 |
| `frontend` | 42 | 200 |
| `windowing` | 17 | 41 |
| `rendering` | 4 | 17 |
| `server` | 3 | 4 |
| `client` | 1 | 2 |
| `core` | 0 | 0 |
| `base` | 0 | 0 |
| `platform` | 0 | 0 |
| `application` | 0 | 0 |
<!-- END GENERATED: reach -->

---

## 9. The subsystems with no directory

Tests 1, 2 and 3 are all directory-shaped and structurally cannot see a concern that spans directories.
These are found by reading duty, which is why the vocabulary is declared in the script rather than
discovered by it.

<!-- BEGIN GENERATED: scripts/arch-graph.py#crosscut -->
```mermaid
flowchart TB
  subgraph T0["T0 vendored"]
    direction LR
    extern
  end
  subgraph T1["T1 language"]
    direction LR
    core
  end
  subgraph T2["T2 services"]
    direction LR
    base
    platform
    application
  end
  subgraph T3["T3 simulation"]
    direction LR
    game
  end
  subgraph T4["T4 presentation"]
    direction LR
    rendering
    windowing
    frontend
  end
  subgraph T5["T5 shells"]
    direction LR
    client
    server
  end
  X0{{"Scripting"}}
  X0 -.->|45| game
  X0 -.->|14| frontend
  X0 -.->|4| core
  X0 -.->|4| windowing
  X0 -.->|3| client
  X0 -.->|2| base
  X1{{"Protocol"}}
  X1 -.->|136| game
  X1 -.->|31| core
  X1 -.->|3| server
  X1 -.->|2| base
  X1 -.->|2| frontend
  X2{{"Telemetry"}}
  X2 -.->|4| game
  X2 -.->|3| base
  X2 -.->|3| rendering
  X2 -.->|2| core
  X2 -.->|2| application
  X3{{"Assets"}}
  X3 -.->|37| game
  X3 -.->|6| base
  X3 -.->|6| rendering
  X3 -.->|4| windowing
  X3 -.->|2| core
  classDef cc fill:#3a2d5c,stroke:#7b5ea7,color:#e8e0f5
  class X0,X1,X2,X3 cc
```

Edge labels are files naming the concern. Single-file touches are elided (4 of them) to keep the picture legible.

| concern | files | directories | tiers | why it has no home |
|:--------|------:|------------:|------:|:-------------------|
| **Scripting** | 72 | 6 | 5 of 6 | Lua VM is 4 files in core; the binding surface is spread across six directories |
| **Protocol** | 175 | 6 | 5 of 6 | wire format and save format, versioned independently of the code that reads them |
| **Telemetry** | 15 | 6 | 5 of 6 | the measurement substrate the perf campaign runs on |
| **Assets** | 57 | 7 | 5 of 6 | loader in base, consumed everywhere, content lives outside the tree entirely |
<!-- END GENERATED: crosscut -->

Each of these is a real subsystem with a real interface and no home. The scripting surface is the
starkest: the Lua VM is a handful of files in `core`, and the bindings that define what mods can actually
*do* are scattered across the tier stack with no single place to read them.

---

## 10. The render subsystem, and the one edge that leaves it

The only place in this document where inheritance is the actual relationship, so the only place a
`classDiagram` is the right form. Layers are namespaces; the library each layer lives in is noted in
`docs/render/architecture-3-target-state.md`.

<!-- BEGIN GENERATED: scripts/arch-graph.py#renderclasses -->
```mermaid
classDiagram
  direction LR
  namespace L1_substrate {
    class GlGpuTimer
    class GlGroupedTexture
    class GlLoneTexture
    class GlRenderBuffer
    class GlRenderOracle
    class GlSurface
    class GlTexture
    class GlTextureAtlasSet
    class GlTextureGroup
    class OpenGlRenderer
    class RenderBuffer
    class Renderer
    class Texture
    class TextureGroup
  }
  namespace L2_primitives {
    class RetainedSurface
  }
  namespace L3_passes {
    class BackdropPass
    class GpuLightmapPass
    class WorldPass
  }
  namespace L3_orchestrator {
    class WorldPainter
  }
  namespace painters_pre_decomposition {
    class AssetTextureGroup
    class DrawablePainter
    class EnvironmentPainter
    class FontTextureGroup
    class TextPainter
    class TilePainter
  }
  namespace star_application {
    class GpuTimer
    class RenderOracle
    class TextureAtlasSet
  }
  namespace star_core {
    class RefCounter
  }
  namespace star_game {
    class TileDrawer
  }
  GpuTimer <|-- GlGpuTimer
  GlTexture <|-- GlGroupedTexture
  GlTexture <|-- GlLoneTexture
  RenderBuffer <|-- GlRenderBuffer
  RenderOracle <|-- GlRenderOracle
  RefCounter <|-- GlSurface
  Texture <|-- GlTexture
  TextureAtlasSet <|-- GlTextureAtlasSet
  TextureGroup <|-- GlTextureGroup
  Renderer <|-- OpenGlRenderer
  RefCounter <|-- Texture
  TileDrawer <|-- TilePainter
  AssetTextureGroup ..> Renderer
  BackdropPass ..> EnvironmentPainter
  BackdropPass ..> Renderer
  BackdropPass ..> RetainedSurface
  DrawablePainter ..> Renderer
  EnvironmentPainter ..> Renderer
  FontTextureGroup ..> Renderer
  GpuLightmapPass ..> Renderer
  TilePainter ..> Renderer
  WorldPainter ..> BackdropPass
  WorldPainter ..> DrawablePainter
  WorldPainter ..> EnvironmentPainter
  WorldPainter ..> GpuLightmapPass
  WorldPainter ..> Renderer
  WorldPainter ..> TextPainter
  WorldPainter ..> TilePainter
  WorldPainter ..> WorldPass
  WorldPass ..> DrawablePainter
  WorldPass ..> Renderer
  WorldPass ..> TextPainter
  WorldPass ..> TilePainter
```

`classDiagram` earns its place here and nowhere else in this document, because inheritance is the actual relationship rather than a metaphor for one. `<|--` is inheritance; `..>` is a compile-time dependency between render layers, drawn between each file's representative class.

**26 declared types are not drawn** because they participate in no edge -- vertex PODs, parameter structs and ring buffers. They are listed rather than dropped: L1 substrate: `Effect`, `EffectParameter`, `EffectTexture`, `Face`, `FrameSpanRing`, `GlEffects`, `GlPackedVertexData`, `GlPass`, `GlRenderVertex`, `GlTargets`, `GlVertexBuffer`, `GlVertexBufferTexture`, `RenderPoly`, `RenderQuad`, `RenderTriangle`, `RenderVertex`, `Ring`; L2 primitives: `ContentKey`; L3 passes: `BackdropParams`, `Input`, `LightmapParams`, `LightmapResult`; painters (pre-decomposition): `GlyphTexture`, `LiquidInfo`, `TextPositioning`, `TextureKeyHash`.

Every inheritance edge in the render tree, and where the base class lives:

| child | inherits | child's layer | base declared in | verdict |
|:------|:---------|:--------------|:-----------------|:--------|
| `GlGpuTimer` | `GpuTimer` | L1 substrate | `star_application` | internal |
| `GlGroupedTexture` | `GlTexture` | L1 substrate | `star_application` | internal |
| `GlLoneTexture` | `GlTexture` | L1 substrate | `star_application` | internal |
| `GlRenderBuffer` | `RenderBuffer` | L1 substrate | `star_application` | internal |
| `GlRenderOracle` | `RenderOracle` | L1 substrate | `star_application` | internal |
| `GlSurface` | `RefCounter` | L1 substrate | `star_core` | foundation base -- intended use |
| `GlTexture` | `Texture` | L1 substrate | `star_application` | internal |
| `GlTextureAtlasSet` | `TextureAtlasSet` | L1 substrate | `star_application` | internal |
| `GlTextureGroup` | `TextureGroup` | L1 substrate | `star_application` | internal |
| `OpenGlRenderer` | `Renderer` | L1 substrate | `star_application` | internal |
| `Texture` | `RefCounter` | L1 substrate | `star_core` | foundation base -- intended use |
| `TilePainter` | `TileDrawer` | painters (pre-decomposition) | `star_game` | **leaves the render subsystem for the simulation** |

Of 12 inheritance edges, 9 stay inside the render libraries, 2 take a base from a foundation library (`RefCounter` and friends -- that is what foundations are for), and **1 leaves the subsystem entirely**: `TilePainter : TileDrawer` (`star_game`). That last one is why `WorldPass` cannot finish its input DTO and why the client has no headless expression -- a render class whose base is a simulation class cannot be compiled without the simulation.
<!-- END GENERATED: renderclasses -->

---

## 11. What the measurement says to do

Ordered by cost, cheapest first. Numbers live in the generated blocks above; these are the judgements.

1. **Revoke every unused grant** (§5, dotted edges). Zero risk, zero behaviour change, and it moves the
   boundary from "nobody happens to use it" to "the compiler forbids it".
2. **Cut the thinnest edges** (§5). `windowing → rendering` is the smallest live cross-tier edge in the
   tree by files touched. Each cut removes a grant and makes the lattice shallower.
3. **Break the `TilePainter : TileDrawer` inheritance** (§10). One edge, and it is the reason `WorldPass`
   cannot close its input contract and the client has no headless expression. Tracked as #191.
4. **Give `game` internal boundaries** (§7). Not a refactor — a `CMakeLists.txt` per cluster and a grant
   list each. This is the single highest-leverage structural change available, and it costs no
   behavioural change at all.
5. **Decide whether the cross-cutting subsystems get homes** (§9). Scripting and protocol are the two
   that would benefit; both are currently un-auditable because no instrument is shaped to see them.

The general rule underneath all five: **to make a boundary real, make it a directory with its own grant
list.** That converts a lint we maintain into a compile error we do not. It is the cheapest enforcement
mechanism in the system, and outside the tiers that already exist, nothing uses it.

---

## Regenerating, and what fails if you don't

```
scripts/arch-graph.py                    # every block to stdout
scripts/arch-graph.py --facts            # machine-readable JSON
scripts/arch-graph.py --inject docs/architecture/system-boundaries.md
scripts/arch-graph.py --check  docs/architecture/system-boundaries.md
```

`--check` runs as the `arch_graph_fresh` ctest and in `.github/workflows/gates.yml`. It compares each
generated block against a freshly measured one and fails if any disagrees.

The generator also refuses to run at all in two situations rather than drawing something misleading:

- **ambiguous header basenames** — include attribution resolves headers by basename, so two files with
  the same name in different directories would silently mis-attribute edges;
- **a tier table contradicting the grants** — `TIERS` in the script is the one editorial claim in the
  structural diagrams, and if a directory's grants ever point up a tier, the script says so instead of
  drawing a lattice that is wrong in a way no reader could detect.

**Related:** `docs/render/README.md` (the render subsystem's own index), `docs/board.md` (task ids cited
in commit messages).
