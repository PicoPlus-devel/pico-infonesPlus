# Left black band analysis — the 8 px (40 output px) black column at the left edge

Analysis date: 2026-10-04 / 2026-10-05
Project: `S:\v51\v53\pico-infonesPlus` (v53 + 1280x240 customization, phase 4e)
Method: the `hosttest/` headless harness (runs the unmodified `infones/` core on a PC),
measuring dumped PPM frames.

> This is the English copy of `分析_左黑邊.md`. The images and patches referenced below
> also exist under Chinese names in the same folder; the English-named files are
> byte-identical convenience copies of the same content.

---

## 0. Summary (conclusion first)

1. **It can be fixed, but two different situations must be told apart — same symptom,
   different cause.**

   | Case | `$2001` | Nature of the left band | Fixable |
   |---|---|---|---|
   | A: the game asks for the background left column | bit1 = **1** | **Core bug**: the background is erased by the sprite clip | ✅ Fixable, and the result is **closer to real hardware** |
   | B: the game asks for the left column to be blank | bit1 = **0** | **Authentic hardware behaviour**: a real NES shows the backdrop colour there (usually black) | ⚠️ Only removable by deliberately deviating from hardware |

2. **This is not a regression from the v53 upgrade.** The same code in
   `S:\infonesPlus_240p` and `S:\v49\v50` is byte-identical; the behaviour has always
   been there.
3. **It is not a 1280x240 problem either.** The whole horizontal chain (`+64u` offset,
   the 5x scaler, the 0.0.20 header/source, the `R1_Line_Crcs` table) is byte-identical
   to the 240p build (see `升級_1280x240_runbook.md` §9).
4. **The band can only ever be on the left.** The NES PPU only has left-column blanking;
   there is no right-column clip. So "almost no game has a black band on the right" is
   inevitable, not a coincidence.
5. **Why do only a minority of games show it?** Because the vast majority of games run
   with `$2001 = $1E` during gameplay (both the sprite and the background left 8 px are
   enabled) — so no clipping happens at all.
   Survey of 208 ROMs: **141 (72.7 %) clean, 53 (27.3 %) affected**. See §3.2.
6. **★ But "the left edge looks black" is not the same as "there is a clipped band".**
   Some games simply have their own black background at the left edge.
   Test: run the clip-disabled build; only if the left 8 columns **change** is the
   band caused by the clip. See the end of §3.2.

---

## 1. Cause: two left-column clips in `infones/InfoNES.cpp`

```
L1725  /* Backgroud Clipping */      if (!(byR1 & R1_CLIP_BG))  ->  zero the first 8 px
L2015  /* Sprite Clipping    */      if (!(byR1 & R1_CLIP_SP))  ->  zero the first 8 px
```

`R1_CLIP_BG = 0x02` (`$2001` bit1), `R1_CLIP_SP = 0x04` (`$2001` bit2) — see `InfoNES.h:138-143`.

**The key point**: the sprite clip at L2015 runs **after the background has already been
drawn**, and calls `InfoNES_MemorySet(pPointTop, 0, 8 << 1)` on **the first 8 px of the
whole line buffer** — it erases "the first 8 pixels", **not "the first 8 sprite pixels"**,
so the background is erased as well.

On real hardware these two bits are **independent**:

| `$2001` | Real hardware | This core |
|---|---|---|
| bit1=1 | background left column shown normally | ✅ normal (L1725 does not fire) |
| bit1=0 | background left column = backdrop colour | ⚠️ hard-coded **pure black** (not the backdrop colour) |
| bit2=1 | sprite left column shown normally | ✅ normal |
| bit2=0 | sprites hidden in the left column, **background unchanged** | ❌ **the background is erased to black too** |

-> As soon as bit2=0, the first 8 px are necessarily black, **regardless of bit1**.
That is the origin of the Musashi no Ken class: "the background left column is enabled
but still gets cut off".

**Condition for the band to appear**: `NOT (bit1 AND bit2)`.
**Condition for the band to be a bug**: `bit1=1 AND bit2=0`.
**Condition for the band to be authentic**: `bit1=0`.

---

## 2. Geometry: 8 NES px = 40 output px

`NES px k -> output px 5k..5k+4` (symmetric 5x scaling, full-bleed, no letterboxing —
see `升級_1280x240_runbook.md` §9). So the left 8 NES px = output px **0..39**.

---

## 3. Measured data (harness, restricted to the device-visible scanlines 4..235)

`L` = the number of consecutive columns from column 0 that are >90 % pure black (in NES px).

