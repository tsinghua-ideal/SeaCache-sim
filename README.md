# SeaCache-sim
This is the source code repository of the MICRO'25 paper *SeaCache: Efficient and Adaptive Caching for Sparse Accelerators*.

## Build

```bash
$ g++ -std=c++17 -O3 -march=native src/config.cpp src/data.cpp src/estimation.cpp src/parameters.cpp src/util.cpp src/statistics.cpp src/cache.cpp src/dynamic.cpp src/simulator.cpp src/queue.cpp src/memory.cpp src/pe.cpp src/prefetch.cpp src/main.cpp -o scache
```

## Workload
The scheduler and simulator accept sparse matrices from MatrixMarket (.mtx). The folder containing these matrices is under `data`.

## Run
The following command simulates multiplication of `matrix1` and `matrix2` with the configuration specified in `config/config.json`:
```bash
$ ./scache matrix1 matrix2 config/config.json
```
filter3D command:
```
./scache filter3D filter3D config/config.json
```

Here is a sample json configuration:
```json
{
    "transpose": 0,
    "cachesize": 2,
    "memorybandwidth": 68,
    "PEcnt": 32,
    "srambank": 32,
    "baselinetest": 0,
    "condensedOP": false,
    "tileDir": "./tiles/",
    "outputDir": "./output/"
}
```
- "condensedOP": When set to true, it uses the condensed OP dataflow instead of the default Gustavson's dataflow.
- "tileDir": Represents the directory containing the tiling selection for each matrix.

## Code description

The code shares the same base simulator as the previous work, [HYTE](https://github.com/tsinghua-ideal/HYTE-sim ""). However, this work shifts the focus from tile selection to cache optimization, with the pre-defined tiling selection located in the "tileDir" directory. The modifications primarily involve various cache schemes and prefetching techniques.

The changes are mainly found in the `cache.cpp` and `simulator.cpp` files. The proposed mapping scheme from Section 4.1 of the paper, along with the baseline mapping schemes, are implemented within different branches of the `cacheAccessFiber()` function in `cache.cpp`. The corresponding replacement policies, as described in Section 4.2, are invoked by the different cache schemes. For the guided replacement policies, the prefetch logic and maintenance of the prefetched metadata are implemented in the `prefetchRow()` function, which is iterated during simulation in `simulator.cpp`. The adaptive prefetch size introduced in Section 4.3 is also implemented in `simulator.cpp` and is called during the calculation process.


## Reference

If you use this tool in your research, please kindly cite the following paper.

Xintong Li, Jinchen Jiang, and Mingyu Gao. *SeaCache: Efficient and Adaptive Caching for Sparse Accelerators*. In Proceedings of the 58th Annual IEEE/ACM International Symposium on Microarchitecture (MICRO) , 2025.
