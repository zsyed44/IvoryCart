#include "auth.h"
#include <sstream>
#include <random>
#include <sodium.h>  

// Generate a session ID
std::string generateSessionId() {
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<int> dist(0, 15);
    std::stringstream ss;
    for (int i = 0; i < 32; ++i)
        ss << std::hex << dist(rng);
    return ss.str();
}

AuthService::AuthService(Storage& database) : db(database) {}

std::string AuthService::login(const std::string& email, const std::string& password) {
    std::lock_guard<std::mutex> lock(authMutex);
    auto users = db.get_all<User>(sql::where(sql::c(&User::email) == email));

    if (!users.empty()) {
        // Verify the password using libsodium's crypto_pwhash_str_verify.
        // crypto_pwhash_str_verify returns 0 on successful verification.
        if (crypto_pwhash_str_verify(users[0].passwordHash.c_str(), password.c_str(), password.size()) == 0) {
            std::string sessionId = generateSessionId();
            User user = users[0];
            user.sessionId = sessionId;
            db.update(user);
            return sessionId;
        }
    }
    throw std::runtime_error("Invalid credentials");
}

User AuthService::getUserFromSession(const std::string& sessionId) {
    auto users = db.get_all<User>(sql::where(sql::c(&User::sessionId) == sessionId));
    if (!users.empty()) return users[0];
    throw std::runtime_error("Unauthorized");
}

std::string AuthService::registerUser(const std::string& name, const std::string& email, const std::string& password) {
    std::lock_guard<std::mutex> lock(authMutex);
    // Check if email already exists
    auto users = db.get_all<User>(sql::where(sql::c(&User::email) == email));
    if (!users.empty()) {
        throw std::runtime_error("Email already registered");
    }
    
    // Buffer for the hashed password; crypto_pwhash_STRBYTES defines the required size.
    char hashed[crypto_pwhash_STRBYTES];
    
    // Hash the password using libsodium's crypto_pwhash_str with interactive parameters.
    if (crypto_pwhash_str(
            hashed,
            password.c_str(),
            password.size(),
            crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        throw std::runtime_error("Out of memory while hashing password");
    }
    
    std::string hashedPassword = hashed;
    
    // Create new user record
    User newUser;
    newUser.name = name;
    newUser.email = email;
    newUser.passwordHash = hashedPassword;
    newUser.sessionId = "";  // sessionId remains empty until login
    db.insert(newUser);
    
    return "Registration successful";
}
