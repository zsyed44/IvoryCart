import React, { useState } from "react";

const AdminPage: React.FC = () => {
  const [name, setName] = useState("");
  const [price, setPrice] = useState(0);
  const [stock, setStock] = useState(0);
  const [threshold, setThreshold] = useState(1);
  const [removeId, setRemoveId] = useState(0);
  const [updateStockId, setUpdateStockId] = useState(0);
  const [newStock, setNewStock] = useState(0);
  const [updateThresholdId, setUpdateThresholdId] = useState(0);
  const [newThreshold, setNewThreshold] = useState(0);

  const token = "dummy_token_for_user_1"; // ⚡ Dummy admin token

  async function addProduct() {
    await fetch("http://localhost:8080/api/admin/products/add", {
      method: "POST",
      headers: {
        "Authorization": token,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ name, price, stock, threshold }),
    });
    alert("Product added");
  }

  async function removeProduct() {
    await fetch("http://localhost:8080/api/admin/products/remove", {
      method: "POST",
      headers: {
        "Authorization": token,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ productId: removeId }),
    });
    alert("Product removed");
  }

  async function updateStock() {
    await fetch("http://localhost:8080/api/admin/products/updateStock", {
      method: "POST",
      headers: {
        "Authorization": token,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ productId: updateStockId, newStock }),
    });
    alert("Stock updated");
  }

  async function updateThreshold() {
    await fetch("http://localhost:8080/api/admin/products/updateThreshold", {
      method: "POST",
      headers: {
        "Authorization": token,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ productId: updateThresholdId, threshold: newThreshold }),
    });
    alert("Threshold updated");
  }

  return (
    <div>
      <h1>🔒 Admin Panel</h1>

      <h2>Add Product</h2>
      <input placeholder="Name" value={name} onChange={(e) => setName(e.target.value)} />
      <input placeholder="Price" type="number" value={price} onChange={(e) => setPrice(parseFloat(e.target.value))} />
      <input placeholder="Stock" type="number" value={stock} onChange={(e) => setStock(parseInt(e.target.value))} />
      <input placeholder="Sold Out Threshold" type="number" value={threshold} onChange={(e) => setThreshold(parseInt(e.target.value))} />
      <button onClick={addProduct}>Add</button>

      <h2>Remove Product</h2>
      <input placeholder="Product ID" type="number" value={removeId} onChange={(e) => setRemoveId(parseInt(e.target.value))} />
      <button onClick={removeProduct}>Remove</button>

      <h2>Update Stock</h2>
      <input placeholder="Product ID" type="number" value={updateStockId} onChange={(e) => setUpdateStockId(parseInt(e.target.value))} />
      <input placeholder="New Stock" type="number" value={newStock} onChange={(e) => setNewStock(parseInt(e.target.value))} />
      <button onClick={updateStock}>Update Stock</button>

      <h2>Update Sold-Out Threshold</h2>
      <input placeholder="Product ID" type="number" value={updateThresholdId} onChange={(e) => setUpdateThresholdId(parseInt(e.target.value))} />
      <input placeholder="New Threshold" type="number" value={newThreshold} onChange={(e) => setNewThreshold(parseInt(e.target.value))} />
      <button onClick={updateThreshold}>Update Threshold</button>
    </div>
  );
};

export default AdminPage;
