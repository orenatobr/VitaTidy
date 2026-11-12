# VitaTidy

A PS Vita homebrew app that does two chores on the console itself, with no FTP and no computer:

1. **Reorder the home screen.** CFW apps go on their own page, games follow A to Z (articles ignored, series kept together).
2. **Put a green check on a finished game**, for Vita games and for PSP games launched from Adrenaline.

It also reorders the Adrenaline game list, which sorts by file date.

## Install

You need a Vita with custom firmware that allows unsafe homebrew.

1. Download `VitaTidy.vpk` from the [latest release](https://github.com/orenatobr/VitaTidy/releases/latest).
2. Copy it to the Vita and install it with VitaShell. Accept the extended permissions prompt: the app has to write the LiveArea database.
3. Open **VitaTidy** from the home screen.

## Use

| Menu item | What it does |
| --- | --- |
| Reorder LiveArea | Shows the new order, asks to confirm, backs up `app.db`, rewrites it and offers a restart. |
| Reorder PSP games | Rewrites the dates of the files in `ux0:pspemu/ISO` one minute apart so Adrenaline lists them in order. |
| Mark Vita game | Toggles the green check on a game's bubble icon. |
| Mark PSP game | Toggles the green check on the icon stored inside an ISO. |

Controls: D-pad or left stick to move, L and R to jump a page, X to act, O to go back, Start to exit. Touch works too: tap a row, tap it again to toggle.

The Vita LiveArea reads its database at boot, so restart after reordering. For a Vita check mark, restart or refresh the LiveArea.

## How the order is decided

Apps listed in an order file keep that order. Anything else is placed alphabetically among them, ignoring a leading "The", "A" or "An". Unlisted apps whose id does not look like a game (`PCSE00120`, `NPNA00012`) go to the CFW page.

The release ships the author's lists as defaults (`data/order.txt`, `data/psp-order.txt`). To use your own, copy them to `ux0:data/VitaTidy/` and edit:

```
[cfw]
VITASHELL   # text after a hash is a comment
[games]
PCSE00120
```

The PSP file is one ISO file name per line, first line = newest.

## Safety

- The first `app.db` is saved once as `ux0:data/VitaTidy/backup/app.db.orig`. Before each change the current one is saved as `app.db.last`.
- The new database is built in memory, checked with `PRAGMA integrity_check`, and only then written. If the write fails, the previous file is put back.
- To undo a reorder: copy `backup/app.db.orig` (or `.last`) over `ur0:shell/db/app.db` with VitaShell, then restart.
- Original icons are saved in `ux0:data/VitaTidy/originals/`. Toggling again restores them byte for byte.
- A PSP icon is rewritten inside its original byte range, so the rest of the ISO is untouched. If the badged icon cannot fit, nothing is written.

## Troubleshooting

- **Error `0x80101130` while installing.** Version 1.0.0 used the title id `VTIDY0001`. The installer wants four letters and five digits, so it was renamed to `VTDY00001` in 1.0.1. If you installed 1.0.0 before, nothing needs cleaning up.
- **Error `0x8010113D` while installing.** The installer only accepts PNGs in `sce_sys` that are exactly 8-bit indexed: 24-bit is rejected, and so is a lower bit depth (PIL picks one on its own when `optimize` is on). `tools/make_assets.py` now forces 8 bits.

## Limits

- LiveArea folders are not supported. The reorder stops with a message if it finds one.
- Only `.iso` files can be marked. `.cso` files are reordered but cannot be marked.
- The reorder follows the layout used by the author's tooling: system pages stay as they are, user pages are rebuilt after them, 10 icons per page.

## Build

With Docker (the official VitaSDK image):

```sh
docker run --rm -v "$PWD":/src -w /src vitasdk/vitasdk:latest sh -c \
  'cmake -S . -B build-vita -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake -DCMAKE_BUILD_TYPE=Release \
   && cmake --build build-vita -j4'
# result: build-vita/vitatidy.vpk
```

The logic lives in `src/core` and has no console dependency, so it is tested on a computer:

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

The tests build a small ISO, a LiveArea database and icons, then run every task on them under ASan and UBSan, including mutated PNG and ISO inputs. `tools/` has small command line helpers for trying the tasks on copies of real files.

`tools/make_assets.py` regenerates the icon and LiveArea art (needs Pillow).

## License

MIT. Bundled code is listed in [THIRD_PARTY.md](THIRD_PARTY.md).
