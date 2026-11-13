# BanaTap

An alternative to the Banapassport API DLL for Wangan Midnight Maximum Tune.  

A `card.ini` file needs to be placed in the same directory as the DLL.  

```ini
[card]
chipId=cardchipid
accessCode=accessCode
```

If the `card.ini` file is not detected, the DLL will create one automatically.  
The chipId and accessCode within it will be randomly generated.  

In the game, press the `C` key on your keyboard to trigger the sending of card data.  

`card.ini` and `BanaConfig.ini` are looked up next to the DLL (not the process working
directory). `BanaConfig.ini` is created with defaults on first run:

```ini
[BanaConfig]
ReadKey=C            ; letter, or a virtual-key code such as 67 / 0x43
AutoRead=0           ; 1 = tap automatically when the game asks for a card
AutoReadDelayMs=1500
VerifyPatch=0        ; 1 = only NOP the patch site if it starts with a CALL (0xE8)
PadButton=           ; XInput gamepad button(s) that tap the card, e.g. START or LB+A (empty = off)
BeepOnTap=0          ; 1 = play a short beep when the card is sent
OnlyWhenFocused=1    ; 1 = ignore all taps unless the game window is in the foreground
AutoReadMode=always  ; always = auto tap on every card request, once = only the first one
AutoReadCooldownMs=0 ; minimum time after a tap before auto tap may fire again
ToggleAutoKey=       ; key that switches auto tap on/off while playing, e.g. F9 (empty = off)
LogToFile=1          ; 1 = write banatap.log next to the DLL (also sent to DebugView)
LogExceptions=1      ; 1 = note serious exceptions (access violation, ...) in the log
LogApiCalls=1        ; 1 = log the first call of each BngRw export the game makes
StrictApi=0          ; 1 = reject invalid arguments (reader > 7, missing URL) with -100
```

Notes:

- Inline comments after `;` or `#` are ignored, and values are not case-sensitive.
  Booleans accept `1/0`, `true/false`, `yes/no`, `on/off`.
- Gamepad buttons: `A B X Y START BACK LB RB L3 R3 UP DOWN LEFT RIGHT`, combined with `+`.
  Any connected XInput controller (1-4) is checked.
- Key names (`ReadKey`, `ToggleAutoKey`): a letter, `F1`-`F24`, `SPACE`, `ENTER`, `TAB`, `ESC`,
  `INSERT`, `DELETE`, `HOME`, `END`, `PAGEUP`, `PAGEDOWN`, the arrow keys, or a virtual-key code
  such as `67` / `0x43`.
- Auto tap after a logout: with `AutoReadMode=always` the game asks for a card again right
  after logging out, which would log you straight back in. Use `AutoReadMode=once`, or set
  `AutoReadCooldownMs` (for example `60000`), or switch auto tap off with `ToggleAutoKey`.
  Pressing `ToggleAutoKey` to turn auto tap back on re-arms it for the next card request.
- `OnlyWhenFocused=1` checks that the foreground window belongs to the game process. It applies
  to the tap key, the gamepad and auto tap. If taps never work on your setup, set it to `0`.
- `card.ini` is never modified after it has been created. If `chipId`/`accessCode` are missing
  the card is not sent; if they look unusual they are sent exactly as written and a warning
  goes to the debug log. A new random card is only generated when `card.ini` does not exist.
- `banatap.log` is created next to the DLL and restarted when it grows past 256 KB. It records
  the patch site check, hook results, card requests, which trigger sent the card, config
  reloads and warnings. `chipId` and `accessCode` are never written to it, so it is safe to
  share when asking for help. Lines also go to DebugView.
- If the game never gets a card, look in the log for `not in the foreground` (see
  `OnlyWhenFocused`), `card not sent` (missing values in `card.ini`) or `patch site ... UNEXPECTED`
  (the patch offset does not match your game revision).
- `LogExceptions` adds a line for access violations and similar first-chance exceptions with the
  module and offset. The game may handle some of them itself, so treat a line as a lead rather
  than proof of a crash. At most 20 lines per run.
- `StrictApi` makes `BngRwReqCancel`, `BngRwReqLatchID`, `BngRwReqSendUrlTo` and
  `BngRwReqSendMailTo` return `-100` for a reader number above 7 (and for a missing URL or
  address), as the reference reader API does. It is off by default: the stubs accept everything,
  which is what BanaTap always did and what the older revisions that hook these functions expect.
