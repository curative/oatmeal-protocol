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

To create an Arduino library release:

1. Update the version number in `library.properties`
2. Update the version number in `src/oatmeal_message.h` (`OATMEAL_LIB_VERSION_MAJOR`, `OATMEAL_LIB_VERSION_MINOR`)
3. Update the `PROJECT_NUMBER` in `docs/Doxyfile`
4. Create a release on GitHub, with tag `arduino-vX.Y` and title `Arduino lib release vX.Y`
5. Create issue at https://github.com/arduino/Arduino/issues

To create a python release:

1. Update the version number in `python/setup.py`
2. Run `cd python && make dist`
3. Test upload to test version of PyPI with: `make dist_upload_test`
4. Run upload against the real PyPI: `make dist_upload_prod`

To update the version numbers associated with the protocol version:

1. Update to top of `protocol.md`
2. Update the version number in `src/oatmeal_message.h` (`OATMEAL_PROTOCOL_VERSION_MAJOR`, `OATMEAL_PROTOCOL_VERSION_MINOR`)
3. TODO(Isaac): update protocol variable in `python/oatmeal/protocol.py`
