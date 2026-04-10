# UM EECS570 WN26 Project Repository
This is the repository for our EECS570 project. Our report will be added eventually. This project is forked from SeaCache-sim, and the README below largely follows from theirs.

## SeaCache-sim
This is the source code repository of the MICRO'25 paper *SeaCache: Efficient and Adaptive Caching for Sparse Accelerators*.

### Build

```bash
$ make install
```

### Workload
The scheduler and simulator accept sparse matrices from MatrixMarket (.mtx). The folder containing these matrices is under `data`.

### Run
The following command simulates multiplication of `matrix1` and `matrix2` with the configuration specified in `config/config.json`:
```bash
$ ./scache matrix1 matrix2 config/config.json
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


### scripts
Check inputs exist (scache, tile files, .mtx files).
Generate config JSON files for a sweep.
Run ./scache matrix matrix config.json.
Parse output .txt files into one CSV.

### Code description

The code shares the same base simulator as the previous work, [HYTE](https://github.com/tsinghua-ideal/HYTE-sim ""). However, this work shifts the focus from tile selection to cache optimization, with the pre-defined tiling selection located in the "tileDir" directory. The modifications primarily involve various cache schemes and prefetching techniques.

The changes are mainly found in the `cache.cpp` and `simulator.cpp` files. The proposed mapping scheme from Section 4.1 of the paper, along with the baseline mapping schemes, are implemented within different branches of the `cacheAccessFiber()` function in `cache.cpp`. The corresponding replacement policies, as described in Section 4.2, are invoked by the different cache schemes. For the guided replacement policies, the prefetch logic and maintenance of the prefetched metadata are implemented in the `prefetchRow()` function, which is iterated during simulation in `simulator.cpp`. The adaptive prefetch size introduced in Section 4.3 is also implemented in `simulator.cpp` and is called during the calculation process.


### Reference

If you use this tool in your research, please kindly cite the following paper.

Xintong Li, Jinchen Jiang, and Mingyu Gao. *SeaCache: Efficient and Adaptive Caching for Sparse Accelerators*. In Proceedings of the 58th Annual IEEE/ACM International Symposium on Microarchitecture (MICRO) , 2025.
