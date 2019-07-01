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
  bool filled = false;
  Entry entry;
  size_t next = 0;
};

struct SlotNoHead {
  size_t next = 0;
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
    case Method::COLLISION_ZONE:
    case Method::CHAIN_HEAD: {
      this->zone_offset = sizeof(FileHeader) + (sizeof(Slot) * this->entry_cap);
      break;
    }
    case Method::CHAIN_NO_HEAD: {
      this->zone_offset =
          sizeof(FileHeader) + (sizeof(SlotNoHead) * this->entry_cap);
      break;
    }
    default: break;
    }

    if (file_size() == 0) {
      FileHeader header = {};
      header.entry_cap  = this->entry_cap;

      bool wrote = write_at(0, sizeof(header), &header);
      assert(wrote);

      switch (this->method) {
      case Method::NONE:
      case Method::COLLISION_ZONE:
      case Method::CHAIN_HEAD: {
        for (uint32_t i = 0; i < this->entry_cap; i++) {
          Slot slot = {};
          wrote     = write_at(
              sizeof(FileHeader) + (i * sizeof(slot)), sizeof(slot), &slot);
          assert(wrote);
        }
        break;
      }
      case Method::CHAIN_NO_HEAD: {
        for (uint32_t i = 0; i < this->entry_cap; i++) {
          SlotNoHead slot = {};
          wrote           = write_at(
              sizeof(FileHeader) + (i * sizeof(slot)), sizeof(slot), &slot);
          assert(wrote);
        }
        break;
      }
      }
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
    case Method::CHAIN_HEAD: {
      Slot slot = {};

      size_t pos = sizeof(FileHeader) + (sizeof(slot) * hash);
      if (read_at(pos, sizeof(slot), &slot) && slot.filled) {
        size_t new_pos = std::max(file_size(), this->zone_offset);

        size_t old_next = slot.next;

        slot.next  = new_pos;
        bool wrote = write_at(pos, sizeof(slot), &slot);
        assert(wrote);

        slot.entry  = entry;
        slot.filled = true;
        slot.next   = old_next;
        wrote       = write_at(new_pos, sizeof(slot), &slot);
        assert(wrote);

        if (collided) *collided = true;
      } else {
        if (collided) *collided = false;

        slot.entry  = entry;
        slot.filled = true;
        slot.next   = 0;
        bool wrote  = write_at(pos, sizeof(slot), &slot);
        assert(wrote);
      }

      return true;
    }
    case Method::CHAIN_NO_HEAD: {
      SlotNoHead main_slot = {};
      size_t pos           = sizeof(FileHeader) + sizeof(main_slot) * hash;

      read_at(pos, sizeof(main_slot), &main_slot);
      size_t old = main_slot.next;

      if (collided) *collided = (old != 0);

      main_slot.next = std::max(file_size(), this->zone_offset);
      write_at(pos, sizeof(main_slot), &main_slot);

      Slot slot = {};

      slot.entry  = entry;
      slot.filled = true;
      slot.next   = old;
      bool wrote  = write_at(main_slot.next, sizeof(slot), &slot);
      assert(wrote);

      return true;
      break;
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
    case Method::CHAIN_HEAD: {
      Slot slot = {};

      size_t pos = sizeof(FileHeader) + (sizeof(slot) * hash);
      read_at(pos, sizeof(slot), &slot);

      if (slot.filled) {
        if (slot.entry.id == id) {
          if (entry != nullptr) *entry = slot.entry;
          if (file_pos != nullptr) *file_pos = pos;
          return true;
        }

        while (slot.next != 0) {
          pos = slot.next;
          if (read_at(pos, sizeof(slot), &slot)) {
            if (slot.filled && slot.entry.id == id) {
              if (entry != nullptr) *entry = slot.entry;
              if (file_pos != nullptr) *file_pos = pos;
              return true;
            }
          }
        }
      }

      return false;
    }
    case Method::CHAIN_NO_HEAD: {
      SlotNoHead main_slot = {};
      size_t pos           = sizeof(FileHeader) + (sizeof(main_slot) * hash);
      read_at(pos, sizeof(main_slot), &main_slot);

      size_t slot_pos = main_slot.next;

      Slot slot = {};
      while (slot_pos != 0) {
        read_at(slot_pos, sizeof(slot), &slot);
        slot_pos = slot.next;

        if (slot.entry.id == id) {
          if (entry != nullptr) *entry = slot.entry;
          if (file_pos != nullptr) *file_pos = slot_pos;
          return true;
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
      Slot slot = {};

      bool wrote = write_at(pos, sizeof(slot), &slot);
      assert(wrote);

      return true;
    }
    case Method::CHAIN_HEAD: {
      uint32_t hash = this->hash_fn(id) % this->entry_cap;

      Slot slot       = {};
      size_t main_pos = sizeof(FileHeader) + (sizeof(slot) * hash);
      read_at(pos, sizeof(slot), &slot);

      if (main_pos == pos) {
        // Cabeça
        if (slot.next != 0) {
          read_at(slot.next, sizeof(slot), &slot);
          write_at(main_pos, sizeof(slot), &slot);
          return true;
        } else {
          slot.filled = false;
          write_at(main_pos, sizeof(slot), &slot);
          return true;
        }
      } else {
        // Filho
        size_t parent_pos = main_pos;
        Slot parent;
        read_at(parent_pos, sizeof(parent), &parent);

        while (parent.next != 0) {
          if (parent.next == pos) {
            break;
          }

          parent_pos = parent.next;
          read_at(parent_pos, sizeof(parent), &parent);
        }

        parent.next = slot.next;
        write_at(parent_pos, sizeof(parent), &parent);
      }

      return true;
    }
    case Method::CHAIN_NO_HEAD: {
      uint32_t hash = this->hash_fn(id) % this->entry_cap;

      SlotNoHead main_slot;
      size_t main_pos = sizeof(FileHeader) + (sizeof(main_slot) * hash);
      read_at(main_pos, sizeof(main_slot), &main_slot);

      Slot slot = {};
      read_at(pos, sizeof(slot), &slot);

      if (main_slot.next == pos) {
        // First
        main_slot.next = slot.next;
        write_at(main_pos, sizeof(main_slot), &main_slot);
      } else if (slot.next == 0) {
        // Last/middle
        Slot parent_slot  = {};
        size_t parent_pos = main_slot.next;
        read_at(parent_pos, sizeof(parent_slot), &parent_slot);

        while (parent_slot.next != pos && parent_slot.next != 0) {
          parent_pos = parent_slot.next;
          read_at(parent_pos, sizeof(parent_slot), &parent_slot);
        }

        parent_slot.next = slot.next;
        write_at(parent_pos, sizeof(parent_slot), &parent_slot);
      }

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
  //   size_t pos;
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

  Method methods[]  = {Method::NONE,
                      Method::COLLISION_ZONE,
                      Method::CHAIN_HEAD,
                      Method::CHAIN_NO_HEAD};
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
