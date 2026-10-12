from setuptools import setup, find_packages

setup(
    name="kernelguard",
    version="1.0.0",
    package_dir={"": "python"},
    packages=find_packages(where="python"),
    install_requires=[
        "pyyaml>=6.0",
    ],
    entry_points={
        "console_scripts": [
            "kg=kernelguard.cli:main",
        ],
    },
)
