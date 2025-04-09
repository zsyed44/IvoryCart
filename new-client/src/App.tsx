import { useState, useEffect } from 'react';
import './App.css';
import 'bootstrap/dist/css/bootstrap.min.css';
import React from 'react';

interface Item {
  id: string;
  name: string;
  currentBid: number;
  version: number;
  bidderId?: number;
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
  const [form, setForm] = useState({
    username: '',
    password: '',
    sessionToken: '',
    bidAmounts: {} as { [itemId: string]: string }, // New
    newItemName: '',
    newItemStartBid: ''
  });
  const [isAdmin, setIsAdmin] = useState(false);
  const [notification, setNotification] = useState<{
    message: string;
    type: 'success' | 'error'
  } | null>(null);
  const [isConnecting, setIsConnecting] = useState(true);

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
            ws.send(`GET_ITEMS`);
          }
        };

        ws.onmessage = (event) => {
          if (!isMounted) return;
          if (event.data.includes('price_parity')) {
            console.warn('Ignoring extension message');
            return;
          }

          const [type, ...rest] = event.data.split('|');

          switch (type) {
            case 'LOGIN_SUCCESS':
              setForm(prev => ({
                ...prev,
                sessionToken: rest[0]
              }));
              setIsAdmin(rest[1] === '1');
              showNotification('Login successful!', 'success');
              ws.send('GET_ITEMS');
              break;

            case 'ITEMS_LIST': {
              const newItems = rest.map((item: string) => {
                const [id, name, currentBid, version] = item.split(',');
                return {
                  id,
                  name,
                  currentBid: parseFloat(currentBid),
                  version: parseInt(version)
                };
              });
              setItems(newItems);
              break;
            }

            case 'ITEM_UPDATE': {
              const [id, name, currentBid, bidderId] = rest[0].split(',');
              setItems(prev => prev.map(item =>
                item.id === id ? {
                  ...item,
                  name,
                  currentBid: parseFloat(currentBid),
                  bidderId: parseInt(bidderId)
                } : item
              ));
              break;
            }

            case 'ADMIN_SUCCESS':
              showNotification(rest.join('|'), 'success');
              ws.send('GET_ITEMS');
              break;

            case 'ERROR':
              showNotification(rest.join('|'), 'error');
              break;

            default:
              console.warn('Unknown message type:', type);
          }
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
        ws.onmessage = null; // Remove all message listeners
        ws.close();
      }
    };
  }, []);

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

    setNotification({ message: 'Logging in...', type: 'success' });

    // Create a reference to the current socket
    const currentSocket = socket;

    // Temporary message handler
    const loginHandler = (event: MessageEvent) => {
      const [type] = event.data.split('|');
      if (type === 'LOGIN_SUCCESS' || type === 'ERROR') {
        // Remove this temporary handler after processing
        currentSocket.removeEventListener('message', loginHandler);

        if (type === 'LOGIN_SUCCESS') {
          // Set the main message handler
          currentSocket.addEventListener('message', handleMessage);
        }
      }
    };

    // Add temporary handler
    socket.addEventListener('message', loginHandler);

    try {
      socket.send(`LOGIN|${form.username}|${form.password}`);
    } catch (error) {
      console.error('Login error:', error);
      showNotification('Login failed', 'error');
      socket.removeEventListener('message', loginHandler);
    }
  };
  const handleMessage = (event: MessageEvent) => {
    const [type, ...rest] = event.data.split('|');
    let ws: WebSocket;
    ws = new WebSocket('ws://localhost:8080');
    switch (type) {
      case 'LOGIN_SUCCESS':
        setForm(prev => ({ ...prev, sessionToken: rest[0] }));
        setIsAdmin(rest[1] === '1');
        showNotification('Login successful!', 'success');
        ws.send('GET_ITEMS');
        break;

      case 'ITEMS_LIST': {
        const newItems = rest.map((item: string) => {
          const [id, name, currentBid, version] = item.split(',');
          return {
            id,
            name,
            currentBid: parseFloat(currentBid),
            version: parseInt(version)
          };
        });
        setItems(newItems);
        break;
      }

      case 'ITEM_UPDATE': {
        const [id, name, currentBid, bidderId] = rest[0].split(',');
        setItems(prev => prev.map(item =>
          item.id === id ? {
            ...item,
            name,
            currentBid: parseFloat(currentBid),
            bidderId: parseInt(bidderId)
          } : item
        ));
        break;
      }

      case 'ADMIN_SUCCESS':
        showNotification(rest.join('|'), 'success');
        ws.send('GET_ITEMS');
        break;

      case 'ERROR':
        showNotification(rest.join('|'), 'error');
        break;

      default:
        console.warn('Unknown message type:', type);
    }
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

  const handleLogout = () => {
    if (socket?.readyState === WebSocket.OPEN) {
      bidAmounts: {}
    }
    setForm({
      username: '',
      password: '',
      sessionToken: '',
      bidAmounts: {},
      newItemName: '',
      newItemStartBid: ''
    });
    setItems([]);
  };

  const AdminPanel = () => (
    <div className="card shadow-sm mb-4">
      <div className="card-header bg-warning">
        <h3 className="h5 mb-0">Admin Controls</h3>
      </div>
      <div className="card-body">
        <div className="row g-3">
          <div className="col-md-6">
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
              type="number"
              className="form-control"
              placeholder="Starting bid"
              value={form.newItemStartBid}
              onChange={e => setForm({ ...form, newItemStartBid: e.target.value })}
              min="0"
              step="0.01"
            />
          </div>
          <div className="col-md-2">
            <button
              className="btn btn-warning w-100"
              onClick={() => {
                if (!socket || socket.readyState !== WebSocket.OPEN) {
                  showNotification('Not connected to server', 'error');
                  return;
                }

                if (!form.newItemName || !form.newItemStartBid) {
                  showNotification('Please fill all item fields', 'error');
                  return;
                }

                if (isNaN(Number(form.newItemStartBid))) {
                  showNotification('Invalid starting bid amount', 'error');
                  return;
                }

                socket.send(`ADMIN|${form.sessionToken}|ADD_ITEM|${form.newItemName}|${form.newItemStartBid}`);
                setForm({ ...form, newItemName: '', newItemStartBid: '' });
              }}
            >
              Add Item
            </button>
          </div>
        </div>
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
            Live Auction System
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
                <h2 className="h4 mb-3">Auction Items</h2>
                <div className="row g-3">
                  {items.map(item => (
                    <div key={item.id} className="col-md-6">
                      <div className="card h-100 shadow-sm">
                        <div className="card-body">
                          <h3 className="h5">{item.name}</h3>
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
                        </div>
                      </div>
                    </div>
                  ))}
                </div>
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

export default App;