# Oatmeal Protocol: Python library

Requires python 3.5 or higher.

## Installation

Install dependencies:

    pip3 install -Ur requirements.txt

Then copy into your python path or create a relative soft symlink with:

    ln -rs <YOUR_PYTHON_LIBS_DIR>/oatmeal oatmeal

(Note: requires `gln` on mac).

## Tests

Run tests with:

    make test

## API docs

We use sphinx to auto-generate docs from our python docstrings. Our docstrings
are formatted according to the [Google Style](https://sphinxcontrib-napoleon.readthedocs.io/en/latest/example_google.html).

Generate API docs with:

    pip3 install -Ur requirements.txt
    cd docs && make clean && make html

Generated docs are then in `docs/_build/html/index.html`.
