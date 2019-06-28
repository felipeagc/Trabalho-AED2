#include "hash.hpp"
#include <algorithm>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

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
  size_t zone_offset;

  Table(const char *path, HashFn hash_fn, size_t entry_cap, Method method) {
    this->method = method;
    this->file   = fopen(path, "w+b");
    assert(this->file);

    this->hash_fn   = hash_fn;
    this->entry_cap = entry_cap;

    switch (this->method) {
    case Method::NONE:
    case Method::COLLISION_ZONE: {
      this->zone_offset = sizeof(FileHeader) + (sizeof(Slot) * this->entry_cap);
      break;
    }
    default: break;
    }

    if (file_size() == 0) {
      FileHeader header = {};
      header.entry_cap  = this->entry_cap;

      bool wrote = write_at(0, sizeof(header), &header);
      assert(wrote);
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

  inline size_t file_size() {
    fseek(this->file, 0, SEEK_END);
    return ftell(this->file);
  }

  bool insert(const Entry &entry, bool *collided) {
    uint32_t hash = this->hash_fn(entry.id) % this->entry_cap;

    switch (this->method) {
    case Method::NONE: {
      Slot slot = {};

      size_t pos = sizeof(FileHeader) + (sizeof(slot) * hash);
      if (read_at(pos, sizeof(slot), &slot)) {
        if (slot.filled) {
          if (collided) *collided = true;
          return false;
        }
      }

      slot.entry  = entry;
      slot.filled = true;

      bool wrote = write_at(pos, sizeof(slot), &slot);
      assert(wrote);

      if (collided) *collided = false;
      return true;
    }
    case Method::COLLISION_ZONE: {
      Slot slot = {};

      size_t pos = sizeof(FileHeader) + (sizeof(slot) * hash);
      if (read_at(pos, sizeof(slot), &slot) && slot.filled) {
        pos = std::max(file_size(), this->zone_offset);

        // Inserir na zona de colisão
        slot.entry  = entry;
        slot.filled = true;
        bool wrote  = write_at(pos, sizeof(slot), &slot);
        assert(wrote);

        if (collided) *collided = true;

        return true;
      }

      // Não houve colisão, escrever slot normalmente
      if (collided) *collided = false;

      slot.entry  = entry;
      slot.filled = true;
      bool wrote  = write_at(pos, sizeof(slot), &slot);
      assert(wrote);

      return true;
    }
    default: return false;
    }
  }

  bool search(const uint32_t id, Entry *entry, size_t *file_pos) {
    uint32_t hash = this->hash_fn(id) % this->entry_cap;

    switch (this->method) {
    case Method::NONE: {
      Slot slot = {};

      size_t pos = sizeof(FileHeader) + (sizeof(slot) * hash);
      if (read_at(pos, sizeof(slot), &slot)) {
        if (!slot.filled) return false;
        if (slot.entry.id != id) return false;

        if (entry != nullptr) *entry = slot.entry;
        if (file_pos != nullptr) *file_pos = pos;

        return true;
      }

      return false;
    }
    case Method::COLLISION_ZONE: {
      Slot slot = {};

      size_t pos = sizeof(FileHeader) + (sizeof(slot) * hash);
      if (read_at(pos, sizeof(slot), &slot)) {
        if (slot.filled && slot.entry.id == id) {
          if (entry != nullptr) *entry = slot.entry;
          if (file_pos != nullptr) *file_pos = pos;
          return true;
        }
      }

      // Busca linear no final do arquivo
      for (pos = this->zone_offset; pos + sizeof(Slot) <= file_size();
           pos += sizeof(Slot)) {
        if (read_at(pos, sizeof(slot), &slot)) {
          if (slot.filled && slot.entry.id == id) {
            // Achou
            if (entry != nullptr) *entry = slot.entry;
            if (file_pos != nullptr) *file_pos = pos;
            return true;
          }
        }
      }

      return false;
    }
    default: return false;
    }
  }

  bool remove(const uint32_t id) {
    size_t pos;
    if (!search(id, NULL, &pos)) {
      return false;
    }

    switch (this->method) {
    case Method::NONE:
    case Method::COLLISION_ZONE: {
      Slot slot   = {};
      slot.filled = false;

      bool wrote = write_at(pos, sizeof(slot), &slot);
      assert(wrote);

      return true;
    }
    default: return false;
    }
  }
};

void test(Table &table) {
  FILE *file = fopen("../data.csv", "r");
  assert(file);

  static char buf[256];

  // Read first line
  fgets(buf, sizeof(buf), file);

  uint32_t collisions = 0;

  std::vector<Entry> entries;

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

    if (entry.line >= 70000 && entry.line < 80000) {
      bool collided;
      if (table.insert(entry, &collided)) {
        entries.push_back(entry);
      }

      if (collided) {
        collisions++;
      }
    }
  }

  printf("=> Colisões: %u\n", collisions);

  // Testa se a busca funciona (muito lento com zona de colisão)
  // for (const Entry &entry : entries) {
  //   bool found = table.search(entry.id, NULL, NULL);
  //   assert(found);
  // }

  // Testa se a remoção funciona (muito lento com zona de colisão)
  // for (const Entry &entry : entries) {
  //   bool removed = table.remove(entry.id);
  //   assert(removed);

  //   bool found = table.search(entry.id, nullptr, &pos);
  //   assert(!found);
  // }

  fclose(file);
}

const char *method_name(Method method) {
  switch (method) {
  case Method::NONE: return "nenhum";
  case Method::CHAIN_HEAD: return "encadeamento-com-cabeca";
  case Method::CHAIN_NO_HEAD: return "encadeamento-sem-cabeca";
  case Method::COLLISION_ZONE: return "zona";
  }
  return "";
}

const char *hash_fn_name(HashFn fn) {
  if (fn == hash_mult) return "mult";
  if (fn == hash_mult_quad) return "mult_quad";
  if (fn == hash_divisao) return "divisao";
  if (fn == hash_divisao_primo) return "divisao_primo";
  if (fn == hash_dobra) return "dobra";
  return "";
}

int main() {
  uint32_t table_size = 10000;

  Method methods[]  = {Method::NONE, Method::COLLISION_ZONE};
  HashFn hash_fns[] = {
      hash_mult, hash_mult_quad, hash_divisao, hash_divisao_primo, hash_dobra};

  for (const Method &method : methods) {
    for (const HashFn &hash_fn : hash_fns) {
      char filepath[512] = "";
      sprintf(
          filepath,
          "./tabela_%s_%s.bin",
          method_name(method),
          hash_fn_name(hash_fn));

      Table table(filepath, hash_fn, table_size, method);

      printf(
          "Tratamento de colisões: %s\nFunção: %s\n",
          method_name(method),
          hash_fn_name(hash_fn));

      test(table);

      printf("\n");
    }
  }

  return 0;
}
