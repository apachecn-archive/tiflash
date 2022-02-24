#pragma once

#include <Storages/Transaction/RegionDataRead.h>
#include <Storages/Transaction/RegionManager.h>
#include <Storages/Transaction/RegionPersister.h>
#include <Storages/Transaction/StorageEngineType.h>


namespace TiDB
{
struct TableInfo;
}
namespace DB
{
namespace RegionBench
{
extern void concurrentBatchInsert(const TiDB::TableInfo &, Int64, Int64, Int64, UInt64, UInt64, Context &);
}
namespace DM
{
enum class FileConvertJobType;
}

namespace tests
{
class RegionKVStoreTest;
}

class Context;
class IAST;
using ASTPtr = std::shared_ptr<IAST>;
using ASTs = std::vector<ASTPtr>;

class KVStore;
using KVStorePtr = std::shared_ptr<KVStore>;

class RegionTable;
class Region;
using RegionPtr = std::shared_ptr<Region>;
struct RaftCommandResult;
class KVStoreTaskLock;

struct MockRaftCommand;
struct MockTiDBTable;
struct TiKVRangeKey;

class TMTContext;

struct SSTViewVec;
struct WriteCmdsView;

enum class EngineStoreApplyRes : uint32_t;

struct TiFlashRaftProxyHelper;
struct RegionPreDecodeBlockData;
using RegionPreDecodeBlockDataPtr = std::unique_ptr<RegionPreDecodeBlockData>;
class ReadIndexWorkerManager;
using BatchReadIndexRes = std::vector<std::pair<kvrpcpb::ReadIndexResponse, uint64_t>>;
class ReadIndexStressTest;

/// TODO: brief design document.
class KVStore final : private boost::noncopyable
{
public:
    KVStore(Context & context, TiDB::SnapshotApplyMethod snapshot_apply_method_);
    void restore(const TiFlashRaftProxyHelper *);

    RegionPtr getRegion(RegionID region_id) const;

    using RegionRange = std::pair<TiKVRangeKey, TiKVRangeKey>;

    RegionMap getRegionsByRangeOverlap(const RegionRange & range) const;

    void traverseRegions(std::function<void(RegionID, const RegionPtr &)> && callback) const;

    void gcRegionPersistedCache(Seconds gc_persist_period = Seconds(60 * 5));

    void tryPersist(RegionID region_id);

    static void tryFlushRegionCacheInStorage(TMTContext & tmt, const Region & region, Poco::Logger * log);

    size_t regionSize() const;
    EngineStoreApplyRes handleAdminRaftCmd(raft_cmdpb::AdminRequest && request,
                                           raft_cmdpb::AdminResponse && response,
                                           UInt64 region_id,
                                           UInt64 index,
                                           UInt64 term,
                                           TMTContext & tmt);
    EngineStoreApplyRes handleWriteRaftCmd(
        raft_cmdpb::RaftCmdRequest && request,
        UInt64 region_id,
        UInt64 index,
        UInt64 term,
        TMTContext & tmt);
    EngineStoreApplyRes handleWriteRaftCmd(const WriteCmdsView & cmds, UInt64 region_id, UInt64 index, UInt64 term, TMTContext & tmt);

    void handleApplySnapshot(metapb::Region && region, uint64_t peer_id, const SSTViewVec, uint64_t index, uint64_t term, TMTContext & tmt);
    RegionPreDecodeBlockDataPtr preHandleSnapshotToBlock(
        RegionPtr new_region,
        const SSTViewVec,
        uint64_t index,
        uint64_t term,
        TMTContext & tmt);
    std::vector<UInt64> /*   */ preHandleSnapshotToFiles(
        RegionPtr new_region,
        const SSTViewVec,
        uint64_t index,
        uint64_t term,
        TMTContext & tmt);
    template <typename RegionPtrWrap>
    void handlePreApplySnapshot(const RegionPtrWrap &, TMTContext & tmt);

    void handleDestroy(UInt64 region_id, TMTContext & tmt);
    void setRegionCompactLogConfig(UInt64, UInt64, UInt64);
    EngineStoreApplyRes handleIngestSST(UInt64 region_id, const SSTViewVec, UInt64 index, UInt64 term, TMTContext & tmt);
    RegionPtr genRegionPtr(metapb::Region && region, UInt64 peer_id, UInt64 index, UInt64 term);
    const TiFlashRaftProxyHelper * getProxyHelper() const { return proxy_helper; }

    TiDB::SnapshotApplyMethod applyMethod() const { return snapshot_apply_method; }

    void addReadIndexEvent(Int64 f) { read_index_event_flag += f; }
    Int64 getReadIndexEvent() const { return read_index_event_flag; }

    void setStore(metapb::Store);

    // May return 0 if uninitialized
    uint64_t getStoreID(std::memory_order = std::memory_order_relaxed) const;

    BatchReadIndexRes batchReadIndex(const std::vector<kvrpcpb::ReadIndexRequest> & req, uint64_t timeout_ms) const;

