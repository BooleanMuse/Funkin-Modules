<img width="606" height="383" alt="FunkinLogo" src="https://github.com/user-attachments/assets/1f45e8b6-afbc-4f15-81a3-ece5833348c6" />

# 

Friday Night Funkin' characters that live in your VCV Rack, sing what your patch
tells them to, and let you play the answer back.

Three modules:

- **Funkin Opponent** — four gates go in, a character sings them. The same four
  gates come back out as **CHART**.
- **Funkin Player** — patch that CHART in, and what the opponent just sang comes
  down a road for you to play back with a gamepad, the computer keyboard, a MIDI
  controller or four more gates. What you hit comes out as gates. With nothing
  patched in it is in **freestyle**: no chart, no scoring, just a character that
  sings what you play.
- **Funkin Dancer** — the one in the background. No arrows: it dances on the beat
  and hands the beat back out as a trigger, a bar marker and a falling envelope.

The characters themselves stand *in the rack*, not on the panels — dragged
wherever you want them, and resized by rolling the wheel over them (or with the
SIZE knob, which is the same value). They do not take clicks away from anything
underneath.

---

## How it works

There is no song file and no chart editor, and that is on purpose. **A note is a
gate on a cable.** Anything in your patch that can make four gates — a sequencer,
a drum machine, a clock divider, a MIDI file player, your own hands — is a chart,
and the opponent will sing it.

What makes it a game rather than a reflex test is the **lead**. A note arrives at
the player module the moment the opponent sings it, so it is not due yet: it is
due LEAD seconds from now, and spends that time coming down the road. Which means
you are always answering the opponent one lead late.


**Funkin Opponent** 

<img width="902" height="527" alt="Funkin_Opponent" src="https://github.com/user-attachments/assets/6fa9f8e9-b962-4410-91f3-6e9ecb300517" />


| | |
|---|---|
| LEFT / DOWN / UP / RIGHT | a gate each. A long gate is a held note. |
| NOTES | the same four, on one polyphonic cable. Either, or both. |
| BEAT | a clock. Without one it keeps its own, at the BPM knob. |
| CHART | the four gates it sang, polyphonic. This is what the player plays. |
| SING | high while the character is singing anything. |
| BEAT | a trigger on every beat. |
| BPM / HOLD / SIZE | tempo, how long a sing pose is held, how big they are. |

**Funkin Player** 

<img width="902" height="527" alt="Funkin_Player" src="https://github.com/user-attachments/assets/f79f3aa6-c6bb-4662-bd88-2d89697769f8" />


| | |
|---|---|
| CHART | the chart to play, polyphonic. Straight out of the opponent. |
| LEFT / DOWN / UP / RIGHT | play an arrow from the patch instead of by hand. |
| BEAT | a clock, as above. |
| KEYS | what you pressed, polyphonic. |
| HIT | only the presses that landed on a note. A sustain holds its gate. |
| MISS | a trigger every time you drop one. |
| HEALTH | 0–10 V. 5 V is even. |
| SING / BEAT | as the opponent's. |
| BPM / LEAD / WINDOW / SIZE | tempo, how far ahead notes appear, how forgiving the hit window is, how big they are. |

**Funkin Dancer** 

<img width="505" height="721" alt="Funkin_Dancer" src="https://github.com/user-attachments/assets/d1054caa-cc42-4e79-b9d7-9e267d6fa1ce" />


| | |
|---|---|
| BEAT | a clock, as above. |
| HEY | change the animation to Hey |
| BEAT | Beat that the dancer triggers |
| BAR | Voltage when bar ends |
| BOP | Voltage on every change |

Put the modules **next to each other** and the health bar grows the opponent's
face on its other end. A cable carries gates, not a face, so standing next to one
is the only way the player module can know who it is up against — and the dancer
is allowed to stand between them, because that is where it belongs.

## Playing it

A gamepad and a MIDI controller reach the module as MIDI, because Rack already
ships a **Gamepad** driver and a **Computer keyboard** driver of its own. Pick
one from the MIDI menu, then **Learn a button** → click an arrow → press what you
want on it. The menu stays open while it listens, so a pad button is bound by
pressing the pad button. Clicking a row of the CONTROLS box does the same.

Out of the box the arrows are **the arrow keys**, played while the pointer is on
the module — nothing to set up and no driver to pick. Hovering rather than focus
is what keeps the arrow keys working as the arrow keys everywhere else in the
rack. The menu also has **Bind MIDI notes C4 upwards** for a keyboard, and a row
can be a key while another is a note.

The fifth row, HEY, plays the character's taunt animation if the mod has one.

Hit windows are the game's own: SICK inside 33 ms, GOOD inside 125, BAD inside
150, and past 167 ms it was not a hit at all. The WINDOW knob multiplies all four
if you want it gentler.

**Ghost tapping** is on by default: pressing an arrow with no note under it is
free, and the character sings it like any other note. Turn it off in the menu and
a press off the chart is a miss, with the wince to match, which is how the base
game plays.

**Freestyle** needs no finding. With nothing patched into CHART there is no
chart, so nothing is scored and the module is simply a character that sings what
you play — the panel says FREESTYLE where the road would be. The menu switch is
for the other case: a chart *is* coming in and you would rather jam over it than
be marked on it.

## Getting characters in
<img width="889" height="789" alt="Funkin_Instructions" src="https://github.com/user-attachments/assets/9d40dab5-017a-403d-b448-f7578976282d" />


Right-click either module → **Open the characters folder**, and drop a Friday
Night Funkin' mod into it. Or **Add a mod folder…** and point it straight at a
mod you already have. Every Funkin' module in the rack shares the folders, and
the patch remembers them.

It reads:

- **Psych Engine** character files (`characters/dad.json` + `images/characters/…`)
- **V-Slice** character files (FNF 0.3 and later)
- **a bare spritesheet**: a PNG and an XML with no character file at all, which
  is what the [spritesheet and XML generators](https://gamebanana.com/tools/7136)
  hand you. The animations are worked out from the names in the sheet.

Nothing about a mod's layout is assumed. It walks whatever folder you point it
at, finds character files, spritesheets and health icons, and matches them up.

Offsets, sing durations, health-bar colours, `flip_x` and `no_antialiasing` all
come out of the character file and are used, so a character tuned in the game
looks tuned here — including pixel-art characters, which stay crisp.

A module that has never been told who to be picks somebody out of the library on
its own — the player reaches for a boyfriend, the opponent for a dad, the dancer
for a girlfriend — so a fresh patch opens as the three of them rather than as
three copies of the same character. With no library at all it wears nobody, and
says which of the reasons it is instead of just looking broken.


## Licence

GPL-3.0-or-later. Friday Night Funkin' is by ninjamuffin99, PhantomArcade,
evilsk8r and Kawai Sprite; this plugin ships none of its assets and only reads
the ones you already have.

All the characters of Friday Night Funkin are part of the The Funkin' Crew Inc.
