# CONTRIBUTING.md

We welcome PRs! Bug fixes, protocol changes, implementations in new languages, etc.

Please create a PR. We will review. If approved it will be squash-merged into the master branch.
PRs must not break tests and should ideally not raise any new compile-time warnings.

By submitting a PR, you agree to license your contribution under our license.

## Run tests

Python tests require:

    pip3 install -r mypy flake8

Compiling and running the C++ tests requires a C++ compiler.

Compile and run all tests with:

    make && make test


## Releases

We use `MAJOR.MINOR` versioning to number our releases. We define "major" releases in the same way as [Semantic Versioning](https://semver.org), and use "minor" releases for anything that would be a "minor" or "patch" release under SemVer. That means we increment the:

    - MAJOR version when we make incompatible API changes
    - MINOR version when we add functionality in a backwards-compatible manner, or when we make backwards-compatible bug fixes
