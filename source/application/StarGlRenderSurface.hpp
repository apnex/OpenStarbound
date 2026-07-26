#pragma once

#include "StarGlTexturePrimitives.hpp"

namespace Star {

// THE RENDER-SURFACE SUBSTRATE (Layer 1) -- four sovereign components (GlSurface surface, GlTargets
// registry, GlEffects programs, GlPass the coupled effect/target bind) plus the effect data they own,
// lifted out of OpenGlRenderer (§8.ii Tier 2) so they form a module with a real OUTSIDE: sealed against
// every consumer, constructible in a test, composable by the higher layers. They reach nothing in the
// renderer; the dependency runs strictly downward onto the texture primitives.

// Vertex-buffer texture units. Shared: GlPass::bindEffect flattens this many sampler uniforms; the renderer
// binds this many per draw.
constexpr size_t MultiTextureCount = 4;

// TODO: Bott - This isn't really rendering-specific. Don't really like it either. It's nicer than having redundant bool options, though.
enum class BoolSettingMode {
  Enabled,
  FromSetting,
  Disabled
};
extern EnumMap<BoolSettingMode> const BoolSettingModeNames;

bool settingModeValue(BoolSettingMode const& mode, bool const& setting);

struct EffectParameter {
  GLint parameterUniform = -1;
  VariantTypeIndex parameterType = 0;
  Maybe<RenderEffectParameter> parameterValue;
};

struct EffectTexture {
  // THE BORROW, SET AS ONE ACT. A sampler's storage and its status -- whose it is, and whether we may write it,
  // and which named target to re-point to on a rebuild -- must agree, so the three mutators set them together
  // and the predicates never reconstruct "may I write?" from the others. That reconstruction put half-float
  // RGBA into a live render target (RB-1). borrowedFrom NON-EMPTY names the framebuffer target this sampler
  // borrows a colour attachment from: loadConfig destroys and rebuilds every target, so the binder re-points
  // us at the rebuilt one BY NAME (RB-5) -- otherwise the sampler is not freed but ORPHANED. Empty means "not
  // a named target": either we adopted our own storage, or we share a texture another sampler owns.
  //
  //   adopt()   -- we allocated this texture; it is OURS to write. (m_owned = true)
  //   share()   -- we point at storage someone else owns: a framebuffer face BY NAME, or another sampler's
  //                already-uploaded texture with an EMPTY name (shared-but-owned-elsewhere). NOT ours to write.
  //   release() -- let go of everything.
  //
  // Writability is its OWN recorded fact (m_owned, set only by adopt), because adopt() and share(tex, "") both
  // leave borrowedFrom empty yet mean opposite things -- setEffectTextureAlias of a NON-borrowed source is
  // exactly share(tex, ""), and deriving writability from the name alone let a later upload re-specify the
  // source's live texture through the alias. The three fields are PRIVATE (below): the only way to change any
  // of them is one of the three atomic acts, so no call site can poke one and desync the trio -- RB-1 closed
  // by the compiler, not by care.
  void adopt(RefPtr<GlLoneTexture> tex) { m_textureValue = std::move(tex); m_borrowedFrom = ""; m_owned = true; }
  void share(RefPtr<GlLoneTexture> tex, String from) { m_textureValue = std::move(tex); m_borrowedFrom = std::move(from); m_owned = false; }
  void release() { m_textureValue.reset(); m_borrowedFrom = ""; m_owned = false; }

  RefPtr<GlLoneTexture> const& texture() const { return m_textureValue; }   // the storage, read-only to the outside
  String const& borrowFrom() const { return m_borrowedFrom; }               // which named target (empty = none)
  bool borrowed() const { return !m_borrowedFrom.empty(); }                 // borrows a NAMED target -> re-point on rebuild

