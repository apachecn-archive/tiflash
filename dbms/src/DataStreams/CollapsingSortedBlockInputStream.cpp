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

#include <Columns/ColumnsNumber.h>
#include <Common/FieldVisitors.h>
#include <Common/FmtUtils.h>
#include <DataStreams/CollapsingSortedBlockInputStream.h>
#include <common/logger_useful.h>
#include <fmt/core.h>

/// Maximum number of messages about incorrect data in the log.
#define MAX_ERROR_MESSAGES 10


namespace DB
{
namespace ErrorCodes
{
extern const int INCORRECT_DATA;
extern const int LOGICAL_ERROR;
} // namespace ErrorCodes


void CollapsingSortedBlockInputStream::reportIncorrectData()
{
    FmtBuffer fmt_buf;
    fmt_buf.fmtAppend(
        "Incorrect data: number of rows with sign = 1 ({}) differs with number of rows with sign = -1 ({}) by more than one (for key: ",
        count_positive,
        count_negative);

    fmt_buf.joinStr(
        std::begin(*current_key.columns),
        std::end(*current_key.columns),
        [this](const auto arg, FmtBuffer & fb) {
            fb.append(applyVisitor(FieldVisitorToString(), (*arg)[current_key.row_num]));
        },
        ", ");
    fmt_buf.append(").");

    /** Fow now we limit ourselves to just logging such situations,
      *  since the data is generated by external programs.
      * With inconsistent data, this is an unavoidable error that can not be easily corrected by admins. Therefore Warning.
      */
    LOG_WARNING(log, fmt_buf.toString());
}


void CollapsingSortedBlockInputStream::insertRows(MutableColumns & merged_columns, size_t & merged_rows, bool last_in_stream)
{
    if (count_positive == 0 && count_negative == 0)
        return;

    if (count_positive == count_negative && !last_is_positive)
    {
        /// If all the rows in the input streams was collapsed, we still want to give at least one block in the result.
        if (last_in_stream && merged_rows == 0 && !blocks_written)
        {
            LOG_INFO(log, "All rows collapsed");
            ++merged_rows;
            for (size_t i = 0; i < num_columns; ++i)
                merged_columns[i]->insertFrom(*(*last_positive.columns)[i], last_positive.row_num);
            ++merged_rows;
            for (size_t i = 0; i < num_columns; ++i)
                merged_columns[i]->insertFrom(*(*last_negative.columns)[i], last_negative.row_num);

            if (out_row_sources_buf)
            {
                /// true flag value means "skip row"
                current_row_sources[last_positive_pos].setSkipFlag(false);
                current_row_sources[last_negative_pos].setSkipFlag(false);
            }
        }
    }
    else
    {
        if (count_positive <= count_negative)
        {
            ++merged_rows;
            for (size_t i = 0; i < num_columns; ++i)
                merged_columns[i]->insertFrom(*(*first_negative.columns)[i], first_negative.row_num);

            if (out_row_sources_buf)
                current_row_sources[first_negative_pos].setSkipFlag(false);
        }

        if (count_positive >= count_negative)
        {
            ++merged_rows;
            for (size_t i = 0; i < num_columns; ++i)
                merged_columns[i]->insertFrom(*(*last_positive.columns)[i], last_positive.row_num);

            if (out_row_sources_buf)
                current_row_sources[last_positive_pos].setSkipFlag(false);
        }

        if (!(count_positive == count_negative || count_positive + 1 == count_negative || count_positive == count_negative + 1))
        {
            if (count_incorrect_data < MAX_ERROR_MESSAGES)
                reportIncorrectData();
            ++count_incorrect_data;
        }
    }

    if (out_row_sources_buf)
        out_row_sources_buf->write(
            reinterpret_cast<const char *>(current_row_sources.data()),
            current_row_sources.size() * sizeof(RowSourcePart));
}


Block CollapsingSortedBlockInputStream::readImpl()
{
    if (finished)
        return {};

    MutableColumns merged_columns;
    init(merged_columns);

    if (has_collation)
        throw Exception(fmt::format("Logical error: {} does not support collations", getName()), ErrorCodes::LOGICAL_ERROR);

    if (merged_columns.empty())
        return {};

    merge(merged_columns, queue);
    return header.cloneWithColumns(std::move(merged_columns));
}


void CollapsingSortedBlockInputStream::merge(MutableColumns & merged_columns, std::priority_queue<SortCursor> & queue)
{
    size_t merged_rows = 0;

    /// Take rows in correct order and put them into `merged_columns` until the rows no more than `max_block_size`
    for (; !queue.empty(); ++current_pos)
    {
        SortCursor current = queue.top();

        if (current_key.empty())
            setPrimaryKeyRef(current_key, current);

        Int8 sign = static_cast<const ColumnInt8 &>(*current->all_columns[sign_column_number]).getData()[current->pos];
        setPrimaryKeyRef(next_key, current);

        bool key_differs = next_key != current_key;

        /// if there are enough rows and the last one is calculated completely
        if (key_differs && merged_rows >= max_block_size)
        {
            ++blocks_written;
            return;
        }

        queue.pop();

        if (key_differs)
        {
            /// We write data for the previous primary key.
            insertRows(merged_columns, merged_rows);

            current_key.swap(next_key);

            count_negative = 0;
            count_positive = 0;

            current_pos = 0;
            first_negative_pos = 0;
            last_positive_pos = 0;
            last_negative_pos = 0;
            current_row_sources.resize(0);
        }

        /// Initially, skip all rows. On insert, unskip "corner" rows.
        if (out_row_sources_buf)
            current_row_sources.emplace_back(current.impl->order, true);

        if (sign == 1)
        {
            ++count_positive;
            last_is_positive = true;

            setRowRef(last_positive, current);
            last_positive_pos = current_pos;
        }
        else if (sign == -1)
        {
            if (!count_negative)
            {
                setRowRef(first_negative, current);
                first_negative_pos = current_pos;
            }

            if (!blocks_written && !merged_rows)
            {
                setRowRef(last_negative, current);
                last_negative_pos = current_pos;
            }

            ++count_negative;
            last_is_positive = false;
        }
        else
            throw Exception(
                fmt::format("Incorrect data: Sign = {} (must be 1 or -1).", sign),
                ErrorCodes::INCORRECT_DATA);

        if (!current->isLast())
        {
            current->next();
            queue.push(current);
        }
        else
        {
            /// We take next block from the corresponding source, if there is one.
            fetchNextBlock(current, queue);
        }
    }

    /// Write data for last primary key.
    insertRows(merged_columns, merged_rows, true);

    finished = true;
}

} // namespace DB
