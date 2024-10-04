const ApiError = require('../exceptions/api-error');

// Error handling middleware
// This middleware is responsible for catching errors that occur in 
// the application and returning appropriate responses to the client.
const errorMiddleware = (err, req, res, next) => {
    // Log the error to the console for debugging purposes
    console.error(err);

    // Check if the error is an instance of ApiError
    if (err instanceof ApiError) {
        // Return the specific error message and status from the ApiError instance
        return res.status(err.status).json({ 
            message: err.message, 
            errors: err.errors 
        });
    }

    // For any other errors, return a generic server error response
    return res.status(500).json({ message: 'Server error' });
};

// Export the error middleware for use in the application
module.exports = errorMiddleware;
