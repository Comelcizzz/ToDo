const jwt = require('jsonwebtoken');
const TokenModel = require('../models/token-model');

require('dotenv').config();

class TokenService {
    // Method to generate access and refresh tokens
    generateTokens(payload) {
        // Create access token with a 180-minute expiration
        const accessToken = jwt.sign(payload, process.env.JWT_ACCESS_SECRET, { expiresIn: '180m' });
        // Create refresh token with a 30-day expiration
        const refreshToken = jwt.sign(payload, process.env.JWT_REFRESH_SECRET, { expiresIn: '30d' });
    
        return {
            accessToken, 
            refreshToken
        };
    }

    // Method to validate access token
    validationAccessToken(token) {
        try {
            // Verify the token using the access secret
            const userData = jwt.verify(token, process.env.JWT_ACCESS_SECRET);
            return userData; // Return the decoded user data if valid
        } catch (e) {
            return null; // Return null if validation fails
        }
    }

    // Method to validate refresh token
    validationRefreshToken(token) {
        try {
            // Verify the token using the refresh secret
            const userData = jwt.verify(token, process.env.JWT_REFRESH_SECRET);
            console.log("Decoded user data:", userData); // Log decoded user data for debugging
            return userData; // Return the decoded user data if valid
        } catch (e) {
            console.error("Token validation error:", e); // Log error if validation fails
            return null; // Return null if validation fails
        }
    }
    
    // Method to save refresh token in the database
    async saveToken(userId, refreshToken) {
        // Check if a token already exists for the user
        const tokenData = await TokenModel.findOne({ userId });
    
        if (tokenData) {
            // If token exists, update it
            tokenData.refreshToken = refreshToken;
            return tokenData.save(); // Save and return updated token data
        }
    
        // If no token exists, create a new one
        const token = await TokenModel.create({ userId, refreshToken });
        return token; // Return the newly created token
    }
    
    // Method to remove a refresh token from the database
    async removeToken(refreshToken) {
        const tokenData = await TokenModel.deleteOne({ refreshToken });
        return tokenData; // Return the result of the deletion operation
    }
    
    // Method to find a refresh token in the database
    async findToken(refreshToken) {
        const tokenData = await TokenModel.findOne({ refreshToken }); 
        return tokenData; // Return the found token data (or null if not found)
    }
}

module.exports = new TokenService();