    /// Initialize read-index worker context. It only can be invoked once.
    /// `worker_coefficient` means `worker_coefficient * runner_cnt` workers will be created.
    /// `runner_cnt` means number of runner which controls behavior of worker.
    void initReadIndexWorkers(
        std::function<std::chrono::milliseconds()> && fn_min_dur_handle_region,
        size_t runner_cnt,
        size_t worker_coefficient = 64);

    /// Create `runner_cnt` threads to run ReadIndexWorker asynchronously and automatically.
    /// If there is other runtime framework, DO NOT invoke it.
    void asyncRunReadIndexWorkers();

    /// Stop workers after there is no more read-index task.
    void stopReadIndexWorkers();

    /// TODO: if supported by runtime framework, run one round for specific runner by `id`.
    void runOneRoundOfReadIndexRunner(size_t runner_id);

    ~KVStore();

private:
    friend class MockTiDB;
    friend struct MockTiDBTable;
    friend struct MockRaftCommand;
    friend class RegionMockTest;
    friend class NaturalDag;
    friend void RegionBench::concurrentBatchInsert(const TiDB::TableInfo &, Int64, Int64, Int64, UInt64, UInt64, Context &);
    using DBGInvokerPrinter = std::function<void(const std::string &)>;
    friend void dbgFuncRemoveRegion(Context &, const ASTs &, DBGInvokerPrinter);
    friend void dbgFuncPutRegion(Context &, const ASTs &, DBGInvokerPrinter);
    friend class tests::RegionKVStoreTest;
    friend class ReadIndexStressTest;
    struct StoreMeta
    {
        using Base = metapb::Store;
        Base base;
        std::atomic_uint64_t store_id{0};
        void update(Base &&);
        friend class KVStore;
    };
    StoreMeta & getStore();
    const StoreMeta & getStore() const;

    std::vector<UInt64> preHandleSSTsToDTFiles(
        RegionPtr new_region,
        const SSTViewVec,
        uint64_t index,
        uint64_t term,
        DM::FileConvertJobType,
        TMTContext & tmt);

    template <typename RegionPtrWrap>
    void checkAndApplySnapshot(const RegionPtrWrap &, TMTContext & tmt);
    template <typename RegionPtrWrap>
    void onSnapshot(const RegionPtrWrap &, RegionPtr old_region, UInt64 old_region_index, TMTContext & tmt);

    RegionPtr handleIngestSSTByDTFile(const RegionPtr & region, const SSTViewVec, UInt64 index, UInt64 term, TMTContext & tmt);

    // Remove region from this TiFlash node.
    // If region is destroy or moved to another node(change peer),
    // set `remove_data` true to remove obsolete data from storage.
    void removeRegion(RegionID region_id,
                      bool remove_data,
                      RegionTable & region_table,
                      const KVStoreTaskLock & task_lock,
                      const RegionTaskLock & region_lock);
    void mockRemoveRegion(RegionID region_id, RegionTable & region_table);
    KVStoreTaskLock genTaskLock() const;

    RegionManager::RegionReadLock genRegionReadLock() const;

    RegionManager::RegionWriteLock genRegionWriteLock(const KVStoreTaskLock &);

    EngineStoreApplyRes handleUselessAdminRaftCmd(
        raft_cmdpb::AdminCmdType cmd_type,
        UInt64 curr_region_id,
        UInt64 index,
        UInt64 term,
        TMTContext & tmt);

    void persistRegion(const Region & region, const RegionTaskLock & region_task_lock, const char * caller);
    void releaseReadIndexWorkers();
    void handleDestroy(UInt64 region_id, TMTContext & tmt, const KVStoreTaskLock &);

private:
    RegionManager region_manager;

    RegionPersister region_persister;

    std::atomic<Timepoint> last_gc_time = Timepoint::min();

    mutable std::mutex task_mutex;

    // raft_cmd_res stores the result of applying raft cmd. It must be protected by task_mutex.
    std::unique_ptr<RaftCommandResult> raft_cmd_res;

    TiDB::SnapshotApplyMethod snapshot_apply_method;

    Poco::Logger * log;

    std::atomic<UInt64> REGION_COMPACT_LOG_PERIOD;
    std::atomic<UInt64> REGION_COMPACT_LOG_MIN_ROWS;
    std::atomic<UInt64> REGION_COMPACT_LOG_MIN_BYTES;

    mutable std::mutex bg_gc_region_data_mutex;
    std::list<RegionDataReadInfoList> bg_gc_region_data;

    const TiFlashRaftProxyHelper * proxy_helper{nullptr};

    // It should be initialized after `proxy_helper` is set.
    // It should be visited from outside after status of proxy is `Running`
    ReadIndexWorkerManager * read_index_worker_manager{nullptr};

    std::atomic_int64_t read_index_event_flag{0};

    StoreMeta store;
};

/// Encapsulation of lock guard of task mutex in KVStore
class KVStoreTaskLock : private boost::noncopyable
{
    friend class KVStore;
    explicit KVStoreTaskLock(std::mutex & mutex_)
        : lock(mutex_)
    {}
    std::lock_guard<std::mutex> lock;
};

void WaitCheckRegionReady(const TMTContext &, const std::atomic_size_t & terminate_signals_counter);
void WaitCheckRegionReady(const TMTContext &, const std::atomic_size_t &, double, double, double);

} // namespace DB
