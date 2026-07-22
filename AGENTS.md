# Project Agent Guidance

## Modification Markers

- Low-level/framework source changes in this project should be wrapped with modification markers.
- Demo-only changes do not need modification markers.
- Default marker format:

```cpp
//Modify Begin:YYYY-MM-DD by BestHui
// Modified code...
//Modify End
```

- For file types where `//` is not a valid comment syntax, use the native comment marker while preserving the same text, for example CMake:

```cmake
# Modify Begin:YYYY-MM-DD by BestHui
# Modified code...
# Modify End
```

- Use the current calendar date for `YYYY-MM-DD`.

## Existing API First

- Before implementing renderer features, first search the existing project APIs under `Framework`, `DX12Library`, `RenderGraph`, and nearby demos.
- Prefer existing wrappers and data structures over creating duplicate demo-local abstractions.
- This is especially important for lights, materials, command list helpers, upload buffers, shader/resource binding, render passes, and RenderGraph resources.
- Demo code may define GPU packing structures when shader layout requires it, but scene-level concepts should reuse Framework-level types where available.
