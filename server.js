const express = require('express');
const cors = require('cors');
const mongoose = require('mongoose');
const cookieParser = require('cookie-parser');
const router = require('./routes/index');
const errorMiddleware = require('./middlewares/error-middleware');
require('dotenv').config();

const app = express();
const port = process.env.PORT || 3000;

//CORS (Cross-Origin Resource Sharing) is a security mechanism that allows you to 
//control which resources can be accessed from other domains. It helps to avoid 
//potential attacks such as Cross-Site Request Forgery (CSRF) and Cross-Site Scripting (XSS).
// Setting up CORS options to allow requests from the client URL and include credentials
app.use(cors({
    origin: process.env.CLIENT_URL, 
    credentials: true
}));

app.use(express.json()); // Middleware to parse JSON request bodies
app.use(cookieParser()); // Middleware to parse cookies from request headers

// Mounting the main router under the '/api' path
app.use('/api', router); 

// Serving static files from the 'public' directory
app.use('/public', express.static('public')); 

// Using error handling middleware for centralized error handling
app.use(errorMiddleware);

// MongoDB connection URI from environment variables
const uri = process.env.ATLAS_URI;

// Function to start the server and connect to MongoDB
const start = async () => {
    try {
        // Connecting to MongoDB
        await mongoose.connect(uri, { useNewUrlParser: true, useUnifiedTopology: true });
        console.log('MongoDB connected');

        // Starting the Express server and listening on the specified port
        app.listen(port, () => {
            console.log(`Сервер запущено на http://localhost:${port}`);
        });
    } catch (e) {
        console.error('Error starting the server:', e); // Logging any connection errors
    }
};

// Calling the start function to run the server
start();
