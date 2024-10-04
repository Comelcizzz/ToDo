const projectService = require('../services/project-service');
const UserService = require('../services/user-service');
const ApiError = require('../exceptions/api-error');

class ProjectController {
    async createProject(req, res, next) {
        try {
            // Retrieve the user ID from the authenticated user's data
            const userId = req.user.id; 

            // Log the request body for debugging
            console.log("Request Body:", req.body); 

            // Extract project title and description from the request body
            const { projectTitle, description } = req.body; 
            
            // Check if project title is provided; if not, respond with an error
            if (!projectTitle) {
                return res.status(400).json({ message: 'Project title is required' });
            }
    
            // Call the createProject function with project details and user ID
            const project = await projectService.createProject({
                projectTitle,
                description,
                user: userId, 
            });
            
            // Respond with the created project and a 201 status code
            res.status(201).json(project);
        } catch (error) {
            next(error); // Pass any errors to the error handler
        }
    }
    
    async getProject(req, res) {
        try {
            // Extract the project ID from the request body
            const { id } = req.body; 
            console.log("Received ID:", id); 
    
            // Check if the ID is provided; if not, throw an error
            if (!id) {
                throw new ApiError.BadRequest('ID is required');
            }
    
            // Call the getProject function to retrieve project details
            const project = await projectService.getProject(id);
            return res.json(project); // Respond with the retrieved project
        } catch (error) {
            // Respond with an error status and message if an error occurs
            return res.status(error.status || 500).json({ message: error.message });
        }
    }
    
    async updateProject(req, res, next) {
        try {
            // Retrieve the user ID from the authenticated user's data
            const userId = req.user.id;
            const { projectTitle, newTitle, newDescription, newCreatedAt } = req.body;
    
            // Check if the original project title is provided; if not, throw an error
            if (!projectTitle) {
                return next(ApiError.BadRequest('Project title is required'));
            }
    
            // Call the updateProject function with the necessary arguments
            const updatedProject = await projectService.updateProject(userId, projectTitle, newTitle, newDescription, newCreatedAt);
            return res.json(updatedProject); // Respond with the updated project
        } catch (e) {
            next(e); // Pass any errors to the error handler
        }
    }

    async deleteProject(req, res, next) {
        try {
            // Extract the project ID from the request body
            const { id } = req.body;

            // Call the deleteProject function to delete the project and retrieve its ID
            const deletedProjectId = await projectService.deleteProject(id);
            return res.json({ message: 'Project deleted', id: deletedProjectId }); // Respond with a success message and deleted project ID
        } catch (e) {
            next(e); // Pass any errors to the error handler
        }
    }
}

module.exports = new ProjectController();
