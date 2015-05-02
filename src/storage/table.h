/*-------------------------------------------------------------------------
 *
 * table.h
 * file description
 *
 * Copyright(c) 2015, CMU
 *
 * /n-store/src/storage/table.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "catalog/manager.h"
#include "storage/tile_group.h"
#include "storage/backend_vm.h"

#include "index/index.h"

#include <string>

namespace nstore {
namespace storage {

//===--------------------------------------------------------------------===//
// Table
//===--------------------------------------------------------------------===//

/**
 * Represents a group of tile groups logically vertically contiguous.
 *
 * <Tile Group 1>
 * <Tile Group 2>
 * ...
 * <Tile Group n>
 *
 */
class Table {
  friend class TileGroup;
  friend class TableFactory;

  Table() = delete;
  Table(Table const&) = delete;

 public:

  // Table constructor
  Table(catalog::Schema *schema,
        Backend *backend,
        std::string table_name)
 : database_id(INVALID_ID),
   table_id(INVALID_ID),
   backend(backend),
   schema(schema),
   table_name(table_name){
  }

  ~Table() {

    // clean up tile groups
    for(auto tile_group : tile_groups)
      delete tile_group;

    // table owns its backend
    delete backend;

    // clean up schema
    delete schema;
  }

  catalog::Schema *GetSchema() const{
    return schema;
  }

  Backend *GetBackend() const {
    return backend;
  }

  std::string GetName() const {
    return table_name;
  }

  //===--------------------------------------------------------------------===//
  // OPERATIONS
  //===--------------------------------------------------------------------===//

  // add a new tile group to table
  oid_t AddDefaultTileGroup();

  void AddTileGroup(TileGroup *tile_group);

  inline TileGroup *GetTileGroup(id_t tile_group_id) const {
    assert(tile_group_id < tile_groups.size());
    return tile_groups[tile_group_id];
  }

  inline unsigned int NumTileGroups() const {
    return tile_groups.size();
  }

  // insert tuple in table
  oid_t InsertTuple(txn_id_t transaction_id, const Tuple *tuple);

  //===--------------------------------------------------------------------===//
  // INDEXES
  //===--------------------------------------------------------------------===//

  size_t GetIndexCount() const {
    return indexes.size();
  }

  index::Index *GetPrimaryKeyIndex() {
    return primary_key_index;
  }

  const index::Index *GetPrimaryKeyIndex() const {
    return primary_key_index;
  }

  void InsertInIndexes(const storage::Tuple *tuple, ItemPointer location);
  bool TryInsertInIndexes(const storage::Tuple *tuple, ItemPointer location);

  void DeleteInIndexes(const storage::Tuple *tuple);
  void UpdateInIndexes(const storage::Tuple *tuple, ItemPointer location, ItemPointer old_location);

  bool CheckNulls(const storage::Tuple *tuple) const;

 protected:

  //===--------------------------------------------------------------------===//
  // Data members
  //===--------------------------------------------------------------------===//

  // Catalog information
  id_t database_id;
  id_t table_id;

  // backend
  Backend *backend;

  // table schema
  catalog::Schema *schema;

  // table name
  std::string table_name;

  // set of tile groups
  std::vector<TileGroup*> tile_groups;

  // INDEXES
  std::vector<index::Index*> indexes;
  index::Index *primary_key_index = nullptr;

  // TODO need some policy ?
  // number of tuples allocated per tilegroup for this table
  size_t tuples_per_tilegroup = 1000;

  std::mutex table_mutex;

};

//===--------------------------------------------------------------------===//
// Table factory
//===--------------------------------------------------------------------===//

class TableFactory {
 public:
  TableFactory();
  virtual ~TableFactory();

  static Table *GetTable(oid_t database_id,
                         catalog::Schema *schema,
                         std::string table_name = "temp") {

    // create a new backend
    Backend* backend = new VMBackend();

    Table *table =  new Table(schema, backend, table_name);

    table->database_id = database_id;

    return table;
  }

};

} // End storage namespace
} // End nstore namespace