  //   hasStorage()          -- a texture exists and has been specified. The frameBufferTextures binder asks
  //                this: it re-points a sampler at a rebuilt target EVEN when borrowing, so it must NOT exclude
  //                borrowed textures.
  //   ownsWritableStorage() -- hasStorage() AND we adopted it. The upload setters branch on this: false ->
  //                allocate fresh rather than write into storage that is absent, empty, or owned elsewhere.
  bool hasStorage() const { return m_textureValue && m_textureValue->glTextureId() != 0; }
  bool ownsWritableStorage() const { return hasStorage() && m_owned; }

  unsigned textureUnit = 0;
  TextureAddressing textureAddressing = TextureAddressing::Clamp;
  TextureFiltering textureFiltering = TextureFiltering::Linear;
  GLint textureSizeUniform = -1;

private:
  RefPtr<GlLoneTexture> m_textureValue;
  String m_borrowedFrom;
  bool m_owned = false;   // true only after adopt(): the one fact ownsWritableStorage() may trust
};

// A framebuffer is a SURFACE with one or two FACES.
//
// A face is one drawable copy of the surface: a colour texture, plus the framebuffer object that draws
// into it. Normally there is one. A surface declared "double" (#542) has two, and the second is what lets
// a shader read what it is writing -- swap() flips which face is being written and which is readable.
//
// The faces are reached ONLY through writeFace() / readFace(). That is not style: every framebuffer bug
// this struct has had came from a consumer reaching past them. `altId` was leaked because the destructor
// knew about `id` and forgot its twin. `makeAlt` duplicated the entire constructor because there was no
// one allocator. And blitGlSurface / the effect-texture resolution each had to re-derive "which half
// do I mean?" by hand. One resolver, and none of those are expressible.
struct GlSurface : RefCounter {
  // A face OWNS its framebuffer object. The texture is a RefPtr (~GlLoneTexture deletes the GL texture), but the
  // FBO `id` is a raw GLuint with no such wrapper, so the face RAIIs it: deletes it on destruction, hands it off
  // on move, and forbids copy. That is what makes the FBO unleakable -- including the makeDoubled case where a
  // second face is built into a LOCAL and allocateFace throws after glGenFramebuffers: the local's destructor
  // reclaims the FBO. ~GlSurface no longer frees anything by hand; front and back reclaim themselves.
  struct Face {
    GLuint id = 0;
    RefPtr<GlLoneTexture> texture;

    Face() = default;
    Face(Face&& o) noexcept;
    Face& operator=(Face&& o) noexcept;
    Face(Face const&) = delete;
    Face& operator=(Face const&) = delete;
    ~Face();
  };

  String name;
  Maybe<Vec2U> overrideSize;
  BoolSettingMode hdrMode = BoolSettingMode::Disabled;
  // The resolved HDR bit -- settingModeValue(hdrMode, the injected hdrSetting) -- derived ONCE at construction.
  // specifyStorage reads THIS field, not JSON: it runs on every resize, per face, and the config it used to
  // re-hash is fixed for the surface's whole life (loadConfig bakes hdrSetting into the config at construction).
  bool hdr = false;
  bool alpha = false;
  bool clear = true;
  unsigned multisample = 0;
  unsigned sizeDiv = 1;

  // THE RESOLVER. Ask which face you are writing, or which you may read. Never reach for a texture or an
  // id directly. A single-faced surface answers `front` to both.
  Face& writeFace() { return writeToBack ? *back : front; }
  Face& readFace() { return (back && !writeToBack) ? *back : front; }
  Face const& writeFace() const { return writeToBack ? *back : front; }
  Face const& readFace() const { return (back && !writeToBack) ? *back : front; }

  // WHICH FACE IS THE WRITE FACE, as a value the bind cache can key on. `writeToBack` is private (the seal),
  // so GlPass -- another Layer-1 class, granted no friend here -- reaches it through this accessor. swap() flips it;
  // that flip changes GlPass's (target, face, size) key and forces the rebind that the old `justSwapped`
  // bool used to force. Single-faced surfaces (everything in-tree) always answer false.
  bool writingBack() const { return writeToBack; }

