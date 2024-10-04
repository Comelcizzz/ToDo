const ApiError = require('../exceptions/api-error');
const tokenService = require('../services/token-service');

// Middleware function to handle authorization
// This function checks for a valid access token in the request headers 
// and attaches the user data to the request object if valid.
module.exports = function (req, res, next) {
    try {
        // Extract the authorization header from the request
        const authorizationHeaders = req.headers.authorization; 
        if (!authorizationHeaders) {
            console.log("No authorization header");
            return next(ApiError.UnauthorizedError()); // Return unauthorized error if header is missing
        }

        // Split the header to get the access token
        const accessToken = authorizationHeaders.split(' ')[1]; 
        if (!accessToken) {
            console.log("No access token found");
            return next(ApiError.UnauthorizedError()); // Return unauthorized error if token is missing
        }

        // Validate the access token and retrieve user data
        const userData = tokenService.validationAccessToken(accessToken); 
        if (!userData) {
            console.log("Invalid access token");
            return next(ApiError.UnauthorizedError()); // Return unauthorized error if token is invalid
        }
        
        // Log the user data extracted from the token
        console.log("User data from token:", userData);
        req.user = userData; // Attach the user data to the request object
        next(); // Proceed to the next middleware or route handler
    } catch (e) {
        console.error("Authorization middleware error:", e);
        next(ApiError.UnauthorizedError()); // Handle any unexpected errors
    }
};
