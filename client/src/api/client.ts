import axios from 'axios';

const api = axios.create({
    baseURL: import.meta.env.VITE_API_URL,
    withCredentials: true,
});

export const login = async (email: string, password: string) => {
    return api.post('/api/login', { email, password });
};

export const getProducts = async () => {
    return api.get('/api/products');
};

export const checkout = async (cartItems: Array<{ productId: number, quantity: number }>) => {
    return api.post('/api/checkout', { items: cartItems });
};