  // Whether this surface can read what it writes -- i.e. has a second face. Existence IS the fact: this is
  // `back.isValid()` given a name, not a bool that could disagree with whether a second texture exists.
  bool doubled() const { return back.isValid(); }

  // THE SIZE ORACLE. `size()` is what is ACTUALLY allocated, right now, on every live face -- never (0,0)
  // on a live surface. `sizeFor()` is what this surface OUGHT to be at a given screen size, and it is the
  // only place that rule is written down. The two were previously derived by hand at four call sites, two
  // of which silently dropped `overrideSize`.
  Vec2U size() const;
  Vec2U sizeFor(Vec2U const& screenSize) const;

  // Give this surface its second face, mirroring the first face's RECORDED size. Idempotent. It takes no
  // screenSize: re-deriving the size here is how the two faces could be born different.
  void makeDoubled();
  // Flip which face is written and which is read.
  void swap();

  // Re-specify EVERY live face's storage at `size`, in this surface's configured format, and record it.
  // Idempotent: resizing to the size already allocated issues no GL calls at all.
  void resize(Vec2U const& size);
  // Clear every live face. The one consumer (startFrame) used to reach for the raw front/back face ids,
  // which is what made the seal a convention rather than a contract.
  void clearFaces();

  GlSurface(String const& name, Json const& config, Vec2U const& screenSize);
  ~GlSurface();

  // SEALED. The resolver is the contract, so the faces must not be reachable around it -- otherwise the
  // Air-Gap holds only for as long as everyone remembers, which is exactly how altId got leaked and how
  // the viewport went unset. Everything a consumer legitimately needs is public: the resolver
  // (writeFace/readFace), the size oracle, and the lifecycle (makeDoubled/resize/clearFaces). Verified:
  // NOTHING outside this struct reaches the faces, specifyStorage or allocateFace.
private:
  // NO friend. GlSurface once carried a `friend class OpenGlRenderer` while it was NESTED inside the
  // renderer -- a doorbell on a wall with no door, since the enclosing class was where every consumer lived.
  // Both are gone now: the friend was removed (§9 step 5) and the type was lifted OUT of OpenGlRenderer into
  // this file, so it is an ordinary top-level class. The seal is REAL and compiler-enforced by plain access
  // control -- no nesting relationship is involved any more -- so nothing outside GlSurface's own methods,
  // OpenGlRenderer included, can reach the private front/back faces, specifyStorage or allocateFace.

  // THE SINGLE OWNER OF FACE STORAGE. Derives the format from this surface's config (hdr / alpha /
  // multisample), specifies the colour storage at `size`, and RECORDS what it got. Nothing else may do any
  // of those three things.
  //
  // They used to be done by hand in three places -- the constructor, makeAlt, and setScreenSize -- each
  // re-deriving the same format ladder, and none of them recording the size. That is not three bugs. It is
  // one absent owner, and it produced: a second face that could be born a different size than the first;
  // a mod-visible textureSize uniform that read (0,0) forever; and a format ladder that any future change
  // would have had to find all three copies of.
  void specifyStorage(Face& face, Vec2U const& size, char const* which);
  // Bring `face` into existence: mint the GL objects, hand the storage to specifyStorage, set the sampling
  // parameters, attach, verify. Creation only -- it does not duplicate the storage or the format rules.
  void allocateFace(Face& face, Vec2U const& size, char const* which);

