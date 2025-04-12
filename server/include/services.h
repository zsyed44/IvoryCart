#ifndef SERVICES_H
#define SERVICES_H

#include "database.h"
#include <mutex>

class InventoryService {
    Storage& db;
    std::mutex inventoryMutex;
    
public:
    InventoryService(Storage& database) : db(database) {}

    bool reserveStock(int productId, int quantity) {
        std::lock_guard<std::mutex> lock(inventoryMutex);
        return db.transaction([&] {
            auto product = db.get<Product>(productId);
            if (product.stock >= quantity) {
                product.stock -= quantity;
                db.update(product);
                return true;
            }
            return false;
        });
    }
};

#endif
