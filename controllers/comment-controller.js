const CommentService = require('../services/comment-service');

class CommentController {
    async create(req, res, next) {
        try {
            // Extract taskId and userId from the authenticated user's data
            const { taskId, userId } = req.user; 
            const { text } = req.body; 

            // Call the createComment function with taskId, userId, and text as arguments
            const comment = await CommentService.createComment(taskId, userId, text);

            // Respond to the user with the created comment
            return res.json(comment);
        } catch (e) {
            next(e);
        }
    }

    async delete(req, res, next) {
        try {
            // Extract the comment id from the request body
            const { id } = req.body; 

            // Call the deleteComment function with the comment id as an argument
            await CommentService.deleteComment(id); 

            // Respond to the user with a success status
            return res.status(200).json();
        } catch (e) {
            next(e);
        }
    }
}

module.exports = new CommentController();
