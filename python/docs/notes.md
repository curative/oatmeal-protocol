# Sphinx

Sphinx was set up by running sphinx-quickstart and editing `index.rst`.


To make sphinx convert docstrings to html pages, you either have to use automodule
by adding to the following to `index.rst`:

    .. automodule:: oatmeal
       :members:
       :show-inheritance:
       :inherited-members:


Or run `sphinx-apidoc -f -o . ..` and add the following to `index.rst`:

    .. include:: oatmeal.rst

We're using automodule for now.
