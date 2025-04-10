import React, { useEffect, useState } from "react";

interface Product {
  id: number;
  name: string;
  price: number;
  stock: number;
  soldOut: boolean;
}

const ProductsPage: React.FC = () => {
  const [products, setProducts] = useState<Product[]>([]);

  useEffect(() => {
    async function fetchProducts() {
      const res = await fetch("http://localhost:8080/api/products");
      const data = await res.json();
      setProducts(data.products);
    }
    fetchProducts();
  }, []);

  return (
    <div>
      <h1>🛒 Products</h1>
      {products.map((p) => (
        <div key={p.id}>
          <h2>{p.name}</h2>
          <p>Price: ${p.price.toFixed(2)}</p>
          <p>Stock: {p.stock}</p>
          <p>Status: {p.soldOut ? <strong style={{ color: "red" }}>Sold Out</strong> : "Available"}</p>
          <hr />
        </div>
      ))}
    </div>
  );
};

export default ProductsPage;
