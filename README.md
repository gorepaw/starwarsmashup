# Star Wars Mashup

A non-commercial Star Wars fangame: classic Battlefront II (2005) third-person combat spilling out across the open planets of Star Wars Galaxies, with MMO-depth customization, player housing, and SWG-scale professions with transparent systems.

Built on Godot 4 (custom double-precision build) with gameplay in C++.

**Status:** Planning. No playable code yet.

## No game assets are included

This repository contains **no** assets from Star Wars Battlefront II or Star Wars Galaxies, and never will. The project will ship an extractor that reads assets from **your own legally owned installs** of those games. Pull requests containing extracted or original game assets will be rejected.

The extractor only uses files that match the original SOE/LucasArts releases byte-for-byte (see [Supported game versions](docs/ARCHITECTURE.md#supported-game-versions)). Work made by other fan projects, such as emulator servers' custom content or HD texture packs, is never extracted without that team's permission.

This project does not use, and will not accept contributions derived from, any leaked Star Wars Galaxies source code.

## Docs

- [Design](docs/DESIGN.md): pillars, combat, professions, housing, economy
- [Architecture](docs/ARCHITECTURE.md): engine, code layering, networking, persistence, asset pipeline
- [Phase 0](docs/PHASE0.md): feasibility spikes and task list
- [Roadmap](docs/ROADMAP.md): battles first (Stage 1), then weapons and loadouts (Stage 2), then world systems

## Legal

The project's source code is licensed under the [MIT License](LICENSE). That license covers only code written for this project. It grants nothing for any Star Wars, Battlefront, or Galaxies asset or trademark.

Star Wars and all related properties are trademarks of Lucasfilm Ltd. / Disney. Star Wars Battlefront II and Star Wars Galaxies are properties of their respective owners. This is an unofficial, non-commercial fan project and is not affiliated with or endorsed by Lucasfilm, Disney, Electronic Arts, Sony Online Entertainment, Daybreak, or Pandemic Studios.
