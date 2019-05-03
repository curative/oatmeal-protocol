import setuptools

with open("README.md", "r") as fh:
    long_description = fh.read()

setuptools.setup(
    name="oatmeal",
    version="1.0",
    author="Shield Dx and Oatmeal Protocol Authors",
    author_email="turner.isaac@gmail.com",
    description="A protocol to control and communicate with Arduino devices from Python.",
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
