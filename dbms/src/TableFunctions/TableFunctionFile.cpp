// Copyright 2022 PingCAP, Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <Common/Exception.h>
#include <Common/typeid_cast.h>
#include <DataTypes/DataTypeFactory.h>
#include <Interpreters/Context.h>
#include <Interpreters/evaluateConstantExpression.h>
#include <Parsers/ASTFunction.h>
#include <Parsers/ASTLiteral.h>
#include <Storages/StorageFile.h>
#include <TableFunctions/ITableFunction.h>
#include <TableFunctions/TableFunctionFactory.h>
#include <TableFunctions/TableFunctionFile.h>

#include <boost/algorithm/string.hpp>

namespace DB
{
namespace ErrorCodes
{
extern const int NUMBER_OF_ARGUMENTS_DOESNT_MATCH;
extern const int DATABASE_ACCESS_DENIED;
} // namespace ErrorCodes

StoragePtr TableFunctionFile::executeImpl(const ASTPtr & ast_function, const Context & context) const
{
    // Parse args
    ASTs & args_func = typeid_cast<ASTFunction &>(*ast_function).children;

    if (args_func.size() != 1)
        throw Exception("Table function '" + getName() + "' must have arguments.", ErrorCodes::LOGICAL_ERROR);

    ASTs & args = typeid_cast<ASTExpressionList &>(*args_func.at(0)).children;

    if (args.size() != 3)
        throw Exception("Table function '" + getName() + "' requires exactly 3 arguments: path, format and structure.",
                        ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH);

    for (size_t i = 0; i < 3; ++i)
        args[i] = evaluateConstantExpressionOrIdentifierAsLiteral(args[i], context);

    std::string path = static_cast<const ASTLiteral &>(*args[0]).value.safeGet<String>();
    std::string format = static_cast<const ASTLiteral &>(*args[1]).value.safeGet<String>();
    std::string structure = static_cast<const ASTLiteral &>(*args[2]).value.safeGet<String>();

    // Create sample block
    std::vector<std::string> structure_vals;
    boost::split(structure_vals, structure, boost::algorithm::is_any_of(" ,"), boost::algorithm::token_compress_on);

    if (structure_vals.size() % 2 != 0)
        throw Exception("Odd number of elements in section structure: must be a list of name type pairs", ErrorCodes::LOGICAL_ERROR);

    Block sample_block;
    const DataTypeFactory & data_type_factory = DataTypeFactory::instance();

    for (size_t i = 0, size = structure_vals.size(); i < size; i += 2)
    {
        ColumnWithTypeAndName column;
        column.name = structure_vals[i];
        column.type = data_type_factory.get(structure_vals[i + 1]);
        column.column = column.type->createColumn();
        sample_block.insert(std::move(column));
    }

    // Create table
    StoragePtr storage = StorageFile::create(
        path,
        -1,
        context.getUserFilesPath(),
        getName(),
        format,
        ColumnsDescription{sample_block.getNamesAndTypesList()},
        const_cast<Context &>(context));

    storage->startup();

    return storage;
}


void registerTableFunctionFile(TableFunctionFactory & factory)
{
    factory.registerFunction<TableFunctionFile>();
}

} // namespace DB
