![teaser](https://github.com/HendrikBrueckler/C4HexMeshing/assets/38473042/f70ef0d1-e1f4-4e35-a024-ad9440baf83c)

# Collapsing Cubical Cell Complexes for Hex Meshing

`C4HexMeshing` is an implementation of [Collapsing Embedded Cell Collapses for Safer Hexahedral Meshing \[Brückler et al. 2023\]](http://graphics.cs.uos.de/papers/T-Collapsing_Bru%CC%88ckler_SA2023_Preprint.pdf) (SIGGRAPH Asia 2023) distributed under GPLv3.

If you make use of `C4HexMeshing` in your scientific work, please cite our paper. For your convenience,
you can use the following bibtex snippet:

```bibtex
    @article{C4HexMeshing,
        author     = {Hendrik Br{\"{u}}ckler and
                     Marcel Campen},
        title      = {Collapsing Embedded Cell Collapses for Safer Hexahedral Meshing},
        journal    = {ACM Trans. Graph.},
        volume     = {42},
        number     = {6},
        year       = {2023},
    }
```

***

## How does it work?

`C4HexMeshing` makes use of the [3D Motorcycle Complex](https://github.com/HendrikBrueckler/MC3D) to partition a tetrahedral mesh, equipped with a suitable seamless map, into blocks. A quantization of the cubical cell complex is then computed using [QGP3D](https://github.com/HendrikBrueckler/QGP3D), assigning non-negative integer lengths to the complex' edges.

To extract from this complex a valid integer-grid map and thus a hex mesh, all edges, patches and blocks of the complex which were quantized to zero extent are collapsed and their content redistributed among the remaining elements. On the remaining cell complex, whose cells are now blocks of strictly positive integer extents, an integer-grid map can be computed blockwise via (relatively simple) cube maps.
From this a hex mesh can be extracted.

Note, that (currently) no geometric optimization other than tentative untangling of the integer-grid map is performed, but some might be added in the future.

![Collapses](https://github.com/HendrikBrueckler/C4HexMeshing/assets/38473042/4fbb3c16-1baf-47e7-9d6b-58b7fc8555b3)

***

### Dependencies
- GMP (NOT included, must be installed on your system)
- Clp (NOT included, must be installed on your system)
- NLOPT (optional, for map optimization, NOT included, must be installed on your system)
- [MC3D](https://github.com/HendrikBrueckler/MC3D) (Included as submodule, together with all subdependencies)
- [QGP3D](https://github.com/HendrikBrueckler/QGP3D) (Included as submodule, together with all subdependencies)

### Building
In root directory

    mkdir build
    cd build
    cmake -DGUROBI_BASE=<path/to/gurobi/> ..
    make

### Usage
An example command-line application is included that reads a tetrahedral mesh including a seamless parametrization from a file in .hexex-format, as used and documented in [libHexEx](https://www.graphics.rwth-aachen.de/software/libHexEx/).
It can generate the output of several stages of the algorithm, including the original MC, collapsed MC, integer-grid map (unoptimized) and hex mesh (unoptimized).

After building, the CLI app can be found in ```build/Build/bin/cli``` .
For full information on its usage, execute

    c4hex_cli --help

Example input can be found in folder ```extern/QGP3D/extern/MC3D/tests/resources```.

### API
For details on the API of the library, check the headers in ```include```, they are thoroughly documented. Apart from that, ```cli/main.cpp``` demonstrates usage of the entire pipeline for both simple and advanced usage.

### Comments on building with Visual studio (VS) 

Sorry for not providing a detailed building process, as I'm unsure if others have the same machine configuration as mine, and I'm also uncertain whether everyone's building needs and file settings are consistent. Below are the ideas for successful building with VS for your reference, hoping it will be helpful.

First, some notes:

- My machine configuration is: Windows 10 Pro, Microsoft Visual Studio Community 2022 (64-bit)
- Because builing with Gurobi on Windows requires a proper license, which I didn't obtain successfully. So, I chose an open-source alternative, namely, using the Coin library for compilation.
- Since this project uses the C++ 17 standard, but VS doesn't strictly support this standard in some places, leading to building errors, I modified a few lines of code. You can refer to the commits of this project for the specific changes.
- Because the configuration process is quite cumbersome, and problems can arise at every step, I apologize for not having enough time to respond in detail to related issues.

The specific building ideas are as follows:

1. Clone and build the related libraries from [COIN-OR](https://github.com/coin-or), including Cbc, Clp, Cgl, CoinUtils, and Osi. These are interconnected, and they all contain VS solution files. You can build them according to your machine configuration, or you can directly download the pre-built library files.
2. git clone and build [GMP](https://github.com/gx/gmp.git) using VS.
3. git clone and build [nlopt](https://github.com/stevengj/nlopt.git) using VS.
4. git clone and build [eigen](https://gitlab.com/libeigen/eigen.git). Note that you need to clone the specified version (3.3), as higher versions may cause errors during Cmake configuration.
5. git clone [this project](https://github.com/simech-tech/C4HexMeshing), open the Windows version of CMake, and configure Cmake according to the following instructions.
6. In CMake, I removed the BUILD_SHARED_LIBS option (using static library compilation) and removed the TEST-related options.
7. In CMake, set the relevant paths for eigen, gmp, gmpxx, Cbc, Clp, Cgl, Osi, etc.
8. In CMake, after completing the above settings, click Configure, and then click Generate. Although CMake may output some error messages, as long as the VS sln file is successfully generated, it's fine.
9. Open the sln file generated by CMake. Do not use the Build-All project for building. Instead, build each project in the following order:
10. glog: No other configuration is needed, build it.
11. OpenVolumeMesh: No other configuration is needed, build it.
12. HexEx: No other configuration is needed, build it.
13. TS3D: No other configuration is needed, build it.
14. MC3D: No other configuration is needed, build it.
15. QGP3D: Set the include path of the Coin library used by this project in VS, and then build.
16. hexex_cli: No other configuration is needed, build it.
17. C4Hex: Set the include paths of the Coin library and the nlopt library used by this project in VS, and then set the required Coin library path in VS, and then build.
18. c4hex_cli / mc3d_cli / qgp3d_cli: Set the required Coin library path in VS, and then build.
19. In your CMake build path, you can see the generated library files and executable files of the above projects, which can be used.
20. When running the executable file, you need to copy the relevant DLL files to the executable file directory.

Finally, if you are not successful in building, or you do not want to bother, you can use the pre-built executable files on the project's release page.

Good luck!
