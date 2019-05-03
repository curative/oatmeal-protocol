
all: docs

clean:
	cd docs && $(MAKE) clean
	rm -rf dist build oatmeal_*.egg-info oatmeal/__pycache__

test:
	cd tests && ./run_tests.py

docs:
	cd docs && $(MAKE) html

dist:
	python3 setup.py sdist bdist_wheel

dist_upload_test: dist
	@echo "Uploading to the test PyPI"
	python3 -m twine upload --repository-url https://test.pypi.org/legacy/ dist/*

dist_upload_prod: dist
	@echo "Uploading to the LIVE PyPI"
	python3 -m twine upload dist/*

.PHONY: all clean test docs dist dist_upload_test dist_upload_prod
