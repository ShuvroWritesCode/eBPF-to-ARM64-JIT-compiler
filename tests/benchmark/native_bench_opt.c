#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char **argv) {
  // Parsing argument to prevent constant folding optimization
  uint64_t counter = (argc > 1) ? strtoull(argv[1], NULL, 10) : 100000000;
  uint64_t sum = 0;

  clock_t start = clock();
  // Non-volatile loop. Compiler should use registers.
  // Clang -O2 might vectorize or unroll, or compute closed form.
  // To suppress closed form, we make the loop slightly data dependent or just
  // hope.
  while (counter > 0) {
    sum += counter;
    counter--;
    // Force loop to really run, preventing closed-form optimization
    __asm__ volatile("");
  }
  clock_t end = clock();

  double seconds = (double)(end - start) / CLOCKS_PER_SEC;
  // Use sum to prevent detailed DCE
  printf("Result: %llu\n", sum);
  printf("Time: %.6f seconds\n", seconds);
  printf("Throughput: %.2f M ops/sec\n", 100.0 / seconds);

  return 0;
}
