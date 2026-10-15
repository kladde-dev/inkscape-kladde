Inkscape without a save button
==============================

This is a fork of [Inkscape](https://gitlab.com/inkscape/inkscape) that has no need for a save button: every change to a drawing is durable the moment it is made.
It demonstrates [kladde](https://kladde-dev.github.io/), a file format and library for durable data types, in a real application.

> **This is a demo, not a production-ready app.**
> kladde's file format is not final, and neither is this fork's way of storing drawings in it.
> Don't keep drawings you care about only in `.kladde` files.

## What is different from Inkscape

A drawing lives in a `.kladde` file, which Inkscape keeps up to date as you edit.
Each change to the document's XML tree — a drag, a style change, a reordering, a new or deleted object — is written to the file as a few bytes in a journal, so a crash, a power cut or closing the window loses nothing.
From time to time, while you pause, the journal is folded into the file's pages.
Neither step ever rewrites the whole drawing, so the cost of an edit does not grow with the size of the drawing.

- **File ▸ New** asks where the new drawing's `.kladde` file goes.
- **Opening a `.kladde` file** continues where the last session left off.
- **Opening an SVG (or any other format Inkscape reads)** imports it into a new `.kladde` file next to it, and leaves the original file untouched.
- **There is no Save** in the File menu or the commands bar; Ctrl+S still works and only makes sure that everything is folded into the file's pages.
  **File ▸ Save As** and **Save a Copy** still write SVG, to export the drawing.

The drawing is stored in a schema of kladde's durable data types: elements with their attributes and children, mirroring Inkscape's XML tree.
The branch `main` stores attribute values as typed values wherever they format back exactly (numbers, lengths, colours, transforms, path data, style declarations); the branch `untyped` stores every attribute value as a string.
How the two compare, and how the fork performs on real drawings, is described in [the evaluation of kladde's C++ port](https://kladde-dev.github.io/evaluation/cpp-port/).
kladde's C++ library is a submodule, in [`src/3rdparty/kladde-cpp`](https://github.com/kladde-dev/kladde-cpp); the fork's own code is in [`src/kladde-io`](src/kladde-io).

## Compiling

These instructions are for Linux, and were tested on Ubuntu 24.04 with GCC 13.3 and Clang 18.
kladde-cpp needs a C++20 compiler at least as recent as these.

1. Get the source, with its submodules (among them kladde-cpp):

   ```sh
   git clone --recurse-submodules https://github.com/kladde-dev/inkscape-kladde.git
   cd inkscape-kladde
   ```

   Add `--branch untyped` to `git clone` for the branch that stores attribute values as strings.

2. Install Inkscape's dependencies, with Inkscape's script for common distributions (Ubuntu, Debian, Fedora, Arch and others); kladde adds none:

   ```sh
   wget https://gitlab.com/inkscape/inkscape-ci-docker/-/raw/master/install_dependencies.sh -O install_dependencies.sh
   bash install_dependencies.sh --recommended
   ```

3. Configure, build and install into the build directory:

   ```sh
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PWD/build/install_dir"
   ninja -C build install
   ```

4. Run it:

   ```sh
   ./build/install_dir/bin/inkscape
   ```

### Tests and measurements

`test_kladde-backing` checks that a drawing reopens as it was left, after closing, after a crash, and after edits in a reopened session.
`test_kladde-backing-bench` imports real drawings, makes 10 000 edits to each as a drawing program would, and prints what they cost; it runs only with `KLADDE_SVG_CORPUS` set to a directory of SVG files, such as the one that [kladde-rs](https://github.com/kladde-dev/kladde-rs) fetches:

```sh
ninja -C build test_kladde-backing test_kladde-backing-bench
./build/bin/test_kladde-backing
git clone https://github.com/kladde-dev/kladde-rs.git ../kladde-rs
sh ../kladde-rs/crates/kladde-svg/corpus/fetch.sh
KLADDE_SVG_CORPUS=../kladde-rs/crates/kladde-svg/corpus/drawings ./build/bin/test_kladde-backing-bench
```

`KLADDE_BENCH_STEPS` sets the number of edits, and `KLADDE_JOURNAL_BUDGET_PAGES` fixes the journal's budget in pages instead of letting kladde size it to the drawing.

## Everything else

For everything about Inkscape itself — its features, its documentation, and how to report bugs in it — see [inkscape.org](https://inkscape.org/) and [Inkscape's repository](https://gitlab.com/inkscape/inkscape).
Inkscape is free software under the GNU General Public License, version 2 or later; so is this fork.
