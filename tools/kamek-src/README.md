# Kamek linker (Pulsar fork)

Source of the Kamek linker as modified by Pulsar to write the combined
four-region `Code.pul` that Pulsar's `Loader.pul` reads.

- Copied unchanged from [MelgMKW/Pulsar](https://github.com/MelgMKW/Pulsar)
  `KamekLinker/` at commit `820ad929c3c7141a0396692d8b0896d1546240fd`, except `Kamek.csproj`
  (adds `RollForward` so it runs on newer .NET, drops trimming).
- Kamek by Treeki/Ash Wolf and contributors, Pulsar changes by MelgMKW;
  both MIT, see `../../third_party_licenses/`.

`build.sh` compiles this into `tools/kamek/` the first time it runs.
