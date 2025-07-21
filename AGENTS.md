## Build Commands
- **Do not build this project. just ask the user to**

## Code Style

- **Language:** C++17
- **Formatting:** Follow existing code style.
- **Types:** Use `fix15` for audio processing. Minimize floating-point operations in the audio thread.
- **Naming Conventions:** Follow existing conventions (e.g., `AudioModule`, `g_synth_parameters`).
- **Error Handling:** The system has real-time constraints. Avoid blocking operations and dynamic memory allocation in the audio thread.
- **Architecture:**
    - Dual-core design: Core 0 for control, Core 1 for audio.
    - Audio modules inherit from `AudioModule`.
    - Parameters are managed in `ParameterStore.h`.
