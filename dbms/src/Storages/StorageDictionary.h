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

#pragma once

#include <Core/Defines.h>
#include <IO/Operators.h>
#include <IO/WriteBufferFromString.h>
#include <Storages/IStorage.h>
#include <common/MultiVersion.h>

#include <ext/shared_ptr_helper.h>


namespace Poco
{
class Logger;
}

namespace DB
{
struct DictionaryStructure;
struct IDictionaryBase;
class ExternalDictionaries;

class StorageDictionary : public ext::SharedPtrHelper<StorageDictionary>
    , public IStorage
{
public:
    std::string getName() const override { return "Dictionary"; }
    std::string getTableName() const override { return table_name; }
    BlockInputStreams read(const Names & column_names,
                           const SelectQueryInfo & query_info,
                           const Context & context,
                           QueryProcessingStage::Enum & processed_stage,
                           size_t max_block_size = DEFAULT_BLOCK_SIZE,
                           unsigned threads = 1) override;

    void drop() override {}
    static NamesAndTypesList getNamesAndTypes(const DictionaryStructure & dictionary_structure);

    template <typename ForwardIterator>
    static std::string generateNamesAndTypesDescription(ForwardIterator begin, ForwardIterator end)
    {
        std::string description;
        {
            WriteBufferFromString buffer(description);
            bool first = true;
            for (; begin != end; ++begin)
            {
                if (!first)
                    buffer << ", ";
                first = false;

                buffer << begin->name << ' ' << begin->type->getName();
            }
        }

        return description;
    }

private:
    using Ptr = MultiVersion<IDictionaryBase>::Version;

    String table_name;
    String dictionary_name;
    Poco::Logger * logger;

    void checkNamesAndTypesCompatibleWithDictionary(const DictionaryStructure & dictionary_structure) const;

protected:
    StorageDictionary(const String & table_name_,
                      const ColumnsDescription & columns_,
                      const DictionaryStructure & dictionary_structure_,
                      const String & dictionary_name_);
};

} // namespace DB
