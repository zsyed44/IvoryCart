#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <sqlite3.h>
#include <iostream>
#include <mutex>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <queue>
#include <sstream>
#include <vector>
#include <string>
#include <ctime>
#include <random>
#include <algorithm>

using namespace std;

// --------------------------
// Database Setup & Utilities
// --------------------------
sqlite3 *db = nullptr;
mutex db_mutex;

void init_database()
{
    int rc = sqlite3_open("bidding.db", &db);
    if (rc != SQLITE_OK)
    {
        cerr << "Cannot open database: " << sqlite3_errmsg(db) << endl;
        exit(1);
    }

    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", 0, 0, 0);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", 0, 0, 0);

    const char *sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    username TEXT UNIQUE NOT NULL,"
        "    password_hash TEXT NOT NULL,"
        "    is_admin INTEGER DEFAULT 0);"
        
        "CREATE TABLE IF NOT EXISTS items ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    name TEXT NOT NULL,"
        "    description TEXT,"
        "    listing_type TEXT NOT NULL,"  // 'auction' or 'fixed'
        "    current_bid REAL DEFAULT 0.0,"
        "    fixed_price REAL DEFAULT 0.0,"
        "    inventory INTEGER DEFAULT 1,"
        "    bidder_id INTEGER,"
        "    end_time INTEGER,"  // Unix timestamp for auction end
        "    version INTEGER DEFAULT 1);"
        
        "CREATE TABLE IF NOT EXISTS bids ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    item_id INTEGER NOT NULL,"
        "    user_id INTEGER NOT NULL,"
        "    amount REAL NOT NULL,"
        "    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);"
        
        "CREATE TABLE IF NOT EXISTS orders ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    user_id INTEGER NOT NULL,"
        "    total_amount REAL NOT NULL,"
        "    status TEXT DEFAULT 'pending',"  // pending, paid, shipped, completed
        "    created_at DATETIME DEFAULT CURRENT_TIMESTAMP);"
        
        "CREATE TABLE IF NOT EXISTS order_items ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    order_id INTEGER NOT NULL,"
        "    item_id INTEGER NOT NULL,"
        "    quantity INTEGER NOT NULL,"
        "    price REAL NOT NULL,"
        "    is_auction BOOLEAN NOT NULL);"
        
        "CREATE TABLE IF NOT EXISTS cart ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    user_id INTEGER NOT NULL,"
        "    item_id INTEGER NOT NULL,"
        "    quantity INTEGER NOT NULL,"
        "    added_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "    UNIQUE(user_id, item_id));"
        
        "CREATE TABLE IF NOT EXISTS payments ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    order_id INTEGER NOT NULL,"
        "    amount REAL NOT NULL,"
        "    payment_method TEXT NOT NULL,"
        "    status TEXT DEFAULT 'pending',"
        "    transaction_id TEXT,"
        "    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);";

    char *errMsg = 0;
    rc = sqlite3_exec(db, sql, 0, 0, &errMsg);
    if (rc != SQLITE_OK)
    {
        cerr << "SQL error: " << errMsg << endl;
        sqlite3_free(errMsg);
    }
}

// --------------------------
// Core Data Structures
// --------------------------
struct CartItem {
    int item_id;
    int quantity;
};

struct UserSession {
    int user_id;
    chrono::steady_clock::time_point last_activity;
    unordered_map<int, CartItem> cart;  // Maps item_id to CartItem
    weak_ptr<ix::WebSocket> ws;
};

struct Item
{
    int id;
    string name;
    string description;
    string listing_type;  // "auction" or "fixed"
    double current_bid = 0.0;
    double fixed_price = 0.0;
    int inventory = 1;
    int bidder_id = -1;
    int64_t end_time = 0;  // Unix timestamp for auction end
    int version = 1;
    queue<pair<int, double>> bid_queue;
};

class ItemsMonitor
{
private:
    mutex mtx;
    condition_variable cv;

public:
    unique_lock<mutex> get_lock() { return unique_lock<mutex>(mtx); }
    void wait(unique_lock<mutex> &lock) { cv.wait(lock); }
    void notify() { cv.notify_one(); }
};

mutex sessions_mutex;
mutex clients_mutex;
ItemsMonitor items_monitor;
unordered_map<string, UserSession> active_sessions;
unordered_map<int, Item> items;
vector<shared_ptr<ix::WebSocket>> connected_clients;

// --------------------------
// Utility Functions
// --------------------------
string generate_uuid()
{
    static random_device rd;
    static mt19937 gen(rd());
    uniform_int_distribution<> dis(0, 15);
    uniform_int_distribution<> dis2(8, 11);

    stringstream ss;
    ss << hex;
    for (int i = 0; i < 8; i++)
        ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; i++)
        ss << dis(gen);
    ss << "-4";
    for (int i = 0; i < 3; i++)
        ss << dis(gen);
    ss << "-";
    ss << dis2(gen);
    for (int i = 0; i < 3; i++)
        ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; i++)
        ss << dis(gen);
    return ss.str();
}

void broadcast(const string &message)
{
    vector<shared_ptr<ix::WebSocket>> clients_copy;
    {
        lock_guard<mutex> lock(clients_mutex);
        clients_copy = connected_clients;
    }

    for (auto &client : clients_copy)
    {
        if (client)
            client->send(message);
    }
}

