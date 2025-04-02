#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite_orm/sqlite_orm.h>

namespace sql = sqlite_orm;

struct User {
    int id;
    std::string name;
    std::string email;
    std::string passwordHash;
    std::string sessionId;
};

struct Product {
    int id;
    std::string name;
    double price;
    int stock;
};

struct Order {
    int id;
    int userId;
    std::string status;
    std::string timestamp;
};

struct OrderItem {
    int orderId;
    int productId;
    int quantity;
};

struct Payment {
    int id;
    int orderId;
    double amount;
    std::string status;
};

// define Storage (the actual database type)
using Storage = decltype(sql::make_storage("dummy.db",
    sql::make_table("Users",
        sql::make_column("id", &User::id, sql::primary_key().autoincrement()),
        sql::make_column("name", &User::name),
        sql::make_column("email", &User::email, sql::unique()),
        sql::make_column("passwordHash", &User::passwordHash),
        sql::make_column("sessionId", &User::sessionId)),
    sql::make_table("Products",
        sql::make_column("id", &Product::id, sql::primary_key().autoincrement()),
        sql::make_column("name", &Product::name),
        sql::make_column("price", &Product::price),
        sql::make_column("stock", &Product::stock))
    // Add others here...
));

// Declare initStorage function
Storage initStorage(const std::string& filename);

#endif
