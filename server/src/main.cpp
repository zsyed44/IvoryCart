#include "crow.h"
#include "database.h"
#include "services.h"
#include "auth.h"
#include "routes.h" // Include the header file where setupRoutes is declared

// httplib

int main() {
    crow::SimpleApp app;

    auto db = initStorage("ecommerce.db");
    db.sync_schema();

    AuthService authService(db);
    InventoryService inventoryService(db);

    CROW_ROUTE(app, "/")
    ([] {
        return "E-Commerce Server";
    });

    std::cout << "🛒 E-Commerce Server is live on http://localhost:8080" << std::endl;
    app.port(8080).multithreaded().run();
}