  Face front;                     // the primary face -- present the moment the surface exists
  Maybe<Face> back;               // the second face. Its EXISTENCE is doubledness. There is no `bool doubled`
                                  // to disagree with whether a second texture is actually allocated -- and
                                  // that exact disagreement is how upstream's altId leaked back in: a face
                                  // the destructor knew about as a bool but not as a thing to free. Here the
                                  // fact and the storage are one object; you cannot have one without the other.
  bool writeToBack = false;       // which face draws land on. Only ever true while `back` exists.
};

class Effect {
public:
  GLuint program = 0;
  // The frame-buffer wiring, parsed ONCE from the effect config at load (GlEffects::load) into fields, so
  // switchEffectConfig re-hashes no JSON. NAMES, never resolved targets: loadConfig destroys and rebuilds every
  // target, devOnly targets may be absent, and GlTargets::get throws -- a stored RefPtr would dangle. The
  // texture list is ORDERED, not a map: switchEffectConfig's `undefined || doubled` guard is order-sensitive
  // when a config names one framebuffer on two samplers. Entries are (textureUniform, framebufferName), for
  // framebuffer-named entries only; `doubleBuffered` (below) is derived from these in the same load pass.
  Maybe<String> frameBuffer;
  Maybe<String> blitFrameBuffer;
  List<pair<String, String>> frameBufferTextures;
  StringMap<EffectParameter> parameters;
  StringMap<EffectParameter> scriptables; // scriptable parameters which can be changed when the effect is not loaded
  StringMap<EffectTexture> textures;

  // THE FIXED LOCATIONS, resolved ONCE at load. They used to be two StringMaps memoizing glGet*Location, looked
  // up BY NAME on every effect switch by the one caller (GlPass::bindEffect) that immediately flattened them
  // into fixed fields -- a cache serving a function that already caches. Resolved here at load, bindEffect is a
  // handful of field copies. -1 == absent: glGet* returns -1, and these are GLint, not the old GLuint that made
  // "absent" read as 0xFFFFFFFF and only came right by a lossy round-trip into a GLint pass field.
  bool includeVBTextures = false;
  bool doubleBuffered = false;
  GLint positionAttribute = -1, colorAttribute = -1, texCoordAttribute = -1, dataAttribute = -1;
  GLint screenSizeUniform = -1, vertexTransformUniform = -1;
  GLint vbTextureUniforms[MultiTextureCount] = {};      // the sampler and its size, resolved iff includeVBTextures
  GLint vbTextureSizeUniforms[MultiTextureCount] = {};

  // Resolve the fixed locations from `program`. Call after the program is linked and includeVBTextures is set.
  void resolveLocations();
};

// GlTargets -- owns which render targets exist.
//
// That is the whole duty. Not what they contain, not what draws into them, not how they are bound: just
// which of them are alive, at what size, in the current config generation.
//
// The generation is the point. loadConfig destroys and rebuilds every target, so all their content becomes
// UNDEFINED -- and a retained (clear:false) surface cannot see that, because its own refresh key (size,
// camera, counter) is unchanged across the rebuild. It would happily composite garbage. So consumers fold
// the generation into their refresh key.
//
// It used to be two statements a caller had to remember to pair:
//     ++m_frameBufferGeneration;
//     m_frameBuffers.clear();
// Forget the first and a retained surface silently composites undefined GPU memory. Here they are ONE act,
// because they ARE one act: destroying the set IS the event the generation exists to announce.
struct GlTargets {
  // The tolerant lookup: null when absent. Callers that can degrade (the lighting passes fall back to CPU)
  // use this one.
  RefPtr<GlSurface> find(String const& id) const;
  // The strict lookup: throws when absent. Callers that cannot proceed without it use this one.
  RefPtr<GlSurface> get(String const& id) const;
  bool has(String const& id) const;
  uint64_t generation() const;

  // Destroy every target, and announce it. The bump cannot be forgotten because it is not a separate step.
  void destroyAll();
  void add(String const& name, Json const& config, Vec2U const& screenSize);

  // Operations over the whole set. Each was a hand-rolled loop reaching into the map.
  void resizeAll(Vec2U const& screenSize);
  void clearAll();                 // the per-frame clear, honouring each target's clear:false

private:
  // NO friend (§9 step 5): nothing outside GlTargets touches m_byId or m_generation -- the oracle reaches
  // targets through the public find(), not around it -- so no other class needs access, and plain access
  // control now enforces that.

