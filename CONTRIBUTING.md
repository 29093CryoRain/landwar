# Contributing

## Before Opening a Change

- Use C++20 and keep project targets warning-free under `-Wall -Wextra`.
- Keep runtime assets in `data/` and generated files in `userdata/`.
- Put temporary tools in `tools/`; do not add local build output or machine
  configuration to a commit.
- For configuration changes, update `Config`, its JSONC data, the matching
  schema, `.docs/配置说明.md`, and focused tests together.
- Faction IDs are zero-based and continuous. ID 0 is neutral; at most 63
  playable factions are supported by the fixed simulation tables.

## Local Checks

```bash
cmake --preset default
cmake --build --preset release
ctest --preset release --output-on-failure
./build-release/landwar.exe --validate-config
./build-release/landwar.exe --headless --seed 42 --ticks 1000 --summary
```

Describe behavior changes, configuration changes, and the checks you ran in
the pull request. For rendering or UI changes, include a screenshot when it
helps reviewers reproduce the result.