// --------------------------
// Database Operations
// --------------------------
int authenticate_user(const string &username, const string &password)
{
    lock_guard<mutex> db_lock(db_mutex);
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id FROM users WHERE username = ? AND password_hash = ?";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        cerr << "Prepare failed: " << sqlite3_errmsg(db) << endl;
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);

    int user_id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        user_id = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return user_id;
}

void load_items_from_db()
{
    lock_guard<mutex> db_lock(db_mutex);
    auto lock = items_monitor.get_lock();
    items.clear();

    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, name, description, listing_type, current_bid, fixed_price, "
                      "inventory, bidder_id, end_time, version FROM items";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << endl;
        return;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        Item item;
        item.id = sqlite3_column_int(stmt, 0);
        item.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        
        // Description might be NULL
        if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
            item.description = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        }
        
        item.listing_type = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
        item.current_bid = sqlite3_column_double(stmt, 4);
        item.fixed_price = sqlite3_column_double(stmt, 5);
        item.inventory = sqlite3_column_int(stmt, 6);
        item.bidder_id = sqlite3_column_int(stmt, 7);
        item.end_time = sqlite3_column_int64(stmt, 8);
        item.version = sqlite3_column_int(stmt, 9);
        
        items[item.id] = item;
    }

    sqlite3_finalize(stmt);
}

void seed_test_data()
{
    lock_guard<mutex> db_lock(db_mutex);
    // In production: Use proper password hashing (e.g., bcrypt)
    const char *users_sql =
        "INSERT OR IGNORE INTO users (id, username, password_hash, is_admin) VALUES "
        "(1, 'admin', 'admin', 1), "
        "(2, 'user1', 'pass1', 0), "
        "(3, 'user2', 'pass2', 0);";
    sqlite3_exec(db, users_sql, 0, 0, 0);

    // Get current time
    int64_t now = time(nullptr);
    int64_t one_day = 24 * 60 * 60;
    
    // Clear existing items
    sqlite3_exec(db, "DELETE FROM items", 0, 0, 0);
    
    // Create some auction items
    sqlite3_stmt *auction_stmt;
    const char *auction_sql = 
        "INSERT INTO items (name, description, listing_type, current_bid, inventory, end_time) VALUES "
        "(?, ?, 'auction', ?, 1, ?)";
    
    if (sqlite3_prepare_v2(db, auction_sql, -1, &auction_stmt, nullptr) == SQLITE_OK)
    {
        // Item 1: Ending in 1 hour
        sqlite3_bind_text(auction_stmt, 1, "Antique Chair", -1, SQLITE_STATIC);
        sqlite3_bind_text(auction_stmt, 2, "A beautiful handcrafted chair from the 18th century", -1, SQLITE_STATIC);
        sqlite3_bind_double(auction_stmt, 3, 100.0);
        sqlite3_bind_int64(auction_stmt, 4, now + 3600);  // 1 hour from now
        sqlite3_step(auction_stmt);
        sqlite3_reset(auction_stmt);
        
        // Item 2: Ending in 1 day
        sqlite3_bind_text(auction_stmt, 1, "Vintage Painting", -1, SQLITE_STATIC);
        sqlite3_bind_text(auction_stmt, 2, "An original oil painting from a renowned artist", -1, SQLITE_STATIC);
        sqlite3_bind_double(auction_stmt, 3, 500.0);
        sqlite3_bind_int64(auction_stmt, 4, now + one_day);
        sqlite3_step(auction_stmt);
        sqlite3_reset(auction_stmt);
        
        // Item 3: Ending in 3 days
        sqlite3_bind_text(auction_stmt, 1, "Rare Coin Collection", -1, SQLITE_STATIC);
        sqlite3_bind_text(auction_stmt, 2, "A collection of rare coins from around the world", -1, SQLITE_STATIC);
        sqlite3_bind_double(auction_stmt, 3, 1000.0);
        sqlite3_bind_int64(auction_stmt, 4, now + 3 * one_day);
        sqlite3_step(auction_stmt);
        
        sqlite3_finalize(auction_stmt);
    }
    
    // Create some fixed-price items
    sqlite3_stmt *fixed_stmt;
    const char *fixed_sql = 
        "INSERT INTO items (name, description, listing_type, fixed_price, inventory) VALUES "
        "(?, ?, 'fixed', ?, ?)";
    
    if (sqlite3_prepare_v2(db, fixed_sql, -1, &fixed_stmt, nullptr) == SQLITE_OK)
    {
        // Item 1
        sqlite3_bind_text(fixed_stmt, 1, "Designer Watch", -1, SQLITE_STATIC);
        sqlite3_bind_text(fixed_stmt, 2, "A luxury watch with premium materials", -1, SQLITE_STATIC);
        sqlite3_bind_double(fixed_stmt, 3, 299.99);
        sqlite3_bind_int(fixed_stmt, 4, 5);
        sqlite3_step(fixed_stmt);
        sqlite3_reset(fixed_stmt);
        
        // Item 2
        sqlite3_bind_text(fixed_stmt, 1, "Smartphone", -1, SQLITE_STATIC);
        sqlite3_bind_text(fixed_stmt, 2, "The latest smartphone with advanced features", -1, SQLITE_STATIC);
        sqlite3_bind_double(fixed_stmt, 3, 699.99);
        sqlite3_bind_int(fixed_stmt, 4, 10);
        sqlite3_step(fixed_stmt);
        sqlite3_reset(fixed_stmt);
        
        // Item 3
        sqlite3_bind_text(fixed_stmt, 1, "Leather Jacket", -1, SQLITE_STATIC);
        sqlite3_bind_text(fixed_stmt, 2, "A genuine leather jacket, perfect for all seasons", -1, SQLITE_STATIC);
        sqlite3_bind_double(fixed_stmt, 3, 199.99);
        sqlite3_bind_int(fixed_stmt, 4, 8);
        sqlite3_step(fixed_stmt);
        
        sqlite3_finalize(fixed_stmt);
    }
}

