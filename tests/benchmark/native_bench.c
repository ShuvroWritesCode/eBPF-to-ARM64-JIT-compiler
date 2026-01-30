#include <stdint.h>
#include <stdio.h>
#include <time.h>

int main() {
  // volatile to prevent compiler from optimizing away the loop entirely
  // but we want to measure the loop overhead, not just "return 100M"
  // However, -O2 might still unify this.
  // The user provided code uses volatile counter and sum.
  volatile uint64_t counter = 100000000;
  volatile uint64_t sum = 0;

  clock_t start = clock();
  while (counter > 0) {
    sum += counter;
    counter--;
  }
  clock_t end = clock();

  double seconds = (double)(end - start) / CLOCKS_PER_SEC;
  printf("Result: %llu\n", sum);
  printf("Time: %.6f seconds\n", seconds);
  printf("Throughput: %.2f M ops/sec\n", 100.0 / seconds);

  return 0;
}
