#include <DataTypes/DataTypeDecimal.h>
#include <DataTypes/DataTypeEnum.h>
#include <DataTypes/DataTypeMyDate.h>
#include <DataTypes/DataTypeMyDateTime.h>
#include <DataTypes/DataTypeNothing.h>
#include <DataTypes/DataTypeNullable.h>
#include <DataTypes/DataTypeSet.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypesNumber.h>
#include <Debug/MockTiDB.h>
#include <Interpreters/Context.h>
#include <Parsers/ASTFunction.h>
#include <Parsers/ASTLiteral.h>
#include <Poco/StringTokenizer.h>
#include <Storages/Transaction/KVStore.h>
#include <Storages/Transaction/TMTContext.h>
#include <Storages/Transaction/TiDB.h>
#include <Storages/Transaction/TypeMapping.h>

namespace DB
{

namespace ErrorCodes
{
extern const int BAD_ARGUMENTS;
extern const int LOGICAL_ERROR;
extern const int TABLE_ALREADY_EXISTS;
extern const int UNKNOWN_TABLE;
} // namespace ErrorCodes

using ColumnInfo = TiDB::ColumnInfo;
using TableInfo = TiDB::TableInfo;
using PartitionInfo = TiDB::PartitionInfo;
using PartitionDefinition = TiDB::PartitionDefinition;
using Table = MockTiDB::Table;
using TablePtr = MockTiDB::TablePtr;

Table::Table(const String & database_name_, DatabaseID database_id_, const String & table_name_, TableInfo && table_info_)
    : table_info(std::move(table_info_)),
      database_name(database_name_),
      database_id(database_id_),
      table_name(table_name_),
      col_id(table_info_.columns.size())
{}

MockTiDB::MockTiDB() { databases["default"] = 0; }

TablePtr MockTiDB::dropTableInternal(Context & context, const String & database_name, const String & table_name, bool drop_regions)
{
    String qualified_name = database_name + "." + table_name;
    auto it_by_name = tables_by_name.find(qualified_name);
    if (it_by_name == tables_by_name.end())
        return nullptr;

    auto & kvstore = context.getTMTContext().getKVStore();
    auto & region_table = context.getTMTContext().getRegionTable();

    auto table = it_by_name->second;
    if (table->isPartitionTable())
    {
        for (const auto & partition : table->table_info.partition.definitions)
        {
            tables_by_id.erase(partition.id);
            if (drop_regions)
            {
                for (auto & e : region_table.getRegionsByTable(partition.id))
                    kvstore->mockRemoveRegion(e.first, region_table);
                region_table.removeTable(partition.id);
            }
        }
    }
    tables_by_id.erase(table->id());

    tables_by_name.erase(it_by_name);

    if (drop_regions)
    {
        for (auto & e : region_table.getRegionsByTable(table->id()))
            kvstore->mockRemoveRegion(e.first, region_table);
        region_table.removeTable(table->id());
    }

    return table;
}

void MockTiDB::dropDB(Context & context, const String & database_name, bool drop_regions)
{
    std::lock_guard lock(tables_mutex);

    std::vector<String> table_names;
    std::for_each(tables_by_id.begin(), tables_by_id.end(), [&](const auto & pair) {
        if (pair.second->database_name == database_name)
            table_names.emplace_back(pair.second->table_info.name);
    });

    for (const auto & table_name : table_names)
        dropTableInternal(context, database_name, table_name, drop_regions);

    version++;

    SchemaDiff diff;
    diff.type = SchemaActionType::DropSchema;
    if (databases.find(database_name) == databases.end())
        diff.schema_id = -1;
    else
        diff.schema_id = databases[database_name];
    diff.version = version;
    version_diff[version] = diff;

    databases.erase(database_name);
}

void MockTiDB::dropTable(Context & context, const String & database_name, const String & table_name, bool drop_regions)
{
    std::lock_guard lock(tables_mutex);

    auto table = dropTableInternal(context, database_name, table_name, drop_regions);
    if (!table)
        return;

    version++;

    SchemaDiff diff;
    diff.type = SchemaActionType::DropTable;
    diff.schema_id = table->database_id;
    diff.table_id = table->id();
    diff.version = version;
    version_diff[version] = diff;
}

DatabaseID MockTiDB::newDataBase(const String & database_name)
{
    DatabaseID schema_id = 0;

    if (databases.find(database_name) == databases.end())
    {
        schema_id = databases.size() + 1;
        databases.emplace(database_name, schema_id);
    }

    version++;
    SchemaDiff diff;
    diff.type = SchemaActionType::CreateSchema;
    diff.schema_id = schema_id;
    diff.version = version;
    version_diff[version] = diff;

    return schema_id;
}

TiDB::TableInfoPtr MockTiDB::parseColumns(
    const String & tbl_name, const ColumnsDescription & columns, const String & handle_pk_name, String engine_type)
{
    TableInfo table_info;
    table_info.name = tbl_name;
    table_info.pk_is_handle = false;
    table_info.is_common_handle = false;

    bool has_pk = false;
    bool has_non_int_pk = false;
    Poco::StringTokenizer string_tokens(handle_pk_name, ",");
    std::unordered_map<String, size_t> pk_column_pos_map;
    int i = 1;
    for (auto & column : columns.getAllPhysical())
    {
        Field default_value;
        auto it = columns.defaults.find(column.name);
        if (it != columns.defaults.end())
            default_value = getDefaultValue(it->second.expression);
        table_info.columns.emplace_back(reverseGetColumnInfo(column, i++, default_value, true));
        for (auto sit = string_tokens.begin(); sit != string_tokens.end(); sit++)
        {
            // todo support prefix index
            if (*sit == column.name)
            {
                has_pk = true;
                if (!column.type->isInteger() && !column.type->isUnsignedInteger())
                    has_non_int_pk = true;
                table_info.columns.back().setPriKeyFlag();
                pk_column_pos_map[*sit] = i - 2;
                break;
            }
        }
    }

    if (has_pk)
    {
        if (string_tokens.count() > 1 || has_non_int_pk)
        {
            table_info.is_common_handle = true;
            // construct IndexInfo
            table_info.index_infos.resize(1);
            TiDB::IndexInfo & index_info = table_info.index_infos[0];
            index_info.id = 1;
            index_info.is_primary = true;
            index_info.idx_name = "PRIMARY";
            index_info.tbl_name = tbl_name;
            index_info.is_unique = true;
            index_info.index_type = 1;
            index_info.idx_cols.resize(string_tokens.count());
            for (size_t index = 0; index < string_tokens.count(); index++)
            {
                String & name = string_tokens[index];
                index_info.idx_cols[index].name = name;
                index_info.idx_cols[index].offset = pk_column_pos_map[name];
                index_info.idx_cols[index].length = -1;
            }
        }
        else
            table_info.pk_is_handle = true;
    }

    table_info.comment = "Mocked.";

    // set storage engine type
    std::transform(engine_type.begin(), engine_type.end(), engine_type.begin(), [](unsigned char c) { return std::tolower(c); });
    if (engine_type == "tmt")
        table_info.engine_type = TiDB::StorageEngine::TMT;
    else if (engine_type == "dt")
        table_info.engine_type = TiDB::StorageEngine::DT;
    else
        throw Exception("Unknown engine type : " + engine_type + ", must be 'tmt' or 'dt'", ErrorCodes::BAD_ARGUMENTS);

    return std::make_shared<TiDB::TableInfo>(std::move(table_info));
}

TableID MockTiDB::newTable(const String & database_name, const String & table_name, const ColumnsDescription & columns, Timestamp tso,
    const String & handle_pk_name, const String & engine_type)
{
    std::lock_guard lock(tables_mutex);

    String qualified_name = database_name + "." + table_name;
    if (tables_by_name.find(qualified_name) != tables_by_name.end())
    {
        throw Exception("Mock TiDB table " + qualified_name + " already exists", ErrorCodes::TABLE_ALREADY_EXISTS);
    }

    if (databases.find(database_name) == databases.end())
    {
        throw Exception("MockTiDB not found db: " + database_name, ErrorCodes::LOGICAL_ERROR);
    }

    auto table_info = parseColumns(table_name, columns, handle_pk_name, engine_type);
    table_info->id = table_id_allocator++;
    table_info->update_timestamp = tso;

    auto table = std::make_shared<Table>(database_name, databases[database_name], table_name, std::move(*table_info));
    tables_by_id.emplace(table->table_info.id, table);
    tables_by_name.emplace(qualified_name, table);

    version++;
    SchemaDiff diff;
    diff.type = SchemaActionType::CreateTable;
    diff.schema_id = table->database_id;
    diff.table_id = table->id();
    diff.version = version;
    version_diff[version] = diff;

    return table->table_info.id;
}

Field getDefaultValue(const ASTPtr & default_value_ast)
{
    const auto * func = typeid_cast<const ASTFunction *>(default_value_ast.get());
    if (func != nullptr)
    {
        const auto * value_ptr
            = typeid_cast<const ASTLiteral *>(typeid_cast<const ASTExpressionList *>(func->arguments.get())->children[0].get());
        return value_ptr->value;
    }
    else if (typeid_cast<const ASTLiteral *>(default_value_ast.get()) != nullptr)
        return typeid_cast<const ASTLiteral *>(default_value_ast.get())->value;
    return Field();
}

void MockTiDB::newPartition(const String & database_name, const String & table_name, TableID partition_id, Timestamp tso, bool is_add_part)
{
    std::lock_guard lock(tables_mutex);

    TablePtr table = getTableByNameInternal(database_name, table_name);
    TableInfo & table_info = table->table_info;

    const auto & part_def = find_if(table_info.partition.definitions.begin(), table_info.partition.definitions.end(),
        [&partition_id](PartitionDefinition & part_def) { return part_def.id == partition_id; });
    if (part_def != table_info.partition.definitions.end())
        throw Exception("Mock TiDB table " + database_name + "." + table_name + " already has partition " + std::to_string(partition_id),
            ErrorCodes::LOGICAL_ERROR);

    table_info.is_partition_table = true;
    table_info.partition.enable = true;
    table_info.partition.num++;
    PartitionDefinition partition_def;
    partition_def.id = partition_id;
    partition_def.name = std::to_string(partition_id);
    table_info.partition.definitions.emplace_back(partition_def);
    table_info.update_timestamp = tso;

    if (is_add_part)
    {
        version++;

        SchemaDiff diff;
        diff.type = SchemaActionType::AddTablePartition;
        diff.schema_id = table->database_id;
        diff.table_id = table->id();
        diff.version = version;
        version_diff[version] = diff;
    }
}

void MockTiDB::dropPartition(const String & database_name, const String & table_name, TableID partition_id)
{
    std::lock_guard lock(tables_mutex);

    TablePtr table = getTableByNameInternal(database_name, table_name);
    TableInfo & table_info = table->table_info;

    const auto & part_def = find_if(table_info.partition.definitions.begin(), table_info.partition.definitions.end(),
        [&partition_id](PartitionDefinition & part_def) { return part_def.id == partition_id; });
    if (part_def == table_info.partition.definitions.end())
        throw Exception("Mock TiDB table " + database_name + "." + table_name + " already drop partition " + std::to_string(partition_id),
            ErrorCodes::LOGICAL_ERROR);

    table_info.partition.num--;
    table_info.partition.definitions.erase(part_def);

    version++;

    SchemaDiff diff;
    diff.type = SchemaActionType::DropTablePartition;
    diff.schema_id = table->database_id;
    diff.table_id = table->id();
    diff.version = version;
    version_diff[version] = diff;
}

void MockTiDB::addColumnToTable(
    const String & database_name, const String & table_name, const NameAndTypePair & column, const Field & default_value)
{
    std::lock_guard lock(tables_mutex);

    TablePtr table = getTableByNameInternal(database_name, table_name);
    String qualified_name = database_name + "." + table_name;
    auto & columns = table->table_info.columns;
    if (std::find_if(columns.begin(), columns.end(), [&](const ColumnInfo & column_) { return column_.name == column.name; })
        != columns.end())
        throw Exception("Column " + column.name + " already exists in TiDB table " + qualified_name, ErrorCodes::LOGICAL_ERROR);

    ColumnInfo column_info = reverseGetColumnInfo(column, table->allocColumnID(), default_value, true);
    columns.emplace_back(column_info);

    version++;

    SchemaDiff diff;
    diff.type = SchemaActionType::AddColumn;
    diff.schema_id = table->database_id;
    diff.table_id = table->id();
    diff.version = version;
    version_diff[version] = diff;
}

void MockTiDB::dropColumnFromTable(const String & database_name, const String & table_name, const String & column_name)
{
    std::lock_guard lock(tables_mutex);

    TablePtr table = getTableByNameInternal(database_name, table_name);
    String qualified_name = database_name + "." + table_name;
    auto & columns = table->table_info.columns;
    auto it = std::find_if(columns.begin(), columns.end(), [&](const ColumnInfo & column_) { return column_.name == column_name; });
    if (it == columns.end())
        throw Exception("Column " + column_name + " does not exist in TiDB table  " + qualified_name, ErrorCodes::LOGICAL_ERROR);

    columns.erase(it);

    version++;

    SchemaDiff diff;
    diff.type = SchemaActionType::DropColumn;
    diff.schema_id = table->database_id;
    diff.table_id = table->id();
    diff.version = version;
    version_diff[version] = diff;
}

void MockTiDB::modifyColumnInTable(const String & database_name, const String & table_name, const NameAndTypePair & column)
{
    std::lock_guard lock(tables_mutex);

    TablePtr table = getTableByNameInternal(database_name, table_name);
    String qualified_name = database_name + "." + table_name;
    auto & columns = table->table_info.columns;
    auto it = std::find_if(columns.begin(), columns.end(), [&](const ColumnInfo & column_) { return column_.name == column.name; });
    if (it == columns.end())
        throw Exception("Column " + column.name + " does not exist in TiDB table  " + qualified_name, ErrorCodes::LOGICAL_ERROR);

    ColumnInfo column_info = reverseGetColumnInfo(column, 0, Field(), true);
    if (it->hasUnsignedFlag() != column_info.hasUnsignedFlag())
        throw Exception("Modify column " + column.name + " UNSIGNED flag is not allowed", ErrorCodes::LOGICAL_ERROR);
    if (it->tp == column_info.tp && it->hasNotNullFlag() == column_info.hasNotNullFlag())
        throw Exception("Column " + column.name + " type not changed", ErrorCodes::LOGICAL_ERROR);

    it->tp = column_info.tp;
    it->flag = column_info.flag;

    version++;
    SchemaDiff diff;
    diff.type = SchemaActionType::ModifyColumn;
    diff.schema_id = table->database_id;
    diff.table_id = table->id();
    diff.version = version;
    version_diff[version] = diff;
}

void MockTiDB::renameColumnInTable(
    const String & database_name, const String & table_name, const String & old_column_name, const String & new_column_name)
{
    std::lock_guard lock(tables_mutex);

    TablePtr table = getTableByNameInternal(database_name, table_name);
    String qualified_name = database_name + "." + table_name;
    auto & columns = table->table_info.columns;
    auto it = std::find_if(columns.begin(), columns.end(), [&](const ColumnInfo & column_) { return column_.name == old_column_name; });
    if (it == columns.end())
        throw Exception("Column " + old_column_name + " does not exist in TiDB table  " + qualified_name, ErrorCodes::LOGICAL_ERROR);

    if (columns.end()
        != std::find_if(columns.begin(), columns.end(), [&](const ColumnInfo & column_) { return column_.name == new_column_name; }))
        throw Exception("Column " + new_column_name + " exists in TiDB table  " + qualified_name, ErrorCodes::LOGICAL_ERROR);

    it->name = new_column_name;

    version++;
    SchemaDiff diff;
    diff.type = SchemaActionType::ModifyColumn;
    diff.schema_id = table->database_id;
    diff.table_id = table->id();
    diff.version = version;
    version_diff[version] = diff;
}

void MockTiDB::renameTable(const String & database_name, const String & table_name, const String & new_table_name)
{
    std::lock_guard lock(tables_mutex);

    TablePtr table = getTableByNameInternal(database_name, table_name);
    String qualified_name = database_name + "." + table_name;
    String new_qualified_name = database_name + "." + new_table_name;

    TableInfo new_table_info = table->table_info;
    new_table_info.name = new_table_name;
    auto new_table = std::make_shared<Table>(database_name, table->database_id, new_table_name, std::move(new_table_info));

    tables_by_id[new_table->table_info.id] = new_table;
    tables_by_name.erase(qualified_name);
    tables_by_name.emplace(new_qualified_name, new_table);

    version++;
    SchemaDiff diff;
    diff.type = SchemaActionType::RenameTable;
    diff.schema_id = table->database_id;
    diff.old_schema_id = table->database_id;
    diff.table_id = table->id();
    diff.version = version;
    version_diff[version] = diff;
}

void MockTiDB::truncateTable(const String & database_name, const String & table_name)
{
    std::lock_guard lock(tables_mutex);

    TablePtr table = getTableByNameInternal(database_name, table_name);

    TableID old_table_id = table->table_info.id;
    table->table_info.id = table_id_allocator++;

    tables_by_id.erase(old_table_id);
    tables_by_id.emplace(table->id(), table);

    version++;
    SchemaDiff diff;
    diff.type = SchemaActionType::TruncateTable;
    diff.schema_id = table->database_id;
    diff.old_table_id = old_table_id;
    diff.table_id = table->id();
    diff.version = version;
    version_diff[version] = diff;
}

TablePtr MockTiDB::getTableByName(const String & database_name, const String & table_name)
{
    std::lock_guard lock(tables_mutex);

    return getTableByNameInternal(database_name, table_name);
}

TablePtr MockTiDB::getTableByNameInternal(const String & database_name, const String & table_name)
{
    String qualified_name = database_name + "." + table_name;
    auto it = tables_by_name.find(qualified_name);
    if (it == tables_by_name.end())
    {
        throw Exception("Mock TiDB table " + qualified_name + " does not exists", ErrorCodes::UNKNOWN_TABLE);
    }

    return it->second;
}

TiDB::TableInfoPtr MockTiDB::getTableInfoByID(TableID table_id)
{
    auto it = tables_by_id.find(table_id);
    if (it == tables_by_id.end())
    {
        return nullptr;
    }
    return std::make_shared<TiDB::TableInfo>(TiDB::TableInfo(it->second->table_info));
}

TiDB::DBInfoPtr MockTiDB::getDBInfoByID(DatabaseID db_id)
{
    TiDB::DBInfoPtr db_ptr = std::make_shared<TiDB::DBInfo>(TiDB::DBInfo());
    db_ptr->id = db_id;
    for (auto it = databases.begin(); it != databases.end(); it++)
    {
        if (it->second == db_id)
        {
            db_ptr->name = it->first;
            break;
        }
    }
    return db_ptr;
}

SchemaDiff MockTiDB::getSchemaDiff(Int64 version_) { return version_diff[version_]; }

} // namespace DB