| ROM | mapper | `$2001`(R1) | bit1 | bit2 | stock L | sprite clip disabled | both disabled |
|---|---|---|---|---|---|---|---|
| Musashi no Ken - Tadaima Shugyou Chuu | 3 | `1A` | **1** | 0 | **8** | **0** ✅ | 0 |
| Kunio-kun no Nekketsu Soccer League | 4 | `1A` | **1** | 0 | **8** | **0** ✅ | 0 |
| Nekketsu Kouha Kunio-kun (VC) | 2 | `18` | 0 | 0 | 8 | 8 | **0** |
| Nekketsu Koukou Dodgeball-bu (VC) | 1 | `18` | 0 | 0 | 8 | 8 | **0** |
| Nekketsu Koukou Dodgeball-bu | 1 | `18` | 0 | 0 | 8 | 8 | **0** |
| Downtown Special - Kunio-kun no Jidaigeki | 4 | `18` | 0 | 0 | 8 | 8 | **0** |
| Super Mario Bros. (World) | 0 | `1E` | 1 | 1 | 0 | 0 | 0 |
| Mario Bros. (World) | 0 | `1E` | 1 | 1 | 0 | 0 | 0 |
| Gradius (Japan) | 3 | `1E` | 1 | 1 | 0 | 0 | 0 |
| Star Force (Japan) | 0 | `1E` | 1 | 1 | 0 | 0 | 0 |

**Note**: `$2001` is evaluated **per scanline**
(`byR1 = PPU_R1 | (PPU_R1_Line & PPU_R1_LineMask)`), so the same game can differ between
screens and between scanlines. The values above are sampled at frame 400.

### 3.1 What gets filled in is correct background, not garbage

Dodgeball gameplay frame, dominant colour per column (240 rows):

| Column | Stock | Both clips disabled |
|---|---|---|
| col 0..7 | **100 % pure black (0,0,0) x240** | black x63 + (68,34,0) x49 (= exactly the same as col 8..11) |
| col 8..11 | black x63 + (68,34,0) x49 | black x63 + (68,34,0) x49 |

So after disabling, the left 8 columns carry content whose **distribution matches the
adjacent col 8** -> it is the correct continuation of the background.
(Reason: the core's "left-end block" at `InfoNES.cpp:1534-1573` already renders the
partial tile at column `PPU_Scr_H_Byte` starting from bit `PPU_Scr_H_Bit` correctly —
it was simply being erased immediately afterwards.)

---

### 3.2 Why only a minority of games show it (208-ROM survey, 2026-10-05)

**Exact condition (note it also requires the layer to be enabled):**

```
band <=> ( bit1=0 AND bit3=1 )     <- background clip (bit3 = BG enabled)
      OR ( bit4=1 AND bit2=0 )     <- sprite clip    (bit4 = sprites enabled)
```

### Survey results

A systematic sample of **208 ROMs** out of 2486 in `/s/FC ROM` (every 12th file).
Each ROM ran 400 frames with START/RIGHT injected; the modal `$2001` over 4
"rendering active" samples was recorded:

| Category | Count | Share |
|---|---|---|
| **No band** | 141 | **72.7 %** |
| **Band** | 53 | **27.3 %** |
| (no rendering sample at all: title / blank screen) | 14 | — |

The 53 affected ROMs broken down by cause:

| Cause | Count | Nature |
|---|---|---|
| bit1=0 **and** bit2=0 (the game turned off both left columns) | 43 | authentic hardware behaviour |
| **bit2=0 only** (= pure core bug, the Musashi class) | 9 | **fixable** |
| bit1=0 only | 1 | authentic hardware behaviour |

### `$2001` distribution over all samples (208 ROMs x 4 samples = 784)

**Grouped by "does it clip" (using the exact rule above):**

| Category | Samples | Share |
|---|---|---|
| **No clip -> no band** | 553 | **70.5 %** |
| Both clipped -> band | 167 | 21.3 % |
| Sprite column only clipped -> band | 39 | 5.0 % |
| Background column only clipped -> band | 3 | 0.4 % |
| Rendering off / title (n/a) | 22 | 2.8 % |

**Raw `$2001` values, top 12:**

