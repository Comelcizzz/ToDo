const UserService = require("../services/user-service");
const { validationResult } = require('express-validator'); 
const ApiError = require('../exceptions/api-error'); 
const TokenService = require("../services/token-service"); 

class UserController {
    async registration(req, res, next) {
        try {
            // Extract email, password, and username from the request body
            const { email, password, username } = req.body; 

            // Call UserService to register the user and obtain user and token data
            const { user, tokens } = await UserService.register(email, password, username); 

            // Respond with the user data and generated tokens
            return res.json({
                user,
                accessToken: tokens.accessToken,
                refreshToken: tokens.refreshToken
            });
        } catch (e) {
            next(e); // Pass any errors to the error handler
        }
    }
    
    async login(req, res, next) {
        try {
            // Extract email and password from the request body
            const { email, password } = req.body;

            // Call UserService to log in the user and obtain user data and tokens
            const userData = await UserService.login(email, password);
            
            // Set the refresh token as a cookie with a defined expiration time and security settings
            res.cookie('refreshToken', userData.refreshToken, {
                maxAge: 30 * 24 * 60 * 60 * 1000, // 30 days
                sameSite: 'none', // Allows cross-site requests
                secure: true // Cookie is only sent over HTTPS
            });

            // Respond with the user data
            return res.json(userData);
        } catch (e) {
            next(e); // Pass any errors to the error handler
        }
    }

    async logout(req, res, next) {
        try {
            // Extract refresh token from cookies
            const { refreshToken } = req.cookies; 

            // Call UserService to log out the user
            const response = await UserService.logout(refreshToken); 
            
            // Clear the refresh token cookie upon logout
            res.clearCookie('refreshToken'); 

            // Respond with the logout response data
            return res.json(response); 
        } catch (e) {
            next(e); // Pass any errors to the error handler
        }
    }
    
    async updateEmail(req, res, next) {
        try {
            // Extract refresh token from cookies
            const { refreshToken } = req.cookies; 
            
            // Check if the refresh token is provided; if not, throw an error
            if (!refreshToken) {
                return next(ApiError.Unauthorized('No refresh token provided'));
            }

            // Call UserService to update the email using the refresh token
            const userData = await UserService.updateEmail(refreshToken);

            // Set the new refresh token as a cookie
            res.cookie('refreshToken', userData.refreshToken, {
                maxAge: 30 * 24 * 60 * 60 * 1000, // 30 days
                sameSite: 'none', // Allows cross-site requests
                secure: true // Cookie is only sent over HTTPS
            });
            
            // Respond with the updated user data
            return res.json(userData);
        } catch (e) {
            next(e); // Pass any errors to the error handler
        }
    }
}

module.exports = new UserController();