- `LogApiCalls` writes one `api: <name> called by the game` line per export. It shows which
  functions your game revision really uses, which helps when adding a new revision.
- Exports: besides the original BanaTap names the DLL also exports `BngRwExReadMifareAllBlock`,
  `BngRwGetTotalRetryCount`, `BngRwReqSendMailTo` and `BngRwReqFwVersionup`, the names used by
  other builds. `BngRwExReadMifareAllBlock` / `BngRwReadMifare` return `0xFFFFFF9C` as a 64-bit
  value on purpose, matching the reference implementation.
- The DLL carries version information (right-click > Properties > Details) and prints it as the
  first line of the log, for example `BanaTap 1.0.4-3a1b2c4`. The resource is added when `windres`
  is found at build time; the git revision comes from `git describe`.
- Changes to `BanaConfig.ini` are picked up within about a second, no restart needed.
  Removing a key restores its default.

The older `[settings]` section in `card.ini` (`key`, `autoTap`, `autoTapDelayMs`,
`verifyPatch`) is still read; `BanaConfig.ini` wins when both are present.

## Supported revisions

| Game | Revision | Output file |
|:----:|:----:|:--------:|
| WMMT5 | `1.05.00` | `dist/wm5/1.05.00/bngrw.dll` |
| WMMT5DX | `2.00.02` | `dist/w5x/2.00.02/bngrw.dll` |
| WMMT5DX+ | `3.05.00` | `dist/w5p/3.05.00/bngrw.dll` |
| WMMT6 | `1.03.04` | `dist/wm6/1.03.04/bngrw.dll` |
| WMMT6R | `2.00.08` | `dist/w6r/2.00.08/bngrw.dll` |
| WMMT6RR | `3.05.03` | `dist/w6w/3.05.03/bngrw.dll` |

## Download

You do not have to build anything yourself. GitHub Actions builds all six DLLs on every push:

- **Latest build:** open the repository's **Actions** tab, pick the newest run of the `Build`
  workflow and download the `BanaTap-dlls` artifact. It contains the `dist/` layout shown below
  and a `SHA256SUMS.txt` file.
- **Releases:** pushing a tag such as `v1.1.0` publishes `BanaTap-v1.1.0.zip` and its `.sha256`
  file on the Releases page. The tag must match the `version` in `meson.build`, otherwise the
  workflow stops with a message.

Check a download from the extracted folder: `sha256sum -c SHA256SUMS.txt` (Linux/MSYS2), or
`Get-FileHash <file> -Algorithm SHA256` in PowerShell and compare it with the listed value.
Copy the DLL for your revision next to the game executable and rename nothing: every file is
already called `bngrw.dll`.

A separate `Logic tests (Linux)` job runs `tests/check.sh` and `tests/run.sh` first; see
`tests/README.md`.

## Build

### Environmental requirements

- `MSYS2 MINGW64`  
- `meson`  
- `ninja`  

Please perform all of the following operations using `MSYS2 MINGW64`.  

### Install dependencies

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-meson mingw-w64-x86_64-ninja
```

### Compilation

```bash
./build.sh
```

The output file is located in the `dist/` directory.  

## Add support

1. Copy an existing `dllmain_xxx_xxxxx.cpp` as a template, or create a new one from scratch.  

> You may need to find and set the correct `PATCH_OFFSET` address for the game.  

2. Add the following to `meson.build`:  

   ```meson
   library(
       '<dllmain_game_revision>',
       include_directories: ['src'],
       sources: common_sources + ['src/<dllmain_game_revision>.cpp'],
       name_prefix: '',
       link_args: ['-static'],
   )
   ```

   example:  

   ```meson
   # W6W 3.05.03
   library(
       'bngrw_w6w_30503',
       include_directories: ['src'],
       sources: common_sources + ['src/dllmain_w6w_30503.cpp'],
       name_prefix: '',
       link_args: ['-static'],
   )
   ```

3. Add the following to `build.sh`:  

   ```bash
   mkdir -p dist/<game>/<revision>
   cp build/<dllmain_game_revision>.dll dist/<game>/<revision>/bngrw.dll
   ```

   example:  

   ```meson
   mkdir -p dist/w6w/3.05.03
   cp build/bngrw_w6w_30503.dll dist/w6w/3.05.03/bngrw.dll
   ```

## Special thanks

- [OpenBanapass](https://github.com/ProjectAsakura/OpenBanapass)  
- [WanganArcadeLoader](https://github.com/vixen256/WanganArcadeLoader)  