const mongoose = require('mongoose');

const TokenSchema = new mongoose.Schema({
    userId: {
        type: mongoose.Schema.Types.ObjectId,
        ref: 'user-model',
        required: true
    },
    refreshToken: {
        type: String, 
        required: true
    }
});

module.exports = mongoose.model('token-model', TokenSchema);
