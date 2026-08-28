# AGENTS.md

Keep this project small, readable, and easy to change.

## Guidance

- Prefer simple C over clever abstractions.
- Follow the existing module boundaries unless a change clearly improves readability.
- Prefer small, established libraries over homemade protocol or DSP machinery when they reduce code.
- Keep ownership, lifetimes, and threading behavior explicit.
- Avoid broad refactors while making focused fixes.
- Add comments only when they explain intent, invariants, or non-obvious DSP choices.
- Use the repository clang-format style for C code.
- Build after C/CMake/dependency changes when practical.
- Treat `README.md` as the public project overview and `TODO.md` as a lightweight idea list.
