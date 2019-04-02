#pragma once

#include <functional>
#include <shared_mutex>
#include <unordered_map>

#include <IO/ReadHelpers.h>
#include <IO/WriteHelpers.h>

#include <Storages/Transaction/RegionMeta.h>
#include <Storages/Transaction/TiKVHelper.h>
#include <Storages/Transaction/TiKVKeyValue.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <tikv/RegionClient.h>
#pragma GCC diagnostic pop

namespace DB
{

class Region;
using RegionPtr = std::shared_ptr<Region>;
using Regions = std::vector<RegionPtr>;

std::pair<HandleID, HandleID> getHandleRangeByTable(const TiKVKey & start_key, const TiKVKey & end_key, TableID table_id);

std::pair<HandleID, HandleID> getHandleRangeByTable(const std::pair<TiKVKey, TiKVKey> & range, TableID table_id);

/// Store all kv data of one region. Including 'write', 'data' and 'lock' column families.
/// TODO: currently the synchronize mechanism is broken and need to fix.
class Region : public std::enable_shared_from_this<Region>
{
public:
    const static UInt32 CURRENT_VERSION;

    const static String lock_cf_name;
    const static String default_cf_name;
    const static String write_cf_name;

    // In both lock_cf and write_cf.
    enum CFModifyFlag : UInt8
    {
        PutFlag = 'P',
        DelFlag = 'D',
        // useless for TiFLASH
        /*
        LockFlag = 'L',
        // In write_cf, only raft leader will use RollbackFlag in txn mode. Learner should ignore it.
        RollbackFlag = 'R',
        */
    };

    // This must be an ordered map. Many logics rely on it, like iterating.
    using KVMap = std::map<TiKVKey, TiKVValue>;

    /// A quick-and-dirty copy of LockInfo structure in kvproto.
    /// Used to transmit to client using non-ProtoBuf protocol.
    struct LockInfo
    {
        std::string primary_lock;
        UInt64 lock_version;
        std::string key;
        UInt64 lock_ttl;
    };
    using LockInfoPtr = std::unique_ptr<LockInfo>;
    using LockInfos = std::vector<LockInfoPtr>;

    class CommittedScanner : private boost::noncopyable
    {
    public:
        CommittedScanner(const RegionPtr & store_, TableID expected_table_id_)
            : store(store_), lock(store_->mutex), expected_table_id(expected_table_id_), write_map_it(store->write_cf.cbegin())
        {}

        /// Check if next kv exists.
        /// Return InvalidTableID if not.
        TableID hasNext()
        {
            if (expected_table_id != InvalidTableID)
            {
                for (; write_map_it != store->write_cf.cend(); ++write_map_it)
                {
                    if (likely(RecordKVFormat::getTableId(write_map_it->first) == expected_table_id))
                        return expected_table_id;
                }
            }
            else
            {
                if (write_map_it != store->write_cf.cend())
                    return RecordKVFormat::getTableId(write_map_it->first);
            }
            return InvalidTableID;
        }

        auto next(std::vector<TiKVKey> * keys = nullptr) { return store->readDataByWriteIt(write_map_it++, keys); }

        LockInfoPtr getLockInfo(TableID expected_table_id, UInt64 start_ts) { return store->getLockInfo(expected_table_id, start_ts); }

    private:
        RegionPtr store;
        std::shared_lock<std::shared_mutex> lock;

        TableID expected_table_id;
        KVMap::const_iterator write_map_it;
    };

    class CommittedRemover : private boost::noncopyable
    {
    public:
        CommittedRemover(const RegionPtr & store_) : store(store_), lock(store_->mutex) {}

        void remove(const TiKVKey & key)
        {
            if (auto it = store->write_cf.find(key); it != store->write_cf.end())
                store->removeDataByWriteIt(it);
        }

    private:
        RegionPtr store;
        std::unique_lock<std::shared_mutex> lock;
    };

public:
    explicit Region(RegionMeta && meta_) : meta(std::move(meta_)), client(nullptr), log(&Logger::get("Region")) {}

    explicit Region(const RegionMeta & meta_) : meta(meta_), client(nullptr), log(&Logger::get("Region")) {}

    using RegionClientCreateFunc = std::function<pingcap::kv::RegionClientPtr(pingcap::kv::RegionVerID)>;

