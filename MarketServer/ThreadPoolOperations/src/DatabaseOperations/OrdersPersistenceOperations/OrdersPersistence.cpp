#include <OrdersPersistence.hpp>
#include <cstdlib>

namespace ThreadPoolOperations
{
    OrdersPersistence::OrdersPersistence(std::string &path)
        : Path(path)
    {
        rocksdb::Options options;

        options.create_if_missing = true;

        int nbCores =
            std::atoi(std::getenv("NB_CORES") ? std::getenv("NB_CORES") : "-1");

        int nbDB =
            std::atoi(std::getenv("NB_DB") ? std::getenv("NB_DB") : "-1");

        if (nbCores < 1 || nbDB < 1)
        {
            Status =
                rocksdb::Status::InvalidArgument("nbCores < 1  || nbDB < 1");
        }

        options.max_background_jobs = nbCores / nbDB / 2;

        std::vector<rocksdb::ColumnFamilyDescriptor> cf_descriptors;
        rocksdb::ColumnFamilyOptions cf_options;

        cf_descriptors.push_back(rocksdb::ColumnFamilyDescriptor(
            rocksdb::kDefaultColumnFamilyName, cf_options));
        cf_descriptors.push_back(
            rocksdb::ColumnFamilyDescriptor("ORDER_STATUS", cf_options));

        std::vector<rocksdb::ColumnFamilyHandle *> handles;

        Status =
            rocksdb::DB::Open(options, path, cf_descriptors, &handles, &db);

        if (Status.ok())
        {
            for (auto it = handles.begin(); it != handles.end();)
            {
                auto handle = *it;
                std::string cf_name = handle->GetName();
                if (cf_name == rocksdb::kDefaultColumnFamilyName)
                {
                    DefaultTable = handle;
                }
                else if (cf_name == "ORDER_STATUS")
                {
                    StatusTable = handle;
                }
                else
                {
                    delete *it;
                    it = handles.erase(it);
                    continue;
                }

                ++it;
            }
        }
    }

    OrdersPersistence::~OrdersPersistence()
    {
        if (Status.ok())
        {
            delete db;
            delete DefaultTable;
            delete StatusTable;
            db = nullptr;
            DefaultTable = nullptr;
            StatusTable = nullptr;
        }
    }

    rocksdb::Status OrdersPersistence::PutOrder(Order order)
    {
        return db->Put(rocksdb::WriteOptions(), DefaultTable, order.getKey(),
                       serializeOrder(order));
    }
    rocksdb::Status OrdersPersistence::PersistOrder(Order order)
    {
        rocksdb::WriteBatch batch;

        batch.Put(DefaultTable, order.getKey(), serializeOrder(order));

        // If executed, key = "{assetID}:1:{orderID}"
        // else, key = "{assetID}:0:{orderID}"
        if (order.getAmount() > 0)
        {
            std::string toDelete =
                std::to_string(order.getAsset()) + ":0:" + order.getKey();

            batch.Delete(StatusTable, toDelete);

            std::string toWrite =
                std::to_string(order.getAsset()) + ":1:" + order.getKey();

            batch.Put(StatusTable, toWrite, order.getKey());
        }
        else
        {
            std::string toDelete =
                std::to_string(order.getAsset()) + ":1:" + order.getKey();

            batch.Delete(StatusTable, toDelete);

            std::string toWrite =
                std::to_string(order.getAsset()) + ":0:" + order.getKey();

            batch.Put(StatusTable, toWrite, order.getKey());
        }

        rocksdb::WriteOptions write_options;
        return db->Write(write_options, &batch);
    }
    rocksdb::Status OrdersPersistence::GetOrder(std::string key, Order *order)
    {
        std::string binOrder;

        rocksdb::Status status =
            db->Get(rocksdb::ReadOptions(), key, &binOrder);

        bool valid = parseBinOrder(binOrder.c_str(), binOrder.size(), order);

        if (!valid)
        {
            return rocksdb::Status::Corruption("can't be parsed as order");
        }

        return status;
    }
    rocksdb::Status OrdersPersistence::DeleteOrder(std::string key)
    {
        rocksdb::Status status = db->Delete(rocksdb::WriteOptions(), key);

        return status;
    }
} // namespace ThreadPoolOperations
