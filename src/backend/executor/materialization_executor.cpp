/**
 * @brief Executor for materialization node.
 *
 * This executor also performs all functions of a projection node, in order
 * to support late materialization).
 *
 * TODO Integrate expression system into materialization.
 *
 * Copyright(c) 2015, CMU
 */

#include "backend/executor/materialization_executor.h"

#include <cassert>
#include <memory>
#include <utility>

#include "backend/executor/logical_tile.h"
#include "backend/executor/logical_tile_factory.h"
#include "backend/planner/materialization_node.h"
#include "backend/storage/tile.h"

namespace peloton {
namespace executor {

/**
 * @brief Constructor for the materialization executor.
 * @param node Materialization node corresponding to this executor.
 */
MaterializationExecutor::MaterializationExecutor(
    planner::AbstractPlanNode *node,
    ExecutorContext *executor_context)
: AbstractExecutor(node, executor_context) {}

/**
 * @brief Nothing to init at the moment.
 * @return true on success, false otherwise.
 */
bool MaterializationExecutor::DInit() {
  assert(children_.size() == 1);

  return true;
}

/**
 * @brief Generates map from each base tile to columns originally from that
 *        base tile to be materialized.
 * @param column_ids Ids of columns to be materialized.
 * @param source_tile Logical tile that contains mapping from columns to
 *        base tiles.
 * @param tile_to_cols Map to be populated with mappings from tile to columns.
 *
 * We generate this mapping so that we can materialize columns tile by tile for
 * efficiency reasons.
 */
void MaterializationExecutor::GenerateTileToColMap(
    const std::unordered_map<oid_t, oid_t> &old_to_new_cols,
    LogicalTile *source_tile,
    std::unordered_map<storage::Tile *, std::vector<oid_t> > &
    cols_in_physical_tile) {
  for (const auto &kv : old_to_new_cols) {
    oid_t col = kv.first;

    // figure out base physical tile for column in logical tile
    storage::Tile *base_tile = source_tile->GetBaseTile(col);

    std::vector<oid_t> &cols_from_tile = cols_in_physical_tile[base_tile];
    cols_from_tile.push_back(col);
  }
}

/**
 * @brief Does the actual copying of data into the new physical tile.
 * @param source_tile Source tile to copy data from.
 * @param tile_to_cols Map from base tile to columns in that tile
 *        to be materialized.
 * @param dest_tile New tile to copy data into.
 */
void MaterializationExecutor::MaterializeByTiles(
    LogicalTile *source_tile,
    const std::unordered_map<oid_t, oid_t> &old_to_new_cols,
    const std::unordered_map<storage::Tile *, std::vector<oid_t> > &
    tile_to_cols,
    storage::Tile *dest_tile) {
  // Copy over all data from each base tile.

  //std::cout << *source_tile;
  for (const auto &kv : tile_to_cols) {
    const std::vector<oid_t> &old_column_ids = kv.second;

    // Go over each column in given base physical tile
    for (oid_t old_col_id : old_column_ids) {
      oid_t new_tuple_id = 0;

      auto it = old_to_new_cols.find(old_col_id);
      assert(it != old_to_new_cols.end());
      oid_t new_col_id = it->second;

      // Copy all values in the column to the physical tile
      for (oid_t old_tuple_id : *source_tile) {
        peloton::Value value = source_tile->GetValue(old_tuple_id, old_col_id);
        //std::cout << "old row: " << old_tuple_id << ", col: " << old_col_id;
        //std::cout << ", new row: " << new_tuple_id << ", col: " << new_col_id << " : " << value << std::endl;
        LOG_TRACE("Old Tuple : %u Column : %u \n", old_tuple_id, old_col_id);
        LOG_TRACE("New Tuple : %u Column : %u \n", new_tuple_id, new_col_id);
        dest_tile->SetValue(value, new_tuple_id++, new_col_id);
      }

    }
  }

}

std::unordered_map<oid_t, oid_t> MaterializationExecutor::BuildIdentityMapping(
    const catalog::Schema *schema) {
  std::unordered_map<oid_t, oid_t> old_to_new_cols;
  oid_t column_count = schema->GetColumnCount();
  for (oid_t col = 0; col < column_count; col++) {
    old_to_new_cols[col] = col;
  }

  return old_to_new_cols;
}

/**
 * @brief Create a physical tile for the given logical tile
 * @param source_tile Source tile from which the physical tile is created
 * @return a logical tile wrapper for the created physical tile
 */
LogicalTile *MaterializationExecutor::Physify(LogicalTile *source_tile) {
  std::unique_ptr<catalog::Schema> source_tile_schema(
      source_tile->GetPhysicalSchema());
  const int num_tuples = source_tile->GetTupleCount();
  const catalog::Schema *output_schema;
  std::unordered_map<oid_t, oid_t> old_to_new_cols;
  auto node = GetRawNode();

  // Create a default identity mapping node if we did not get one
  if (node == nullptr) {
    assert(source_tile_schema.get());
    output_schema = source_tile_schema.get();

    old_to_new_cols = BuildIdentityMapping(output_schema);
  }
  // Else use the mapping in the given plan node
  else {
    const planner::MaterializationNode &node = GetPlanNode<
        planner::MaterializationNode>();
    if (node.GetSchema()) {
      output_schema = node.GetSchema();
      old_to_new_cols = node.old_to_new_cols();
    } else {
      output_schema = source_tile_schema.get();
      old_to_new_cols = BuildIdentityMapping(output_schema);
    }
  }

  // Generate mappings.
  std::unordered_map<storage::Tile *, std::vector<oid_t> > tile_to_cols;
  GenerateTileToColMap(old_to_new_cols, source_tile, tile_to_cols);

  // Create new physical tile.
  std::unique_ptr<storage::Tile> dest_tile(
      storage::TileFactory::GetTempTile(*output_schema, num_tuples));

  // Proceed to materialize logical tile by physical tile at a time.
  MaterializeByTiles(source_tile, old_to_new_cols, tile_to_cols,
                     dest_tile.get());

  bool own_base_tile = true;

  // Wrap physical tile in logical tile.
  return LogicalTileFactory::WrapTiles( { dest_tile.release() }, own_base_tile);
}

/**
 * @brief Creates materialized physical tile from logical tile and wraps it
 *        in a new logical tile.
 *
 * @return true on success, false otherwise.
 */
bool MaterializationExecutor::DExecute() {
  // Retrieve child tile.
  const bool success = children_[0]->Execute();
  if (!success) {
    return false;
  }

  std::unique_ptr<LogicalTile> source_tile(children_[0]->GetOutput());
  LogicalTile *output_tile = nullptr;

  // Check the number of tuples in input logical tile
  // If none, then just return false
  const int num_tuples = source_tile->GetTupleCount();
  if (num_tuples == 0) {
    return false;
  }

  auto node = GetRawNode();
  bool physify_flag = true;  // by default, we create a physical tile

  if (node != nullptr) {
    const planner::MaterializationNode &node = GetPlanNode<
        planner::MaterializationNode>();
    physify_flag = node.GetPhysifyFlag();
  }

  if (physify_flag) {
    /* create a physical tile and a logical tile wrapper to be the output */
    output_tile = Physify(source_tile.get());
  } else {
    /* just pass thru the underlying logical tile */
    output_tile = source_tile.release();
  }

  SetOutput(output_tile);

  return true;
}

}  // namespace executor
}  // namespace peloton
