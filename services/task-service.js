const projectModel = require("../models/project-model");
const taskModel = require("../models/task-model");
const ApiError = require('../exceptions/api-error'); 

class TaskService {
    // Method to retrieve tasks associated with a project, with optional filtering
    async getTasks(projectId, val = '', searchType = '') {
        try {
            const props = {
                projectId: projectId // Set the project ID for the query
            };

            // If searchType is 'title', create a regex for filtering tasks by title
            if (searchType === 'title') {
                val = new RegExp(`${val}`, 'i'); // Create a case-insensitive regex
                props.title = { $regex: val }; // Add regex filter to properties
            }

            // Retrieve tasks from the database that match the properties
            const tasks = await taskModel.find(props);
            return tasks; // Return the list of tasks
        } catch (e) {
            throw ApiError.InternalServerError('Error retrieving tasks'); // Handle error if task retrieval fails
        }
    }

    // Method to create a new task
    async createTask(projectId, taskData) {
        try {
            console.log('Creating task with data:', taskData); 
    
            // Create a new task instance
            const newTask = new taskModel({
                title: taskData.title,         // Title of the task
                description: taskData.description, // Description of the task
                projectId: projectId,          // ID of the associated project
                deadline: taskData.deadline     // Deadline for the task
            });
    
            // Save the new task to the database
            const savedTask = await newTask.save(); 
            console.log('Task saved successfully:', savedTask); 
            return savedTask; // Return the saved task
        } catch (e) {
            console.error('Error creating task:', e.message); 
            throw ApiError.InternalServerError('Error creating task'); // Handle error if task creation fails
        }
    }
    
    // Method to update an existing task
    async updateTask(taskId, updatedData) {
        try {
            console.log('Updating task with ID:', taskId);
            console.log('Updated data:', updatedData);
    
            // Find and update the task by its ID
            const updatedTask = await taskModel.findByIdAndUpdate(taskId, updatedData, { new: true });
            
            if (!updatedTask) {
                console.error('Task not found for ID:', taskId);
                throw ApiError.BadRequest('Task not found'); // Handle case when task is not found
            }
    
            console.log('Task updated successfully:', updatedTask);
            return updatedTask; // Return the updated task
        } catch (e) {
            console.error('Error during task update:', e);
            throw ApiError.InternalServerError('Error updating task'); // Handle error if task update fails
        }
    }
     
    // Method to delete a task
    async deleteTask(projectId, taskId) {
        try {
            // Find the task by its ID
            const task = await taskModel.findById(taskId);
            if (!task) {
                throw ApiError.BadRequest('Task not found'); // Handle case when task is not found
            }
            // Delete the task by its ID
            await taskModel.findByIdAndDelete(taskId);
            // Retrieve the updated list of tasks for the project
            const updatedTasks = await this.getTasks(projectId);
            return updatedTasks; // Return the updated list of tasks
        } catch (e) {
            throw ApiError.InternalServerError('Error deleting task'); // Handle error if task deletion fails
        }
    }      
}

module.exports = new TaskService();