// --------------------------
// Bid Processing & Cart Operations
// --------------------------
void process_bid(Item &item, int user_id, double amount)
{
    // Don't process bids for non-auction items or ended auctions
    if (item.listing_type != "auction") {
        return;
    }
    
    // Check if auction has ended
    if (item.end_time > 0 && item.end_time < time(nullptr)) {
        return;
    }

    const auto timeout = chrono::seconds(5);
    const auto start = chrono::steady_clock::now();
    bool success = false;

    while (!success && chrono::steady_clock::now() - start < timeout)
    {
        lock_guard<mutex> db_lock(db_mutex);
        sqlite3_exec(db, "BEGIN IMMEDIATE", 0, 0, 0);

        sqlite3_stmt *check_stmt;
        const char *check_sql = "SELECT current_bid, version FROM items WHERE id = ?";

        if (sqlite3_prepare_v2(db, check_sql, -1, &check_stmt, nullptr) != SQLITE_OK)
        {
            sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
            continue;
        }

        sqlite3_bind_int(check_stmt, 1, item.id);
        if (sqlite3_step(check_stmt) == SQLITE_ROW)
        {
            double current_bid = sqlite3_column_double(check_stmt, 0);
            int db_version = sqlite3_column_int(check_stmt, 1);

            if (amount > current_bid && db_version == item.version)
            {
                sqlite3_stmt *update_stmt;
                const char *update_sql =
                    "UPDATE items SET current_bid = ?, bidder_id = ?, version = ? WHERE id = ?";

                if (sqlite3_prepare_v2(db, update_sql, -1, &update_stmt, nullptr) == SQLITE_OK)
                {
                    sqlite3_bind_double(update_stmt, 1, amount);
                    sqlite3_bind_int(update_stmt, 2, user_id);
                    sqlite3_bind_int(update_stmt, 3, db_version + 1);
                    sqlite3_bind_int(update_stmt, 4, item.id);

                    if (sqlite3_step(update_stmt) == SQLITE_DONE)
                    {
                        success = true;
                        item.version = db_version + 1;
                    }
                    sqlite3_finalize(update_stmt);
                }
            }
        }
        sqlite3_finalize(check_stmt);

        if (success)
        {
            sqlite3_exec(db, "COMMIT", 0, 0, 0);

            sqlite3_stmt *insert_stmt;
            const char *insert_sql =
                "INSERT INTO bids (item_id, user_id, amount) VALUES (?, ?, ?)";
            if (sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_int(insert_stmt, 1, item.id);
                sqlite3_bind_int(insert_stmt, 2, user_id);
                sqlite3_bind_double(insert_stmt, 3, amount);
                sqlite3_step(insert_stmt);
                sqlite3_finalize(insert_stmt);
            }
        }
        else
        {
            sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
        }
    }

    if (success)
    {
        item.current_bid = amount;
        item.bidder_id = user_id;
        broadcast("ITEM_UPDATE|" + to_string(item.id) + "," + item.name + "," 
                 + item.listing_type + "," + to_string(item.current_bid) + "," 
                 + to_string(item.fixed_price) + "," + to_string(item.inventory) + ","
                 + to_string(item.bidder_id) + "," + to_string(item.end_time));
    }
}

