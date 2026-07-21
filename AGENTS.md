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
