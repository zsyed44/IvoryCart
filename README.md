# IvoryCart 🛒

## Overview
**IvoryCart** is a scalable and secure **Shopping & Auction System** designed to provide a seamless online shopping experience. The platform enables users to browse products, add items to their cart, participate in auctions, and securely complete transactions, while ensuring real-time inventory management and transaction integrity.

## Features
- **User Authentication & Session Management**: Secure login system to protect user data.
- **Product Catalog & Inventory Management**: Real-time stock updates to prevent overselling.
- **Shopping Cart & Order Processing**: Add/remove items from cart, ensuring availability until checkout.
- **Concurrency & Multi-User Support**: Handles multiple users simultaneously without conflicts.

## System Architecture
**IvoryCart** follows a **client-server architecture**, where:
- **Frontend** (Client): A web-based UI that allows users to interact with the system.
- **Backend** (Server): A C++ backend that uses threading, monitors, and more to handle bidding and order processing.
- **Database**: A SQLITE relational database that stores users, products, orders, and other information.

## Technologies Used
- **Frontend**: HTML, CSS, TypeScript, React
- **Backend**: C++ (Using processes liking forking, and multi-threading)
- **Database**: SQLite
- **Security**: Encrypted transactions, secure authentication

## Installation
To set up the project locally:
1. Clone the repository:
   ```bash
   git clone https://github.com/your-username/ivorycart.git
   ```
2. Install dependencies (for frontend).
   ```bash
   cd new-client
   npm install
   ```
3. Run the server:
   ```bash
   cd server
   mkdir build
   cd build
   cmake ..
   make
   ./server
   ```
6. Access the frontend via [http://localhost:5173/](http://localhost:5173/)

## Contributors
- Sumail Aasi
- Mohammed Al-Hashimi
- Dev Chaudhari
- Obaid Mohiuddin
- Zain Syed
