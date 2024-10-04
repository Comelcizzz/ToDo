const CommentModel = require('../models/comment-model'); 
const ApiError = require('../exceptions/api-error'); 

class CommentService {
    // Method to create a comment
    async createComment(taskId, userId, text) {
        try {
            // Create a new comment based on the model
            const comment = new CommentModel({
                taskId, 
                userId, 
                text 
            });
            // Save the comment to the database
            await comment.save();
            return comment; // Return the saved comment
        } catch (e) {
            // Handle the error if creating the comment fails
            throw ApiError.InternalServerError('Error creating comment');
        }
    }

    // Method to delete a comment
    async deleteComment(id) {
        try {
            // Find and delete the comment by its ID
            const deletedComment = await CommentModel.findByIdAndDelete(id);
            if (!deletedComment) {
                // If the comment is not found, throw an error
                throw ApiError.NoCommentOnRequest();
            }
            return deletedComment._id; // Return the ID of the deleted comment
        } catch (e) {
            // Handle the error if deleting the comment fails
            throw ApiError.InternalServerError('Error deleting comment');
        }
    }
}

module.exports = new CommentService();