  StringMap<RefPtr<GlSurface>> m_byId;
  uint64_t m_generation = 0;
};

// THE PASS: what is CURRENTLY bound -- one effect and one target -- for a sequence of draws.
//
// GL has exactly one current program and exactly one current draw framebuffer. Every draw reads BOTH, plus
// the flattened attribute/uniform locations of that program. GlPass owns that ambient draw state as one
// thing because it IS one thing: the pair is set together and read together. And the two are coupled --
// binding an effect resolves and binds ITS declared framebuffer (and swaps its faces, if it reads what it
// writes) -- so they cannot be owned by two components blind to each other without violating the Air-Gap.
//
// The coupling is not an obstacle to the decomposition. It IS a component, and nobody had written it.
// switchEffectConfig, setRenderTarget, composite, blitGlSurface and setEffectTextureFromTarget are not
// five problems: they are one missing Pass wearing several hats. A Pass depends DOWNWARD on effects and on
// targets; neither of them knows the other exists.
//
// (The header once made a STRONGER claim for this component -- that bindTarget itself writes the current
// program's screenSize uniform, so target-binding was also an effect operation. F3b.1 deleted that write as
// dead at both call sites. The component stands on the pair-and-locations above, not on the retracted write.)
//
// THIS STEP MOVES THE STATE ONLY. The six functions follow, one at a time, each certified bit-identical
// against the three GPU oracles -- because three of them carry real behaviour changes that must not ride
// in on a refactor, or the oracle goes red and we cannot tell a fix from a regression.
struct GlPass {
  // The flattened locations of the bound program. Read per draw, on the hot path.
  GLint positionAttribute = -1;
  GLint colorAttribute = -1;
  GLint texCoordAttribute = -1;
  GLint dataAttribute = -1;
  List<GLint> textureSizeUniforms = {};
  GLint screenSizeUniform = -1;
  GLint vertexTransformUniform = -1;

  // THE COUPLED PAIR. This is the whole reason the component exists. A null target means the screen
  // (framebuffer 0); a non-null target is the surface subsequent draws land on. Read externally (the oracle
  // saves/restores the bound target; applyEffectParameter reaches the bound effect) through the const accessors
  // below -- but the fields themselves are PRIVATE (bottom of the class), so only GlPass's own bind acts
  // (bindEffect / bindTarget / invalidate) can move them. The "one writer per fact" seal on the bind is now
  // compiler-enforced, not a comment: no external site can poke `m_effect` or `m_target`.
  Effect* effect() const { return m_effect; }
  RefPtr<GlSurface> const& target() const { return m_target; }

  // ASK GL WHAT IS ACTUALLY BOUND, AND COMPARE IT TO WHAT THIS PASS BELIEVES (#139 phase 1b). Returns the
  // number of disagreements; logs each one when `report`.
  //
  // WHY BELIEF-VS-REALITY AND NOT AN ABSOLUTE END-OF-FRAME CHECK. This pass is a CACHE: bindTarget and
  // bindEffect early-out when they can prove the thing is already bound, so a cache that disagrees with GL
  // does not merely mis-report -- it SKIPS the bind that would have fixed it, and every draw after that
  // lands somewhere nobody asked for. Every framebuffer bug this module has had was that desync: RB-1's
  // borrowed target, RB-4's passes early-out, RB-7's rebuilt target, and the stale-cache hazard
  // resetToScreen closes. Asserting "the screen is bound at end of frame" catches the last frame of one of
  // those and none of the rest; asserting that the cache tells the truth catches the class.
  //
  // It is also the only check that can see the failure mode the pixel oracles structurally cannot. They are
  // DIFFERENTIAL -- reference and cache-under-test share one draw lambda at one frame position under one
  // ambient GL state -- so an ambient-state divergence cancels exactly on both sides and reads as MATCH.
  // Four bugs reached the Director through that hole (#136).
  //
  // Reads only: glGetIntegerv of driver-side state, no glFinish, no readback, nothing that syncs.
  unsigned auditGlState(bool report) const;

