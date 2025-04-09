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

    const char *sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    username TEXT UNIQUE NOT NULL,"
        "    password_hash TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS items ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    name TEXT NOT NULL,"
        "    current_bid REAL DEFAULT 0.0,"
        "    bidder_id INTEGER,"
        "    version INTEGER DEFAULT 1);" 
        "CREATE TABLE IF NOT EXISTS bids ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    item_id INTEGER NOT NULL,"
        "    user_id INTEGER NOT NULL,"
        "    amount REAL NOT NULL,"
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
struct UserSession
{
    int user_id;
    chrono::steady_clock::time_point last_activity;
};

struct Item
{
    int id;
    string name;
    double current_bid = 0.0;
    int bidder_id = -1;
    int version = 1;
    queue<pair<int, double>> bid_queue; // user_id, bid_amount
};

mutex sessions_mutex;
mutex items_mutex;
mutex clients_mutex;
unordered_map<string, UserSession> active_sessions; // session_token -> UserSession
unordered_map<int, Item> items;                     // item_id -> Item
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
    for (int i = 0; i < 8; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; i++) ss << dis(gen);
    ss << "-4";
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    ss << dis2(gen);
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; i++) ss << dis(gen);
    return ss.str();
}

void broadcast(const string& message) {
    lock_guard<mutex> lock(clients_mutex);
    for (auto& client : connected_clients) {
        if (client) client->send(message);
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
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        user_id = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return user_id;
}

void load_items_from_db()
{
    lock_guard<mutex> db_lock(db_mutex);
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, name, current_bid, bidder_id, version FROM items";

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
        item.current_bid = sqlite3_column_double(stmt, 2);
        item.bidder_id = sqlite3_column_int(stmt, 3);
        item.version = sqlite3_column_int(stmt, 4);

        lock_guard<mutex> items_lock(items_mutex);
        items[item.id] = item;
    }
    sqlite3_finalize(stmt);
}

void seed_test_data()
{
    lock_guard<mutex> db_lock(db_mutex);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM users", -1, &stmt, nullptr) == SQLITE_OK)
    {
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0)
        {
            const char *users_sql =
                "INSERT INTO users (username, password_hash) VALUES "
                "('user1', 'pass1'), ('user2', 'pass2'), ('admin', 'admin');";
            sqlite3_exec(db, users_sql, 0, 0, 0);
        }
        sqlite3_finalize(stmt);
    }

    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM items", -1, &stmt, nullptr) == SQLITE_OK)
    {
        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0)
        {
            const char *items_sql =
                "INSERT INTO items (name, current_bid) VALUES "
                "('Antique Chair', 100.0), "
                "('Vintage Painting', 500.0), "
                "('Rare Coin Collection', 1000.0);";
            sqlite3_exec(db, items_sql, 0, 0, 0);
        }
        sqlite3_finalize(stmt);
    }
}

