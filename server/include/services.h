#ifndef SERVICES_H
#define SERVICES_H

#include "database.h"
#include <mutex>

class AuthService {
    Storage& db;
    std::mutex authMutex;

public:
    AuthService(Storage& database) : db(database) {}

    std::string login(const std::string& email, const std::string& password) {
        std::lock_guard<std::mutex> lock(authMutex);
        auto users = db.get_all<User>(sql::where(sql::c(&User::email) == email));
        if (!users.empty() && password == users[0].passwordHash) {
            return "dummy_token_for_user_" + std::to_string(users[0].id);
        }
        throw std::runtime_error("Invalid credentials");
    }

    int validateTokenAndGetUserId(const std::string& token) {
        std::string prefix = "dummy_token_for_user_";
        if (token.rfind(prefix, 0) == 0) {
            return std::stoi(token.substr(prefix.size()));
        }
        return -1;
    }

    bool isAdmin(int userId) {
        if (userId < 0) return false;
        auto userPtr = db.get_pointer<User>(userId);
        return userPtr && userPtr->isAdmin;
    }
};

class InventoryService {
    Storage& db;
    std::mutex inventoryMutex;

public:
    InventoryService(Storage& database) : db(database) {}

    void addProduct(const std::string& name, double price, int stock, int threshold) {
        std::lock_guard<std::mutex> lock(inventoryMutex);
        db.transaction([&] {
            Product p{-1, name, price, stock, threshold};
            db.replace(p);
            return true;
        });
    }

    void removeProduct(int productId) {
        std::lock_guard<std::mutex> lock(inventoryMutex);
        db.transaction([&] {
            db.remove<Product>(productId);
            return true;
        });
    }

    void updateStock(int productId, int newStock) {
        std::lock_guard<std::mutex> lock(inventoryMutex);
        db.transaction([&] {
            auto productPtr = db.get_pointer<Product>(productId);
            if (productPtr) {
                productPtr->stock = (newStock < 0 ? 0 : newStock);
                db.update(*productPtr);
            }
            return true;
        });
    }

    void updateThreshold(int productId, int threshold) {
        std::lock_guard<std::mutex> lock(inventoryMutex);
        db.transaction([&] {
            auto productPtr = db.get_pointer<Product>(productId);
            if (productPtr) {
                productPtr->soldOutThreshold = (threshold < 0 ? 0 : threshold);
                db.update(*productPtr);
            }
            return true;
        });
    }
};

#endif
