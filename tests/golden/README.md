# Golden outputs

`tests/integration_tests.cpp` runs the whole pipeline over the sample projects in
`examples/projects/CheatMenu` and `examples/projects/ToolDock`: load, validate, serialize,
interpret headlessly (design and runtime mode), generate code, compile it, merge scripted edits
back, talk over a TCP loopback live link, amalgamate the client header and run the version
matrix. So far every check is structural (determinism, clean compiles, idempotence, ids
present). No expected files are checked in yet.

## Freezing golden files

Once codegen output settles, put it here as `tests/golden/<Project>/<file>` and compare it
byte for byte in a new `TEST_CASE`. The determinism tests already write exactly the files you'd
freeze (same `codegen::OptionsFromProject` and the project's `types.json`), so regenerating is:

```
./integration_tests -tc="*GenerateAll succeeds and is deterministic"
rm -rf tests/golden/CheatMenu tests/golden/ToolDock
cp -r <build dir>/integration_tmp/CheatMenu_gen tests/golden/CheatMenu
cp -r <build dir>/integration_tmp/ToolDock_gen tests/golden/ToolDock
```

The round-trip test leaves `integration_tmp/roundtrip/MainMenu.{fresh,edited,merged,merged2,regenerated_carried}.cpp`;
freeze `MainMenu.edited.cpp` and `MainMenu.merged.cpp` the same way if you want a merge golden.
Look at the diff of `tests/golden/` before committing. Generated files have no timestamps, so
any diff is a real codegen or merge change.

## Scratch output

The tests write generated code, stub headers, amalgamated headers and compiler logs to
`<build dir>/integration_tmp/` (`IMFORGE_BINARY_DIR`, set by `imforge_add_test`), or to the
system temp directory when built by hand. Each test case starts in a fresh subdirectory.

## Environment

| Variable | Meaning | Default |
|---|---|---|
| `IMFORGE_IMGUI_DEPS` | directory with `imgui-v1.85`, `imgui-v1.91.9b`, ... | `$HOME/deps` |
| `IMFORGE_CXX` | compiler for the syntax checks of generated code | `g++` |
| `IMFORGE_PYTHON` | interpreter for `tools/*.py` | `python3` (`python` on Windows) |

Tests that need a compiler, Python or the pinned Dear ImGui checkouts print `SKIP` and pass
when the tool is missing (for example in an MSVC build on Windows).
