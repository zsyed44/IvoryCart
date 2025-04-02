#include "crow.h"
#include "database.h"
#include "services.h"

int main() {
    crow::SimpleApp app;
    
    // Initialize database
    auto db = initStorage("ecommerce.db");
    db.sync_schema();
    
    // Initialize services
    AuthService authService(db);
    InventoryService inventoryService(db);
    
    // Setup routes
    void setupRoutes(crow::SimpleApp& app, AuthService& auth, InventoryService& inventory, Storage& db);
    
    // Configure CORS
    app.loglevel(crow::LogLevel::Warning);
    CROW_ROUTE(app, "/")
    ([]{
        return "E-Commerce Server";
    });

    std::cout << "🛒 E-Commerce Server is live on http://localhost:8080" << std::endl;
    
    app.port(8080).multithreaded().run();
}