  // Make `newTarget` the surface subsequent draws land on: bind its write face and set the viewport to that
  // face's size. A null newTarget binds the screen instead (see unbind). Keyed on (target, write-face,
  // viewport): it early-outs only when the cache proves all three already match, so a swap() (face flip) or
  // a same-target resize rebinds where the old identity-only key would have wrongly skipped. It writes NO
  // uniform -- that write was dead at both call sites and was deleted in F3b.1 (the .cpp explains why).
  void bindTarget(RefPtr<GlSurface> const& newTarget, Vec2U const& screenSize);

  // THE ONE screen-bind path: bind framebuffer 0 at the full-screen viewport, via bindTarget's null branch.
  // Replaces the two hand-rolled `target.reset(); glBindFramebuffer(0)` inverse-of-bindTarget sites. Public:
  // its callers (setRenderTarget / switchEffectConfig) are OpenGlRenderer methods, external to GlPass.
  void unbind(Vec2U const& screenSize) { bindTarget({}, screenSize); }

  // DROP THE CACHE without touching GL. Called wherever GL_DRAW is changed OUTSIDE the pass: loadConfig
  // (destroys and rebuilds every target, leaving GL_DRAW on a just-reallocated FBO while `target` becomes
  // null) and startFrame (clears every face and raw-binds 0 at the frame boundary). Without it the cache
  // would read a stale (target/screen, size) and let the next bind early-out over a corpse or wrong binding.
  // The {0,0} viewport sentinel matches no real screen or target size, forcing the next bind to emit real GL.
  void invalidate() { m_target = {}; boundWriteToBack = false; boundViewport = Vec2U(0, 0); }

  // DOES THIS CACHE CLAIM ANYTHING RIGHT NOW? Only the sentinel can answer, and that is worth stating: a null
  // m_target normally MEANS "the screen is bound", but invalidate() also nulls it -- so after an invalidate the
  // two readings are textually identical and mean opposite things ("I assert the screen" vs "I assert nothing").
  // The {0,0} viewport is what separates them, which makes it part of the cache's INTERFACE, not an
  // implementation detail of bindTarget's early-out.
  //
  // auditGlState() is the consumer, and needed it immediately: on boot frames startFrame invalidates and
  // nothing binds afterwards, so the audit's first run reported the sentinel as a wrong belief nine times and
  // read framebuffer creation's incidental bind as a desync. Neither was a defect. An audit that checks CLAIMS
  // must first ask whether a claim is being made.
  bool holdsBelief() const { return boundViewport != Vec2U(0, 0); }

  // THE FRAME-BOUNDARY RESET, as ONE act. Raw-bind framebuffer 0 AND drop the cache together, so the invalidate
  // can never be separated from the raw bind that necessitates it. startFrame's clearAll() has just bound every
  // face outside the pass; this puts GL_DRAW back on the screen and leaves the {0,0} viewport sentinel forcing
  // the next bindTarget to emit real GL. Replaces the hand-ordered glBindFramebuffer(0) + invalidate() pair that
  // startFrame used to keep in sync by care -- now it is by construction (the bind and the drop are one method).
  void resetToScreen();

  // Make `newEffect` the program that subsequent draws run: bind it, flatten its attribute and uniform
  // locations for the draw path to read, point its vertex-buffer samplers at their texture units, tell it
  // the screen size, and replay any scriptable values a script set while it was unbound.
  //
  // NO IDENTITY EARLY-OUT, deliberately. A reload erases an effect and re-emplaces it, and can hand back
  // the SAME address carrying a BRAND-NEW program -- an "already bound, skip" test here would leave the
  // pass holding the dead program's flattened locations. switchEffectConfig's early-out is a different
  // animal: it guards a whole target-bind and texture-rebind sequence, not this.
  void bindEffect(Effect& newEffect, Vec2U const& screenSize);

