const mongoose = require('mongoose');

const taskSchema = new mongoose.Schema({
    title: { 
        type: String, 
        required: true 
    },
    description: { 
        type: String, 
        required: true 
    },
    projectId: { 
        type: mongoose.Schema.Types.ObjectId, 
        ref: 'Project'
    },
    deadline: { 
        type: Date 
    },
    createdAt: { 
        type: Date, 
        default: Date.now }
});

module.exports = mongoose.model('task-model', taskSchema);
