#include "Engine/Data/SqliteStore.h"
#include "Engine/Data/DataValueCodec.h"
#include <future>
#include <iostream>
#include <stdexcept>
using namespace Rice::Data;
int main(int argc,char** argv){
    try {
        if(argc!=3)throw std::runtime_error("Expected data directory and write/read mode");
        const std::filesystem::path directory=argv[1];std::filesystem::create_directories(directory);
        SqliteStore store(directory/"retirement.sqlite3");
        DataRecord record;record.key.domain="test";record.key.build="retirement";record.key.dataId="value";
        record.value=EncodeValue(0);
        if(std::string(argv[2])=="read"){
            auto saved=store.Get(record.key);
            if(!saved || DecodeValue<int>(saved->value)!=42)throw std::runtime_error("Retirement commit did not survive process exit");
            return 0;
        }
        store.Ensure(record);
        std::promise<void> entered,release;
        auto gate=release.get_future();
        auto writer=std::async(std::launch::async,[&]{
            store.Mutate(record,[&](const EncodedValue&){entered.set_value();gate.wait();return EncodeValue(42);});
        });
        entered.get_future().wait();
        const bool premature=SqliteStore::TrySealProcessWrites();
        release.set_value();writer.get();
        if(premature)throw std::runtime_error("Save barrier acknowledged an in-flight transaction");
        if(!SqliteStore::TrySealProcessWrites() || !SqliteStore::TrySealProcessWrites())throw std::runtime_error("Completed barrier is not idempotent");
        bool rejected=false;
        try {store.Mutate(record,[](const EncodedValue&){return EncodeValue(99);});}catch(const DataError&){rejected=true;}
        if(!rejected || DecodeValue<int>(store.Get(record.key)->value)!=42)throw std::runtime_error("Late write crossed retirement barrier");
        std::cout<<"Durable retirement seal passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
