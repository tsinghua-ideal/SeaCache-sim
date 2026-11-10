#include "cache.h"
#include "estimation.h"
#include "headers.h"
#include "json.hpp"
#include "simulator.h"
#include "memory.h"
#include <fstream>

using json = nlohmann::json;

int main(int argc, char *argv[]) {

  std::string matrix_name1 = argv[1];
  std::string matrix_name2 = argv[2];
  std::string config_file = argv[3];

  std::ifstream file(config_file);
  if (!file.is_open()) {
    std::cerr << "Error opening config file." << std::endl;
    return 1;
  }

  json config;
  file >> config;

  dataflow = Gust;
  format = RR;
  int transpose = config["transpose"].get<int>();
  minBlock = 2;
  float tmpsram = config["cachesize"].get<float>();
  cachesize = tmpsram * 262144 * 0.9; // pran: what are these numbers? A: cache size in terms of 4-byte words, and only 90% is usable, rest is for metadata and tags?
  inputcachesize = cachesize;
  float tmpbandw = config["memorybandwidth"].get<float>();
  HBMbandwidth = (tmpbandw / 4.0) * 0.6; // pran: tmpbandw is I guess in bytes per cycle, and HBMbandwidth converts to words per cycle, and only 60% of that is assumed to be available for use
  int tmpPE = config["PEcnt"].get<int>();
  PEcnt = tmpPE;
  mergecnt = tmpPE; // pran: number of merge units that combine partial results into rows of C
  HBMbandwidthperPE = HBMbandwidth / PEcnt;
  int tmpbank = config["srambank"].get<int>();
  sramBank = tmpbank;
  // event timing settings, defaults are used if they're not in the config file
  dramChannels = config.value("dramChannels", 4);
  dramLatency = config.value("dramLatency", 100);
  ISCACHE = 1;
  int baselinetest = config["baselinetest"].get<int>();
  bool condensedOP = config["condensedOP"].get<bool>();
  std::string tile_dir = config["tileDir"].get<std::string>();
  std::string output_dir = config["outputDir"].get<std::string>();

  if (!freopen(
          ("largedata/" + matrix_name1 + "/" + matrix_name1 + ".mtx").c_str(),
          "r", stdin)) {
    if (!freopen(("data/" + matrix_name1 + ".mtx").c_str(), "r", stdin)) {
      if (!freopen(("dense/" + matrix_name1 + ".mtx").c_str(), "r", stdin)) {
        if (!freopen(("bfs/" + matrix_name1 + ".mtx").c_str(), "r", stdin)) {
          std::cerr << "Error opening input file." << std::endl;
          return 1;
        }
      }
    }
  }

  if (!freopen((output_dir + (ISCACHE ? "C" : "_") + printDataFlow[dataflow] +
                (baselinetest ? "Base_" : "SeaCache_") +
                std::to_string(tmpsram) + "MB_" + std::to_string(tmpbandw) +
                "GBs_" + std::to_string(tmpPE) + "PEs_" +
                std::to_string(tmpbank) + "sbanks_" + "_" + matrix_name1 + "_" +
                matrix_name1 + "_" + printFormat[format] + "_" + // pran: why not matrix_name2
                (transpose ? "1" : "0") + ".txt")
                   .c_str(),
               "w", stdout)) {
    std::cerr << "Error opening output folder." << std::endl;
    return 1;
  }

  const int BUFFERSIZE = 512;
  char readbuffer[BUFFERSIZE];

  // read and ignore annotation '%' lines
  while (std::cin.getline(readbuffer, BUFFERSIZE)) {  // pran: getting dimesions and sparsity from command line??
    if (readbuffer[0] != '%') {
      break;
    }
  }

  std::sscanf(readbuffer, "%d%d%d", &N, &M, &nzA);

  if (transpose) {
    swap(N, M);
  }

  printf("Matrix A: %d x %d, number of non-zeros = %d\n", N, M, nzA);
  fflush(stdout);

  samplek = 100;
  samplep = 0.1;

  I = N;
  J = M;

  string input;

  fflush(stdout);

  for (int i = 1; i <= nzA; i++) {

    std::getline(std::cin, input);

    std::istringstream iss(input);
    std::vector<std::string> tokens;
    std::string token;

    while (iss >> token) {
      tokens.push_back(token);
    }

    int xx, yy;
    double zz, lala;

    if (tokens.size() == 2) {

      std::istringstream(tokens[0]) >> xx;
      std::istringstream(tokens[1]) >> yy;
      // std::cout << "values: " << xx << ", " << yy << std::endl;
    } else if (tokens.size() == 3) {

      std::istringstream(tokens[0]) >> xx;
      std::istringstream(tokens[1]) >> yy;
      std::istringstream(tokens[2]) >> zz;
      // std::cout << "values: " << xx << ", " << yy << ", " << zz << std::endl;
    } else if (tokens.size() == 4) {

      std::istringstream(tokens[0]) >> xx;
      std::istringstream(tokens[1]) >> yy;
      std::istringstream(tokens[2]) >> zz;
      std::istringstream(tokens[2]) >> lala;
      // std::cout << "values: " << xx << ", " << yy << ", " << zz << std::endl;
    } else {

      std::cout << "Format Incorrect! " << std::endl;
      cout << tokens.size() << endl;
      cout << i << endl << input << endl;
      return 0;
    }

    if (transpose) {
      Ac[xx].push_back(yy);
      A[yy].push_back(xx);
    } else {
      A[xx].push_back(yy);
      Ac[yy].push_back(xx);
    }
  }

  for (int i = 0; i <= I; i++) {
    sort(A[i].begin(), A[i].end());
  }
  for (int j = 0; j <= J; j++) {
    sort(Ac[j].begin(), Ac[j].end());
  }

  if (condensedOP) {
    // if use the condensed OP dataflow, need to preprocess the A matrix into
    // the condensed format first. first put the data into sparchA[], then put
    // it back to A[], and call gust dataflow
    for (int j = 0; j < J; j++) {
      for (int i = 0; i < I; i++) {
        if (A[i].size() > j) {
          sparchA[j].push_back(A[i][j]);
          sparchAi[j].push_back(i);
        }
      }
    }

    for (int j = 0; j < J; j++) {
      A[j].clear();
      for (int i = 0; i < sparchA[j].size(); i++) {
        A[j].push_back(sparchA[j][i]);
      }
    }
  }

  long long totalempty = 0;

  long long totalincache = 0;

  long long totaltagmatch48 = 0;
  long long totaltagmatch16 = 0;

  for (int i = 1; i <= I + 2; i++) {
    offsetarrayA[i] = offsetarrayA[i - 1] + A[i - 1].size();
    if (A[i - 1].size() < 48) {
      totalempty += (48 - A[i - 1].size());
    }
    totalincache += min(48, (int)A[i - 1].size());
    totaltagmatch48 += ((int)A[i - 1].size() + 47) / 48;
    totaltagmatch16 += ((int)A[i - 1].size() + 15) / 16;
  }

  printf("*** ratio of empty %lf, ratio of not empty %lf\n",
         totalempty / (I * 48.0), 1 - (totalempty / (I * 48.0)));
  printf("*** ratio of in cache %lf\n", totalincache / ((double)nzA));

  printf("** ratio tag access 48 %lf\n", I / ((double)I + totaltagmatch48));
  printf("** ratio tag access 16 %lf\n", I / ((double)I + totaltagmatch16));

  for (int i = 1; i <= J + 2; i++) {
    offsetarrayAc[i] = offsetarrayAc[i - 1] + Ac[i - 1].size();
  }

  initsample();

  sampleA();

  fclose(stdin);

  /////////////////////////// input B /////////////////////////////////

  cin.clear();
  //  freopen(("data/" + matrix_name2+".mtx").c_str(), "r", stdin);
  if (!freopen(
          ("largedata/" + matrix_name2 + "/" + matrix_name2 + ".mtx").c_str(),
          "r", stdin)) {
    if (!freopen(("data/" + matrix_name2 + ".mtx").c_str(), "r", stdin)) {
      if (!freopen(("dense/" + matrix_name2 + ".mtx").c_str(), "r", stdin)) {
        if (!freopen(("bfs/" + matrix_name2 + ".mtx").c_str(), "r", stdin)) {
          std::cerr << "Error opening input file." << std::endl;
          return 1;
        }
      }
    }
  }

  // read and ignore annotation '%' lines
  while (std::cin.getline(readbuffer, BUFFERSIZE)) {
    if (readbuffer[0] != '%') {
      break;
    }
  }

  std::sscanf(readbuffer, "%d%d%d", &N, &M, &nzB);

  printf("Matrix B: %d x %d, number of non-zeros = %d\n", N, M, nzB);
  fflush(stdout);

  if (N != M)
    transpose ^= 1; // when transposeA = 0 -> transposeB = 1; when tranposeA=
                    // 1-> transposeB = 0

  if (transpose) {
    swap(N, M);
  }

  printf("transpose: %d\n", transpose);

  if (J != N) {
    printf("Mismatch J!\n");
    return 0;
  }

  K = M;

  // std::getline(std::cin, input);

  for (int i = 1; i <= nzB; i++) {

    std::getline(std::cin, input);

    std::istringstream iss(input);
    std::vector<std::string> tokens;
    std::string token;

    // 将输入行分割为单词
    while (iss >> token) {
      tokens.push_back(token);
    }

    int xx, yy;
    double zz, lala;

    if (tokens.size() == 2) {

      std::istringstream(tokens[0]) >> xx;
      std::istringstream(tokens[1]) >> yy;
      // std::cout << "values: " << xx << ", " << yy << std::endl;
    } else if (tokens.size() == 3) {

      std::istringstream(tokens[0]) >> xx;
      std::istringstream(tokens[1]) >> yy;
      std::istringstream(tokens[2]) >> zz;
      // std::cout << "values: " << xx << ", " << yy << ", " << zz << std::endl;
    } else if (tokens.size() == 4) {

      std::istringstream(tokens[0]) >> xx;
      std::istringstream(tokens[1]) >> yy;
      std::istringstream(tokens[2]) >> zz;
      std::istringstream(tokens[2]) >> lala;
      // std::cout << "values: " << xx << ", " << yy << ", " << zz << std::endl;
    } else {
      std::cout << "Format Incorrect! " << std::endl;
      cout << i << endl << input << endl;
      return 0;
    }

    if (transpose) {
      Bc[xx].push_back(yy);
      B[yy].push_back(xx);
    } else {
      B[xx].push_back(yy);
      Bc[yy].push_back(xx);
    }
  }

  // cout << N << endl<<M <<endl<< nz << endl << nz/N <<endl;

  for (int j = 0; j <= J; j++) {
    sort(B[j].begin(), B[j].end());
  }
  for (int k = 0; k <= K; k++) {
    sort(Bc[k].begin(), Bc[k].end());
  }

  for (int j = 1; j <= J + 2; j++) {
    int tmplen = B[j - 1].size();
    offsetarrayB[j] = offsetarrayB[j - 1] + tmplen;

    // the actual access size
    tmplen = tmplen * 3;

    int freqj = (offsetarrayAc[j + 1] - offsetarrayAc[j]);
  }
  // two problem:
  // 1) this calculate way just calculate the minimum
  // 2) the + J will change is tiling J  -> but actually long will alos change
  // -> counteract? but the above calculate seems don't consider the emptys
  // (larger than real) so maybe counteract

  // move this to above for the weights (1 -> )
  // shortpart += J/(CACHEBLOCKSHORT);

  for (int k = 1; k <= K + 2; k++) {
    offsetarrayBc[k] = offsetarrayBc[k - 1] + Bc[k - 1].size();
  }

  if (ISCACHE == 1) {

    SET = cachesize / (CACHEBLOCK * SETASSOC);
    SETLOG = getlog(SET);
  }

  sampleB();

  /******************Config************************************/

  // notation of J and K in the code is swapped as in the paper
  // use the paper's notation as print output
  printf("I = %d, K = %d, J = %d\n", I, K, J);
  /************************************************************/

  // getParameterSample();
  getParameter();

  configPartial(0.05, 0.5, 0.45);
  // also calculate the pbound here as the bound of tile search
  int pbound = K;
  int leftbound = 0; // (leftbound, k] is the current window
  int sumnow = 0;

  int Bbound = Bsize;

  for (int k = 0; k < K; k++) {
    sumnow += Bc[k].size() * 3 + 1;

    while (sumnow > Bbound && leftbound < k) {
      pbound = min(pbound, k - leftbound);
      leftbound++;
      sumnow -= Bc[leftbound].size() * 3 + 1;
    }
  }

  long long SmallestTile = ((long long)pbound) * J;

  int kbound = getkbound();
  int jbound = getjbound();
  int ibound = getibound();

  int usesearchedtile = 1;
  if (usesearchedtile) {

    ISDYNAMICJ = 0;
    ISDYNAMICK = 0;
    ISDYNAMICI = 0;

    freopen((tile_dir + matrix_name1).c_str(), "r", stdin);
    int t_i, t_j, t_k;
    scanf("%d%d%d", &t_i, &t_j, &t_k);
    fclose(stdin);

    iii = t_i;
    jjj = t_j;
    kkk = t_k;
    tti = (I + iii - 1) / iii;
    ttj = (J + jjj - 1) / jjj;
    ttk = (K + kkk - 1) / kkk;

    /////////////// Baseline configurations

    if (baselinetest) {

      adaptive_prefetch = 0;

      ////////////  InnserSP
      // static FLRU + 16 words scheme0
      puts("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!   test InnerSP   "
           "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
      prefetchSize = inputcachesize / 6;
      cacheScheme = 11100;
      cachesize = inputcachesize;
      CACHEBLOCK = 16;
      CACHEBLOCKLOG = 4;
      SET = cachesize / (CACHEBLOCK * SETASSOC);
      SETLOG = getlog(SET);
      runTile(0, iii, jjj, kkk, tti, ttk, ttj, 0);

      fflush(stdout);

      ////////////  Sparch
      // dynamic FLRU + 128KB prefetch size + 144 words scheme0
      puts("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!   test Sparch   "
           "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
      ISCACHE = 1;
      cacheScheme = 11101;
      prefetchSize = inputcachesize / 6;
      cachesize = inputcachesize - prefetchSize;
      CACHEBLOCK = 144;
      CACHEBLOCKLOG = 8;
      SET = cachesize / (CACHEBLOCK * SETASSOC);
      SETLOG = getlog(SET);
      // calculate metadata overhead.
      // if metadata overflow, choose smaller tile
      int keepkkk = kkk;
      int keepttk = ttk;
      // if can keep, just use current kkk
      if (cachesize > kkk * 2) {
        cachesize -= kkk * 2;
      } else {
        // if can't keep, use smaller kkk
        // (make kkk*2 to be half cachesize)
        kkk = cachesize / 4;
        ttk = (K + kkk - 1) / kkk;
        cachesize -= kkk * 2;
      }
      runTile(0, iii, jjj, kkk, tti, ttk, ttj, 0);
      // return to the selected tile size.
      kkk = keepkkk;
      ttk = keepttk;
      // return to the default setting
      CACHEBLOCK = 16;
      CACHEBLOCKLOG = 4;
      cachesize = inputcachesize;
      SET = cachesize / (CACHEBLOCK * SETASSOC);
      SETLOG = getlog(SET);

      fflush(stdout);

      ////////////  X-cache
      puts("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!   test X-cache   "
           "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
      // LRU + 4 words scheme0
      // just same as using scheme0 with cacheline = 4
      ISCACHE = 1;
      cacheScheme = 0;
      cachesize = inputcachesize;
      CACHEBLOCK = 4;
      CACHEBLOCKLOG = 2;
      SET = cachesize / (CACHEBLOCK * SETASSOC);
      SETLOG = getlog(SET);
      runTile(0, iii, jjj, kkk, tti, ttk, ttj, 0);
      // return to the default setting
      CACHEBLOCK = 16;
      CACHEBLOCKLOG = 4;
      SET = cachesize / (CACHEBLOCK * SETASSOC);
      SETLOG = getlog(SET);

      fflush(stdout);

      puts("!!!!!!!!!!!!!!!!!!!!  Scratchpad   !!!!!!!!!!!!!!!!!!!!!!!");
      ISCACHE = 0;

      configPartial(0.05, 0.9, 0.05);

      reinitialize();

      run();

      return 0;
    }

    bool testseacache = 1;
    if (testseacache) {

      puts("\n***************** SeaCache *******************");

      adaptive_prefetch = 1;

      printf("nnzB:%d  K:%d  J/TJ:%d  nzlB:%d\n", nzB, K, (J + jjj - 1) / jjj,
             nzB / (K * ((J + jjj - 1) / jjj)));

      useVirtualTag = 1;
      cacheScheme = 88;
      cachesize = inputcachesize;

      runTile(0, iii, jjj, kkk, tti, ttk, ttj, 0);

      adaptive_prefetch = 0;
      useVirtualTag = 0;
    }

    bool ablationtest = 0;
    if (ablationtest) {

      adaptive_prefetch = 0;

      /////////////// ablation test

      puts("\n!!!!!!!!!!!!!!!!!!!!!!!!!! scheme0 (base)   "
           "!!!!!!!!!!!!!!!!!!!!!!!!");
      puts("CacheScheme 0");
      ISCACHE = 1;
      cacheScheme = 0;
      cachesize = inputcachesize;
      SET = cachesize / (CACHEBLOCK * SETASSOC);
      SETLOG = getlog(SET);
      runTile(0, iii, jjj, kkk, tti, ttk, ttj, 0);

      puts("\n!!!!!!!!!!!!!!!!!!!!!!!!!! scheme1 (mapping)   "
           "!!!!!!!!!!!!!!!!!!!!!!!!");

      puts("CacheScheme 1");
      ISCACHE = 1;
      cacheScheme = 1;
      cachesize = inputcachesize;
      SET = cachesize / (CACHEBLOCK * SETASSOC);
      SETLOG = getlog(SET);
      runTile(0, iii, jjj, kkk, tti, ttk, ttj, 0);

      puts("\n!!!!!!!!!!!!!!!!!!!!!!!!!! scheme88 without virtue   "
           "!!!!!!!!!!!!!!!!!!!!!!!!");

      useVirtualTag = 0;
      cacheScheme = 88;
      cachesize = inputcachesize;
      prefetchSize = cachesize / 6;
      runTile(0, iii, jjj, kkk, tti, ttk, ttj, 0);

      puts("\n!!!!!!!!!!!!!!!!!!!!!!!!!! scheme88 with virtue   "
           "!!!!!!!!!!!!!!!!!!!!!!!!");

      puts("CacheScheme 88 practical FLFU  with virtual tag 1/6");
      useVirtualTag = 1;
      cacheScheme = 88;
      cachesize = inputcachesize;
      prefetchSize = cachesize / 6;
      runTile(0, iii, jjj, kkk, tti, ttk, ttj, 0);
      useVirtualTag = 0;

      puts("CacheScheme 88 practical FLFU  with virtual tag 1/16");
      useVirtualTag = 1;
      cacheScheme = 88;
      cachesize = inputcachesize;
      prefetchSize = cachesize / 16;
      runTile(0, iii, jjj, kkk, tti, ttk, ttj, 0);
      useVirtualTag = 0;
    }
  }

  return 0;
}
