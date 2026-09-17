# KuDroid

> **KuDroid is not an emulator.**

### What is KuDroid?
KuDroid is a translation layer for ios to run android's apps

---

### How it was made
This project is **vibecoded** by AI (write the code) and me just leading it.  
*(Yeah, I vibecoded this. Please don't send me death threats)*

---

### Contributing & Pull Requests
- **Pull Requests**: I currently do not accept direct Pull Requests (PRs) to prevent breaking ongoing architectural changes and complex internal state. Sorry for any inconvenience caused!
- **Reporting Issues**: If you find bugs, crashes, or have feature suggestions, please **open an Issue** on GitHub. I will investigate and fix it directly.
- **Forks**: You are completely free to **fork** the repository and experiment with your own modifications!

---

### Key Technical Highlights
- **KuART (KuDroid Android Runtime)**: Standalone, lightweight DEX bytecode interpreter tailored for Apple Silicon (ARM64), reading directly from `.apk`.
- **Native Metal Graphics**: Bridges Android Surface / OpenGL ES 3.0 via ANGLE directly to Apple's CAMetalLayer for best rendering.
- **Low-Latency Audio**: Seamless translation from Android OpenSL ES / AAudio to iOS CoreAudio (AudioToolbox).
- **Bionic Translation Shim**: Re-links Android ELF shared libraries (`.so`) to Mach-O POSIX syscalls on Darwin.

---
### For iOS 26,27
- Attract universal.js script in stikdebug to make JIT work again

---

### Credits & Acknowledgements
- **Special thanks to `sakayorii`** 

Note:please enable JIT 
