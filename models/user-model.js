const mongoose = require('mongoose');

const UserSchema = new mongoose.Schema({
    username: {
        type: String, 
        required: true
    },
    email: {
        type: String, 
        required: true
    },
    password: {
        type: String, 
        required: true
    },
    token: {
        type: String
    },
    projects: [{
        type: mongoose.Schema.Types.ObjectId, 
        ref: 'project-model'
    }]
});

module.exports = mongoose.model('user-model', UserSchema);