bool add_to_cart(int user_id, int item_id, int quantity)
{
    lock_guard<mutex> db_lock(db_mutex);
    
    // First, check if the item exists and has enough inventory
    sqlite3_stmt *check_stmt;
    const char *check_sql = "SELECT listing_type, inventory FROM items WHERE id = ?";
    
    if (sqlite3_prepare_v2(db, check_sql, -1, &check_stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_int(check_stmt, 1, item_id);
    
    if (sqlite3_step(check_stmt) != SQLITE_ROW) {
        sqlite3_finalize(check_stmt);
        return false;
    }
    
    string listing_type = reinterpret_cast<const char *>(sqlite3_column_text(check_stmt, 0));
    int inventory = sqlite3_column_int(check_stmt, 1);
    
    sqlite3_finalize(check_stmt);
    
    // Only fixed-price items can be added to cart
    if (listing_type != "fixed" || inventory < quantity) {
        return false;
    }
    
    // Now add or update the cart
    sqlite3_stmt *upsert_stmt;
    const char *upsert_sql = 
        "INSERT INTO cart (user_id, item_id, quantity) VALUES (?, ?, ?) "
        "ON CONFLICT(user_id, item_id) DO UPDATE SET quantity = quantity + ?";
    
    if (sqlite3_prepare_v2(db, upsert_sql, -1, &upsert_stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_int(upsert_stmt, 1, user_id);
    sqlite3_bind_int(upsert_stmt, 2, item_id);
    sqlite3_bind_int(upsert_stmt, 3, quantity);
    sqlite3_bind_int(upsert_stmt, 4, quantity);
    
    bool success = sqlite3_step(upsert_stmt) == SQLITE_DONE;
    sqlite3_finalize(upsert_stmt);
    
    return success;
}

bool update_cart(int user_id, int item_id, int quantity)
{
    lock_guard<mutex> db_lock(db_mutex);
    
    if (quantity <= 0) {
        // Remove from cart
        sqlite3_stmt *delete_stmt;
        const char *delete_sql = "DELETE FROM cart WHERE user_id = ? AND item_id = ?";
        
        if (sqlite3_prepare_v2(db, delete_sql, -1, &delete_stmt, nullptr) != SQLITE_OK) {
            return false;
        }
        
        sqlite3_bind_int(delete_stmt, 1, user_id);
        sqlite3_bind_int(delete_stmt, 2, item_id);
        
        bool success = sqlite3_step(delete_stmt) == SQLITE_DONE;
        sqlite3_finalize(delete_stmt);
        return success;
    } else {
        // Update quantity
        sqlite3_stmt *update_stmt;
        const char *update_sql = "UPDATE cart SET quantity = ? WHERE user_id = ? AND item_id = ?";
        
        if (sqlite3_prepare_v2(db, update_sql, -1, &update_stmt, nullptr) != SQLITE_OK) {
            return false;
        }
        
        sqlite3_bind_int(update_stmt, 1, quantity);
        sqlite3_bind_int(update_stmt, 2, user_id);
        sqlite3_bind_int(update_stmt, 3, item_id);
        
        bool success = sqlite3_step(update_stmt) == SQLITE_DONE;
        sqlite3_finalize(update_stmt);
        return success;
    }
}

vector<pair<Item, int>> get_cart_items(int user_id)
{
    lock_guard<mutex> db_lock(db_mutex);
    vector<pair<Item, int>> cart_items;
    
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT i.id, i.name, i.description, i.listing_type, i.current_bid, i.fixed_price, "
        "i.inventory, i.bidder_id, i.end_time, i.version, c.quantity "
        "FROM cart c JOIN items i ON c.item_id = i.id "
        "WHERE c.user_id = ?";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return cart_items;
    }
    
    sqlite3_bind_int(stmt, 1, user_id);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Item item;
        item.id = sqlite3_column_int(stmt, 0);
        item.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        
        if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
            item.description = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        }
        
        item.listing_type = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
        item.current_bid = sqlite3_column_double(stmt, 4);
        item.fixed_price = sqlite3_column_double(stmt, 5);
        item.inventory = sqlite3_column_int(stmt, 6);
        item.bidder_id = sqlite3_column_int(stmt, 7);
        item.end_time = sqlite3_column_int64(stmt, 8);
        item.version = sqlite3_column_int(stmt, 9);
        
        int quantity = sqlite3_column_int(stmt, 10);
        cart_items.emplace_back(item, quantity);
    }
    
    sqlite3_finalize(stmt);
    return cart_items;
}

void send_cart_update_to_user(int user_id)
{
    string cart_message;

    {
        lock_guard<mutex> lock(sessions_mutex);
        for (const auto &[token, session] : active_sessions)
        {
            if (session.user_id == user_id)
            {
                auto cart_items = get_cart_items(user_id);
                stringstream response;
                response << "CART_ITEMS";

                double total = 0.0;
                for (const auto &[item, quantity] : cart_items)
                {
                    response << "|" << item.id << "," 
                             << item.name << ","
                             << item.fixed_price << ","
                             << quantity;
                    total += item.fixed_price * quantity;
                }
                response << "|TOTAL," << total;
                cart_message = response.str();

                if (auto ws_ptr = session.ws.lock())
                {
                    ws_ptr->send(cart_message);
                }
                break;
            }
        }
    }
}


void add_item(const string &name, const string &description, const string &listing_type, 
             double price, int inventory, int64_t end_time = 0)
{
    lock_guard<mutex> db_lock(db_mutex);
    sqlite3_stmt *stmt;
    
    const char *sql = 
        "INSERT INTO items (name, description, listing_type, current_bid, fixed_price, inventory, end_time) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, description.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, listing_type.c_str(), -1, SQLITE_STATIC);
        
        if (listing_type == "auction") {
            sqlite3_bind_double(stmt, 4, price);  // starting bid
            sqlite3_bind_double(stmt, 5, 0.0);    // fixed price (0 for auctions)
        } else {
            sqlite3_bind_double(stmt, 4, 0.0);    // current bid (0 for fixed)
            sqlite3_bind_double(stmt, 5, price);  // fixed price
        }
        
        sqlite3_bind_int(stmt, 6, inventory);
        sqlite3_bind_int64(stmt, 7, end_time);
        
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    load_items_from_db();
}

int create_order(int user_id, const vector<pair<Item, int>> &items, bool from_cart = true)
{
    lock_guard<mutex> db_lock(db_mutex);
    sqlite3_exec(db, "BEGIN IMMEDIATE", 0, 0, 0);
    
    // Calculate total amount
    double total = 0.0;
    for (const auto &[item, quantity] : items) {
        if (item.listing_type == "fixed") {
            total += item.fixed_price * quantity;
        } else {
            total += item.current_bid;  // For auction items, just use winning bid
        }
    }
    
    // Create order
    sqlite3_stmt *order_stmt;
    const char *order_sql = "INSERT INTO orders (user_id, total_amount) VALUES (?, ?)";
    
    if (sqlite3_prepare_v2(db, order_sql, -1, &order_stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
        return -1;
    }
    
    sqlite3_bind_int(order_stmt, 1, user_id);
    sqlite3_bind_double(order_stmt, 2, total);
    
    if (sqlite3_step(order_stmt) != SQLITE_DONE) {
        sqlite3_finalize(order_stmt);
        sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
        return -1;
    }
    
    sqlite3_finalize(order_stmt);
    int order_id = sqlite3_last_insert_rowid(db);
    
    // Add order items
    for (const auto &[item, quantity] : items) {
        sqlite3_stmt *item_stmt;
        const char *item_sql = 
            "INSERT INTO order_items (order_id, item_id, quantity, price, is_auction) "
            "VALUES (?, ?, ?, ?, ?)";
        
        if (sqlite3_prepare_v2(db, item_sql, -1, &item_stmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
            return -1;
        }
        
        sqlite3_bind_int(item_stmt, 1, order_id);
        sqlite3_bind_int(item_stmt, 2, item.id);
        sqlite3_bind_int(item_stmt, 3, quantity);
        
        if (item.listing_type == "fixed") {
            sqlite3_bind_double(item_stmt, 4, item.fixed_price);
            sqlite3_bind_int(item_stmt, 5, 0);  // not auction
        } else {
            sqlite3_bind_double(item_stmt, 4, item.current_bid);
            sqlite3_bind_int(item_stmt, 5, 1);  // is auction
        }
        
        if (sqlite3_step(item_stmt) != SQLITE_DONE) {
            sqlite3_finalize(item_stmt);
            sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
            return -1;
        }
        
        sqlite3_finalize(item_stmt);
        
        // Update inventory for fixed-price items
        if (item.listing_type == "fixed") {
            sqlite3_stmt *update_stmt;
            const char *update_sql = 
                "UPDATE items SET inventory = inventory - ? WHERE id = ? AND inventory >= ?";
            
            if (sqlite3_prepare_v2(db, update_sql, -1, &update_stmt, nullptr) != SQLITE_OK) {
                sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
                return -1;
            }
            
            sqlite3_bind_int(update_stmt, 1, quantity);
            sqlite3_bind_int(update_stmt, 2, item.id);
            sqlite3_bind_int(update_stmt, 3, quantity);
            
            if (sqlite3_step(update_stmt) != SQLITE_DONE) {
                sqlite3_finalize(update_stmt);
                sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
                return -1;
            }
            
            sqlite3_finalize(update_stmt);
        }
    }
    
    // Clear cart if order was created from cart
    if (from_cart) {
        sqlite3_stmt *clear_stmt;
        const char *clear_sql = "DELETE FROM cart WHERE user_id = ?";
        
        if (sqlite3_prepare_v2(db, clear_sql, -1, &clear_stmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
            return -1;
        }
        
        sqlite3_bind_int(clear_stmt, 1, user_id);
        
        if (sqlite3_step(clear_stmt) != SQLITE_DONE) {
            sqlite3_finalize(clear_stmt);
            sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
            return -1;
        }
        
        sqlite3_finalize(clear_stmt);
    }
    
    sqlite3_exec(db, "COMMIT", 0, 0, 0);
    load_items_from_db();  // Reload items to update inventory changes
    
    return order_id;
}

bool process_payment(int order_id, const string &payment_method, const string &transaction_id)
{
    lock_guard<mutex> db_lock(db_mutex);
    sqlite3_exec(db, "BEGIN IMMEDIATE", 0, 0, 0);
    
    // Get order amount
    sqlite3_stmt *order_stmt;
    const char *order_sql = "SELECT total_amount FROM orders WHERE id = ?";
    
    if (sqlite3_prepare_v2(db, order_sql, -1, &order_stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
        return false;
    }
    
    sqlite3_bind_int(order_stmt, 1, order_id);
    
    if (sqlite3_step(order_stmt) != SQLITE_ROW) {
        sqlite3_finalize(order_stmt);
        sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
        return false;
    }
    
    double amount = sqlite3_column_double(order_stmt, 0);
    sqlite3_finalize(order_stmt);
    
    // Create payment record
    sqlite3_stmt *payment_stmt;
    const char *payment_sql = 
        "INSERT INTO payments (order_id, amount, payment_method, status, transaction_id) "
        "VALUES (?, ?, ?, 'completed', ?)";
    
    if (sqlite3_prepare_v2(db, payment_sql, -1, &payment_stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
        return false;
    }
    
    sqlite3_bind_int(payment_stmt, 1, order_id);
    sqlite3_bind_double(payment_stmt, 2, amount);
    sqlite3_bind_text(payment_stmt, 3, payment_method.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(payment_stmt, 4, transaction_id.c_str(), -1, SQLITE_STATIC);
    
    if (sqlite3_step(payment_stmt) != SQLITE_DONE) {
        sqlite3_finalize(payment_stmt);
        sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
        return false;
    }
    
    sqlite3_finalize(payment_stmt);
    
    // Update order status
    sqlite3_stmt *update_stmt;
    const char *update_sql = "UPDATE orders SET status = 'paid' WHERE id = ?";
    
    if (sqlite3_prepare_v2(db, update_sql, -1, &update_stmt, nullptr) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
        return false;
    }
    
    sqlite3_bind_int(update_stmt, 1, order_id);
    
    if (sqlite3_step(update_stmt) != SQLITE_DONE) {
        sqlite3_finalize(update_stmt);
        sqlite3_exec(db, "ROLLBACK", 0, 0, 0);
        return false;
    }
    
    sqlite3_finalize(update_stmt);
    sqlite3_exec(db, "COMMIT", 0, 0, 0);
    
    return true;
}

// --------------------------
// Message Handling
// --------------------------
vector<string> split_string(const string &s, char delimiter)
{
    vector<string> tokens;
    string token;
    istringstream tokenStream(s);
    while (getline(tokenStream, token, delimiter))
    {
        tokens.push_back(token);
    }
    return tokens;
}

void handle_message(const string &msg, shared_ptr<ix::WebSocket> ws)
{
    vector<string> parts = split_string(msg, '|');
    if (parts.empty())
        return;

    try
    {
        // Update session activity
        if (parts[0] != "GET_ITEMS" && parts.size() > 3)
        {
            lock_guard<mutex> session_lock(sessions_mutex);
            if (auto it = active_sessions.find(parts[3]); it != active_sessions.end())
            {
                it->second.last_activity = chrono::steady_clock::now();
            }
        }

        if (parts[0] == "LOGIN" && parts.size() == 3)
        {
            string username = parts[1];
            string password = parts[2];

            int user_id = authenticate_user(username, password);
            if (user_id != -1)
            {
                string session_token = generate_uuid();
                {
                    lock_guard<mutex> lock(sessions_mutex);
                    UserSession session;
                    session.user_id = user_id;
                    session.last_activity = chrono::steady_clock::now();
                    session.ws = ws;
                    active_sessions[session_token] = session;
                }
                ws->send("LOGIN_SUCCESS|" + session_token + "|" + to_string(user_id));
                ws->send("GET_ITEMS");
            }
            else
            {
                ws->send("ERROR|Invalid credentials");
            }
        }
        else if (parts[0] == "GET_ITEMS")
        {
            auto lock = items_monitor.get_lock();
            stringstream response;
            response << "ITEMS_LIST";
            for (const auto &[id, item] : items)
            {
                response << "|" << id << "," 
                         << item.name << ","
                         << item.listing_type << ","
                         << item.current_bid << ","
                         << item.fixed_price << ","
                         << item.inventory << ","
                         << item.bidder_id << ","
                         << item.end_time;
            }
            ws->send(response.str());
        }
        else if (parts[0] == "BID" && parts.size() == 4)
        {
            int item_id = stoi(parts[1]);
            double amount = stod(parts[2]);
            string session_token = parts[3];

            int user_id = -1;
            {
                lock_guard<mutex> lock(sessions_mutex);
                if (auto it = active_sessions.find(session_token); it != active_sessions.end())
                {
                    user_id = it->second.user_id;
                }
            }

            if (user_id == -1)
            {
                ws->send("ERROR|Invalid session");
                return;
            }

            auto lock = items_monitor.get_lock();
            if (items.find(item_id) != items.end())
            {
                if (items[item_id].listing_type != "auction")
                {
                    ws->send("ERROR|Item is not an auction");
                    return;
                }
                
                if (items[item_id].end_time > 0 && items[item_id].end_time < time(nullptr))
                {
                    ws->send("ERROR|Auction has ended");
                    return;
                }
                
                items[item_id].bid_queue.emplace(user_id, amount);
                items_monitor.notify();
                ws->send("ACK|Bid queued");
            }
            else
            {
                ws->send("ERROR|Invalid item ID");
            }
        }
        else if (parts[0] == "ADD_TO_CART" && parts.size() == 4)
        {
            int item_id = stoi(parts[1]);
            int quantity = stoi(parts[2]);
            string session_token = parts[3];
            
            int user_id = -1;
            {
                lock_guard<mutex> lock(sessions_mutex);
                if (auto it = active_sessions.find(session_token); it != active_sessions.end())
                {
                    user_id = it->second.user_id;
                }
            }
            
            if (user_id == -1)
            {
                ws->send("ERROR|Invalid session");
                return;
            }
            
            if (add_to_cart(user_id, item_id, quantity))
            {
                // Update session cart
                {
                    lock_guard<mutex> lock(sessions_mutex);
                    if (auto it = active_sessions.find(session_token); it != active_sessions.end())
                    {
                        auto cart_items = get_cart_items(user_id);
                        it->second.cart.clear();
                        for (const auto &[item, qty] : cart_items)
                        {
                            it->second.cart[item.id] = {item.id, qty};
                        }
                    }
                }
                send_cart_update_to_user(user_id);
                ws->send("CART_UPDATED|Item added to cart");
            }
            else
            {
                ws->send("ERROR|Failed to add item to cart");
            }
        }
        else if (parts[0] == "UPDATE_CART" && parts.size() == 4)
        {
            int item_id = stoi(parts[1]);
            int quantity = stoi(parts[2]);
            string session_token = parts[3];
            
            int user_id = -1;
            {
                lock_guard<mutex> lock(sessions_mutex);
                if (auto it = active_sessions.find(session_token); it != active_sessions.end())
                {
                    user_id = it->second.user_id;
                }
            }
            
            if (user_id == -1)
            {
                ws->send("ERROR|Invalid session");
                return;
            }
            
            if (update_cart(user_id, item_id, quantity))
            {
                // Update session cart
                {
                    lock_guard<mutex> lock(sessions_mutex);
                    if (auto it = active_sessions.find(session_token); it != active_sessions.end())
                    {
                        auto cart_items = get_cart_items(user_id);
                        it->second.cart.clear();
                        for (const auto &[item, qty] : cart_items)
                        {
                            it->second.cart[item.id] = {item.id, qty};
                        }
                    }
                }
                send_cart_update_to_user(user_id);
                ws->send("CART_UPDATED|Cart updated");
            }
            else
            {
                ws->send("ERROR|Failed to update cart");
            }
        }
        else if (parts[0] == "GET_CART" && parts.size() == 2)
        {
            string session_token = parts[1];
            
            int user_id = -1;
            {
                lock_guard<mutex> lock(sessions_mutex);
                if (auto it = active_sessions.find(session_token); it != active_sessions.end())
                {
                    user_id = it->second.user_id;
                }
            }
            
            if (user_id == -1)
            {
                ws->send("ERROR|Invalid session");
                return;
            }
            
            auto cart_items = get_cart_items(user_id);
            
            stringstream response;
            response << "CART_ITEMS";
            
            double cart_total = 0.0;
            for (const auto &[item, quantity] : cart_items)
            {
                response << "|" << item.id << "," 
                         << item.name << ","
                         << item.fixed_price << ","
                         << quantity;
                         
                cart_total += item.fixed_price * quantity;
            }
            
            response << "|TOTAL," << cart_total;
            ws->send(response.str());
        }
        else if (parts[0] == "CHECKOUT" && parts.size() == 2)
        {
            string session_token = parts[1];
            
            int user_id = -1;
            {
                lock_guard<mutex> lock(sessions_mutex);
                if (auto it = active_sessions.find(session_token); it != active_sessions.end())
                {
                    user_id = it->second.user_id;
                }
            }
            
            if (user_id == -1)
            {
                ws->send("ERROR|Invalid session");
                return;
            }
            
            auto cart_items = get_cart_items(user_id);
            if (cart_items.empty())
            {
                ws->send("ERROR|Cart is empty");
                return;
            }
            
            int order_id = create_order(user_id, cart_items);
            if (order_id > 0)
            {
                ws->send("ORDER_CREATED|" + to_string(order_id));
                send_cart_update_to_user(user_id);
            }
            else
            {
                ws->send("ERROR|Failed to create order");
            }
        }
        else if (parts[0] == "PROCESS_PAYMENT" && parts.size() == 4)
        {
            int order_id = stoi(parts[1]);
            string payment_method = parts[2];
            string session_token = parts[3];
            
            int user_id = -1;
            {
                lock_guard<mutex> lock(sessions_mutex);
                if (auto it = active_sessions.find(session_token); it != active_sessions.end())
                {
                    user_id = it->second.user_id;
                }
            }
            
            if (user_id == -1)
            {
                ws->send("ERROR|Invalid session");
                return;
            }
            
            // In a real system, this would interact with a payment gateway
            // For this example, we'll simulate a successful payment with a random transaction ID
            string transaction_id = "TX" + to_string(time(nullptr)) + "_" + to_string(rand() % 10000);
            
            if (process_payment(order_id, payment_method, transaction_id))
            {
                ws->send("PAYMENT_SUCCESS|" + transaction_id);
            }
            else
            {
                ws->send("ERROR|Payment processing failed");
            }
        }
        else if (parts[0] == "GET_ORDERS" && parts.size() == 2)
        {
            string session_token = parts[1];

            int user_id = -1;
            {
                lock_guard<mutex> lock(sessions_mutex);
                if (auto it = active_sessions.find(session_token); it != active_sessions.end())
                {
                    user_id = it->second.user_id;
                }
            }

            if (user_id == -1)
            {
                ws->send("ERROR|Invalid session");
                return;
            }

            // Query orders for this user
            lock_guard<mutex> db_lock(db_mutex);
            sqlite3_stmt *stmt;
            const char *sql = "SELECT id, total_amount, status, created_at FROM orders WHERE user_id = ?";

            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            {
                ws->send("ERROR|Failed to fetch orders");
                return;
            }

            sqlite3_bind_int(stmt, 1, user_id);

            stringstream response;
            response << "ORDERS";
            while (sqlite3_step(stmt) == SQLITE_ROW)
            {
                int id = sqlite3_column_int(stmt, 0);
                double total = sqlite3_column_double(stmt, 1);
                const char *status = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
                const char *created_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));

                response << "|" << id << "," << total << "," << status << "," << created_at;
            }

            sqlite3_finalize(stmt);
            ws->send(response.str());
        }
        else if (parts[0] == "ADMIN" && parts.size() >= 5 && parts[2] == "ADD_ITEM")
        {
            string session_token = parts[1];
            int user_id = -1;
            {
                lock_guard<mutex> lock(sessions_mutex);
                if (auto it = active_sessions.find(session_token); it != active_sessions.end())
                {
                    user_id = it->second.user_id;
                }
            }

            if (user_id != 1) // Assuming admin has ID 1
            {
                ws->send("ERROR|Admin privileges required");
                return;
            }

            try
            {
                string name = parts[3];
                string listing_type = parts[4];
                double price = stod(parts[5]);
                int inventory = parts.size() > 6 ? stoi(parts[6]) : 1;
                string description = parts.size() > 7 ? parts[7] : name;
                int64_t end_time = 0;
                
                if (listing_type == "auction" && parts.size() > 8) {
                    // Duration in hours
                    int duration = stoi(parts[8]);
                    end_time = time(nullptr) + duration * 3600;
                }
                
                add_item(name, description, listing_type, price, inventory, end_time);
                ws->send("ADMIN_SUCCESS|Item added: " + name);
            }
            catch (const exception &e)
            {
                ws->send("ERROR|Invalid item parameters");
            }
        }
    }
    catch (const exception &e)
    {
        ws->send("ERROR|Invalid message format");
    }
}

// --------------------------
// Background Threads
// --------------------------
void bid_processor_thread()
{
    while (true)
    {
        auto lock = items_monitor.get_lock();
        bool processed = false;

        for (auto &[item_id, item] : items)
        {
            if (!item.bid_queue.empty())
            {
                auto [user_id, amount] = item.bid_queue.front();
                item.bid_queue.pop();
                lock.unlock();

                process_bid(item, user_id, amount);

                lock.lock();
                processed = true;
                break;
            }
        }

        if (!processed)
        {
            items_monitor.wait(lock);
        }
    }
}

void auction_end_processor_thread()
{
    while (true)
    {
        this_thread::sleep_for(chrono::seconds(5));
        
        auto now = time(nullptr);
        vector<pair<Item, int>> ended_auctions;
        
        // Find ended auctions with bidders
        {
            auto lock = items_monitor.get_lock();
            for (auto &[item_id, item] : items)
            {
                if (item.listing_type == "auction" && 
                    item.end_time > 0 && 
                    item.end_time <= now && 
                    item.bidder_id > 0)
                {
                    ended_auctions.emplace_back(item, 1);  // Quantity is always 1 for auction items
                }
            }
        }
        
        // Process each ended auction into an order
        for (const auto &[item, quantity] : ended_auctions)
        {
            vector<pair<Item, int>> order_items = { {item, quantity} };
            int order_id = create_order(item.bidder_id, order_items, false);
            
            if (order_id > 0)
            {
                // Broadcast notification to all users
                broadcast("AUCTION_ENDED|" + to_string(item.id) + "," + 
                         item.name + "," + to_string(item.current_bid) + "," + 
                         to_string(item.bidder_id) + "," + to_string(order_id));
                
                // Update auction end time to 0 to mark it as processed
                lock_guard<mutex> db_lock(db_mutex);
                sqlite3_stmt *update_stmt;
                const char *update_sql = "UPDATE items SET end_time = 0 WHERE id = ?";
                
                if (sqlite3_prepare_v2(db, update_sql, -1, &update_stmt, nullptr) == SQLITE_OK)
                {
                    sqlite3_bind_int(update_stmt, 1, item.id);
                    sqlite3_step(update_stmt);
                    sqlite3_finalize(update_stmt);
                }
            }
        }
    }
}

void session_cleanup_thread()
{
    while (true)
    {
        this_thread::sleep_for(chrono::minutes(5));
        lock_guard<mutex> lock(sessions_mutex);
        auto now = chrono::steady_clock::now();

        for (auto it = active_sessions.begin(); it != active_sessions.end();)
        {
            if (now - it->second.last_activity > chrono::hours(1))
            {
                it = active_sessions.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

// --------------------------
// Main Server
// --------------------------
int main()
{
    init_database();
    seed_test_data();
    load_items_from_db();

    ix::initNetSystem();
    ix::WebSocketServer server(8080, "0.0.0.0");

    thread(bid_processor_thread).detach();
    thread(auction_end_processor_thread).detach();
    thread(session_cleanup_thread).detach();

    server.setOnConnectionCallback(
        [&](weak_ptr<ix::WebSocket> weakWebSocket,
            shared_ptr<ix::ConnectionState> connectionState)
        {
            auto webSocket = weakWebSocket.lock();
            if (webSocket)
            {
                // Add to connected clients
                {
                    lock_guard<mutex> lock(clients_mutex);
                    connected_clients.push_back(webSocket);
                }

                // Set message callback
                webSocket->setOnMessageCallback(
                    [weakWebSocket](const ix::WebSocketMessagePtr& msg)
                    {
                        auto ws = weakWebSocket.lock();
                        if (!ws) return;

                        // Handle close event
                        if (msg->type == ix::WebSocketMessageType::Close)
                        {
                            lock_guard<mutex> lock(clients_mutex);
                            auto it = find(connected_clients.begin(), 
                                        connected_clients.end(), ws);
                            if (it != connected_clients.end())
                            {
                                connected_clients.erase(it);
                            }
                            return;
                        }

                        // Handle other message types
                        if (msg->type == ix::WebSocketMessageType::Message)
                        {
                            handle_message(msg->str, ws);
                        }
                    });
            }
        });

    auto res = server.listen();
    if (!res.first)
    {
        cerr << "Error starting server: " << res.second << endl;
        return 1;
    }

    server.start();
    cout << "Server running on port 8080\n";
    while (true)
        this_thread::sleep_for(chrono::seconds(1));
}