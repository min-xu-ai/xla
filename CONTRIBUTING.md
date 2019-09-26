## C++ Style Guide

`pytorch/xla` uses `clang-format-7` with a customized style config.
If your PR touches the C++ source files, please run the following command before submmiting a PR.

```Shell
# If your PR only changes foo.cpp, run the following in xla/ folder
clang-format-7 -i -style /PATH/TO/foo.cpp
# To format all cpp files, run the follwoing in xla/ folder
find -name '*.cpp' -o -name '*.h' | xargs clang-format-7 -i -style=file
```

## Python Style Guide

`pytorch/xla` uses `yapf` with a customized style config.
If your PR touches the Python source files, please run the following command before submmiting a PR.

```Shell
#TODO:
```

## Building Manually

To build from source:

* Clone the _PyTorch_ repo as per [instructions](https://github.com/pytorch/pytorch#from-source).

  ```Shell
  git clone --recursive https://github.com/pytorch/pytorch
  cd pytorch/
  ```

* Clone the _PyTorch/XLA_ repo:

  ```Shell
  git clone --recursive https://github.com/pytorch/xla.git
  ```

## Building Docker Image

* We provide a Dockerfile in `docker/` that you can use to build images as the
  following:

  ```Shell
  docker build -t torch-xla -f docker/Dockerfile .
  ```

## Building With Script

* To build and install `torch` and `torch_xla`:

  ```Shell
  xla/scripts/build_torch_wheels.sh
  ```

## Build From Source

* Apply PyTorch patches:

  ```Shell
  xla/scripts/apply_patches.sh
  ```

* Install the Lark parser used for automatic code generation:

  ```Shell
  pip install lark-parser
  ```

* Currently _PyTorch_ does not build with _GCC_ 6.x, 7.x, and 8.x (various kind of ICEs). _CLANG_ 7.x is known to be working, so install that in your VM:

  ```Shell
  sudo apt-get install clang-7 clang++-7
  export CC=clang-7 CXX=clang++-7
  ```

  You may need to add the following line to your _/etc/apt/sources.list_ file:

  ```Shell
  deb http://deb.debian.org/debian/ testing main
  ```

  And run the following command before trying again to install _CLANG_:

  ```Shell
  sudo apt-get update
  ```

* Build _PyTorch_ from source following the regular [instructions](https://github.com/pytorch/pytorch#from-source).

  ```Shell
  python setup.py install
  ```

* Install Bazel following the [instructions](https://docs.bazel.build/versions/master/install.html). You should only install version 0.24.1, as no older nor newer releases will be able to build the required dependencies.

* Build the _PyTorch/XLA_ source:

  ```Shell
  cd xla/
  python setup.py install
  ```


