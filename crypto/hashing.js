const bcrypt = require('bcrypt'); // Import the bcrypt library for password hashing

// Function to hash a given password
async function hashPassword(password) {
    try {
        // Generate a salt for hashing
        const salt = await bcrypt.genSalt(10); // The '10' indicates the number of salt rounds
        // Hash the password with the generated salt and return the hashed password
        return await bcrypt.hash(password, salt);
    } catch (error) {
        // Log any error that occurs during the hashing process
        console.error('Hashing error:', error);
    }
}

// Export the hashPassword function for use in other modules
module.exports = { hashPassword };
