# IvoryCart 🛒

## Overview
**IvoryCart** is a scalable and secure **E-Commerce Order Processing System** designed to provide a seamless online shopping experience. The platform enables users to browse products, add items to their cart, and securely complete transactions, while ensuring real-time inventory management and transaction integrity.

## Features
- **User Authentication & Session Management**: Secure login system to protect user data.
- **Product Catalog & Inventory Management**: Real-time stock updates to prevent overselling.
- **Shopping Cart & Order Processing**: Add/remove items from cart, ensuring availability until checkout.
- **Payment & Transaction Handling**: ACID-compliant transactions to maintain data consistency.
- **Order History & Notifications**: Track past purchases and receive updates on order status.
- **Concurrency & Multi-User Support**: Handles multiple users simultaneously without conflicts.

## System Architecture
**IvoryCart** follows a **client-server architecture**, where:
- **Frontend** (Client): A web-based UI that allows users to interact with the system.
- **Backend** (Server): A C++ backend that handles authentication, product management, transactions, and order tracking.
- **Database**: A relational database that stores users, products, orders, and payments.

## Technologies Used
- **Frontend**: HTML, CSS, JavaScript (Framework TBD)
- **Backend**: C++ (Using processes liking forking, and multi-threading)
- **Database**: MySQL/PostgreSQL
- **Security**: Encrypted transactions, secure authentication

## Installation
To set up the project locally:
1. Clone the repository:
   ```bash
   git clone https://github.com/your-username/ivorycart.git
   ```
2. Install dependencies (if applicable).
3. Configure the database and environment variables.
4. Run the server:
   ```bash
   ./ivorycart_server
   ```
5. Access the frontend via `http://localhost:3000`.

## Contributors
- Sumail Aasi
- Mohammed Al-Hashimi
- Dev Chaudhari
- Obaid Mohiuddin
- Zain Syed
