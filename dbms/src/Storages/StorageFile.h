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

#include <Poco/File.h>
#include <Poco/Path.h>
#include <Storages/IStorage.h>
#include <common/logger_useful.h>

#include <atomic>
#include <ext/shared_ptr_helper.h>
#include <shared_mutex>


namespace DB
{
class StorageFileBlockInputStream;
class StorageFileBlockOutputStream;

class StorageFile : public ext::SharedPtrHelper<StorageFile>
    , public IStorage
{
public:
    std::string getName() const override
    {
        return "File";
    }

    std::string getTableName() const override
    {
        return table_name;
    }

    BlockInputStreams read(
        const Names & column_names,
        const SelectQueryInfo & query_info,
        const Context & context,
        QueryProcessingStage::Enum & processed_stage,
        size_t max_block_size,
        unsigned num_streams) override;

    BlockOutputStreamPtr write(
        const ASTPtr & query,
        const Settings & settings) override;

    void drop() override;

    void rename(const String & new_path_to_db, const String & new_database_name, const String & new_table_name) override;

    String getDataPath() const override { return path; }

protected:
    friend class StorageFileBlockInputStream;
    friend class StorageFileBlockOutputStream;

    /** there are three options (ordered by priority):
    - use specified file descriptor if (fd >= 0)
    - use specified table_path if it isn't empty
    - create own table inside data/db/table/
    */
    StorageFile(
        const std::string & table_path_,
        int table_fd_,
        const std::string & db_dir_path,
        const std::string & table_name_,
        const std::string & format_name_,
        const ColumnsDescription & columns_,
        Context & context_);

private:
    std::string table_name;
    std::string format_name;
    Context & context_global;

    std::string path;
    int table_fd = -1;

    bool is_db_table = true; /// Table is stored in real database, not user's file
    bool use_table_fd = false; /// Use table_fd insted of path
    std::atomic<bool> table_fd_was_used{false}; /// To detect repeating reads from stdin
    off_t table_fd_init_offset = -1; /// Initial position of fd, used for repeating reads

    mutable std::shared_mutex rwlock;

    Poco::Logger * log = &Poco::Logger::get("StorageFile");
};

} // namespace DB
