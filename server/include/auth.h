// include/auth.h
#ifndef AUTH_SERVICE_H
#define AUTH_SERVICE_H

#include "database.h"
#include <mutex>
#include <string>

class AuthService {
    Storage& db;
    std::mutex authMutex;

public:
    AuthService(Storage& database);
    std::string login(const std::string& email, const std::string& password);
    User getUserFromSession(const std::string& sessionId);
    std::string registerUser(const std::string& name, const std::string& email, const std::string& password);
};



#endif