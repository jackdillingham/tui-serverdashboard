# ServerDash

ServerDash is a C++ terminal application built with FTXUI.

## Build

Requirements: CMake 3.20 or newer, a C++17 compiler, Git, and network access
on the first configure (CMake fetches FTXUI v7.0.3). On Windows with GCC from
MSYS2 UCRT64, make sure `C:\msys64\ucrt64\bin` is on `PATH`.

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
.\build\serverdash.exe
```
Press `q` or Escape to exit.