  // THE REST OF THE BIND KEY, sealed. boundViewport / boundWriteToBack are private and read only by GlPass's
  // own methods, so the renderer cannot poke them -- which is the point: the bind cache cannot be corrupted
  // from outside. This is the same seal every Layer-1 component now has. All four (GlSurface / GlTargets /
  // GlPass / GlEffects) are top-level classes with NO `friend`, so plain access control keeps each one's
  // privates unreachable from outside its own methods. There is no enclosing class and no nested-type
  // relationship any more -- the components were lifted out of OpenGlRenderer.
private:
  // THE COUPLED PAIR, sealed. Only bindEffect / bindTarget / invalidate write these; the renderer reads them
  // through effect() / target(). A null m_target is the screen (framebuffer 0).
  Effect* m_effect = nullptr;
  RefPtr<GlSurface> m_target;
  bool boundWriteToBack = false;  // which face of m_target GL currently draws into
  Vec2U boundViewport = {};       // the viewport GL currently has set (== screenSize when m_target is null)
};

// GlEffects -- owns the compiled GPU programs. Which effects exist, what program each one compiled to, and
// what a script has set on them. It binds nothing, draws nothing, and does not know a pass exists.
//
// THE REFERENCE DIES AT THE NEXT load(). StringMap is a FLAT, open-addressed hash map (StringMap -> HashMap
// -> MapMixin<FlatHashMap>): every Effect is stored INLINE in a std::vector of buckets, so an insert that
// rehashes MOVES all of them and an erase back-shifts. An `Effect&` or `Effect*` handed out here is valid
// only until the next load().
//
// GlPass caches exactly one such pointer, and it survives only because loadEffectConfig re-binds it
// immediately after every load -- an invariant that used to be an accident of a stray assignment buried in
// the middle of the load path, and is now the line after it. Do not cache a second Effect*.
struct GlEffects {
  // Compile, link, and register `name`, replacing whatever was under it. Throws RendererException when the
  // shaders will not compile or the program will not link.
  Effect& load(String const& name, Json const& config, StringMap<String> const& shaders);

  Effect* find(String const& name);   // null when absent

  // THE SCRIPTABLE SURFACE. Addressed BY NAME, on an effect that may not be the bound one -- that is the
  // whole point of it, and the reason it lives here and not on the pass. These write a CPU value and issue
  // no GL: the value reaches the GPU when GlPass::bindEffect next binds that effect.
  void setScriptable(String const& effectName, String const& parameterName, RenderEffectParameter const& value);
  Maybe<RenderEffectParameter> getScriptable(String const& effectName, String const& parameterName);
  Maybe<VariantTypeIndex> getScriptableType(String const& effectName, String const& parameterName);

  void destroyAll();   // delete every program, then forget them

  // GlTargets has just destroyed and rebuilt every render target. Point every sampler that was BORROWING a
  // target's texture at the new one of the same name -- or, if that target is gone from the config
  // entirely, let go of it. Without this the sampler holds an orphan: a live GL texture belonging to a
  // framebuffer that has been destroyed, which it will happily keep sampling.
  void rebindBorrows(GlTargets& targets);

private:
  // NO friend (§9 step 5): only GlEffects' own methods touch m_byName; the renderer reaches effects through
  // find()/load()/get(), so plain access control now enforces the seal against it too.
  StringMap<Effect> m_byName;
};

// The one place a RenderEffectParameter becomes a glUniform call -- shared by GlPass::bindEffect (scriptable
// replay) and OpenGlRenderer::applyEffectParameter (bound-effect writes).
void uploadUniform(GLint location, RenderEffectParameter const& value);

// Default GL shaders, shared: GlEffects::load falls back to them on a compile failure, and the
// renderer's ctor registers them as the "internal" effect. Defined in StarGlRenderSurface.cpp.
extern char const* DefaultVertexShader;
extern char const* DefaultFragmentShader;

}
