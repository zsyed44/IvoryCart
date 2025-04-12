import { useState, useEffect } from 'react';
import './App.css';
import 'bootstrap/dist/css/bootstrap.min.css';
import React from 'react';

interface Item {
  id: string;
  name: string;
  description: string;
  listingType: 'auction' | 'fixed';
  currentBid: number;
  fixedPrice: number;
  inventory: number;
  bidderId?: number;
  endTime: number;
  version: number;
}

interface CartItem {
  item: Item;
  quantity: number;
}

interface Order {
  id: number;
  totalAmount: number;
  status: string;
  items: CartItem[];
}

class ErrorBoundary extends React.Component<{ children: React.ReactNode }> {
  state = { hasError: false };

  static getDerivedStateFromError() {
    return { hasError: true };
  }

  componentDidCatch(error: Error) {
    console.error('App error:', error);
  }

  render() {
    if (this.state.hasError) {
      return <div className="alert alert-danger m-3">Application error occurred. Please refresh the page.</div>;
    }
    return this.props.children;
  }
}

function App() {
  const [socket, setSocket] = useState<WebSocket | null>(null);
  const [items, setItems] = useState<Item[]>([]);
  const [cart, setCart] = useState<CartItem[]>([]);
  const [orders, setOrders] = useState<Order[]>([]);
  const [form, setForm] = useState({
    username: '',
    password: '',
    sessionToken: '',
    bidAmounts: {} as { [itemId: string]: string },
    newItemName: '',
    newItemDescription: '',
    newItemPrice: '',
    newItemInventory: '1',
    newItemType: 'auction',
    newItemDuration: '24' // hours
  });
  const [isAdmin, setIsAdmin] = useState(false);
  const [notification, setNotification] = useState<{
    message: string;
    type: 'success' | 'error'
  } | null>(null);
  const [isConnecting, setIsConnecting] = useState(true);
  const [activeTab, setActiveTab] = useState<'auctions' | 'shop' | 'cart' | 'orders'>('auctions');

  const handleMessage = (event: MessageEvent) => {
    const [type, ...rest] = event.data.split('|');
    console.log('Received message:', type, rest); // Debug log

    if (type === 'LOGIN_SUCCESS') {
      setForm(prev => ({ ...prev, sessionToken: rest[0] }));
      setIsAdmin(rest[1] === '1');
      showNotification('Login successful!', 'success');
      if (socket) {
        socket.send('GET_ITEMS');
        socket.send(`GET_CART|${rest[0]}`);
        socket.send(`GET_ORDERS|${rest[0]}`);
      }
    } else if (type === 'GET_ITEMS') {
      // Server is requesting items, send them
      if (socket) {
        socket.send('GET_ITEMS');
      }
    } else if (type === 'ITEMS_LIST') {
      const newItems = rest.map((item: string) => {
        const [id, name, listingType, currentBid, fixedPrice, inventory, bidderId, endTime] = item.split(',');
        return {
          id,
          name,
          description: '', // Description not included in list view
          listingType: listingType as 'auction' | 'fixed',
          currentBid: parseFloat(currentBid),
          fixedPrice: parseFloat(fixedPrice),
          inventory: parseInt(inventory),
          bidderId: bidderId ? parseInt(bidderId) : undefined,
          endTime: parseInt(endTime),
          version: 1
        };
      });
      setItems(newItems);
    } else if (type === 'ITEM_UPDATE') {
      const [id, name, listingType, currentBid, fixedPrice, inventory, bidderId, endTime] = rest[0].split(',');
      setItems(prev => prev.map(item =>
        item.id === id ? {
          ...item,
          name,
          listingType: listingType as 'auction' | 'fixed',
          currentBid: parseFloat(currentBid),
          fixedPrice: parseFloat(fixedPrice),
          inventory: parseInt(inventory),
          bidderId: bidderId ? parseInt(bidderId) : undefined,
          endTime: parseInt(endTime)
        } : item
      ));
    } else if (type === 'CART_ITEMS') {
      console.log('Processing cart items:', rest);
      const cartItems: CartItem[] = [];
      for (let i = 0; i < rest.length; i++) {
        if (rest[i].startsWith('TOTAL')) continue;
        const [id, name, price, quantity] = rest[i].split(',');
        const item = items.find(item => item.id === id);
        if (item) {
          cartItems.push({
            item: {
              ...item,
              name,
              fixedPrice: parseFloat(price)
            },
            quantity: parseInt(quantity)
          });
        } else {
          // If item not found in items list, create a basic item
          cartItems.push({
            item: {
              id,
              name,
              description: '',
              listingType: 'fixed',
              currentBid: 0,
              fixedPrice: parseFloat(price),
              inventory: 1,
              endTime: 0,
              version: 1
            },
            quantity: parseInt(quantity)
          });
        }
      }
      console.log('Updated cart items:', cartItems);
      setCart(cartItems);
    } else if (type === 'CART_UPDATED') {
      showNotification(rest.join('|'), 'success');
      if (socket && form.sessionToken) {
        // Immediately request updated cart
        socket.send(`GET_CART|${form.sessionToken}`);
      }
    } else if (type === 'ORDER_CREATED') {
      const orderId = rest[0];
      showNotification(`Order created with ID: ${orderId}`, 'success');
      if (socket) {

        socket.send(`GET_CART|${form.sessionToken}`);
        socket.send(`GET_ORDERS|${form.sessionToken}`);
        socket.send(`PROCESS_PAYMENT|${orderId}|credit_card|${form.sessionToken}`);
      }
    } else if (type === 'PAYMENT_SUCCESS') {
      showNotification(`Payment successful! Transaction ID: ${rest[0]}`, 'success');
      if (socket && form.sessionToken) {
        socket.send(`GET_CART|${form.sessionToken}`);
        socket.send(`GET_ORDERS|${form.sessionToken}`);
      }
    } else if (type === 'AUCTION_ENDED') {
      const [itemId, , finalBid, winnerId] = rest[0].split(',');
      showNotification(`Auction ended: Item ${itemId} sold for $${finalBid} to user ${winnerId}`, 'success');
    } else if (type === 'ADMIN_SUCCESS') {
      showNotification(rest.join('|'), 'success');
      if (socket) {
        socket.send('GET_ITEMS');
      }
    } else if (type === 'ERROR') {
      const errorMessage = rest.join('|');
      showNotification(errorMessage, 'error');
      console.error('Server error:', errorMessage);
    } else if (type === 'ORDERS_LIST') {
      const newOrders: Order[] = rest.map((order: string) => {
        const [id, totalAmount, status, itemsStr] = order.split(',');
        const orderItems: CartItem[] = itemsStr.split(';').map(itemStr => {
          const [itemId, quantity, price] = itemStr.split(':');
          const item = items.find(i => i.id === itemId);
          if (!item) return null;
          return {
            item: {
              ...item,
              fixedPrice: parseFloat(price)
            },
            quantity: parseInt(quantity)
          };
        }).filter(Boolean) as CartItem[];
        
        return {
          id: parseInt(id),
          totalAmount: parseFloat(totalAmount),
          status,
          items: orderItems
        };
      });
      setOrders(newOrders);
      console.log("ORDERS_LIST fired:", rest);
      console.log("Parsed orders:", newOrders);
    } else if (type === 'ACK') {
      // Acknowledge message, no action needed
    } else {
      console.warn('Unknown message type:', type, rest);
    }
  };

  useEffect(() => {
    let ws: WebSocket;
    let reconnectTimeout: NodeJS.Timeout;
    let isMounted = true;

    const connect = () => {
      setIsConnecting(true);
      try {
        ws = new WebSocket('ws://localhost:8080');

        ws.onopen = () => {
          if (!isMounted) return;
          console.log('Connected to WebSocket server');
          setSocket(ws);
          setIsConnecting(false);
          if (form.sessionToken) {
            ws.send('GET_ITEMS');
            ws.send(`GET_CART|${form.sessionToken}`);
          }
        };

        ws.onmessage = (event) => {
          console.log("[WS] Raw message from server:", event.data);
          if (!isMounted) return;
          handleMessage(event);
        };

        ws.onclose = (event) => {
          if (!isMounted) return;
          if (!event.wasClean) {
            showNotification('Connection lost. Reconnecting...', 'error');
            reconnectTimeout = setTimeout(connect, 3000);
          }
        };

        ws.onerror = (error) => {
          if (!isMounted) return;
          console.error('WebSocket error:', error);
          showNotification('Connection error', 'error');
        };

      } catch (error) {
        console.error('WebSocket creation error:', error);
        showNotification('Connection failed', 'error');
        reconnectTimeout = setTimeout(connect, 3000);
      }
    };

    connect();

    return () => {
      isMounted = false;
      clearTimeout(reconnectTimeout);
      if (ws) {
        ws.onmessage = null;
        ws.close();
      }
    };
  }, [form.sessionToken]);

  const showNotification = (message: string, type: 'success' | 'error') => {
    setNotification({ message, type });
    setTimeout(() => setNotification(null), 5000);
  };

  const handleLogin = () => {
    if (!socket || socket.readyState !== WebSocket.OPEN) {
      showNotification('Not connected to server', 'error');
      return;
    }

    if (!form.username || !form.password) {
      showNotification('Please enter both username and password', 'error');
      return;
    }

    socket.send(`LOGIN|${form.username}|${form.password}`);
  };

  const handleBid = (itemId: string) => {
    if (!socket || socket.readyState !== WebSocket.OPEN || !form.sessionToken || !form.bidAmounts[itemId]) {
      showNotification('Not connected to server', 'error');
      return;
    }

    const bidAmount = parseFloat(form.bidAmounts[itemId] || '');
    const currentItem = items.find(item => item.id === itemId);

    if (!currentItem || bidAmount <= currentItem.currentBid) {
      showNotification(`Bid must be higher than $${currentItem?.currentBid.toFixed(2)}`, 'error');
      return;
    }

    socket.send(`BID|${itemId}|${form.bidAmounts[itemId]}|${form.sessionToken}`);
    setForm(prev => ({
      ...prev,
      bidAmounts: {
        ...prev.bidAmounts,
        [itemId]: ''
      }
    }));
  };

  const handleAddToCart = (itemId: string, quantity: number) => {
    if (!socket || socket.readyState !== WebSocket.OPEN) {
      showNotification('Not connected to server', 'error');
      return;
    }

    if (!form.sessionToken) {
      showNotification('Please login first', 'error');
      return;
    }

    console.log('Adding to cart:', { itemId, quantity, sessionToken: form.sessionToken });
    socket.send(`ADD_TO_CART|${itemId}|${quantity}|${form.sessionToken}`);
  };

  const handleUpdateCart = (itemId: string, quantity: number) => {
    if (!socket || socket.readyState !== WebSocket.OPEN) {
      showNotification('Not connected to server', 'error');
      return;
    }

    if (!form.sessionToken) {
      showNotification('Please login first', 'error');
      return;
    }

    console.log('Updating cart:', { itemId, quantity, sessionToken: form.sessionToken });
    socket.send(`UPDATE_CART|${itemId}|${quantity}|${form.sessionToken}`);
  };

  const handleCheckout = () => {
    if (!socket || socket.readyState !== WebSocket.OPEN || !form.sessionToken) {
      showNotification('Not connected to server', 'error');
      return;
    }

    socket.send(`CHECKOUT|${form.sessionToken}`);
  };

  const handlePayment = (orderId: string) => {
    if (!socket || socket.readyState !== WebSocket.OPEN || !form.sessionToken) {
      showNotification('Not connected to server', 'error');
      return;
    }

    // In a real app, this would integrate with a payment gateway
    socket.send(`PROCESS_PAYMENT|${orderId}|credit_card|${form.sessionToken}`);
  };

  const handleLogout = () => {
    setForm({
      username: '',
      password: '',
      sessionToken: '',
      bidAmounts: {},
      newItemName: '',
      newItemDescription: '',
      newItemPrice: '',
      newItemInventory: '1',
      newItemType: 'auction',
      newItemDuration: '24'
    });
    setItems([]);
    setCart([]);
  };

  const AdminPanel = () => (
    <div className="card shadow-sm mb-4">
      <div className="card-header bg-warning">
        <h3 className="h5 mb-0">Admin Controls</h3>
      </div>
      <div className="card-body">
        <div className="row g-3">
          <div className="col-md-4">
            <input
              type="text"
              className="form-control"
              placeholder="Item name"
              value={form.newItemName}
              onChange={e => setForm({ ...form, newItemName: e.target.value })}
            />
          </div>
          <div className="col-md-4">
            <input
              type="text"
              className="form-control"
              placeholder="Description"
              value={form.newItemDescription}
              onChange={e => setForm({ ...form, newItemDescription: e.target.value })}
            />
          </div>
          <div className="col-md-4">
            <select
              className="form-select"
              value={form.newItemType}
              onChange={e => setForm({ ...form, newItemType: e.target.value })}
            >
              <option value="auction">Auction</option>
              <option value="fixed">Fixed Price</option>
            </select>
          </div>
          <div className="col-md-3">
            <input
              type="number"
              className="form-control"
              placeholder={form.newItemType === 'auction' ? 'Starting bid' : 'Price'}
              value={form.newItemPrice}
              onChange={e => setForm({ ...form, newItemPrice: e.target.value })}
              min="0"
              step="0.01"
            />
          </div>
          <div className="col-md-3">
            <input
              type="number"
              className="form-control"
              placeholder="Inventory"
              value={form.newItemInventory}
              onChange={e => setForm({ ...form, newItemInventory: e.target.value })}
              min="1"
            />
          </div>
          {form.newItemType === 'auction' && (
            <div className="col-md-3">
              <input
                type="number"
                className="form-control"
                placeholder="Duration (hours)"
                value={form.newItemDuration}
                onChange={e => setForm({ ...form, newItemDuration: e.target.value })}
                min="1"
              />
            </div>
          )}
          <div className="col-md-3">
            <button
              className="btn btn-warning w-100"
              onClick={() => {
                if (!socket || socket.readyState !== WebSocket.OPEN) {
                  showNotification('Not connected to server', 'error');
                  return;
                }

                if (!form.newItemName || !form.newItemPrice) {
                  showNotification('Please fill all required fields', 'error');
                  return;
                }

                const params = [
                  form.sessionToken,
                  'ADD_ITEM',
                  form.newItemName,
                  form.newItemType,
                  form.newItemPrice,
                  form.newItemInventory,
                  form.newItemDescription,
                  form.newItemType === 'auction' ? form.newItemDuration : undefined
                ].filter(Boolean);

                socket.send(params.join('|'));
                setForm(prev => ({
                  ...prev,
                  newItemName: '',
                  newItemDescription: '',
                  newItemPrice: '',
                  newItemInventory: '1',
                  newItemDuration: '24'
                }));
              }}
            >
              Add Item
            </button>
          </div>
        </div>
      </div>
    </div>
  );

  const AuctionItem = ({ item }: { item: Item }) => {
    const now = Math.floor(Date.now() / 1000);
    const timeLeft = item.endTime - now;
    const hoursLeft = Math.floor(timeLeft / 3600);
    const minutesLeft = Math.floor((timeLeft % 3600) / 60);
    
    return (
      <div className="card h-100 shadow-sm">
        <div className="card-body">
          <h3 className="h5">{item.name}</h3>
          <p className="text-muted small">{item.description}</p>
          <div className="mb-3">
            <span className="text-muted">Current Bid: </span>
            <span className="fw-bold text-primary">
              ${item.currentBid.toFixed(2)}
            </span>
            {item.bidderId && (
              <span className="text-muted ms-2">
                (Bidder: {item.bidderId})
              </span>
            )}
          </div>
          <div className="mb-3">
            <span className="text-muted">Time Left: </span>
            <span className="fw-bold">
              {timeLeft > 0 ? `${hoursLeft}h ${minutesLeft}m` : 'Ended'}
            </span>
          </div>
          {timeLeft > 0 && (
            <div className="input-group">
              <input
                type="number"
                className="form-control"
                placeholder="Enter bid"
                value={form.bidAmounts[item.id] || ''}
                onChange={e => setForm(prev => ({
                  ...prev,
                  bidAmounts: {
                    ...prev.bidAmounts,
                    [item.id]: e.target.value
                  }
                }))}
                min={item.currentBid + 1}
              />
              <button
                className="btn btn-primary"
                onClick={() => handleBid(item.id)}
                disabled={!socket || socket.readyState !== WebSocket.OPEN}
              >
                Bid
              </button>
            </div>
          )}
        </div>
      </div>
    );
  };

  const ShopItem = ({ item }: { item: Item }) => {
    const [quantity, setQuantity] = useState(1);

    const handleQuantityChange = (e: React.ChangeEvent<HTMLInputElement>) => {
      const value = parseInt(e.target.value);
      if (value > 0 && value <= item.inventory) {
        setQuantity(value);
      }
    };

    return (
      <div className="card h-100 shadow-sm">
        <div className="card-body">
          <h3 className="h5">{item.name}</h3>
          <p className="text-muted small">{item.description}</p>
          <div className="mb-3">
            <span className="text-muted">Price: </span>
            <span className="fw-bold text-primary">
              ${item.fixedPrice.toFixed(2)}
            </span>
          </div>
          <div className="mb-3">
            <span className="text-muted">In Stock: </span>
            <span className="fw-bold">
              {item.inventory}
            </span>
          </div>
          {item.inventory > 0 && (
            <div className="input-group">
              <input
                type="number"
                className="form-control"
                placeholder="Quantity"
                min="1"
                max={item.inventory}
                value={quantity}
                onChange={handleQuantityChange}
              />
              <button
                className="btn btn-primary"
                onClick={() => handleAddToCart(item.id, quantity)}
                disabled={!socket || socket.readyState !== WebSocket.OPEN}
              >
                Add to Cart
              </button>
            </div>
          )}
        </div>
      </div>
    );
  };

  const CartView = () => {
    const [localCart, setLocalCart] = useState<CartItem[]>(cart);
    const total = cart.reduce((sum, { item, quantity }) => sum + item.fixedPrice * quantity, 0);
    const [showCheckoutModal, setShowCheckoutModal] = useState(false);

    useEffect(() => {
      setLocalCart(cart);
    }, [cart]);

    return (
      <div className="card shadow-sm">
        <div className="card-body">
          <h3 className="h5 mb-4">Shopping Cart</h3>
          {localCart.length === 0 ? (
            <p className="text-muted">Your cart is empty</p>
          ) : (
            <>
              <div className="table-responsive">
                <table className="table">
                  <thead>
                    <tr>
                      <th>Item</th>
                      <th>Price</th>
                      <th>Quantity</th>
                      <th>Total</th>
                      <th></th>
                    </tr>
                  </thead>
                  <tbody>
                    {localCart.map(({ item, quantity }) => (
                      <tr key={item.id}>
                        <td>{item.name}</td>
                        <td>${item.fixedPrice.toFixed(2)}</td>
                        <td>
                          <input
                            type="number"
                            className="form-control form-control-sm"
                            value={quantity}
                            min="1"
                            max={item.inventory}
                            onChange={e => {
                              const newQuantity = parseInt(e.target.value);
                              if (newQuantity > 0 && newQuantity <= item.inventory) {
                                handleUpdateCart(item.id, newQuantity);
                              }
                            }}
                          />
                        </td>
                        <td>${(item.fixedPrice * quantity).toFixed(2)}</td>
                        <td>
                          <button
                            className="btn btn-sm btn-danger"
                            onClick={() => handleUpdateCart(item.id, 0)}
                          >
                            Remove
                          </button>
                        </td>
                      </tr>
                    ))}
                  </tbody>
                  <tfoot>
                    <tr>
                      <td colSpan={3} className="text-end fw-bold">Total:</td>
                      <td className="fw-bold">${total.toFixed(2)}</td>
                      <td></td>
                    </tr>
                  </tfoot>
                </table>
              </div>
              <div className="text-end">
              <button
                className="btn btn-primary"
                onClick={() => setShowCheckoutModal(true)}
                disabled={!socket || socket.readyState !== WebSocket.OPEN}
              >
                Checkout
              </button>

                {showCheckoutModal && (
                  <CheckoutModal
                    total={total}
                    onClose={() => setShowCheckoutModal(false)}
                    onSubmit={(cardNumber: string) => {
                      console.log('Order placed with card:', cardNumber);
                      handleCheckout(); // still call the backend logic
                      setShowCheckoutModal(false);
                    }}
                  />
                )}
              </div>
            </>
          )}
        </div>
      </div>
    );
  };

  const OrdersView = ({orders}: { orders: Order[] }) => (
    <div className="card shadow-sm">
      <div className="card-body">
        <h3 className="h5 mb-4">Order History</h3>
        {orders.length === 0 ? (
          <p className="text-muted">No orders found</p>
        ) : (
          <div className="table-responsive">
            <table className="table">
              <thead>
                <tr>
                  <th>Order ID</th>
                  <th>Items</th>
                  <th>Total</th>
                  <th>Status</th>
                  <th>Actions</th>
                </tr>
              </thead>
              <tbody>
                {orders.map(order => (
                  <tr key={order.id}>
                    <td>#{order.id}</td>
                    <td>
                      <ul className="list-unstyled mb-0">
                        {order.items.map(({ item, quantity }, index) => (
                          <li key={index}>
                            {item.name} x{quantity} @ ${item.fixedPrice.toFixed(2)}
                          </li>
                        ))}
                      </ul>
                    </td>
                    <td>${order.totalAmount.toFixed(2)}</td>
                    <td>
                      <span className={`badge bg-${order.status === 'paid' ? 'success' : 'warning'}`}>
                        {order.status}
                      </span>
                    </td>
                    <td>
                      {order.status === 'pending' && (
                        <button
                          className="btn btn-sm btn-primary"
                          onClick={() => handlePayment(order.id.toString())}
                        >
                          Pay Now
                        </button>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>
    </div>
  );

  return (
    <ErrorBoundary>
      <div className="min-vh-100 bg-light">
        {notification && (
          <div className={`alert alert-${notification.type} position-fixed top-0 end-0 m-3`}>
            {notification.message}
          </div>
        )}

        {isConnecting && (
          <div className="position-fixed top-50 start-50 translate-middle">
            <div className="spinner-border text-primary" role="status">
              <span className="visually-hidden">Loading...</span>
            </div>
          </div>
        )}

        <div className="container py-4">
          <h1 className="display-5 fw-bold text-center mb-4 text-primary">
            IvoryCart - Marketplace
          </h1>

          {!form.sessionToken ? (
            <div className="row justify-content-center">
              <div className="col-md-6 col-lg-4">
                <div className="card shadow-sm">
                  <div className="card-body">
                    <h2 className="card-title mb-4">Login</h2>
                    <div className="mb-3">
                      <input
                        className="form-control"
                        placeholder="Username"
                        value={form.username}
                        onChange={e => setForm({ ...form, username: e.target.value })}
                      />
                    </div>
                    <div className="mb-3">
                      <input
                        type="password"
                        className="form-control"
                        placeholder="Password"
                        value={form.password}
                        onChange={e => setForm({ ...form, password: e.target.value })}
                      />
                    </div>
                    <button
                      className="btn btn-primary w-100"
                      onClick={handleLogin}
                      disabled={isConnecting}
                    >
                      {isConnecting ? 'Connecting...' : 'Login'}
                    </button>
                  </div>
                </div>
              </div>
            </div>
          ) : (
            <div className="row g-4">
              <div className="col-lg-8">
                {isAdmin && <AdminPanel />}
                
                <ul className="nav nav-tabs mb-4">
                  <li className="nav-item">
                    <button
                      className={`nav-link ${activeTab === 'auctions' ? 'active' : ''}`}
                      onClick={() => setActiveTab('auctions')}
                    >
                      Auctions
                    </button>
                  </li>
                  <li className="nav-item">
                    <button
                      className={`nav-link ${activeTab === 'shop' ? 'active' : ''}`}
                      onClick={() => setActiveTab('shop')}
                    >
                      Shop
                    </button>
                  </li>
                  <li className="nav-item">
                    <button
                      className={`nav-link ${activeTab === 'cart' ? 'active' : ''}`}
                      onClick={() => setActiveTab('cart')}
                    >
                      Cart ({cart.length})
                    </button>
                  </li>
                  <li className="nav-item">
                    <button
                      className={`nav-link ${activeTab === 'orders' ? 'active' : ''}`}
                      onClick={() => setActiveTab('orders')}
                    >
                      Orders
                    </button>
                  </li>
                </ul>

                {activeTab === 'auctions' && (
                  <div className="row g-3">
                    {items
                      .filter(item => item.listingType === 'auction')
                      .map(item => (
                        <div key={item.id} className="col-md-6">
                          <AuctionItem item={item} />
                        </div>
                      ))}
                  </div>
                )}

                {activeTab === 'shop' && (
                  <div className="row g-3">
                    {items
                      .filter(item => item.listingType === 'fixed')
                      .map(item => (
                        <div key={item.id} className="col-md-6">
                          <ShopItem item={item} />
                        </div>
                      ))}
                  </div>
                )}

                {activeTab === 'cart' && (
                  <div className="row">
                    <div className="col-12">
                      <CartView />
                    </div>
                  </div>
                )}
                
                {activeTab === 'orders' && <OrdersView orders={orders}/>}
              </div>

              <div className="col-lg-4">
                <div className="card shadow-sm">
                  <div className="card-body">
                    <div className="d-flex justify-content-between align-items-center mb-3">
                      <h3 className="h6 mb-0">Logged in as: {form.username}</h3>
                      <button
                        className="btn btn-sm btn-outline-danger"
                        onClick={handleLogout}
                      >
                        Logout
                      </button>
                    </div>
                    {isAdmin && <p className="text-warning mb-0">Administrator Account</p>}
                    <div className="mt-3">
                      <small className="text-muted">
                        Connection status: {socket?.readyState === WebSocket.OPEN ? '✅ Connected' : '❌ Disconnected'}
                      </small>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          )}
        </div>
      </div>
    </ErrorBoundary>
  );
}

interface CheckoutModalProps {
  total: number;
  onClose: () => void;
  onSubmit: (cardNumber: string) => void;
}

function CheckoutModal({ total, onClose, onSubmit }: CheckoutModalProps) {
  const [cardNumber, setCardNumber] = useState('');

  return (
    <div className="modal d-block" tabIndex={-1} style={{ backgroundColor: 'rgba(0,0,0,0.5)' }}>
      <div className="modal-dialog">
        <div className="modal-content shadow-sm">
          <div className="modal-header">
            <h5 className="modal-title">Confirm Your Order</h5>
            <button type="button" className="btn-close" onClick={onClose}></button>
          </div>
          <div className="modal-body">
            <p>Total Cost: <strong>${total.toFixed(2)}</strong></p>
            <div className="mb-3">
              <label className="form-label">Card Number</label>
              <input
                type="text"
                className="form-control"
                placeholder="Enter card number"
                value={cardNumber}
                onChange={(e) => setCardNumber(e.target.value)}
              />
            </div>
          </div>
          <div className="modal-footer">
            <button className="btn btn-secondary" onClick={onClose}>Cancel</button>
            <button
              className="btn btn-primary"
              onClick={() => onSubmit(cardNumber)}
              disabled={!cardNumber.trim()}
            >
              Place Order
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}


export default App;