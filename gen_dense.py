import random

def write_dense_mtx(path, n, m, density=1.0, seed=0):
    random.seed(seed)
    nnz = int(n*m*density)

    with open(path, "w") as f:
        f.write("%%MatrixMarket matrix coordinate real general\n")
        f.write(f"{n} {m} {nnz}\n")

        # density=1.0 就是全 dense
        for i in range(1, n+1):
            for j in range(1, m+1):
                if density == 1.0 or random.random() < density:
                    f.write(f"{i} {j} 1.0\n")

if __name__ == "__main__":
    # 先做一个 1024x1024 的 dense（nnz=1,048,576）
    write_dense_mtx("data/dense1024.mtx", 1024, 1024, density=1.0)
    # 再做一个 10% 稠密的（更像 semi-dense）
    write_dense_mtx("data/semi1024_10p.mtx", 1024, 1024, density=0.1, seed=1)
    print("done")