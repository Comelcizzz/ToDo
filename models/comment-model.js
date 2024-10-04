const mongoose = require('mongoose');

const CommentSchema = new mongoose.Schema({
    text: {
        type: String, 
        required: true
    },
    createdAt: { 
        type: Date, 
        default: Date.now
    },
    projectId: {
        type: mongoose.Schema.Types.ObjectId,
        ref: 'project-model'
    },
    taskId: {
        type: mongoose.Schema.Types.ObjectId,
        ref: 'task-model'
    },
    userId: {
        type: mongoose.Schema.Types.ObjectId,
        ref: 'user-model'
    }
});

module.exports = mongoose.model('comment-model', CommentSchema);

