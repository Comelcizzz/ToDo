const projectService = require("../services/project-service");
const TaskService = require("../services/task-service");

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
            const { projectId, task } = req.body;

            // Log the received task data for debugging purposes
            console.log('Received task data for creation:', task); 

            // Call the TaskService to create a new task
            const data = await TaskService.createTask(projectId, task);
            return res.json(data); // Respond with the created task data
        } catch (e) {
            // Log any error messages for debugging
            console.error('Error in createTask controller:', e.message);
            next(e); // Pass any errors to the error handler
        }
    }
    
    async updateTask(req, res, next) {
        try {
            // Extract task ID and updated data from the request body
            const { id, updatedData } = req.body; 
            
            // Log the request to update the task for debugging
            console.log('Request to update task ID:', id, 'with data:', updatedData);
            
            // Call the TaskService to update the specified task
            const updatedTask = await TaskService.updateTask(id, updatedData);  
            return res.json(updatedTask); // Respond with the updated task data
        } catch (e) {
            // Log any error messages for debugging
            console.error('Error in updateTask controller:', e.message);
            next(e); // Pass any errors to the error handler
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