    explicit Region(RegionMeta && meta_, const RegionClientCreateFunc & region_client_create)
        : meta(std::move(meta_)), client(region_client_create(meta.getRegionVerID())), log(&Logger::get("Region"))
    {}

    explicit Region(const RegionMeta & meta_, const RegionClientCreateFunc & region_client_create)
        : meta(meta_), client(region_client_create(meta.getRegionVerID())), log(&Logger::get("Region"))
    {}

    TableID insert(const std::string & cf, const TiKVKey & key, const TiKVValue & value);
    TableID remove(const std::string & cf, const TiKVKey & key);

    using BatchInsertNode = std::tuple<const TiKVKey *, const TiKVValue *, const String *>;
    void batchInsert(std::function<bool(BatchInsertNode &)> && f);

    std::tuple<std::vector<RegionPtr>, TableIDSet, bool> onCommand(const enginepb::CommandRequest & cmd);

    std::unique_ptr<CommittedScanner> createCommittedScanner(TableID expected_table_id);
    std::unique_ptr<CommittedRemover> createCommittedRemover();

    size_t serialize(WriteBuffer & buf, enginepb::CommandResponse * response = nullptr);
    static RegionPtr deserialize(ReadBuffer & buf, const RegionClientCreateFunc * region_client_create = nullptr);

    RegionID id() const;
    RegionRange getRange() const;

    enginepb::CommandResponse toCommandResponse() const;
    std::string toString(bool dump_status = true) const;

    bool isPendingRemove() const;
    void setPendingRemove();
    bool isPeerRemoved() const;

    size_t dataSize() const;

    void markPersisted();
    Timepoint lastPersistTime() const;
    size_t persistParm() const;
    void decPersistParm(size_t x);
    void incPersistParm();

    friend bool operator==(const Region & region1, const Region & region2)
    {
        std::shared_lock<std::shared_mutex> lock1(region1.mutex);
        std::shared_lock<std::shared_mutex> lock2(region2.mutex);

        return region1.meta == region2.meta && region1.data_cf == region2.data_cf && region1.write_cf == region2.write_cf
            && region1.lock_cf == region2.lock_cf && region1.cf_data_size == region2.cf_data_size;
    }

    UInt64 learnerRead();

    void waitIndex(UInt64 index);

    UInt64 getIndex() const;
    UInt64 getProbableIndex() const;

    RegionVersion version() const;
    RegionVersion confVer() const;

    std::pair<HandleID, HandleID> getHandleRangeByTable(TableID table_id) const;

    void reset(Region && new_region);

private:
    // Private methods no need to lock mutex, normally

    TableID doInsert(const std::string & cf, const TiKVKey & key, const TiKVValue & value);
    TableID doRemove(const std::string & cf, const TiKVKey & key);

    bool checkIndex(UInt64 index);
    KVMap & getCf(const std::string & cf);

    using ReadInfo = std::tuple<UInt64, UInt8, UInt64, TiKVValue>;
    ReadInfo readDataByWriteIt(const KVMap::const_iterator & write_it, std::vector<TiKVKey> * keys = nullptr);
    KVMap::iterator removeDataByWriteIt(const KVMap::iterator & write_it);

    LockInfoPtr getLockInfo(TableID expected_table_id, UInt64 start_ts);

    RegionPtr splitInto(const RegionMeta & meta);
    Regions execBatchSplit(const raft_cmdpb::AdminRequest & request, const raft_cmdpb::AdminResponse & response, UInt64 index, UInt64 term);
    void execChangePeer(const raft_cmdpb::AdminRequest & request, const raft_cmdpb::AdminResponse & response, UInt64 index, UInt64 term);

private:
    // TODO: We should later change to lock free structure if needed.
    KVMap data_cf;
    KVMap write_cf;
    KVMap lock_cf;

    mutable std::shared_mutex mutex;

    RegionMeta meta;

    pingcap::kv::RegionClientPtr client;

    // Size of data cf & write cf, without lock cf.
    std::atomic<size_t> cf_data_size = 0;

    std::atomic<Timepoint> last_persist_time = Clock::now();

    std::atomic<size_t> persist_parm = 1;

    Logger * log;
};

} // namespace DB