| `$2001` | Samples | Bits | Band |
|---|---|---|---|
| `$1E` | **534** | SP \| BG \| sprite-left \| bg-left | **none** |
| `$18` | 156 | SP \| BG (both left columns off) | yes |
| `$1A` | 39 | SP \| BG \| bg-left (sprite-left off) | yes |
| `$00` / `$F8` | 11 / 11 | rendering off / emphasis bits | n/a |
| `$06` | 10 | left-column bits only, display off -> nothing renders | n/a |
| `$0A` | 8 | BG \| bg-left (**no sprites**) | **none** (the sprite clip never runs) |
| `$0E` | 6 | BG \| sprite-left \| bg-left | **none** |
| `$08` | 3 | BG (bg-left off) | yes |
| others | 6 | — | — |

### Conclusion

**The vast majority of games run with `$2001 = $1E`** — both the sprite and the
background left 8 px are **enabled**, so nothing is clipped and the picture is a full
256 px. Only a "minority" of games clear one of the two bits:

- **Clearing bit1 (background left column)**: horizontal-scrolling games use this to hide
  the **left-edge scroll seam / attribute garbage**, or to blank the left edge of a
  status bar.
- **Clearing bit2 (sprite left column)**: avoids sprites "popping in" from the left edge
  while scrolling — very common in horizontally scrolling action games (this is the
  bulk of the 27 %).

Two related questions:

- **Why does it look black instead of the backdrop colour?** Because the core hard-codes
  the blanked area to `0` (pure black); a real NES shows the `$3F00` backdrop colour
  there. (-> Option C)
- **Why only on the left?** The NES PPU only has left-column blanking; there is no
  right-column mask.

### Frame-by-frame verification (2026-10-05)

14 ROMs were sampled, comparing the left 8 columns of the **stock** and
**clip-disabled** (`nes_host_noboth.exe`) builds of the *same* frame:

- predicted "clips" -> the stock left 8 columns should be **>99 % pure black**
- predicted "no clip" -> the stock left 8 columns should be **pixel-identical** to the
  disabled build

**Result: 28 / 28 frames matched.**

| ROM | `$2001` | Prediction | Stock left8 | Disabled left8 | Verdict |
|---|---|---|---|---|---|
| Bionic Commando (USA) | `1A` | clips | 100 % | 100 % / 0 % | OK |
| Defender of the Crown (EU) | `1A` | clips | 100 % | 100 % | OK |
| Famicom Igo Nyuumon | `1A` | clips | 100 % | 0 % | OK |
| Hayauchi Super Igo | `1A` | clips | 100 % | 100 % | OK |
| Pennant League!! | `1A` | clips | 100 % | 100 % / 99 % | OK |
| Section-Z (USA) | `1A` | clips | 100 % | 0 % / 100 % | OK |
| Sky Destroyer (JP) | `1A` | clips | 100 % | 0 % | OK |
| Chessmaster (EU) | `0A` | no clip | 100 % | 100 % | OK |
| Galaxy 5000 | `0A` | no clip | 75 % | 75 % | OK |
| Bomberman (USA) | `0E` / `1E` | no clip | 100 % / 10 % | 100 % / 10 % | OK |
| Donkey Kong 3 | `1E` / `0E` | no clip | 62 % / 100 % | 62 % / 100 % | OK |
| 4 Nin Uchi Mahjong | `1E` | no clip | 0 % | 0 % | OK |
| AD&D Pool of Radiance | `1E` | no clip | 0 % | 0 % | OK |
| Adventures of Lolo II | `1E` | no clip | 100 % / 0 % | 100 % / 0 % | OK |

**★ An important side finding**: the disabled build's left 8 columns are **also often
100 % black** (Chessmaster, Bionic Commando, ...) — which means that black is the
**game's own black background**, not caused by the clip.

-> **"The left edge looks black" is not the same as "there is a clipped band".**
How to tell: run the game once with `nes_host_noboth.exe`. If the left 8 columns are
**completely unchanged** -> it is the game's own picture; if they **change** -> the clip
caused it (and it is fixable).

---

## 4. Options and trade-offs

### Option A — correctness fix (recommended)

Make the sprite clip **hide sprites only, not the background**: save the left 8 px of
background before the sprites are composited, and restore them afterwards.

- Effect: **fixes the bit1=1 class** (Musashi, Kunio-kun Soccer League, ...); bit1=0 games
  are unchanged.
- Cost: negligible. 8 extra WORDs (16 B) of stack plus two 8-element copies, only executed
  when `R1_SHOW_SP` is set.
- Accuracy: matches real hardware (background shown, sprites hidden).
- Risk: low. No timing, scaling or offset changes.

### Option B — cosmetic (the band disappears everywhere)

Disable the background clip (L1725) as well.

- Effect: **the band disappears for every game** (including the bit1=0 class — e.g. the
  "ku" of the Kunio title screen that was being cut off comes back).
