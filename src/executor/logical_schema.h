/*-------------------------------------------------------------------------
 *
 * logical_schema.h
 * Schema for logical tile.
 *
 * Copyright(c) 2015, CMU
 *
 * /n-store/src/executor/logical_schema.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include <bitset>
#include <iostream>
#include <vector>

#include "common/types.h"
#include "storage/tile.h"

namespace nstore {
namespace executor {

class LogicalSchema {
 public:
  storage::Tile *GetBaseTile(id_t column_id);
  id_t GetOriginColumnId(id_t column_id);
  void AddColumn(storage::Tile *base_tile, id_t column_id);
  bool IsValid(id_t column_id);
  // Note that this includes invalidated columns.
  size_t NumCols();

  //===--------------------------------------------------------------------===//
  // Utilities
  //===--------------------------------------------------------------------===//

  // Get a string representation of this tile
  friend std::ostream& operator<<(std::ostream& os, const LogicalSchema& logical_schema);

 private:
  // Pointer to tile that column is from.
  std::vector<storage::Tile *> base_tiles;

  // Original column id in base tile of column.
  std::vector<id_t> origin_columns;

  // Valid bits of columns (used to implement late-materialization for projection).
  // We don't use std::bitset because it requires the size at compile-time.
  std::vector<bool> valid_bits;
};

} // namespace executor
} // namespace nstore
