#ifndef SERVICES_H
#define SERVICES_H

#include "database.h"
#include <bcrypt/bcrypt.h>
#include <mutex>

class AuthService {
    Storage& db;
    std::mutex authMutex;
    
public:
    AuthService(Storage& database) : db(database) {}
    
    std::string login(const std::string& email, const std::string& password) {
        std::lock_guard<std::mutex> lock(authMutex);
        auto users = db.get_all<User>(sql::where(sql::c(&User::email) = email));
        // if(!users.empty() && bcrypt::validatePassword(password, users[0].passwordHash)) {
        if (!users.empty() && password == users[0].passwordHash) {
        // Generate JWT token
            return "generated_jwt_token";
        }
        throw std::runtime_error("Invalid credentials");
    }
};

class InventoryService {
    Storage& db;
    std::mutex inventoryMutex;
    
public:
    InventoryService(Storage& database) : db(database) {}
    
    bool reserveStock(int productId, int quantity) {
        std::lock_guard<std::mutex> lock(inventoryMutex);
        return db.transaction([&] {
            auto product = db.get<Product>(productId);
            if(product.stock >= quantity) {
                product.stock -= quantity;
                db.update(product);
                return true;
            }
            return false;
        });
    }
};

#endif