- Cost: for bit1=0 games it shows content that **real hardware would not show**. The
  content itself is the correct background continuation (see §3.1), not garbage; but it
  is a **deliberate deviation from hardware**.
- Risk: medium-low. In theory, if a game relies on the blanked column to hide a scroll
  seam, the seam would become visible; this core's left-end block renders correctly and
  no garbage was observed, but the coverage tested was only a small number of ROMs.

### Option C — small extra (can coexist with A/B)

L1725/L2015 currently hard-code **pure black** `0`; a real NES shows the **backdrop
colour** (`$3F00`) in the blanked area.

`PalTable[0]` is confirmed to be the backdrop (`InfoNES.cpp:231,236`:
`const WORD backdrop = NesPalette[PPURAM[0x3f00] & 0x3f] | 0x8000;` -> `PalTable[0] = backdrop`),
so replacing `InfoNES_MemorySet(pPointTop, 0, 8 << 1)` at both sites with a fill of
`PalTable[0]` is all that is needed.

- Effect: only affects games whose backdrop is not black; no change for the majority
  whose backdrop is black.
- Cost: negligible.
- Caveat: `PalTable[0]` carries the `0x8000` bit; every normal pixel in this core carries
  it too, so it is consistent and safe (both clips run after `compositeSprite()`, so the
  sprite opacity test is unaffected).
- **Not implemented yet**; apply when needed.

---

## 5. How to reproduce

```bash
cd /s/v51/v53/pico-infonesPlus
# stock
./hosttest/nes_host.exe "<rom>.nes" 620 400 "C:/.../out"
python <measurement script> "C:/.../out"
```

Scratch variant binaries used here (**the project source was not modified**; these are
separately compiled copies):

| Binary | Contents |
|---|---|
| `hosttest/nes_host.exe` | stock (= device behaviour) |
| `hosttest/nes_host_nospritclip.exe` | L2015 sprite clip disabled only (= rough version of Option A) |
| `hosttest/nes_host_noboth.exe` | L1725 + L2015 disabled (= Option B) |
| `hosttest/nes_host_fixA.exe` | exact Option A (saves/restores the background 8 px) |

**Ready-to-apply patch files** (verified clean with `git apply --check`):

| File | Contents |
|---|---|
| `patch_A_sprite_clip_hides_sprites_only.diff` | Option A. Two changes in `InfoNES.cpp` (insert `bgLeft8[8]` to save the background + rewrite the L2015 block) |
| `patch_B_both_left_clips_disabled.diff` | Option B = A + a compile-time switch `NES_SHOW_LEFT_COLUMN` (default 0) in front of L1725 |

How to apply:

```bash
cd /s/v51/v53/pico-infonesPlus
git apply --check "S:/v51/v53/左黑邊分析/patch_A_sprite_clip_hides_sprites_only.diff"   # check first
git apply          "S:/v51/v53/左黑邊分析/patch_A_sprite_clip_hides_sprites_only.diff"
```

> Note: Git Bash's `patch` falsely reports `different line endings` (this project is all
> LF; no CRLF was found). **Use `git apply`, not `patch`.**

---

## 6. Current status and "run this later" checklist

### 6.1 Status (stopped on 2026-10-04)

- **Owner's instruction: do not change anything today; revisit when there is enough
  time** (because it requires flashing to the device and testing on the screen, which is
  time-consuming).
- `infones/InfoNES.cpp` has **not been touched at all**. The project source and the
  phase-4e firmware are unchanged.
- Options A/B/C, the patch files, the measurements and the comparison images are **all
  saved in this folder**, ready to resume at any time.
- Current device behaviour = `hosttest/nes_host.exe` (stock).

### 6.2 Checklist for the next session (just follow it)

**Step 0 — decide the option** (think it through before touching anything)

