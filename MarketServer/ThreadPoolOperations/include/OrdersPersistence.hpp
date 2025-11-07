#pragma once

#include <Orders.hpp>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/write_batch.h>

namespace ThreadPoolOperations
{
    class OrdersPersistence
    {
    private:
        std::string Path;
        rocksdb::DB *db;
        int MaxThreadsNum;
        rocksdb::ColumnFamilyHandle *DefaultTable;
        rocksdb::ColumnFamilyHandle *StatusTable;
        rocksdb::Status Status;

    public:
        OrdersPersistence(std::string &path);
        ~OrdersPersistence();

        rocksdb::Status PutOrder(Order order);
        rocksdb::Status PersistOrder(Order order);
        rocksdb::Status GetOrder(std::string key, Order *order);
        rocksdb::Status DeleteOrder(std::string key);
    };

    OrdersPersistence Open(std::string &path);
    void Close(OrdersPersistence db);
} // namespace ThreadPoolOperations
