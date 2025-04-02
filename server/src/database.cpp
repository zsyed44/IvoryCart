#include "database.h"
using namespace sqlite_orm;

Storage initStorage(const std::string& filename) {
    return make_storage(
        filename,
        make_table("Users",
            make_column("id", &User::id, primary_key().autoincrement()),
            make_column("name", &User::name),
            make_column("email", &User::email, unique()),
            make_column("passwordHash", &User::passwordHash),
            make_column("sessionId", &User::sessionId)),

        make_table("Products",
            make_column("id", &Product::id, primary_key().autoincrement()),
            make_column("name", &Product::name),
            make_column("price", &Product::price),
            make_column("stock", &Product::stock))

        // Add other tables here
    );
}