| Desired outcome | Choose |
|---|---|
| Fix only the "core bug" class (Musashi-like, where the background is wrongly erased) and keep the authentic look for bit1=0 games | **A** |
| Remove the band everywhere (including restoring the "ku" of the Kunio title), accepting a deviation from hardware | **B** (confirm A's result first, then move to B) |
| Additionally show the backdrop colour instead of pure black in the blanked area | add **C** on top of A/B |

**Step 1 — create a restore point** (byte-exact, per `safe-edit-restore-point`)

```bash
cd /s/v51
D=_restore_$(date +%Y%m%d)_leftband
mkdir -p $D
# use tar, not git checkout (core.autocrlf rewrites line endings)
tar --force-local -cf $D/v53_leftband_$(date +%Y%m%d).tar -C /s/v51/v53 pico-infonesPlus
# per-file sha256 manifest + verify after restore (0 FAILED is the pass condition)
```

**Step 2 — apply the patch**

```bash
cd /s/v51/v53/pico-infonesPlus
git apply --check "S:/v51/v53/左黑邊分析/patch_A_sprite_clip_hides_sprites_only.diff"
git apply          "S:/v51/v53/左黑邊分析/patch_A_sprite_clip_hides_sprites_only.diff"
git diff --stat    # should show only infones/InfoNES.cpp
```

**Step 3 — full clean rebuild** (hard rule: never incremental)

```bash
export PICO_SDK_PATH="S:/sdk220/sdk220"
export PICO_PIO_USB_PATH="S:/sdk220/sdk220/lib/tinyusb/Pico-PIO-USB"
export PATH="/s/arm-none-eabi/bin:/s/mingw64/bin:$PATH"   # GCC 10.3.1, not S:/toolchain
mv build build_phase4e_ui                                  # the sandbox blocks bld.sh's internal rm -rf
./bld.sh -c7
```

**Step 4 — software verification (do this before flashing, it saves time)**

```bash
# 4a) rebuild the harness (see the skill section "rebuilding the harness on a PC"),
#     then re-verify frame by frame:
#     Musashi no Ken         -> L should go from 8 to 0
#     Kunio-kun Soccer League-> L should go from 8 to 0
#     Dodgeball VC / Kunio-kun VC / Downtown Special -> L should stay 8 (Option A)
#     SMB / Mario Bros.      -> L should stay 0 (no regression)
#     only count scanlines 4..235
# 4b) symbol-level A/B: nm -S to compare the size delta of InfoNES_PreDrawLine
# 4c) confirm build/piconesPlus.elf is a new artifact (timestamp + sha256 differ from
#     build_phase4e_ui)
```

**Step 5 — flash and test on the device (the time-consuming part)**

| What to look at | Expected |
|---|---|
| Musashi no Ken | left band gone, sky/ground extend to the edge |
| Kunio-kun Soccer League | same |
| Dodgeball / Kunio-kun VC | **Option A: band unchanged** (correct); Option B: gone |
| SMB / Gradius and other previously fine games | no new garbage or shift |
| Is there a "colourful noisy column" at the left edge? | it must not appear (the harness showed the fill is the correct background, not garbage) |
| Performance | the FPS overlay's `R<n>` should not rise (this change does not touch the ISR hot path, but check anyway) |

**Step 6 — rollback** (if the result is not as expected)

```bash
cd /s/v51/v53/pico-infonesPlus
git checkout -- infones/InfoNES.cpp      # or git apply -R with the corresponding patch
# safest: extract the Step 1 tar, compare, then overwrite and do a full rebuild
```

### 6.3 Why on-device testing is mandatory

The harness only proves that the **core output frame** is correct. Only after flashing do
we learn about the HSTX output, the CXA1645 analogue chain, and the overscan boundary of
the actual screen. So the A/B trade-off must ultimately be settled on the screen.

---

## Attachments

| File | Contents |
|---|---|
| `patch_A_sprite_clip_hides_sprites_only.diff` | **applyable Option A patch** (verified with `git apply`) |
| `patch_B_both_left_clips_disabled.diff` | **applyable Option B patch** (= A + compile-time switch) |
| `1_Musashi_original_vs_FixA.png` | left = stock (left band, sky/ground cut) / right = Fix A (filled in) |
| `2_Kunio_title_original_vs_OptionB.png` | left = stock (left band, "ku" cut) / right = Option B (title complete) |
| `3_Musashi_original_left24_zoom8x.png` | stock, left 24 columns magnified 8x — the 8 px band is directly visible |
| `4_Musashi_FixA_left24_zoom8x.png` | same area after Fix A — band gone |

**Scratch binaries** (in `pico-infonesPlus/hosttest/`, not project source; safe to delete
at any time):

| File | Contents |
|---|---|
| `nes_host.exe` | stock (= device behaviour) |
| `nes_host_nospritclip.exe` | L2015 disabled only (rough Option A) |
| `nes_host_noboth.exe` | L1725 + L2015 disabled (Option B) |
| `nes_host_fixA.exe` | exact Option A (saves/restores the background 8 px) |

> These `.exe` files are build artifacts, not project files; deleting them breaks nothing.
> Rebuild with the commands in the skill section "rebuilding the harness on a PC"
> (about 1 minute).
