#include "crow.h"
#include "services.h"
#include "database.h"

void setupRoutes(crow::SimpleApp& app, AuthService& auth, InventoryService& inventory, Storage& db) {
    CROW_ROUTE(app, "/api/login").methods("POST"_method)
    ([&auth](const crow::request& req) {
        auto json = crow::json::load(req.body);
        std::string token = auth.login(json["email"].s(), json["password"].s());
        crow::json::wvalue result;
        result["token"] = token;
        return crow::response{200, result};
    });

    CROW_ROUTE(app, "/api/products").methods("GET"_method)
    ([&db]() {
        auto products = db.get_all<Product>();
        crow::json::wvalue result;
        result["products"] = crow::json::wvalue::list(products.size());
        for (size_t i = 0; i < products.size(); ++i) {
            result["products"][i]["id"] = products[i].id;
            result["products"][i]["name"] = products[i].name;
            result["products"][i]["price"] = products[i].price;
            result["products"][i]["stock"] = products[i].stock;
        }
        return crow::response{200, result};
    });

    CROW_ROUTE(app, "/api/checkout").methods("POST"_method)
    ([&db](const crow::request& req) {
        auto json = crow::json::load(req.body);
        bool success = db.transaction([&] {
            // Simulate successful checkout logic
            return true;
        });
        return success ? crow::response(200) : crow::response(400);
    });
}
