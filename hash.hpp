#include <math.h>
#include <stdint.h>
#include <stdio.h>

typedef uint32_t (*HashFn)(uint32_t);

const uint32_t MAX = 10000;
const uint32_t MAX_PRIME = 9973;

static uint32_t hash_divisao(uint32_t chave) { return chave % MAX; }

static uint32_t hash_divisao_primo(uint32_t chave) { return chave % MAX_PRIME; }

static uint32_t hash_mult(uint32_t chave) {
  uint32_t val = chave;
  uint32_t i = 0;
  while (val >= MAX) {
    if (i % 2 == 0)
      val = val >> 1;
    else {
      uint32_t shift = 32 - log2(chave);
      val = val << shift;
      val = val >> shift;
    }
    i++;
  }
  return val;
}

static uint32_t hash_mult_quad(uint32_t chave) {
  uint64_t quad = (uint64_t)(chave * chave);
  uint32_t i = 0, shift;
  while (quad >= MAX) {
    if (i % 2 == 0)
      quad = quad >> 1;
    else {
      shift = 64 - log2(quad);
      quad = quad << shift;
      quad = quad >> shift;
    }
    i++;
  }
  return (uint32_t)quad;
}

// Calcula o numero de digitos em binario do número
static uint32_t digits(uint32_t num) {
  for (uint32_t i = 0; i < sizeof(uint32_t) * 8; i++)
    if ((1u << i) > num)
      return i;
  return 0;
}

static uint32_t hash_dobra(uint32_t num) {
  while (num >= MAX) {
    uint32_t digits_ = digits(num);
    if (digits_ % 2 != 0)
      digits_++;

    uint32_t d1 = digits_ / 2;
    uint32_t n1 = (num >> d1);
    uint32_t d2 = sizeof(uint32_t) * 8 - (digits_ / 2);
    uint32_t n2 = ((num << d2) >> d2);

    num = n1 + n2;
  }
  return num;
}
