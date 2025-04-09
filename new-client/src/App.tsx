import { useState, useEffect } from 'react';
import './App.css';
import 'bootstrap/dist/css/bootstrap.min.css';

interface Item {
  id: string;
  name: string;
  currentBid: number;
  version: number;
}

function App() {
  const [socket, setSocket] = useState<WebSocket | null>(null);
  const [items, setItems] = useState<Item[]>([]);
  const [form, setForm] = useState({
    itemId: '',
    bidAmount: '',
    username: '',
    password: '',
    sessionToken: '',
    newItemName: ''
  });
  const [isAdmin, setIsAdmin] = useState(false);
  const [notification, setNotification] = useState<{ message: string; type: 'success' | 'error' } | null>(null);

  useEffect(() => {
    const ws = new WebSocket('ws://localhost:8080');
    
    ws.onopen = () => {
      console.log('Connected to WebSocket server');
      setSocket(ws);
    };

    ws.onmessage = (event) => {
      const [type, ...rest] = event.data.split('|');
      
      switch(type) {
        case 'LOGIN_SUCCESS':
          setForm(prev => ({...prev, sessionToken: rest[0]}));
          setIsAdmin(rest[1] === '1');
          showNotification('Login successful!', 'success');
          ws.send('GET_ITEMS');
          break;
          
        case 'ITEMS_LIST': {
          const newItems = rest.map((item: string) => {
            const [id, name, currentBid, version] = item.split(',');
            return { id, name, currentBid: parseFloat(currentBid), version: parseInt(version) };
          });
          setItems(newItems);
          break;
        }

        case 'ITEM_UPDATE': {
          const [id, name, currentBid] = rest[0].split(',');
          setItems(prev => prev.map(item => 
            item.id === id ? {...item, name, currentBid: parseFloat(currentBid)} : item
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
      }
    };

    return () => ws.close();
  }, []);

  const showNotification = (message: string, type: 'success' | 'error') => {
    setNotification({ message, type });
    setTimeout(() => setNotification(null), 5000);
  };

  const handleLogin = () => {
    if (socket && form.username && form.password) {
      socket.send(`LOGIN|${form.username}|${form.password}`);
    } else {
      showNotification('Please enter both username and password', 'error');
    }
  };

  const handleBid = (itemId: string) => {
    if (socket && form.sessionToken && form.bidAmount) {
      const bidAmount = parseFloat(form.bidAmount);
      const currentItem = items.find(item => item.id === itemId);
      
      if (!currentItem || bidAmount <= currentItem.currentBid) {
        showNotification(`Bid must be higher than $${currentItem?.currentBid.toFixed(2)}`, 'error');
        return;
      }
      
      socket.send(`BID|${itemId}|${form.bidAmount}|${form.sessionToken}`);
      setForm(prev => ({...prev, bidAmount: ''}));
    }
  };

  const AdminPanel = () => (
    <div className="card shadow-sm mb-4">
      <div className="card-header bg-warning">
        <h3 className="h5 mb-0">Admin Controls</h3>
      </div>
      <div className="card-body">
        <div className="input-group">
          <input
            type="text"
            className="form-control"
            placeholder="New item name"
            value={form.newItemName}
            onChange={e => setForm({...form, newItemName: e.target.value})}
          />
          <button 
            className="btn btn-warning"
            onClick={() => {
              if (socket && form.newItemName) {
                socket.send(`ADMIN|${form.sessionToken}|ADD_ITEM|${form.newItemName}`);
                setForm({...form, newItemName: ''});
              }
            }}
          >
            Add Item
          </button>
        </div>
      </div>
    </div>
  );

  return (
    <div className="min-vh-100 bg-light">
      {notification && (
        <div className={`alert alert-${notification.type} position-fixed top-0 end-0 m-3`}>
          {notification.message}
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
                      onChange={e => setForm({...form, username: e.target.value})}
                    />
                  </div>
                  <div className="mb-3">
                    <input
                      type="password"
                      className="form-control"
                      placeholder="Password"
                      value={form.password}
                      onChange={e => setForm({...form, password: e.target.value})}
                    />
                  </div>
                  <button 
                    className="btn btn-primary w-100"
                    onClick={handleLogin}
                  >
                    Login
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
                        </div>
                        <div className="input-group">
                          <input
                            type="number"
                            className="form-control"
                            placeholder="Enter bid"
                            value={form.itemId === item.id ? form.bidAmount : ''}
                            onChange={e => setForm({
                              ...form,
                              itemId: item.id,
                              bidAmount: e.target.value
                            })}
                            min={item.currentBid + 1}
                          />
                          <button 
                            className="btn btn-primary"
                            onClick={() => handleBid(item.id)}
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
                      onClick={() => setForm({...form, sessionToken: ''})}
                    >
                      Logout
                    </button>
                  </div>
                  {isAdmin && <p className="text-warning mb-0">Administrator Account</p>}
                </div>
              </div>
            </div>
          </div>
        )}
      </div>
    </div>
  );
}

export default App;