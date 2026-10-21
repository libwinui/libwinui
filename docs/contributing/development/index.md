# Development

Building, the conventions, tests and these docs.

## The Pages

- [Building](building.md): what you need, how to configure, build and test, the CMake options, the example apps, and formatting.
- [Conventions](conventions.md): the C++ rules the codebase follows. File layout, includes, naming, ownership, handlers, comments and formatting.
- [Write Tests](write-tests.md): the one Catch2 binary, its tags, `TestRenderer`, the headless host, and the two kinds of golden files.
- [Documentation](docs.md): how this site is built, how the reference is generated from the headers, how tutorial code stays compilable, and the comment rules that go with it.
- [API Stability](api-stability.md): what a release promises about the API, and the rules a change follows to keep that promise.

The rules a library *user* meets (spec field order, handler capture, the `build()` order) are on the reference side, in [Conventions](../../reference/conventions.md).
