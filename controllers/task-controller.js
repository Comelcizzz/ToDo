const projectService = require("../services/project-service");
const TaskService = require("../services/task-service");
const ApiError = require('../exceptions/api-error'); 
const taskModel = require('../models/task-model'); 

class TaskController {
    async getTasks(req, res, next) {
        try {
            // Extract relevant fields from the request body
            const { queue, development, done, val, searchType } = req.body;

            // Call the TaskService to retrieve tasks based on the provided filters
            const tasks = await TaskService.getTasks(queue, development, done, val, searchType);
            return res.json(tasks); // Respond with the retrieved tasks
        } catch (e) {
            next(e); // Pass any errors to the error handler
        }
    }
    
    async createTask(req, res, next) {
        try {
            // Extract project ID and task data from the request body
            const { projectId, projectData } = req.body; // Правильно отримуємо projectData
    
            // Log the received task data for debugging purposes
            console.log('Received task data for creation:', projectData); // Виводимо projectData в консоль
    
            // Call the TaskService to create a new task
            const data = await TaskService.createTask(projectId, projectData); // Використовуємо projectData
            return res.json(data); // Respond with the created task data
        } catch (e) {
            // Log any error messages for debugging
            console.error('Error in createTask controller:', e.message);
            next(e); // Pass any errors to the error handler
        }
    }
    
    async updateTask(req, res, next) {
        try {
            // Retrieve the task ID from the URL parameters
            const taskId = req.params.id; 
            // Get the updated data for the task from the request body
            const updatedData = req.body.updatedData; 
    
            console.log('Updating task with ID:', taskId); // Log the task ID being updated
            console.log('Updated data:', updatedData); // Log the new data for debugging
    
            // Find the task by its ID and update it with the new data, returning the updated task
            const updatedTask = await taskModel.findByIdAndUpdate(taskId, updatedData, { new: true });
            
            // If the task is not found, log an error and throw a BadRequest error
            if (!updatedTask) {
                console.error('Task not found for ID:', taskId);
                throw ApiError.BadRequest('Task not found');
            }
    
            console.log('Task updated successfully:', updatedTask); // Log the successfully updated task
            return res.json(updatedTask); // Return the updated task as a JSON response
        } catch (e) {
            // Log any errors that occur during the update process
            console.error('Error during task update:', e.message);
            // Pass the error to the error handling middleware
            next(ApiError.InternalServerError(`Error updating task: ${e.message}`));
        }
    }
    
    async updateTask(req, res, next) {
        try {
            // Retrieve the task ID from the URL parameters
            const taskId = req.params.id; 
            // Get the updated data for the task from the request body
            const updatedData = req.body.updatedData; 
    
            console.log('Updating task with ID:', taskId); // Log the task ID being updated
            console.log('Updated data:', updatedData); // Log the new data for debugging
    
            // Find the task by its ID and update it with the new data, returning the updated task
            const updatedTask = await taskModel.findByIdAndUpdate(taskId, updatedData, { new: true });
            
            // If the task is not found, log an error and throw a BadRequest error
            if (!updatedTask) {
                console.error('Task not found for ID:', taskId);
                throw ApiError.BadRequest('Task not found');
            }
    
            console.log('Task updated successfully:', updatedTask); // Log the successfully updated task
            return res.json(updatedTask); // Return the updated task as a JSON response
        } catch (e) {
            // Log any errors that occur during the update process
            console.error('Error during task update:', e.message);
            // Pass the error to the error handling middleware
            next(ApiError.InternalServerError(`Error updating task: ${e.message}`));
        }
    }
    
    async deleteTask(req, res, next) {
        try {
            // Extract project ID and task ID from the request body
            const { projectId, taskId } = req.body; 

            // Log the received project ID and task ID for debugging
            console.log('Received projectId:', projectId);
            console.log('Received taskId:', taskId);
  
            // Call the TaskService to delete the specified task
            const data = await TaskService.deleteTask(projectId, taskId);
            return res.json(data); // Respond with the result of the deletion
        } catch (e) {
            next(e); // Pass any errors to the error handler
        }
    }    
}

module.exports = new TaskController();
