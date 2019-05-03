import setuptools

with open("README.md", "r") as fh:
    long_description = fh.read()

setuptools.setup(
    name="oatmeal-USERNAME",
    version="1.0",
    author="Isaac Turner",
    author_email="turner.isaac@gmail.com",
    description="A simple mechanism to autoconnect and control Arduino devices from Python.",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/shielddx/oatmeal-protocol",
    packages=setuptools.find_packages(),
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: Apache Software License",
        "Operating System :: OS Independent",
        "Topic :: Software Development :: Embedded Systems",
        "Topic :: Software Development :: Object Brokering",
        "Topic :: Terminals :: Serial",
    ],
)
