# Tests

Logic tests that run on Linux, no Windows needed:

```bash
./tests/check.sh   # syntax-check every source against stand-in headers
./tests/run.sh     # build src/banapass.cpp against a Win32 emulation and run the script
```

`run.sh` is timing based (key presses, auto tap delays, config reload) and takes about a
minute. It covers card.ini handling, the tap triggers, focus gating, auto tap modes, live config
reload, logging, the callback pointer check and the BngRw return values.

These tests do not replace trying the DLL in the game: the Windows API behaviour, XInput, MinHook
and the per-revision patch offsets are not exercised.
