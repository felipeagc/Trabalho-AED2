#include "hash.hpp"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Método de tratamento de colisões
enum class Method : uint8_t {
  NONE,
  CHAIN_HEAD,
  CHAIN_NO_HEAD,
  COLLISION_ZONE,
};

struct Entry {
  uint32_t line;
  char address[128];
  uint32_t id;
  char birthdate[20];
  char name[64];
  char email[64];
  char phone[16];
};

struct Slot {
  bool filled;
  Entry entry;
};

struct FileHeader {
  uint32_t entry_cap;
};

struct Table {
  FILE *file;
  HashFn hash_fn;
  uint32_t entry_cap;
  Method method;

  Table(const char *path, HashFn hash_fn, size_t entry_cap, Method method) {
    this->method = method;
    this->file   = fopen(path, "w+b");
    assert(this->file);

    this->hash_fn   = hash_fn;
    this->entry_cap = entry_cap;

    fseek(this->file, 0, SEEK_SET);
    if (ftell(this->file) == 0) {
      FileHeader header = {};
      header.entry_cap  = this->entry_cap;

      write_at(0, sizeof(header), &header);
    }
  }

  ~Table() { fclose(this->file); }

  inline bool read_at(size_t pos, size_t size, void *ptr) {
    fseek(this->file, pos, SEEK_SET);
    size_t read = fread(ptr, size, 1, this->file);
    return read == 1;
  }

  inline bool write_at(size_t pos, size_t size, void *ptr) {
    fseek(this->file, pos, SEEK_SET);
    size_t written = fwrite(ptr, size, 1, this->file);
    return written == 1;
  }

  bool insert(const Entry &entry) {
    uint32_t hash = this->hash_fn(entry.id) % this->entry_cap;

    switch (this->method) {
    case Method::NONE: {
      Slot slot = {};

      size_t pos = sizeof(FileHeader) + (sizeof(slot) * hash);
      if (read_at(pos, sizeof(slot), &slot)) {
        if (slot.filled) return false;
      }

      slot.entry  = entry;
      slot.filled = true;

      write_at(pos, sizeof(slot), &slot);

      return true;
    }
    default: return false;
    }
  }

  bool search(const uint32_t id, Entry *entry) {
    uint32_t hash = this->hash_fn(id) % this->entry_cap;

    switch (this->method) {
    case Method::NONE: {
      Slot slot = {};

      size_t pos = sizeof(FileHeader) + (sizeof(slot) * hash);
      if (read_at(pos, sizeof(slot), &slot)) {
        if (!slot.filled) return false;
        if (slot.entry.id != id) return false;
        if (entry != nullptr) *entry = slot.entry;

        return true;
      }

      return false;
    }
    default: return false;
    }
  }

  void remove(const uint32_t id) {
    if (!search(id, NULL)) {
      return;
    }

    uint32_t hash = this->hash_fn(id) % this->entry_cap;

    switch (this->method) {
    case Method::NONE: {
      Slot slot   = {};
      slot.filled = false;

      size_t pos = sizeof(FileHeader) + (sizeof(slot) * hash);
      write_at(pos, sizeof(slot), &slot);

      break;
    }
    default: return;
    }
  }
};

void test_collisions(Table &table) {
  FILE *file = fopen("../data.csv", "r");
  assert(file);

  static char buf[256];

  // Read first line
  fgets(buf, sizeof(buf), file);

  uint32_t collisions = 0;

  while (fgets(buf, sizeof(buf), file) != NULL) {
    Entry entry;

    char *pt;

    pt         = strtok(buf, ",");
    entry.line = atoi(pt); // numero da linha
    pt         = strtok(NULL, ",");
    strncpy(entry.address, pt, 50); // endereco
    pt       = strtok(NULL, ",");
    entry.id = atoi(pt); // id
    pt       = strtok(NULL, ",");
    strncpy(entry.birthdate, pt, 10); // data de nascimento
    pt = strtok(NULL, ",");
    strncpy(entry.name, pt, 40); // nome
    pt = strtok(NULL, ",");
    strncpy(entry.email, pt, 20); // email
    pt = strtok(NULL, ",");
    strncpy(entry.phone, pt, 13); // celular

    if (entry.line >= 70000 && entry.line <= 80000) {
      if (!table.insert(entry)) {
        collisions++;
      }
    }
  }

  printf("=> Colisões: %u\n", collisions);

  fclose(file);
}

const char *method_name(Method method) {
  switch (method) {
  case Method::NONE: return "nenhum";
  case Method::CHAIN_HEAD: return "encadeamento com cabeça";
  case Method::CHAIN_NO_HEAD: return "encadeamento sem cabeça";
  case Method::COLLISION_ZONE: return "zona de colisão";
  }
  return "";
}

const char *hash_fn_name(HashFn fn) {
  if (fn == hash_mult) return "multiplicação";
  if (fn == hash_mult_quad) return "multiplicação (quadrado)";
  if (fn == hash_divisao) return "divisão";
  if (fn == hash_divisao_primo) return "divisão (primo)";
  if (fn == hash_dobra) return "dobra";
  return "";
}

int main() {
  uint32_t table_size = 10000;

  Method methods[]  = {Method::NONE};
  HashFn hash_fns[] = {
      hash_mult, hash_mult_quad, hash_divisao, hash_divisao_primo, hash_dobra};

  for (const Method &method : methods) {
    for (const HashFn &hash_fn : hash_fns) {
      Table table("./table.bin", hash_fn, table_size, method);

      printf(
          "Tratamento de colisões: %s\nFunção: %s\n",
          method_name(method),
          hash_fn_name(hash_fn));

      test_collisions(table);

      printf("\n");
    }
  }

  return 0;
}