// --------------------------
// Bid Processing
// --------------------------
void process_bid(Item &item, int user_id, double amount)
{
    lock_guard<mutex> db_lock(db_mutex);
    bool success = false;

    sqlite3_exec(db, "BEGIN IMMEDIATE", 0, 0, 0);

    sqlite3_stmt *check_stmt;
    const char *check_sql = "SELECT current_bid, version FROM items WHERE id = ?";
    if (sqlite3_prepare_v2(db, check_sql, -1, &check_stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_int(check_stmt, 1, item.id);
        if (sqlite3_step(check_stmt) == SQLITE_ROW)
        {
            double current_bid = sqlite3_column_double(check_stmt, 0);
            int db_version = sqlite3_column_int(check_stmt, 1);
            
            if (amount > current_bid && db_version == item.version)
            {
                const char *update_sql = "UPDATE items SET current_bid = ?, bidder_id = ?, version = ? WHERE id = ?";
                sqlite3_stmt *update_stmt;
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
    }

    if (success)
    {
        item.current_bid = amount;
        item.bidder_id = user_id;
        
        stringstream update_msg;
        update_msg << "ITEM_UPDATE|" << item.id << "," << item.name << "," 
                   << item.current_bid << "," << item.bidder_id;
        broadcast(update_msg.str());
        
        // Insert bid history
        sqlite3_stmt *insert_stmt;
        const char *insert_sql = "INSERT INTO bids (item_id, user_id, amount) VALUES (?, ?, ?)";
        if (sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_int(insert_stmt, 1, item.id);
            sqlite3_bind_int(insert_stmt, 2, user_id);
            sqlite3_bind_double(insert_stmt, 3, amount);
            sqlite3_step(insert_stmt);
            sqlite3_finalize(insert_stmt);
        }
    }

    sqlite3_exec(db, success ? "COMMIT" : "ROLLBACK", 0, 0, 0);
}

void add_item(const string& name) {
    lock_guard<mutex> db_lock(db_mutex);
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO items (name) VALUES (?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    load_items_from_db();
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
    if (parts.empty()) return;

    try
    {
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
                    active_sessions[session_token] = {user_id, chrono::steady_clock::now()};
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
            lock_guard<mutex> lock(items_mutex);
            stringstream response;
            response << "ITEMS_LIST";
            for (const auto &[id, item] : items)
            {
                response << "|" << item.id << "," << item.name << "," 
                         << item.current_bid << "," << item.version;
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
                auto it = active_sessions.find(session_token);
                if (it != active_sessions.end()) user_id = it->second.user_id;
            }

            if (user_id == -1) {
                ws->send("ERROR|Invalid session");
                return;
            }

            {
                lock_guard<mutex> lock(items_mutex);
                if (items.find(item_id) != items.end()) {
                    items[item_id].bid_queue.emplace(user_id, amount);
                    ws->send("ACK|Bid queued");
                } else {
                    ws->send("ERROR|Invalid item ID");
                }
            }
        }
        else if (parts[0] == "ADMIN" && parts.size() >= 3)
        {
            string session_token = parts[1];
            int user_id = -1;
            {
                lock_guard<mutex> lock(sessions_mutex);
                auto it = active_sessions.find(session_token);
                if (it != active_sessions.end()) user_id = it->second.user_id;
            }

            if (user_id != 1) { // Assuming admin has user_id 1
                ws->send("ERROR|Admin privileges required");
                return;
            }

            if (parts[2] == "ADD_ITEM" && parts.size() == 4) {
                add_item(parts[3]);
                ws->send("ADMIN_SUCCESS|Item added: " + parts[3]);
            }
        }
    }
    catch (const exception &e) {
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
        this_thread::sleep_for(chrono::milliseconds(100));

        lock_guard<mutex> lock(items_mutex);
        for (auto &[item_id, item] : items)
        {
            if (!item.bid_queue.empty())
            {
                auto [user_id, amount] = item.bid_queue.front();
                item.bid_queue.pop();
                process_bid(item, user_id, amount);
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
    thread(session_cleanup_thread).detach();

    server.setOnConnectionCallback(
        [&](weak_ptr<ix::WebSocket> weakWebSocket,
            shared_ptr<ix::ConnectionState> connectionState)
        {
            if (auto webSocket = weakWebSocket.lock())
            {
                webSocket->setOnMessageCallback(
                    [weakWebSocket](const ix::WebSocketMessagePtr &msg)
                    {
                        if (auto ws = weakWebSocket.lock())
                        {
                            if (msg->type == ix::WebSocketMessageType::Message)
                            {
                                handle_message(msg->str, ws);
                            }
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
    cout << "Test users:\n";
    cout << "  user1/pass1\n  user2/pass2\n  admin/admin\n";
    cout << "Sample items preloaded with IDs 1-3\n";

    while (true)
        this_thread::sleep_for(chrono::seconds(1));
}