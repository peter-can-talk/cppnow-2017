# C++Now 2017: clang-useful

Slides and code samples for my talk at C++Now 2017.

## Contents

- `presentation`: My beautiful LaTeX slides.
- `code`: Code samples including a dozen tools to try out.
- `server`: My online polling server.
- `Dockerfile`: A Dockerfile to build a standalone container for LLVM and clang tooling.

To run a code sample, you need to build the docker container:

```sh
$ docker build -t clang .
```

If you then `cd` into any directory, like `code/ast-dump`, you will find a
`Makefile` specifically for that tool. To compile or use the tool, enter the
docker container and mount the folder under `/home`:

```sh
$ docker run -it -v $PWD:/home clang
```

Then just use the code executable already there, or re-build it with `make`.
