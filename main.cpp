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
    this->file = fopen(path, "w+b");
    assert(this->file);

    this->hash_fn = hash_fn;
    this->entry_cap = entry_cap;

    fseek(this->file, 0, SEEK_SET);
    if (ftell(this->file) == 0) {
      // File is empty
      FileHeader header;
      header.entry_cap = this->entry_cap;

      fwrite(&header, sizeof(header), 1, this->file);

      Slot dummy;
      memset(&dummy, 0, sizeof(dummy));

      for (uint32_t i = 0; i < this->entry_cap; i++) {
        fseek(this->file, sizeof(FileHeader) + (sizeof(dummy) * i), SEEK_SET);
        fwrite(&dummy, sizeof(dummy), 1, this->file);
      }
    }
  }

  ~Table() { fclose(this->file); }

  bool insert(const Entry &entry) {
    uint32_t hash = this->hash_fn(entry.id);
    hash = hash % this->entry_cap;

    Slot slot;

    fseek(this->file, sizeof(FileHeader) + (sizeof(slot) * hash), SEEK_SET);
    size_t read = fread(&slot, sizeof(slot), 1, this->file);
    assert(read == 1);

    if (slot.filled) {
      // Collision
      return false;
    }

    slot.entry = entry;
    slot.filled = true;

    fseek(this->file, sizeof(FileHeader) + (sizeof(slot) * hash), SEEK_SET);
    fwrite(&slot, sizeof(slot), 1, this->file);

    return true;
  }

  bool search(const uint32_t id, Entry *entry) {
    uint32_t hash = this->hash_fn(id);
    hash = hash % this->entry_cap;

    Slot slot;

    fseek(this->file, sizeof(FileHeader) + (sizeof(slot) * hash), SEEK_SET);
    size_t read = fread(&slot, sizeof(slot), 1, this->file);
    assert(read == 1);

    if (!slot.filled) {
      return false;
    }

    if (slot.entry.id != id) {
      return false;
    }

    *entry = slot.entry;

    return true;
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

    pt = strtok(buf, ",");
    entry.line = atoi(pt); // numero da linha
    pt = strtok(NULL, ",");
    strncpy(entry.address, pt, 50); // endereco
    pt = strtok(NULL, ",");
    entry.id = atoi(pt); // id
    pt = strtok(NULL, ",");
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

  printf("Collisions: %u\n", collisions);

  fclose(file);
}

int main() {
  {
    Table table("./table.bin", hash_mult, 10000, Method::NONE);
    test_collisions(table);

    Entry entry;
    if (table.search(58172200, &entry)) {
      printf("Found: %s\n", entry.name);
    }
  }

  {
    Table table("./table.bin", hash_mult_quad, 10000, Method::NONE);
    test_collisions(table);
  }

  {
    Table table("./table.bin", hash_divisao, 10000, Method::NONE);
    test_collisions(table);
  }

  {
    Table table("./table.bin", hash_divisao_primo, 10000, Method::NONE);
    test_collisions(table);
  }

  {
    Table table("./table.bin", hash_dobra, 10000, Method::NONE);
    test_collisions(table);
  }

  return 0;
}
