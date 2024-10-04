const User = require('../models/user-model'); 
const tokenService = require('./token-service'); 
const UserDto = require('../dtos/user-dto');
const ApiError = require('../exceptions/api-error');
const ProjectModel = require('../models/project-model');
const { hashPassword, validatePassword } = require('../crypto/hashing');
const bcrypt = require('bcrypt');

require('dotenv').config();

class UserService {
    // Method to register a new user
    async register(email, password, username) {
        try {
            // Check if the user already exists
            const existingUser = await User.findOne({ email });
            if (existingUser) {
                throw ApiError.BadRequest('User with this email already exists.');
            }
            
            // Hash the password before saving
            const hashedPassword = await hashPassword(password);
            const user = new User({ email, password: hashedPassword, username });
            await user.save(); // Save the new user to the database

            // Generate tokens for the user
            const tokens = tokenService.generateTokens({ _id: user._id, email: user.email });
            await tokenService.saveToken(user._id, tokens.refreshToken); // Save refresh token

            return {
                user,
                tokens // Return user and tokens
            };
        } catch (e) {
            console.error('Registration error:', e);
            if (e instanceof ApiError) {
                throw e; // Rethrow known API errors
            }
            throw ApiError.InternalServerError('Registration failed'); // Handle unexpected errors
        }
    }
    
    // Method for user login
    async login(email, password) {
        try {
            console.log(`Attempting to login user: ${email}`);
            const user = await User.findOne({ email }); // Find user by email
            if (!user) {
                throw ApiError.BadRequest(`Can't find user with email ${email}`);
            }

            // Check if the user has a password set
            if (!user.password) {
                throw ApiError.BadRequest('User does not have a password set');
            }

            console.log(`Password from DB: ${user.password}`);
            // Validate the provided password against the stored hash
            const isPasswordValid = await bcrypt.compare(password, user.password);
            console.log(`Password valid: ${isPasswordValid}`);
            if (!isPasswordValid) {
                throw ApiError.BadRequest('Invalid password');
            }

            const userDto = new UserDto(user);
            const tokens = tokenService.generateTokens({ id: userDto.id });
            await tokenService.saveToken(userDto.id, tokens.refreshToken); // Save refresh token

            return {
                ...tokens, // Return access and refresh tokens
                user: userDto // Include user data
            }; 
        } catch (e) {
            console.error('Login error:', e);
            throw ApiError.InternalServerError('Login failed'); // Handle unexpected errors
        }
    }
    
    // Method to update user email
    async updateEmail(userId, newEmail) {
        try {
            const user = await User.findById(userId); // Find user by ID
            if (!user) {
                throw new ApiError('User not found', 404);
            }
    
            user.email = newEmail; // Update email
            await user.save(); // Save changes
    
            console.log('Email updated successfully for user:', userId);
        } catch (error) {
            console.error('Error updating email:', error);
            throw new ApiError('Email update failed', 500); // Handle unexpected errors
        }
    }

    // Method to log out a user
    async logout(refreshToken) {
        try {
            if (!refreshToken) {
                throw ApiError.BadRequest('Refresh token is required'); // Ensure refresh token is provided
            }

            const tokenExists = await tokenService.removeToken(refreshToken); // Remove token from storage
            if (!tokenExists) {
                throw ApiError.BadRequest('Token not found');
            }
    
            return { message: 'Logout successful' }; // Return success message
        } catch (e) {
            console.error('Logout error:', e);
            throw ApiError.InternalServerError('Logout failed'); // Handle unexpected errors
        }
    }
    
    // Method to delete a project by ID
    async deleteProject(projectId) {
        try {
            const deletedProject = await ProjectModel.findByIdAndDelete(projectId); // Delete project
            if (!deletedProject) {
                throw ApiError.NoProjectOnRequest(); // Handle case where project not found
            }
            return deletedProject; // Return deleted project data
        } catch (e) {
            throw ApiError.InternalServerError('Error deleting project'); // Handle unexpected errors
        }
    }
}

module.exports = new UserService();
