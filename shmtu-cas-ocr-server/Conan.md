# Conan

## CMake

```bash
-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="conan_provider.cmake"
```

## Search

```bash
conan search poco --remote=conancenter
```

## Issues

### Invalid: 'settings.compiler' value not defined

```bash
conan profile detect
```

or

Force Detect

```bash
conan profile detect --force
```

```bash
vim ~/.conan2/profiles/default
```

For Example(macOS):

You can use `clang --version` to see the version of Apple Clang.
By the way, you can use `conan profile show default` to see the default profile.

```
[settings] 
arch=x86_64 
build_type=Release 
os=Macos 
compiler=apple-clang 
compiler.version=15.0 
compiler.libcxx=libc++ 
```
