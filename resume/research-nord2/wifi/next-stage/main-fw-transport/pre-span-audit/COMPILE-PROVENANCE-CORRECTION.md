# Private compile provenance correction

Earlier failure-drain and related private-candidate statements saying normal CI-derived ARM64 compilation mean a **modified local command derived from CI**. The saved argv adds bsd/dev and private include directories and substitutes a private source path. It is not the exact prior-CI invocation and must not be cited as proof of the normal source-sibling include environment. It does use the selected board configuration without forcing NORD2, and its actual argv/result remain saved. This correction does not alter frozen source files, manifests or historical outputs.

Parent separately compiles the actual generated source using the exact `wifi-main-state/focused/sweet_touch.c.json` invocation, with normal sibling includes and no added bsd/dev path. That separate result is the authoritative normal-context compile evidence when complete. This agent did not run or claim that exact compile.
