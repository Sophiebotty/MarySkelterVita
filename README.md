# 🌸 MarySkelterVita

### An independent PlayStation Vita adaptation research project

Welcome to **MarySkelterVita**!

This is an independent development project exploring the technical possibility of adapting *Mary Skelter Finale* for real PlayStation Vita hardware.

The goal is to learn, experiment, and build the technical foundations needed to bring a demanding modern game experience to the Vita's hardware limitations.

This is a long-term experimental project, and there's still a lot to figure out. I'm documenting the journey as I go!

---

## 🎮 Project Goals

- Explore the technical feasibility of a Vita adaptation.
- Develop a custom Vita-compatible rendering foundation.
- Research the original game's resource and archive formats.
- Test memory usage, loading performance, and graphics capabilities.
- Build reliable tools for reading and processing game resources.
- Prioritize stable performance on actual PS Vita hardware.

## 🛠️ Current Development

| Component | Description | Status |
|---|---|---|
| Vita Renderer | Basic 3D rendering foundation | Hardware tested |
| VitaBench | Graphics and performance benchmarking | Hardware tested |
| ResProto | Resource container and compression prototype | Hardware tested |
| GDAT Proto | Experimental GDAT/CSH archive reader | Hardware tested |
| ContentFit | Scene and memory compatibility testing | Planned |

## 💻 Technology

- C
- Python
- CMake
- Shell scripting
- VitaSDK
- SDL2
- vitaGL
- zlib
- Git

## 📚 Research & Documentation

This repository includes technical research and development notes covering:

- Graphics compatibility and rendering.
- PS Vita hardware performance.
- Resource archive structures.
- Compression and decompression.
- Memory management.
- Loading and storage performance.
- Technical feasibility of a Vita adaptation.

All archive-format experiments currently use synthetic test fixtures rather than original game archives.

## 🗺️ Development Roadmap

- [x] Establish VitaSDK development environment.
- [x] Build a basic Vita rendering foundation.
- [x] Test graphics performance on real hardware.
- [x] Develop VitaBench.
- [x] Develop ResProto.
- [x] Develop GDAT Proto.
- [ ] Develop ContentFit.
- [ ] Expand hardware compatibility testing.
- [ ] Investigate authorized access to original technical resources.
- [ ] Evaluate the feasibility of a full adaptation.

## 🤖 Development Assistance

AI tools assisted with research, technical planning, code development, and debugging. The development process includes all project decisions, testing, and hardware validation.

## ⚠️ Disclaimer

MarySkelterVita is an unofficial, independent fan project.

It is not affiliated with, endorsed by, or sponsored by Idea Factory, Compile Heart, or any associated rights holders.

*Mary Skelter Finale* and its associated intellectual property belong to their respective owners. This repository does not include the original game's proprietary source code or extracted game assets.

Any future use or distribution of protected game content would require appropriate authorization.

---

## 🌸 Follow the Development

This project is a learning experience as much as it is a technical challenge.

There will be experiments, unexpected problems, improvements, and plenty of things to learn along the way.

Thanks for checking out MarySkelterVita!

**Made with curiosity, determination, and a love for the PlayStation Vita.** 💜
