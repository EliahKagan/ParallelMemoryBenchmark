# Building

64-bit builds are recommended.

Release builds (that is, building with optimizations turned on and debug
symbols not emitted) are highly recommended. This is because the timings are of
only slight interest otherwise, and completely irrelevant for comparison
purposes.

Library dependencies are resolved with
[`vcpkg`](https://vcpkg.io/en/getting-started.html), which is set up as a
submodule.

## Preliminary: Clone repository and submodule

This clones the repository and the `vcpkg` submodule, and enters the directory:

```sh
git clone --recurse-submodules https://github.com/EliahKagan/ParallelMemoryBenchmark.git
cd ParallelMemoryBenchmark
```

Or if you have already cloned the repository without the submodule:

```sh
cd ParallelMemoryBenchmark
git submodule update --init
```

As an alternative to cloning the repository, you can use the [dev
container](https://docs.github.com/en/codespaces/setting-up-your-project-for-codespaces/adding-a-dev-container-configuration/introduction-to-dev-containers),
which is set up to automatically clone the submodule the first time you run it.

## Way 1: On all systems: Use an IDE

[Visual Studio Code](https://code.visualstudio.com/) with the [CMake
Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)
extension should build the software with minimal ceremony on both Unix-like
systems and Windows systems. Make sure set the build variant to “Release”
(unless you specifically want a debug build). You can press
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd> and start typing the word
“variant” to find the action.

If you are missing tools, VS Code should tell you.

On Windows, you have the additional option of [Visual
Studio](https://visualstudio.microsoft.com/#vs-section) (which is separate from
Visual Studio Code).

Some other IDEs may work just as well, but they have not been tested.

## Way 2A: On Unix-like systems

Unless you’re using the dev container, you should make sure `cmake` and a build
toolchain are installed. For example, on Debian and Ubuntu, you could run:

```sh
sudo apt install build-essential cmake
```

Then, in the `ParallelMemoryBenchmark` directory, build `pmb` by running:

```sh
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

If you prefer `ninja` to `make`, then make sure `ninja` is installed (the
package name is often `ninja-build`) and run these commands instead of the
above:

```sh
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -G Ninja ..
ninja
```

If you have trouble finding the created `pmb` executable in `build`, check the
`Release` subdirectory.

## Way 2B: On Windows

You can follow a similar procedure to the above, with `ninja`. You can install
`ninja` with [`scoop`](https://scoop.sh/). Open a terminal in an environment
where your toolchain is available. For example, if you want to build with the
version of MSVC++ that was installed as part of Visual Studio 2022, you can
open Developer PowerShell for VS 2022. Then follow the above Ninja
instructions.

## Addendum: Windows: “Permission denied” errors

On rare occasion, on Windows, whether you use an IDE or not, running `cmake`
gives a message of this form:

```text
file RENAME failed to rename

...

because: Permission denied
```

When this happens, the following steps seem to fix it:

1. Close any editor or IDE.
2. Delete the `build` directory.
3. Delete `vcpkg` directory.
4. Restore the `vcpkg` directory by running `git submodule update --init`.

I think it is possible that antivirus software contributes to the problem, so
if you encounter it and the above steps are insufficient, you might experiment
with allow-listing the `vcpkg` directory or (if you are comfortable doing so)
even turning off active monitoring *temporarily* to investigate.
