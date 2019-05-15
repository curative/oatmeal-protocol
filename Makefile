all: keywords.txt
	cd tests && $(MAKE) all
	cd docs && $(MAKE) all
	cd python && $(MAKE) all
	cd examples && $(MAKE) all

clean:
	cd tests && $(MAKE) clean
	cd docs && $(MAKE) clean
	cd python && $(MAKE) clean
	cd examples && $(MAKE) clean

test: mypy flake8
	cd tests && $(MAKE) test
	cd python && $(MAKE) test

docs:
	cd docs && $(MAKE) docs
	cd python && $(MAKE) docs
	@echo "C++ docs: docs/docs/html/index.html"
	@echo "Python docs: python/docs/_build/html/index.html"


keywords.txt: $(ARDUINO_FILES)
	arduino-keywords .


mypy:
	./run_mypy.sh

flake8:
	flake8

.PHONY: all clean test docs mypy flake8
