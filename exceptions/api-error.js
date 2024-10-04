// Custom error class to handle API errors
// This class extends the built-in Error class to provide 
// more detailed error information, including status codes 
// and additional error details.

module.exports = class ApiError extends Error {
    // HTTP status code for the error
    status;
    // Additional error details, if any
    errors;

    // Constructor to initialize the error with a status, message, and optional errors
    constructor(status, message, errors = []) {
        super(message); // Call the parent constructor with the error message
        this.status = status; // Set the HTTP status code
        this.errors = errors; // Set the additional error details
    }

    // Static method for creating a Bad Request error
    static BadRequest(message, errors = []) {
        return new ApiError(400, message, errors);
    }

    // Static method for creating an Unauthorized error
    static UnauthorizedError() {
        return new ApiError(401, 'User is not authorized');
    }

    // Static method for creating a No Access Request error
    static NoAccessRequetError() {
        return new ApiError(403, 'User is not admin');
    }

    // Static method for creating a Not Found error for projects
    static NoProjectOnRequest() {
        return new ApiError(404, 'No project was found by requested id');
    }

    // Static method for creating an Internal Server Error
    static InternalServerError(message = 'Internal server error', errors = []) {
        return new ApiError(500, message, errors);
    }
}
